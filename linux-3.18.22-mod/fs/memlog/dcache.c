#include <linux/slab.h>
#include <linux/hash.h>
#include <linux/fsnotify.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include "log.h"

static struct kmem_cache *memlog_dentry_rec_cache;

static unsigned int memlog_d_hash_shift __read_mostly;
static unsigned long dhash_entries __read_mostly;

int __init memlog_dcache_init(void)
{
	memlog_d_hash_shift = 8;
	dhash_entries = 1U << memlog_d_hash_shift;
	return 0;
}

int __init memlog_dentry_rec_init(void)
{
	int retval = 0;

	L_ASSERT(memlog_dentry_rec_cache == NULL);
	memlog_dentry_rec_cache = kmem_cache_create("memlog_dentry_rec_cache",
					     sizeof(struct dentry_rec),
					     0,
					     (SLAB_RECLAIM_ACCOUNT|SLAB_PANIC|
					     SLAB_MEM_SPREAD), NULL);
	if (!memlog_dentry_rec_cache) {
		retval = -ENOMEM;
		printk(KERN_EMERG "MEMLOG: no memory for memlog dentry_rec cache\n");
	}
	return retval;
}

void memlog_destroy_dcache(void)
{
	if (memlog_dentry_rec_cache)
		kmem_cache_destroy(memlog_dentry_rec_cache);
}

static inline void dentry_rec_log_list_add(struct dentry_rec *record,
	struct list_head *list)
{
	struct list_head *item;

	item = &record->r_log_list;
	list_add_tail(item, list);
}

inline void dentry_rec_log_list_del(struct dentry_rec *record)
{
	struct list_head *item;

	item = &record->r_log_list;
	if (!list_empty(item))
		list_del_init(item);
}

static inline void dentry_rec_lock_list_add(struct dentry_rec *record,
	struct list_head *list)
{
	struct list_head *item;

	item = &record->r_lock_list;
	list_add_tail(item, list);
}

static inline void dentry_rec_lock_list_del(struct dentry_rec *record)
{
	struct list_head *item;

	item = &record->r_lock_list;
	if (!list_empty(item))
		list_del_init(item);
}

struct dentry_rec *memlog_alloc_dentry_rec(struct dentry *dentry, int flags)
{
	struct dentry_rec *ret = kmem_cache_zalloc(memlog_dentry_rec_cache, (__GFP_IO | __GFP_FS));
	memlog_t *memlog;

	memlog = current->memlog;
	L_ASSERT(memlog);

	if (!ret)
		return NULL;
	ret->r_need_close = false;
	ret->r_memlog = memlog;
	INIT_LIST_HEAD(&ret->r_log_list);
	INIT_LIST_HEAD(&ret->r_lock_list);

	if (flags & REC_COPY) {
		L_ASSERT(dentry);
		ret->r_dentry_copy = dentry;
		ret->r_flags = REC_COPY;
		dentry_rec_log_list_add(ret, &memlog->l_dentry_recs);

	} else if (flags & REC_NEW) {
		BUG_ON(1);

	} else if (flags & REC_NEW_REF) {
		L_ASSERT(dentry);
		ret->r_dentry_copy = dentry;
		ret->r_sort_key = dentry;
		__dget(dentry);
		ret->r_flags = REC_NEW;
		dentry_rec_log_list_add(ret, &memlog->l_dentry_new);

	} else if (flags & REC_DEL) {
		L_ASSERT(dentry);
		ret->r_dentry_copy = dentry;
		ret->r_flags = REC_DEL;
		dentry_rec_log_list_add(ret, &memlog->l_dentry_delete);

	} else if (flags & REC_LOCK) {
		dget(dentry);
		ret->r_sort_key = dentry;
	}

	dentry_rec_lock_list_add(ret, &memlog->l_dentry_lockset);

	return ret;
}

inline void memlog_free_dentry_rec(struct dentry_rec *drecord)
{
	if (!drecord) return;
	dentry_rec_log_list_del(drecord);
	dentry_rec_lock_list_del(drecord);
	kmem_cache_free(memlog_dentry_rec_cache, drecord);
}

