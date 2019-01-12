#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include "log.h"

static struct kmem_cache *memlog_inode_rec_cachep;

static inline void inode_rec_lock_list_add(struct inode_rec *record,
	struct list_head *list)
{
	struct list_head *item;

	item = &record->r_lock_list;
	list_add_tail(item, list);
}

static inline void inode_rec_lock_list_del(struct inode_rec *record)
{
	struct list_head *item;

	item = &record->r_lock_list;
	if (!list_empty(item))
		list_del(item);
}

struct inode_rec *memlog_alloc_inode_rec(struct inode *inode, int flags)
{
	struct inode_rec *ret = kmem_cache_zalloc(memlog_inode_rec_cachep, (__GFP_IO | __GFP_FS));
	memlog_t *memlog;

	memlog = current->memlog;
	L_ASSERT(memlog);

	if (!ret)
		return NULL;
	ret->r_inode_addr = NULL;
	INIT_LIST_HEAD(&ret->r_lock_list);

	if (flags & REC_COPY) {
		ret->r_inode_copy = inode;
		ret->r_flags = REC_COPY;

		/* ext4_dirty_inode */
		memlog->l_overestimated_credits += 2;

	} else if (flags & REC_NEW) {
		printk("REC_NEW deprecated.\n");
		BUG_ON(1);

	} else if (flags & REC_NEW_REF) {
		L_ASSERT(inode);
		ret->r_inode_copy = inode;
		ret->r_flags = REC_NEW;

		memlog->l_overestimated_credits += 8;

	} else if (flags & REC_DEL) {
		printk("REC_DEL deprecated.\n");
		BUG_ON(1);

	} else if (flags & REC_LOCK) {
		ret->r_sort_key = inode;
		__iget(inode);
		ret->r_flags = flags;
	}

	inode_rec_lock_list_add(ret, &memlog->l_inode_lockset);

	return ret;
}

inline void memlog_free_inode_rec(struct inode_rec *record)
{
	if (!record) return;
	inode_rec_lock_list_del(record);
	kmem_cache_free(memlog_inode_rec_cachep, record);
}

void memlog_destroy_inode_rec(struct inode_rec *record)
{
	struct inode *inode = record->r_inode_copy;

	if (inode->i_sb->s_op->destroy_inode_rec)
		inode->i_sb->s_op->destroy_inode_rec(record);
	else
		memlog_free_inode_rec(record);
}

static int inode_rec_ino_cmp(void *priv, struct list_head *a,
		struct list_head *b)
{
	struct inode_rec *record_a = container_of(a, struct inode_rec, r_lock_list);
	struct inode_rec *record_b = container_of(b, struct inode_rec, r_lock_list);

	return (((struct inode*) record_a->r_sort_key)->i_ino
			>= ((struct inode*) record_b->r_sort_key)->i_ino);
}

static int inode_rec_cmp(void *priv, struct list_head *a,
		struct list_head *b)
{
	struct inode_rec *record_a = container_of(a, struct inode_rec, r_lock_list);
	struct inode_rec *record_b = container_of(b, struct inode_rec, r_lock_list);

	return (record_a->r_sort_key >= record_b->r_sort_key);
}

static inline int memlog_lock_inodes_mutex(memlog_t *memlog)
{
	struct inode_rec *record, *record_next;
	struct inode *inode, *inode_prev = NULL;

	list_sort(NULL, &memlog->l_inode_lockset, inode_rec_cmp);

	/* Temporary fix for the i_mutex parent & child lockup.
	 * A parent directory's i_mutex should always be locked before a child's.
	 * A better way to do this in the future is to set up a locking tree.
	 * Or a more efficient temporary solution is to split the lock set into a
	 * directory set and a file set. */
	list_for_each_entry_safe(record, record_next, &memlog->l_inode_lockset,
		r_lock_list) {

		inode = (struct inode *) record->r_sort_key;

		if (inode == inode_prev) {
			iput(inode);
			/* repeating inodes, remove them from lock set. */
			memlog_free_inode_rec(record);
			continue;
		}

		/* 1st round: grab i_mutex only for directories. */
		if (!(record->r_flags & REC_DIR)) continue;

		mutex_lock(&inode->i_mutex);
		inode_prev = inode;
	}

	inode_prev = NULL;
	list_for_each_entry_safe(record, record_next, &memlog->l_inode_lockset,
		r_lock_list) {

		inode = (struct inode *) record->r_sort_key;

		/* 2nd round: grab i_mutex for files. */
		if (record->r_flags & REC_DIR) continue;

		mutex_lock(&inode->i_mutex);
		inode_prev = inode;
	}
	return 0;
}

