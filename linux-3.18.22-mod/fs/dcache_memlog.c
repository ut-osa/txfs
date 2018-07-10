/*
 * fs/memlog_dcache.c
 *
 * Complete reimplementation
 * (C) 2016 Yige Hu,
 */

#include <linux/backing-dev.h>
#include "dcache_memlog.h"

inline void dentry_start_write(struct dentry *dentry)
{
	rdtscll(dentry->d_ctime);
}

static struct dentry *__d_alloc_memlog(struct super_block *sb, const struct qstr *name,
					struct file *file)
{
	struct dentry_rec *record;
	struct dentry *dentry = __d_alloc(sb, name);

	if (dentry) {
		dentry->d_flags |= DCACHE_IN_MEM;

		record = memlog_alloc_dentry_rec(dentry, REC_NEW_REF);
		record->r_new_d_inode = true;
		record->r_file = file;

		dentry->d_dentry_rec = record;
	}
	return dentry;
}

static struct dentry *__d_alloc_copy_memlog(struct super_block *sb, const struct qstr *name,
					struct dentry *input, struct file *file)
{
	struct dentry_rec *record;
	struct dentry *dentry;

	if (unlikely(input->d_inode && (!input->d_inode->i_op->copy_inode_memlog)))
		return input;

	spin_lock(&input->d_lock);
	dentry = __d_alloc_copy(sb, name, input);
	if (likely(dentry)) {
		dentry->d_flags |= DCACHE_IN_MEM;

		record = memlog_alloc_dentry_rec(dentry, REC_COPY);
		record->r_dentry_addr = input;
		record->r_dentry_start_ctime = dentry->d_ctime;

		record->r_sort_key = input;
		__dget_dlock(input);
		spin_unlock(&input->d_lock);

		memlog_alloc_dentry_rec(dentry, REC_LOCK);

		record->r_file = file;
		if (input->d_inode) {
			/* from fdget_pos_memlog() */
			record->r_new_d_inode = false;

			dentry->d_inode = input->d_inode->i_op->copy_inode_memlog(
								input->d_inode);
			__iget(dentry->d_inode);
			record->r_inode_start_ctime = input->d_inode->i_ctime;

//fs_tx_debug("Before tx, inode addr = %p, inode->i_size = %lld:", input->d_inode, input->d_inode->i_size);
//if (input->d_inode->i_op->print_dbg_info)
//	input->d_inode->i_op->print_dbg_info(input->d_inode);

			fs_tx_debug("Got local inode, dentry = %p, addr = %p\n", dentry, dentry->d_inode);
		} else {
			/* from lookup_open_memlog() */
			record->r_new_d_inode = true;
		}

		dentry->d_dentry_rec = record;
	} else {
		spin_unlock(&input->d_lock);
	}
	return dentry;
}

static struct dentry *__d_alloc_del_memlog(struct super_block *sb, const struct qstr *name,
					struct dentry *input, int dfd)
{
	struct dentry_rec *record;
	struct dentry *dentry, *parent = input->d_parent;

	spin_lock(&input->d_lock);
	dentry = __d_alloc_copy(sb, name, input);
	if (likely(dentry)) {
		dentry->d_flags |= DCACHE_IN_MEM;

		record = memlog_alloc_dentry_rec(dentry, REC_DEL);
		record->r_dentry_addr = input;
		record->r_dentry_start_ctime = dentry->d_ctime;
		record->r_inode_start_ctime = input->d_inode->i_ctime;

		record->r_sort_key = input;
		WARN_ON(dentry->d_lockref.count < 1);
		__dget_dlock(input);
		spin_unlock(&input->d_lock);

		dentry->d_dentry_rec = record;

		memlog_alloc_inode_rec(input->d_inode, REC_LOCK);
		memlog_alloc_inode_rec(parent->d_inode, REC_LOCK | REC_DIR);
//		memlog_alloc_dentry_rec(parent, REC_LOCK);

		memlog_alloc_dentry_rec(dentry, REC_LOCK);
	} else {
		spin_unlock(&input->d_lock);
	}
	return dentry;
}

struct dentry *d_alloc_memlog(struct dentry *parent, const struct qstr *name,
				struct file *file)
{
	struct dentry *dentry = __d_alloc_memlog(parent->d_sb, name, file);
	if (!dentry)
		return NULL;
	__dget(parent);
	dentry->d_parent = parent;
	memlog_alloc_dentry_rec(parent, REC_LOCK);
	return dentry;
}

struct dentry *d_alloc_copy_memlog(struct dentry *parent, const struct qstr *name,
				struct dentry *input, struct file *file)
{
	struct dentry *dentry;

	if (!parent) {
		/* from fdget_pos_memlog(), parent == NULL && name == NULL */
		dentry = __d_alloc_copy_memlog(input->d_sb, &input->d_name, input, file);
	} else {
		/* from lookup_open_memlog(), parent != NULL && name != NULL */
		dentry = __d_alloc_copy_memlog(parent->d_sb, name, input, file);
	}
	if (!dentry)
		return NULL;
	return dentry;
}

struct dentry *d_alloc_del_memlog(struct dentry *input, int dfd)
{
	struct dentry *dentry;

	dentry = __d_alloc_del_memlog(input->d_sb, &input->d_name, input, dfd);
	if (!dentry)
		return NULL;
	if (d_unhashed(dentry))
		memlog_d_rehash(dentry);

	return dentry;
}

struct dentry *d_splice_alias_memlog(struct inode *inode, struct dentry *dentry)
{
	struct dentry *new = NULL;

	if (IS_ERR(inode))
		return ERR_CAST(inode);