static int dentry_rec_cmp(void *priv, struct list_head *a,
		struct list_head *b)
{
	struct dentry_rec *drecord_a = container_of(a, struct dentry_rec, r_lock_list);
	struct dentry_rec *drecord_b = container_of(b, struct dentry_rec, r_lock_list);

	return (drecord_a->r_sort_key >= drecord_b->r_sort_key);
}

void memlog_lock_dentries(memlog_t *memlog)
{
	struct dentry_rec *drecord, *drecord_next;
	struct dentry *dentry, *dentry_prev = NULL;

	list_sort(NULL, &memlog->l_dentry_lockset, dentry_rec_cmp);

	/* Spinlocks. */
	list_for_each_entry_safe(drecord, drecord_next, &memlog->l_dentry_lockset,
		r_lock_list) {

		dentry = (struct dentry *) drecord->r_sort_key;

		if (dentry == dentry_prev) {
			/* repeating dentries, remove them from lock set. */
			dentry->d_lockref.count--;
			memlog_free_dentry_rec(drecord);
			continue;
		}

		dentry->d_flags |= DCACHE_IN_COMMIT;
		spin_lock(&dentry->d_lock);
		dentry->d_commit_pid = current->pid;
		//fs_tx_debug("dentry %p locked.\n", dentry);
		dentry_prev = dentry;
	}
}

void memlog_unlock_dentries(memlog_t *memlog)
{
	struct dentry_rec *drecord, *drecord_next;
	struct dentry *dentry;

	list_for_each_entry_safe(drecord, drecord_next, &memlog->l_dentry_lockset,
		r_lock_list) {

		dentry = (struct dentry *) drecord->r_sort_key;
		dentry->d_flags &= ~DCACHE_IN_COMMIT;
		dentry->d_commit_pid = -1;
		spin_unlock(&dentry->d_lock);
	}
}

static inline int check_dentry_new_conflict(struct dentry_rec *drecord)
{
	struct dentry *dentry_global,
		      *dentry = drecord->r_dentry_copy;

	dentry_global = d_lookup(dentry->d_parent, &dentry->d_name);
	if (dentry_global) {
		fs_tx_debug("New dentry creation conflict: local %p, "
				"global %p", dentry, dentry_global);
		dput(dentry_global);
		return -ECONFLICT;
	}

	return 0;
}

static inline int check_dentry_conflict(struct dentry_rec *drecord)
{
	struct dentry *dentry = drecord->r_dentry_addr;

	if (dentry->d_ctime > drecord->r_dentry_start_ctime) {
		fs_tx_debug("Copied dentry conflict: addr %p, start d_ctime = %lu, "
			    "now d_ctime = %lu", dentry,
			    drecord->r_dentry_start_ctime, dentry->d_ctime);
		return -ECONFLICT;
	}
	//WARN_ON(dentry->d_ctime != drecord->r_dentry_start_ctime);
	return 0;
}

int memlog_check_dentry_conflict(memlog_t *memlog)
{
	struct dentry_rec *drecord, *drecord_next;

	list_for_each_entry_safe(drecord, drecord_next, &memlog->l_dentry_new, r_log_list) {
		if (check_dentry_new_conflict(drecord)) return -ECONFLICT;
	}

	list_for_each_entry_safe(drecord, drecord_next, &memlog->l_dentry_recs, r_log_list) {
		if (check_dentry_conflict(drecord)) return -ECONFLICT;
	}

	list_for_each_entry_safe(drecord, drecord_next, &memlog->l_dentry_delete, r_log_list) {
		if (check_dentry_conflict(drecord)) return -ECONFLICT;
	}
	return 0;
}

void memlog_put_dentry_references(memlog_t *memlog)
{
	struct dentry_rec *drecord, *drecord_next;
	struct dentry *dentry;

	list_for_each_entry_safe(drecord, drecord_next, &memlog->l_dentry_lockset,
		r_lock_list) {

		dentry = (struct dentry *) drecord->r_sort_key;
		dput(dentry);
		drecord->r_sort_key = NULL;
	}
}



