/*
 * fs/namei_memlog.c
 *
 * Complete reimplementation
 * (C) 2016 Yige Hu,
 */

#include <linux/security.h>
#include <linux/fsnotify.h>
#include <linux/namei.h>
#include <linux/mount.h>

#include "dcache_memlog.h"

struct dentry *d_lookup_read_memlog(const struct dentry *parent, const struct qstr *name,
					bool *is_unlinked)
{
	struct dentry *dentry;
	struct dentry_rec *record;

	dentry = memlog__d_lookup(parent, name);
	*is_unlinked = false;
	if (dentry) {
		record = dentry->d_dentry_rec;
		BUG_ON(!record);
		if (likely(record->r_flags != REC_DEL)) {
			fs_tx_debug("dentry found in memlog.\n");
			return dentry;
		} else if (record->r_dentry_rec) {
			fs_tx_debug("dentry found in memlog, newly created after unlink.\n");
			return record->r_dentry_rec->r_dentry_copy;
		} else {
			fs_tx_debug("dentry found in memlog, already unlinked.\n");
			*is_unlinked = true;
			return dentry;
		}
	}

	fs_tx_debug("Entering d_lookup(), parent dentry = %p, "
		"d_lockref.count = %d\n", parent, parent->d_lockref.count);

	dentry = d_lookup(parent, name);
	return dentry;
}
EXPORT_SYMBOL(d_lookup_read_memlog);

struct dentry *lookup_dcache_memlog(struct qstr *name, struct dentry *dir,
				    unsigned int flags, bool *need_lookup, struct file *file)
{
	struct dentry *dentry, *dentry_new;
	int error;
	bool is_unlinked;

	*need_lookup = false;
	dentry = d_lookup_read_memlog(dir, name, &is_unlinked);
	if (dentry) {
		fs_tx_debug("dentry found. addr = %p, d_lockref.count = %d, d_inode = %p, "
			"d_dentry_rec = %p",
			dentry, dentry->d_lockref.count, dentry->d_inode, dentry->d_dentry_rec);
		if (dentry->d_flags & DCACHE_OP_REVALIDATE) {
			error = d_revalidate(dentry, flags);
			if (unlikely(error <= 0)) {
				if (error < 0) {
					dput(dentry);
					return ERR_PTR(error);
				} else {
					d_invalidate(dentry);
					dput(dentry);
					dentry = NULL;
				}
			}
		}
	}

	if (!dentry || is_unlinked) {
		// TODO: when committing, need to check if parent still exists;
		//	or mark parents as being accessed by tx.
		dentry_new = d_alloc_memlog(dir, name, file);
		fs_tx_debug("New dentry allocated in memlog, addr = %p, d_lockref.count = %d",
			dentry_new, dentry_new->d_lockref.count);
		if (unlikely(!dentry_new))
			return ERR_PTR(-ENOMEM);

		if (unlikely(is_unlinked)) {
			/* Since dentry is already in memlog->l_dentry_delete,
			 * we should avoid dentry_new being inserted into l_dentry_new. */
			dentry_rec_log_list_del(dentry_new->d_dentry_rec);
			memlog__d_drop(dentry_new, current->memlog);
			dentry_new->d_dentry_rec->r_dentry_rec = dentry->d_dentry_rec;
			dentry->d_dentry_rec->r_dentry_rec = dentry_new->d_dentry_rec;
		} else {
			*need_lookup = true;
		}
		dentry = dentry_new;
	}
	return dentry;
}

struct dentry *lookup_real_memlog(struct inode *dir, struct dentry *dentry,
				  unsigned int flags)
{
	struct dentry *old;

	/* Don't create child dentry for a dead directory. */
	if (unlikely(IS_DEADDIR(dir))) {
		dput(dentry);
		return ERR_PTR(-ENOENT);
	}

	//BUG_ON(!dir->i_op->lookup_memlog);
	old = dir->i_op->lookup_memlog(dir, dentry, flags);
	if (unlikely(old)) {
		dput(dentry);
		dentry = old;
	}
	return dentry;
}

int vfs_create_memlog(struct inode *dir, struct dentry *dentry, umode_t mode,
		bool want_excl)
{
	memlog_t *memlog = current->memlog;
	int error = may_create(dir, dentry);
	if (error)
		return error;

	if (!dir->i_op->create_memlog)
		return -EACCES;	/* shouldn't it be ENOSYS? */
	mode &= S_IALLUGO;
	mode |= S_IFREG;
	error = security_inode_create(dir, dentry, mode);
	if (error)
		return error;
	error = dir->i_op->create_memlog(dir, dentry, mode, want_excl);
	memlog->l_write = true;

	/* Lock the dir block */
	memlog_alloc_inode_rec(dir, REC_LOCK | REC_DIR);

	if (!error)
		fsnotify_create(dir, dentry);
	return error;
}
EXPORT_SYMBOL(vfs_create_memlog);