static inline void memlog_lock_inodes_bh(memlog_t *memlog)
{
	struct inode_rec *record, *record_next;
	struct inode *inode;

	list_sort(NULL, &memlog->l_inode_lockset, inode_rec_ino_cmp);

	/* Blocking locks, might cause the thread to sleep. */
	list_for_each_entry_safe(record, record_next, &memlog->l_inode_lockset,
		r_lock_list) {

		inode = (struct inode *) record->r_sort_key;

		if (record->r_flags & REC_DIR) {
			if (likely(inode->i_sb->s_op->lock_dir_bh))
				inode->i_sb->s_op->lock_dir_bh(inode);
			else
				WARN_ON(1);
		} else {
			if (likely(inode->i_sb->s_op->lock_inode_bh))
				inode->i_sb->s_op->lock_inode_bh(inode);
			else
				WARN_ON(1);
		}
	}
}

static inline void memlog_lock_inodes_spinlock(memlog_t *memlog)
{
	struct inode_rec *record, *record_next;
	struct inode *inode;

	list_sort(NULL, &memlog->l_inode_lockset, inode_rec_cmp);

	/* Spinlocks. */
	list_for_each_entry_safe(record, record_next, &memlog->l_inode_lockset,
		r_lock_list) {

		inode = (struct inode *) record->r_sort_key;

		fs_tx_debug("locking inode (%d, %p)....\n", inode->i_ino, inode);
		spin_lock(&inode->i_lock);

		inode->i_state |= I_IN_COMMIT;
		inode->i_commit_pid = current->pid;
	}
}

static inline void memlog_unlock_inodes_mutex(memlog_t *memlog)
{
	struct inode_rec *record, *record_next;
	struct inode *inode;

	list_for_each_entry_safe(record, record_next, &memlog->l_inode_lockset,
		r_lock_list) {

		inode = (struct inode *) record->r_sort_key;
		mutex_unlock(&inode->i_mutex);
	}
}

static inline void memlog_unlock_inodes_bh(memlog_t *memlog)
{
	struct inode_rec *record, *record_next;
	struct inode *inode;

	list_for_each_entry_safe(record, record_next, &memlog->l_inode_lockset,
		r_lock_list) {

		inode = (struct inode *) record->r_sort_key;

		if (record->r_flags & REC_DIR) {
			if (likely(inode->i_sb->s_op->unlock_dir_bh))
				inode->i_sb->s_op->unlock_dir_bh(inode);
			else
				WARN_ON(1);
		} else {
			if (likely(inode->i_sb->s_op->unlock_inode_bh))
				inode->i_sb->s_op->unlock_inode_bh(inode);
			else
				WARN_ON(1);
		}
	}
}

static inline void memlog_unlock_inodes_spinlock(memlog_t *memlog)
{
	struct inode_rec *record, *record_next;
	struct inode *inode;

	list_for_each_entry_safe(record, record_next, &memlog->l_inode_lockset,
		r_lock_list) {

		inode = (struct inode *) record->r_sort_key;
		fs_tx_debug("unlocking inode (%d, %p)....\n", inode->i_ino, inode);

		if (!(inode->i_state & I_IN_COMMIT)) {
			printk("WARNING: inode (%p, %lu) i_state changes to %lu\n",
					inode, inode->i_ino, inode->i_state);
		}

		inode->i_state &= ~I_IN_COMMIT;
		inode->i_commit_pid = -1;

		spin_unlock(&inode->i_lock);
	}
}