static void memlog_unlink_dentry_rec(struct dentry_rec *record)
{
	struct dentry *dentry = record->r_dentry_addr;
	struct dentry *copy = record->r_dentry_copy;

	/* Existing dentry, existing file */  // check!
	if (!record->r_new_d_inode) {
		struct inode *inode_cp = copy->d_inode;

		BUG_ON(!dentry);
		if (likely(inode_cp->i_op->abort_copy_inode_memlog))
			inode_cp->i_op->abort_copy_inode_memlog(dentry->d_inode, inode_cp);
		else
			WARN_ON(1);

		record->r_file->f_dentry = dentry;
		record->r_file->f_inode = dentry->d_inode;
		record->r_file->f_mapping = dentry->d_inode->i_mapping;

		dentry->d_dentry_rec = NULL;

		goto out;
	}

	/* Existing dentry, no existing file */  // check!
	fs_tx_debug("Abort: dentry copy = %p, d_lockref.count = %d\n",
		    copy, copy->d_lockref.count);
	dentry->d_dentry_rec = NULL;

	//BUG_ON(d_unhashed(dentry)); // Got this BUG. I thought it's unecessary.

	dput(dentry);

out:
	if (copy->d_inode)
		clear_nlink(copy->d_inode);
	/* This will calls into d_delete(copy) -> iput(copy->d_inode) */
	spin_lock(&copy->d_lock);
	memlog__d_drop(copy, record->r_memlog);
	copy->d_parent = copy; /* Make it isolated. */
	copy->d_flags &= ~DCACHE_IN_MEM;
	dput_nolock(copy);
	spin_unlock(&copy->d_lock);
}

static int memlog_abort_copy_dentry_rec(struct dentry_rec *record, bool lock_pages);

void memlog_unlink_copy_dentry_rec(struct dentry_rec *record, bool lock_pages)
{
	memlog_abort_copy_dentry_rec(record, lock_pages);
	memlog_unlink_dentry_rec(record);
}

void memlog_unlink_new_dentry_rec(struct dentry_rec *record, bool lock_pages)
{
	struct dentry *dentry = record->r_dentry_copy;
	//struct dentry *parent = dentry->d_parent;
	//BUG_ON(!dentry);

	memlog__d_drop(dentry, record->r_memlog);

	/* New dentry, existing file inode */  // check!
	if (!record->r_new_d_inode) {
		struct inode *inode = record->r_inode_addr;
		struct inode *inode_cp = dentry->d_inode;

		fs_tx_debug("Aborting copy on dentry_rec: dentry = %p", dentry);
		abort_copy_dentry_inode(inode, inode_cp, lock_pages);

		if (likely(inode_cp->i_op->abort_copy_inode_memlog))
			inode_cp->i_op->abort_copy_inode_memlog(dentry->d_inode, inode_cp);
		else
			WARN_ON(1);

		/* free anonymous inode copy */
		clear_nlink(inode_cp);
		ihold(inode_cp);
		iput(inode_cp);

		hlist_del_init(&dentry->d_u.d_alias);
		d_instantiate(dentry, inode);
		if (d_unhashed(dentry))
			d_rehash(dentry);
		__iget(inode);

		/* Cache it in the global dcache. */
		dget(dentry);
		dentry->d_dentry_rec = NULL;
		record->r_file->f_inode = dentry->d_inode;
		record->r_file->f_mapping = dentry->d_inode->i_mapping;

		goto out;
	}

	/* This will calls into iput(copy->d_inode)
	 * Here we need to check dentry->d_inode to avoid releasing inode
	 * for newly created _negative_ local dentry */
	if (dentry->d_inode)
		clear_nlink(dentry->d_inode);

out:
	if (record->r_need_close) {
		__post_close_fd_memlog(current->files, record->r_file, record->r_fd);
		record->r_need_close = false;
	}

	dentry->d_dentry_rec = NULL;
	fs_tx_debug("Abort: d_lockref.count = %d\n", dentry->d_lockref.count);

	dentry->d_flags &= ~DCACHE_IN_MEM;
	dput(dentry);
}