int lookup_open_memlog(struct nameidata *nd, struct path *path,
			struct file *file,
			const struct open_flags *op,
			bool got_write, int *opened)
{
	struct dentry *dir = nd->path.dentry;
	struct inode *dir_inode = dir->d_inode;
	struct dentry *dentry, *dentry_copy;
	int error;
	bool need_lookup;
	struct dentry_rec *record;

	*opened &= ~FILE_CREATED;
	dentry = lookup_dcache_memlog(&nd->last, dir, nd->flags, &need_lookup, file);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	/* Cached positive dentry: will open in f_op->open */
	if (!need_lookup && dentry->d_inode)
		goto out_no_open;

	if ((nd->flags & LOOKUP_OPEN) && dir_inode->i_op->atomic_open) {
		printk(KERN_ERR "Non-implemented: atomic_open\n");
		BUG_ON(true);
		//return atomic_open(nd, dentry, path, file, op, got_write,
		//		   need_lookup, opened);
	}

	if (need_lookup) {
		BUG_ON(dentry->d_inode);

		dentry = lookup_real_memlog(dir_inode, dentry, nd->flags);
		if (IS_ERR(dentry))
			return PTR_ERR(dentry);

		/* Fall back for newly look-up-ed directories. */
		if (dentry->d_inode && S_ISDIR(dentry->d_inode->i_mode)) {
			struct dentry_rec *record = dentry->d_dentry_rec;
			printk(KERN_ERR "WARNING: TxFS currently does not support "
				"transactional operations on directories. "
				"Falling back to non-tx open.\n");
			BUG_ON(!record);
			BUG_ON(record->r_flags != REC_NEW);
			spin_lock(&dentry->d_parent->d_lock);
			spin_lock(&dentry->d_lock);
			dentry->d_dentry_rec = NULL;
			dentry->d_flags &= ~DCACHE_IN_MEM;
			dentry_start_write(dentry->d_parent);
			list_add(&dentry->d_child, &dentry->d_parent->d_subdirs);
			dentry->d_lockref.count--;
			BUG_ON(dentry->d_lockref.count < 1);
			spin_unlock(&dentry->d_lock);
			spin_unlock(&dentry->d_parent->d_lock);
			/* Don't need extra dputs since we hold the dentry. */
			memlog_free_dentry_rec(record);
			goto out_no_open;
		}
	}

	/* local dentry newly allocated, inode found by lookup_real() */
	if (dentry->d_inode) {
		/* Only do a local copy of inode */
		d_copy_inode_memlog(dentry, file);
	}

	/* Negative dentry, just create the file */
	if (!dentry->d_inode && (op->open_flag & O_CREAT)) {
		umode_t mode = op->mode;
		if (!IS_POSIXACL(dir->d_inode))
			mode &= ~current_umask();
		/*
		 * This write is needed to ensure that a
		 * rw->ro transition does not occur between
		 * the time when the file is created and when
		 * a permanent write count is taken through
		 * the 'struct file' in finish_open().
		 */
		if (!got_write) {
			error = -EROFS;
			goto out_dput;
		}
		*opened |= FILE_CREATED;

		/* Cached global negative dentry */
		if (!need_lookup &&
			(!(dentry->d_dentry_rec && dentry->d_dentry_rec->r_dentry_rec))) {
			dentry_copy = d_alloc_copy_memlog(dir, &nd->last, dentry, file);
			memlog_d_rehash(dentry_copy);
			dentry = dentry_copy;
		}

		error = security_path_mknod(&nd->path, dentry, mode, 0);
		if (error)
			goto out_dput;
		error = vfs_create_memlog(dir->d_inode, dentry, mode,
				   nd->flags & LOOKUP_EXCL);
		if (error)
			goto out_dput;
	}
out_no_open:
	path->dentry = dentry;
	path->mnt = nd->path.mnt;
	return 1;

out_dput:
	/* Creation error path, e.g. permission failure */
	record = dentry->d_dentry_rec;
	if (record) {
		if (record->r_dentry_addr)
			dput(record->r_dentry_addr);
		if (record->r_dentry_copy)
			dput(record->r_dentry_copy);
		dput((struct dentry *) record->r_sort_key);
		memlog_free_dentry_rec(record);
	}

	dput(dentry);
	return error;
}