static void memlog_put_inode_references(memlog_t *memlog)
{
	struct inode_rec *record, *record_next;
	struct inode *inode;

	list_for_each_entry_safe(record, record_next, &memlog->l_inode_lockset,
		r_lock_list) {

		inode = (struct inode *) record->r_sort_key;
		iput(inode);
		record->r_sort_key = NULL;
	}
}

static inline void memlog_dirty_inodes(memlog_t *memlog)
{
	struct dentry_rec *drecord, *dnext;

	list_for_each_entry_safe(drecord, dnext, &memlog->l_dentry_recs, r_log_list) {
		struct inode *inode = drecord->r_dentry_addr->d_inode;
		/* If d_inode is NULL, it means some other thread unlinked the
		   file after we releases the dentry reference. But it doesn't
		   matter. */
		if (inode) {

		  __mark_inode_dirty(inode, I_DIRTY_PAGES | I_DIRTY_SYNC
				  | I_DIRTY_DATASYNC);
		}
	}
	list_for_each_entry_safe(drecord, dnext, &memlog->l_dentry_new, r_log_list) {
		struct inode *inode = drecord->r_dentry_copy->d_inode;
		if (inode) {
			__mark_inode_dirty(inode, I_DIRTY_PAGES | I_DIRTY_SYNC
					| I_DIRTY_DATASYNC);
		}
	}
}

static inline int check_inode_conflict(struct dentry_rec *drecord)
{
	struct inode *inode = drecord->r_dentry_addr->d_inode;

	if (!inode || timespec_compare(&inode->i_ctime,
		    &drecord->r_inode_start_ctime) > 0) {
		fs_tx_debug("Copied inode conflict: addr %p, start i_ctime = %lld.%.9ld"
			    "now i_ctime = %lld.%.9ld", inode,
			    (long long)drecord->r_inode_start_ctime.tv_sec,
			    drecord->r_inode_start_ctime.tv_nsec,
			    (long long)inode->i_ctime.tv_sec,
			    inode->i_ctime.tv_nsec);
		return -ECONFLICT;
	}
	return 0;
}

static int memlog_check_inode_conflict(memlog_t *memlog)
{
	struct dentry_rec *drecord, *drecord_next;

	list_for_each_entry_safe(drecord, drecord_next, &memlog->l_dentry_recs, r_log_list) {
		if (!drecord->r_new_d_inode && check_inode_conflict(drecord))
			return -ECONFLICT;
	}
	list_for_each_entry_safe(drecord, drecord_next, &memlog->l_dentry_recs, r_log_list) {
		if (!drecord->r_new_d_inode && check_inode_conflict(drecord))
			return -ECONFLICT;
	}
	list_for_each_entry_safe(drecord, drecord_next, &memlog->l_dentry_delete, r_log_list) {
		if (check_inode_conflict(drecord)) return -ECONFLICT;
	}
	return 0;
}



memlog_t * alloc_memlog(void)
{
	memlog_t *memlog;

	memlog = kzalloc(sizeof(*memlog), GFP_KERNEL);
	if (!memlog)
		return NULL;

	if (memlog_init_hashtable(memlog))
		goto err_out;

	INIT_LIST_HEAD(&memlog->l_page_lockset);
	INIT_LIST_HEAD(&memlog->l_inode_lockset);
	INIT_LIST_HEAD(&memlog->l_dentry_lockset);
	INIT_LIST_HEAD(&memlog->l_dentry_recs);
	INIT_LIST_HEAD(&memlog->l_dentry_new);
	INIT_LIST_HEAD(&memlog->l_dentry_delete);
	INIT_LIST_HEAD(&memlog->l_read_pages);

	memlog->l_aborted = false;
	memlog->l_write = false;
	memlog->l_overestimated_credits = 0;
	memlog->l_journal = 0;
	memlog->l_nested = 0;

	return memlog;
err_out:
	kfree(memlog);
	return NULL;
}