void memlog_unlink_del_dentry_rec(struct dentry_rec *record)
{
	struct dentry *dentry = record->r_dentry_addr,
			*copy = record->r_dentry_copy;

	/* Abort local new dentry */
	if (record->r_dentry_rec)
		memlog_unlink_new_dentry_rec(record->r_dentry_rec, false);

	record->r_dentry_addr->d_dentry_rec = NULL;

	memlog__d_drop(copy, record->r_memlog);
	dput(dentry);
	copy->d_parent = copy; /* Make it isolated. */
	copy->d_flags &= ~DCACHE_IN_MEM;
	dput(copy);
}


int memlog_init_hashtable(memlog_t *memlog)
{
	struct hlist_bl_head *hashtable;
	unsigned int loop;
	int retval = 0;

	L_ASSERT(memlog->l_dentry_hashtable == NULL);
	hashtable = kcalloc(dhash_entries, sizeof(struct hlist_bl_head),
					GFP_KERNEL);
	if (!hashtable) {
		retval = -ENOMEM;
		printk(KERN_EMERG "MEMLOG: no memory for l_dentry_hashtable\n");
	}

	for (loop = 0; loop < dhash_entries; loop++)
		INIT_HLIST_BL_HEAD(hashtable + loop);
	memlog->l_dentry_hashtable = hashtable;
	return retval;
}

void memlog_destroy_hashtable(memlog_t *memlog)
{
	if (memlog->l_dentry_hashtable)
		kfree(memlog->l_dentry_hashtable);
	memlog->l_dentry_hashtable = NULL;
}

static inline struct hlist_bl_head *memlog_d_hash_current(
		const struct dentry *parent, unsigned int hash)
{
	memlog_t *memlog;
	struct hlist_bl_head *hashtable;

	L_ASSERT(current->memlog);
	memlog = current->memlog;
	L_ASSERT(memlog->l_dentry_hashtable);
	hashtable = memlog->l_dentry_hashtable;
	hash += (unsigned long) parent / L1_CACHE_BYTES;
	return hashtable + hash_32(hash, memlog_d_hash_shift);
}

static inline struct hlist_bl_head *memlog_d_hash(const struct dentry *parent,
		unsigned int hash, memlog_t *memlog)
{
	struct hlist_bl_head *hashtable;

	L_ASSERT(memlog->l_dentry_hashtable);
	hashtable = memlog->l_dentry_hashtable;
	hash += (unsigned long) parent / L1_CACHE_BYTES;
	return hashtable + hash_32(hash, memlog_d_hash_shift);
}



/* Add n delete from hashtable. */

void memlog_d_rehash(struct dentry *dentry)
{
	struct hlist_bl_head *b = memlog_d_hash_current(dentry->d_parent,
			dentry->d_name.hash);

	BUG_ON(!d_unhashed(dentry));
	//hlist_bl_lock(b);
	dentry->d_flags |= DCACHE_RCUACCESS;
	hlist_bl_add_head_rcu(&dentry->d_hash, b);
	//hlist_bl_unlock(b);

	fs_tx_debug("REHASH: Hash list addr = %p\n", b);
}

void memlog__d_drop(struct dentry *dentry, memlog_t *memlog)
{
	if (!d_unhashed(dentry)) {
		struct hlist_bl_head *b;
		/*
		 * Hashed dentries are normally on the dentry hashtable,
		 * with the exception of those newly allocated by
		 * d_obtain_alias, which are always IS_ROOT:
		 */
		if (unlikely(IS_ROOT(dentry)))
			b = &dentry->d_sb->s_anon;
		else
			b = memlog_d_hash(dentry->d_parent,
					dentry->d_name.hash, memlog);

//		hlist_bl_lock(b);
		__hlist_bl_del(&dentry->d_hash);
		dentry->d_hash.pprev = NULL;
//		hlist_bl_unlock(b);
		//dentry_rcuwalk_barrier(dentry);
	}
}