	if (inode && S_ISDIR(inode->i_mode)) {
		/* We don't support transactional r/w for directories.
		 * So just fall back. */
		printk(KERN_ERR "WARNING: d_splice_alias_memlog(): non-implemented.\n"
				"Trying to open dir %s .\n", dentry->d_name.name);
		return d_splice_alias(inode, dentry);
	} else {
		d_instantiate(dentry, inode);
		BUG_ON(!dentry->d_dentry_rec);
		if (unlikely(dentry->d_dentry_rec->r_dentry_rec)) {
			BUG_ON(dentry->d_dentry_rec->r_dentry_rec->r_flags != REC_DEL);
			return new;
		}
		if (d_unhashed(dentry))
			memlog_d_rehash(dentry);
	}
	return new;
}
EXPORT_SYMBOL(d_splice_alias_memlog);

void d_copy_inode_memlog(struct dentry *input, struct file *file)
{
	struct dentry_rec *record = input->d_dentry_rec;

	BUG_ON(!input->d_inode);
	record->r_file = file;
	record->r_new_d_inode = false;
	record->r_inode_addr = input->d_inode;

	if (likely(input->d_inode->i_op->copy_inode_memlog)) {
		input->d_inode = input->d_inode->i_op->copy_inode_memlog(
							input->d_inode);
		__iget(input->d_inode);
		record->r_inode_start_ctime = input->d_inode->i_ctime;
	} else {
		WARN_ON(true);
	}
	fs_tx_debug("Got local inode, addr = %p\n", input->d_inode);
}



/* Only call with dentry and d_inode locked. */
void dput_nolock(struct dentry *dentry)
{
	struct lockref *lockref;

	if (unlikely(!dentry))
		return;

	lockref = &dentry->d_lockref;
	BUG_ON(!spin_is_locked(&lockref->lock));
	/* Only used during memlog commit;
	 * Never to appear for ready-to-kill dentries */
	BUG_ON(lockref->count <= 1);
	lockref->count--;
}

/* Only call with dentry and d_inode locked. */
void dput_and_kill_isolated_nolock(struct dentry *dentry)
{
	if (unlikely(!dentry))
		return;

	BUG_ON(!spin_is_locked(&dentry->d_lock));
	/* Only used during memlog commit, for an isolated local dentry;
	 * which are ready-to-kill. */
	WARN_ON(dentry->d_lockref.count != 1);

	__dentry_kill_nolock(dentry);
}

/* commit dentry */
int do_remaining_new_dentry(struct dentry *dentry, bool new_d_inode,
		memlog_t *memlog)
{
	int error = 0;
	struct dentry *parent = dentry->d_parent;
	struct inode *dir = parent->d_inode;

	/* lookup_open() -> lookup_dcache() -> d_alloc() */
	BUG_ON(!parent);
	dentry_start_write(parent);
	list_add(&dentry->d_child, &parent->d_subdirs);

	/* lookup_open() -> lookup_real() -> ext4_lookup() -> d_splice_alias() */
	memlog__d_drop(dentry, memlog);
	_d_rehash(dentry);

	/* New dentry, new file  // check!
	 * Here we need to check dentry->d_inode to avoid creating a new inode
	 * for newly created _negative_ local dentry */
	if (!dentry->d_inode)
		return 0;

	if (new_d_inode) {
		/* vfs_create() */
		if (!dir->i_op->post_create_memlog)
			return -EACCES; /* shouldn't it be ENOSYS? */
		error = dir->i_op->post_create_memlog(dentry);
		if (error) goto out;

		inode_sb_list_add(dentry->d_inode);
	}

out:
	return error;
}

int do_remaining_copy_dentry(struct dentry *dentry, struct dentry *copy,
				bool new_d_inode)
{
	int error = 0;
	struct dentry *parent = dentry->d_parent;
	struct inode *dir = parent->d_inode;
	struct inode *inode = dentry->d_inode;
	struct inode *inode_cp = copy->d_inode;

	/* Existing dentry, existing file */  // check!
	if (!new_d_inode) {
		BUG_ON(!(inode && inode_cp));

		if (likely(inode->i_op->copy_back_inode_memlog))
			inode->i_op->copy_back_inode_memlog(inode, inode_cp);
		else
			WARN_ON(1);

		list_del_init(&inode_cp->i_wb_list);
		inode_cp->i_state &= ~I_SYNC;

		if (dentry->d_ctime != copy->d_ctime)
			dentry_start_write(dentry);
		dentry->d_flags = copy->d_flags & ~DCACHE_IN_MEM;

		clear_nlink(copy->d_inode);

		fs_tx_debug("After tx, inode addr = %p, inode->i_size = %lld:",
				inode, inode->i_size);
		goto out;
	}

	/* Existing dentry, new file */  // check!
	/* New inode has been allocated, attach it to original dentry */

	if (inode) {
		fs_tx_debug("Conflict detected: others have created an inode!");
		dput_nolock(dentry);
		error = -1;
		goto out;
	}

	if (dentry->d_ctime != copy->d_ctime)
		dentry_start_write(dentry);
	dentry->d_flags = copy->d_flags & ~DCACHE_IN_MEM;
	dentry->d_inode = inode_cp;
	copy->d_inode = NULL;
	hlist_del_init(&copy->d_u.d_alias);
	BUG_ON(d_unhashed(dentry));

	/* vfs_create() */
	if (!dir->i_op->post_create_memlog)
		return -EACCES; /* shouldn't it be ENOSYS? */
	error = dir->i_op->post_create_memlog(dentry);
	if (error) goto out;

	inode = dentry->d_inode;

	inode_sb_list_add(inode);

out:
	return error;
}