int abort_memlog(memlog_t *memlog)
{
	int ret = 0;
	struct dentry_rec *drecord, *dnext;

	local_bh_disable();

	memlog_lock_pages(memlog);

	list_for_each_entry_safe(drecord, dnext, &memlog->l_dentry_recs, r_log_list) {
		memlog_unlink_copy_dentry_rec(drecord, false);
	}
	list_for_each_entry_safe(drecord, dnext, &memlog->l_dentry_new, r_log_list) {
		memlog_unlink_new_dentry_rec(drecord, false);
	}
	list_for_each_entry_safe(drecord, dnext, &memlog->l_dentry_delete, r_log_list) {
		memlog_unlink_del_dentry_rec(drecord);
	}

	memlog_clean_page_access(memlog);

	memlog_unlock_pages_put_references(memlog);

	memlog_put_inode_references(memlog);
	memlog_put_dentry_references(memlog);

	current->in_fs_tx = 0;
	local_bh_enable();

	return ret;
}

int commit_memlog(memlog_t *memlog)
{
	struct dentry_rec *drecord, *dnext;
	int ret = 0, total = memlog->l_overestimated_credits;

	/*
	 * Need to disable softirq here. Otherwise, a signal, such as SIGINT,
	 * can interfere with commit and do_exit() will call fs_txabort_tsk()
	 * again, which may free pages that have already been free-ed.
	 */
	local_bh_disable();

	/* Make sure to get all blocking locks before spinlocks. */

	/* inode->i_mutex should be grabbed before page locks, since there is
	 * the lock order: generic_file_write_iter - inode->i_mutex
	 *			=> ext4_da_write_begin - lock_page */
	/* Blocking locks starting from here. */
	memlog_lock_inodes_mutex(memlog);
	memlog_lock_pages(memlog);
	memlog_lock_inodes_bh(memlog);
	/* If there are newly created inodes inside the user tx,
	 * grab the global inode_sb_list_lock to avoid the deadlock between
	 * 	commit_memlg() [i_lock] => inode_sb_list_add() [inode_sb_list_lock]
	 * and	sync() => wait_sb_inodes() => [inode_sb_list_lock => i_lock] */
	/* Spinlocks starting from here. */
	lock_inode_hash_lock();
	lock_sb_list_lock();
	memlog_lock_inodes_spinlock(memlog);
	memlog_lock_dentries(memlog);

	current->in_fs_tx |= MEMLOG_IN_COMMIT;

	ret = memlog_check_dentry_conflict(memlog);
	if (ret == -ECONFLICT)
		goto out_unlock_all;
	ret = memlog_check_inode_conflict(memlog);
	if (ret == -ECONFLICT)
		goto out_unlock_all;

	if (total)
		ret = jbd2_try_transfer_overestimated_credits_global(total);
	else {
		memlog->l_overestimated_credits = -1; /* Mark as skipped. */
		fs_tx_debug("Skipping jbd2_try_transfer_overestimated_credits_global....\n");
	}
	if (ret) goto out_unlock_all;

	list_for_each_entry_safe(drecord, dnext, &memlog->l_dentry_recs, r_log_list) {
		ret |= memlog_commit_copy_dentry_rec(drecord);
	}
	list_for_each_entry_safe(drecord, dnext, &memlog->l_dentry_new, r_log_list) {
		ret |= memlog_commit_new_dentry_rec(drecord);
	}
	list_for_each_entry_safe(drecord, dnext, &memlog->l_dentry_delete, r_log_list) {
		ret |= memlog_commit_del_dentry_rec(drecord);
	}

	memlog_clean_page_access(memlog);

	current->in_fs_tx &= ~MEMLOG_IN_COMMIT;

	memlog_unlock_dentries(memlog);
	memlog_unlock_inodes_spinlock(memlog);
	unlock_sb_list_lock();
	unlock_inode_hash_lock();
	memlog_unlock_inodes_bh(memlog);
	memlog_unlock_inodes_mutex(memlog);

	memlog_unlock_pages_put_references(memlog);

	memlog_put_inode_references(memlog);
	memlog_put_dentry_references(memlog);

	current->in_fs_tx |= MEMLOG_IN_DIRTY_INODES;
	memlog_dirty_inodes(memlog);
	current->in_fs_tx &= ~MEMLOG_IN_DIRTY_INODES;

	current->in_fs_tx = 0;
	ret = jbd2_submit_user_tx();

	current->in_fs_tx = 0;
	local_bh_enable();

	return ret;

out_unlock_all:
	current->in_fs_tx &= ~MEMLOG_IN_COMMIT;

	memlog_unlock_dentries(memlog);
	memlog_unlock_inodes_spinlock(memlog);
	unlock_sb_list_lock();
	unlock_inode_hash_lock();
	memlog_unlock_inodes_bh(memlog);
	memlog_unlock_pages(memlog);
	memlog_unlock_inodes_mutex(memlog);

	local_bh_enable();

	return ret;
}