static inline int dentry_string_cmp_no_conflict(const unsigned char *cs, const unsigned char *ct,
	unsigned tcount)
{
	do {
		if (*cs != *ct)
			return 1;
		cs++;
		ct++;
		tcount--;
	} while (tcount);
	return 0;
}

struct dentry *memlog__d_lookup(const struct dentry *parent, const struct qstr *name)
{
	unsigned int len = name->len;
	unsigned int hash = name->hash;
	const unsigned char *str = name->name;
	struct hlist_bl_head *b = memlog_d_hash_current(parent, hash); // the difference
	struct hlist_bl_node *node;
	struct dentry *found = NULL;
	struct dentry *dentry;

	fs_tx_debug("LOOKUP: Hash list addr = %p\n", b);

	/*
	 * Note: There is significant duplication with __d_lookup_rcu which is
	 * required to prevent single threaded performance regressions
	 * especially on architectures where smp_rmb (in seqcounts) are costly.
	 * Keep the two functions in sync.
	 */

	/*
	 * The hash list is protected using RCU.
	 *
	 * Take d_lock when comparing a candidate dentry, to avoid races
	 * with d_move().
	 *
	 * It is possible that concurrent renames can mess up our list
	 * walk here and result in missing our dentry, resulting in the
	 * false-negative result. d_lookup() protects against concurrent
	 * renames using rename_lock seqlock.
	 *
	 * See Documentation/filesystems/path-lookup.txt for more details.
	 */

	// TODO: I don't think we need any protection here such as RCU,
	//	since it's per-process operation.

	rcu_read_lock();

	hlist_bl_for_each_entry_rcu(dentry, node, b, d_hash) {

		if (dentry->d_name.hash != hash)
			continue;

		spin_lock(&dentry->d_lock);
		if (dentry->d_parent != parent)
			goto next;
		if (d_unhashed(dentry))
			goto next;

		/*
		 * It is safe to compare names since d_move() cannot
		 * change the qstr (protected by d_lock).
		 */
		if (parent->d_flags & DCACHE_OP_COMPARE) {
			int tlen = dentry->d_name.len;
			const char *tname = dentry->d_name.name;
			if (parent->d_op->d_compare(parent, dentry, tlen, tname, name))
				goto next;
		} else {
			if (dentry->d_name.len != len)
				goto next;
//			if (dentry_cmp(dentry, str, len))
			if (dentry_string_cmp_no_conflict(dentry->d_name.name, str, len))
				goto next;
		}

		dentry->d_lockref.count++;
		found = dentry;
		spin_unlock(&dentry->d_lock);
		break;
next:
		spin_unlock(&dentry->d_lock);
	}
	rcu_read_unlock();

	return found;
}



/* commit dentry */
int memlog_commit_new_dentry_rec(struct dentry_rec *record)
{
	int err = 0;
	struct dentry *dentry = record->r_dentry_copy;

	/* New dentry, existing file inode */
	if (!record->r_new_d_inode) {
		struct inode *inode = record->r_inode_addr;
		struct inode *inode_cp = dentry->d_inode;

		fs_tx_debug("Copying back inode, adding new dentry.\n");
		BUG_ON(!(inode && inode_cp));

		/* copy back inode */
		if (likely(inode->i_op->copy_back_inode_memlog))
			inode->i_op->copy_back_inode_memlog(inode, inode_cp);
		else
			WARN_ON(1);

		/* free anonymous inode copy */
		clear_nlink(inode_cp);

		/* Lockless d_instantiate */
		hlist_del_init(&dentry->d_u.d_alias);
		hlist_add_head(&dentry->d_u.d_alias, &inode->i_dentry);
		dentry->d_inode = inode;
		__iget(inode);

		/* Cache it in the global dcache. */
		record->r_file->f_inode = dentry->d_inode;
		record->r_file->f_mapping = dentry->d_inode->i_mapping;
	}

	/* Continue & New dentry, new file inode */
	fs_tx_debug("Committing new dentry_rec....\n");
	err = do_remaining_new_dentry(dentry, record->r_new_d_inode, record->r_memlog);

	if (record->r_need_close) {
		__post_close_fd_memlog(current->files, record->r_file, record->r_fd);
		record->r_need_close = false;
	}

	dentry->d_dentry_rec = NULL;
	dentry->d_flags &= ~DCACHE_IN_MEM;

	return err; // TODO
}