/* unlink system call */
long do_unlinkat_memlog(int dfd, const char __user *pathname)
{
	int error;
	struct filename *name;
	struct dentry *dentry, *dentry_local;
	struct nameidata nd;
	struct inode *inode = NULL;
	//struct inode *delegated_inode = NULL;
	unsigned int lookup_flags = 0;
	bool exist_global, exist_local;
	struct dentry_rec *record;
retry:
	name = user_path_parent(dfd, pathname, &nd, lookup_flags);
	if (IS_ERR(name))
		return PTR_ERR(name);

	error = -EISDIR;
	if (nd.last_type != LAST_NORM)
		goto exit1;

	nd.flags &= ~LOOKUP_PARENT;
	error = mnt_want_write(nd.path.mnt);
	if (error)
		goto exit1;
//retry_deleg:
	mutex_lock_nested(&nd.path.dentry->d_inode->i_mutex, I_MUTEX_PARENT);

	dentry = lookup_hash(&nd);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto exit3;

	dentry_local = memlog__d_lookup(nd.path.dentry, &nd.last);
	exist_global = d_is_negative(dentry);  /* predicate A */
	exist_local = (dentry_local != NULL);  /* predicate B */

	/* Predicate A: exists global positive dentry;
	 * Predicate B: exists local positive dentry */
	if (nd.last.name[nd.last.len])
		goto slashes;

	/* Case 1: (!A && !B), slashes, no file exists */
	if (d_is_negative(dentry) && (!dentry_local)) {
		fs_tx_debug("Unlink: case 1, (!A && !B)\n");
		goto slashes;
	}

	inode = dentry->d_inode;
	//ihold(inode);

	((memlog_t *) current->memlog)->l_write = true;

	/* Case 2 (1/3): (A && !B), file exists globally, not unlinked before
	 *         - create a REC_DEL entry */
	if (!d_is_negative(dentry) && (!dentry_local)) {
		fs_tx_debug("Unlink: case 2, (A && !B)\n");
		goto create_del;
	}

	/* Case 3: (!A && B), newly created - unlink and abort local dentry */
	record = dentry_local->d_dentry_rec;
	BUG_ON(!record);
	if (d_is_negative(dentry) && dentry_local) {
		fs_tx_debug("Unlink: case 3, (!A && B)\n");
		BUG_ON(!record->r_new_d_inode);

		if (record->r_flags == REC_NEW) {
			memlog_unlink_new_dentry_rec(record, true);
		} else {
			BUG_ON(record->r_flags != REC_COPY);
			memlog_unlink_copy_dentry_rec(record, true);
		}
		dput((struct dentry *) record->r_sort_key);
		memlog_free_dentry_rec(record);

		goto exit2;
	}

	/* Case 4: (A && B), inside local tx - abort local dentry, create
	 * (!d_is_negative(dentry) && dentry_local) */
	fs_tx_debug("Unlink: case 4, (A && B)\n");
	if (record->r_flags == REC_DEL) {
		if (record->r_dentry_rec) {
			fs_tx_debug("\tAbort local new dentry after unlink & create\n");
			/* Global file deleted then created,
			 * abort local new dentry */
			memlog_unlink_new_dentry_rec(record->r_dentry_rec, true);
			/* dput() for balancing REC_NEW_REF alloc */
			dput((struct dentry *) record->r_dentry_rec->r_sort_key);
			memlog_free_dentry_rec(record->r_dentry_rec);
			record->r_dentry_rec = NULL;

		} else {
			/* Already deleted, do nothing */
			fs_tx_debug("\t!record->r_dentry_copy, already unlinked\n");
		}
		goto exit2;
	} else if (record->r_flags == REC_COPY) {
		/* Case 2 (2/3): In local tx
		 * - abort local copied dentry, create local REC_DEL entry */
		fs_tx_debug("\tAbort local copy dentry\n");
		memlog_unlink_copy_dentry_rec(record, true);
		dput((struct dentry *) record->r_sort_key);
		memlog_free_dentry_rec(record);

		goto create_del;
	} else if (record->r_flags == REC_NEW) {
		/* Case 2 (2/3): In local tx
		 * - abort local new dentry, create local REC_DEL entry */
		fs_tx_debug("\tAbort local new dentry\n");
		memlog_unlink_new_dentry_rec(record, true);
		dput((struct dentry *) record->r_sort_key);
		memlog_free_dentry_rec(record);

		goto create_del;
	} else {
		BUG_ON(1);
	}

create_del:
	error = security_path_unlink(&nd.path, dentry);
	if (error)
		goto exit2;

	//error = vfs_unlink(nd.path.dentry->d_inode, dentry, &delegated_inode);
	dentry_local = d_alloc_del_memlog(dentry, dfd);
	goto exit3;

exit2:
	dput(dentry); // for balancing lookup()
	fs_tx_debug("Exit2: dentry = %p, dentry->d_lockref.count = %d", dentry, dentry->d_lockref.count);
exit3:
	mutex_unlock(&nd.path.dentry->d_inode->i_mutex);

#if 0
	if (inode)
		iput(inode);	/* truncate the inode here */
	inode = NULL;
	if (delegated_inode) {
		error = break_deleg_wait(&delegated_inode);
		if (!error)
			goto retry_deleg;
	}
#endif

	mnt_drop_write(nd.path.mnt);
exit1:
	path_put(&nd.path);
	putname(name);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		inode = NULL;
		goto retry;
	}
	return error;

slashes:
	if (d_is_negative(dentry))
		error = -ENOENT;
	else if (d_is_dir(dentry))
		error = -EISDIR;
	else
		error = -ENOTDIR;
	goto exit2;
}