int free_memlog(memlog_t *memlog)
{
	int ret = 0;
	struct dentry_rec *drecord, *dnext;
	struct inode_rec *record, *record_next;
	struct page_rec *precord, *precord_next;

	if (!memlog) return 0;

	list_for_each_entry_safe(drecord, dnext, &memlog->l_dentry_recs, r_log_list) {
		memlog_free_dentry_rec(drecord);
	}
	list_for_each_entry_safe(drecord, dnext, &memlog->l_dentry_new, r_log_list) {
		memlog_free_dentry_rec(drecord);
	}
	list_for_each_entry_safe(drecord, dnext, &memlog->l_dentry_delete, r_log_list) {
		memlog_free_dentry_rec(drecord);
	}
	list_for_each_entry_safe(drecord, dnext, &memlog->l_dentry_lockset, r_lock_list) {
		memlog_free_dentry_rec(drecord);
	}
	list_for_each_entry_safe(record, record_next, &memlog->l_inode_lockset,
			r_lock_list) {
		memlog_free_inode_rec(record);
	}
	list_for_each_entry_safe(precord, precord_next, &memlog->l_page_lockset,
			r_lock_list) {
		memlog_free_page_rec(precord);
	}

	memlog_destroy_hashtable(memlog);
	kfree(memlog);

	return ret;
}



static int __init memlog_inode_rec_init(void)
{
	int retval = 0;

	L_ASSERT(memlog_inode_rec_cachep == NULL);
	memlog_inode_rec_cachep = kmem_cache_create("memlog_inode_rec_cache",
					     sizeof(struct inode_rec),
					     0,
					     (SLAB_RECLAIM_ACCOUNT|SLAB_PANIC|
					     SLAB_MEM_SPREAD), NULL);
	if (!memlog_inode_rec_cachep) {
		retval = -ENOMEM;
		printk(KERN_EMERG "MEMLOG: no memory for memlog inode_rec cache\n");
	}
	return retval;
}

static int __init memlog_init_cache(void)
{
	int ret;

	ret = memlog_inode_rec_init();
	if (ret == 0)
		ret = memlog_dcache_init();
	if (ret == 0)
		ret = memlog_dentry_rec_init();
	if (ret == 0)
		ret = memlog_page_rec_init();
	if (ret == 0)
		ret = read_page_info_init();
	return ret;
}

static void memlog_destroy_cache(void)
{
	if (memlog_inode_rec_cachep)
		kmem_cache_destroy(memlog_inode_rec_cachep);
	memlog_destroy_read_page_info_cache();
	memlog_destroy_dcache();
	memlog_destroy_page_rec_cache();
}

static int __init memlog_module_init(void)
{
	int ret;

	ret = memlog_init_cache();
	if (ret)
		memlog_destroy_cache();
	return ret;
}

static void __exit memlog_module_exit(void)
{
	memlog_destroy_cache();
}

MODULE_LICENSE("GPL");
module_init(memlog_module_init);
module_exit(memlog_module_exit);