int memlog_commit_copy_dentry_rec(struct dentry_rec *record)
{
	int err = 0;
	struct dentry *dentry = record->r_dentry_addr;
	struct dentry *copy = record->r_dentry_copy;

	/* Existing dentry */
	if (likely(dentry && copy)) {
		fs_tx_debug("Committing copy on dentry_rec: dentry = %p, copy = %p\n",
			dentry, copy);
		err = do_remaining_copy_dentry(dentry, copy, record->r_new_d_inode);
		record->r_file->f_dentry = dentry;
		record->r_file->f_inode = dentry->d_inode;
		record->r_file->f_mapping = dentry->d_inode->i_mapping;
	} else
		err = -1;

	if (record->r_need_close) {
		/* Otherwise, the inode is free-ed at fput in OpenLDAP ADD.
		 * TODO: But we get a reference leakage as well. */
		if (record->r_new_d_inode) dentry->d_lockref.count++;

		__post_close_fd_memlog(current->files, record->r_file, record->r_fd);
		record->r_need_close = false;
	}

	dentry->d_dentry_rec = NULL;
	memlog__d_drop(copy, record->r_memlog);
	copy->d_parent = copy; /* Make it isolated. */
	copy->d_flags &= ~DCACHE_IN_MEM;
	dput_nolock(copy);

	return err; // TODO
}

static int memlog_abort_copy_dentry_rec(struct dentry_rec *record, bool lock_pages)
{
	struct dentry *dentry = record->r_dentry_addr;
	struct dentry *copy = record->r_dentry_copy;
	struct inode *inode_cp = copy->d_inode;

	if (dentry && copy && !record->r_new_d_inode) {
		fs_tx_debug("Aborting copy on dentry_rec: dentry = %p, copy = %p\n",
			dentry, copy);
		abort_copy_dentry_inode(dentry->d_inode, inode_cp, lock_pages);
		list_del_init(&inode_cp->i_wb_list);
		inode_cp->i_state &= ~I_SYNC;
	}

	if (record->r_need_close) {
		__post_close_fd_memlog(current->files, record->r_file, record->r_fd);
		record->r_need_close = false;
	}

	return 0; // TODO
}

int memlog_commit_del_dentry_rec(struct dentry_rec *record)
{
	struct dentry *dentry = record->r_dentry_addr,
			*copy = record->r_dentry_copy;
	struct inode *inode = dentry->d_inode;
	int error = 0, error2;

	if (d_is_negative(dentry))
		return -ENOENT;

	WARN_ON(dentry->d_lockref.count <= 1);
	fs_tx_debug("Doing vfs_unlink, dentry = %p, dentry->d_lockref.count = %d, "
			"inode->i_count = %d", dentry, dentry->d_lockref.count,
			inode->i_count);

	/* We expect the underlying filesystem not to be NFS exported.
	 * So we pass NULL for delegated_inode. */
	error2 = vfs_unlink(dentry->d_parent->d_inode, dentry, NULL);
	if (unlikely(error2)) fs_tx_debug("vfs_unlink error2 = %d", error2);

	if (record->r_need_close) {
		__post_close_fd_memlog(current->files, record->r_file, record->r_fd);
		record->r_need_close = false;
	}

	dentry->d_dentry_rec = NULL;
	memlog__d_drop(copy, record->r_memlog);

	copy->d_flags &= ~DCACHE_IN_MEM;

	// TODO: We do not handle the vfs_unlink ret val.
	//	 If return error here, currently we either have bug on duplicated
	//	 commit and abort on the same entry, leading to double ref count
	//	 decrements.

	dput_nolock(dentry);

	if (record->r_dentry_rec)
		error |= memlog_commit_new_dentry_rec(record->r_dentry_rec);

	copy->d_parent = copy; /* Make it isolated. */
	copy->d_flags &= ~DCACHE_IN_MEM;
	dput_nolock(copy);

	return error;
}
