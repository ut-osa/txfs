#include <linux/security.h>
#include <linux/radix-tree.h>
#include <linux/gfp.h>
#include <linux/sched.h>

#include "dcache_memlog.h"

inline void inode_start_write(struct inode *inode)
{
	if (current->in_fs_tx && (inode->i_state & I_IN_MEM)) {
		/* Now we allow this case to pass through without warning, since
		 * this condition is triggered only on std file descriptors. */
		//WARN_ON(!(inode->i_state & I_IN_MEM)); /* tx cannot change global inodes */
		((memlog_t *) current->memlog)->l_write = true;
	}
}

static struct inode *alloc_inode_memlog(struct super_block *sb)
{
	struct inode_rec *record;
	struct inode *inode;

	if (likely(sb->s_op->alloc_inode_rec)) {
		record = sb->s_op->alloc_inode_rec(sb, REC_NEW_REF);
	} else {
		printk("Error: alloc_inode_rec() cannot be found\n");
		return NULL;
	}

	inode = record->r_inode_copy;
	record->r_sort_key = inode;
	if (!inode)
		return NULL;

	// use original inode_init_always()
	if (unlikely(inode_init_always(sb, inode))) {
		memlog_destroy_inode_rec(record);
		return NULL;
	}
	__iget(inode); /* Must be put after inode_init_always. */
	return inode;
}

struct inode *new_inode_pseudo_memlog(struct super_block *sb)
{
	struct inode *inode = alloc_inode_memlog(sb);

	if (inode) {
		spin_lock(&inode->i_lock);
		inode->i_state = I_IN_MEM;
		spin_unlock(&inode->i_lock);
		INIT_LIST_HEAD(&inode->i_sb_list);
	}
	return inode;
}

struct inode *new_inode_memlog(struct super_block *sb)
{
	struct inode *inode;

	spin_lock_prefetch(&inode_sb_list_lock);

	inode = new_inode_pseudo_memlog(sb);
	// Not adding inode to sb->s_inodes
	return inode;
}
EXPORT_SYMBOL(new_inode_memlog);

static void copy_back_radix_tree_memlog(struct address_space *mapping,
	struct address_space *mapping_input)
{
	struct radix_tree_iter iter;
	void **slot;
	struct page *page;
	pgoff_t page_offset;
	int error;

	rcu_read_lock();
	radix_tree_for_each_slot(slot, &mapping_input->page_tree, &iter, 0) {
repeat:
		page = radix_tree_deref_slot(slot);
		if (unlikely(!page))
			continue;
		if (radix_tree_exception(page)) {
			fs_tx_debug("Warning: radix_tree_exception(page)!!!\n");
			if (radix_tree_deref_retry(page))
				break;
			else
				continue;
		}

		if (!page_cache_get_speculative(page))
			goto repeat;

		/* Has the page moved? */
		if (unlikely(page != *slot)) {
			page_cache_release(page);
			goto repeat;
		}

		if (unlikely(!PageUptodate(page) ||
				PageHWPoison(page))) {
			if (!PageUptodate(page)) fs_tx_debug("WARNING: !PageUptodate page %p", page);
			if (PageHWPoison(page)) fs_tx_debug("WARNING: PageHWPoison page %p", page);
			WARN_ON(1);
			//goto skip;
		}
		if (unlikely(PageReadahead(page)))
			fs_tx_debug("WARNING: PageReadahead page %p", page);

		/* insert page into mapping */
		page_offset = page->index;

		/* TODO: Check locking */
		bool page_exists = false;
		struct page *page_original = find_get_entry(mapping, page_offset);

		if (radix_tree_exceptional_entry(page_original))
			page_original = NULL;
		if (likely(page_original))
			page_exists = true;

		if (unlikely(!page_exists)) {
			printk("Warning: page_original not exists, page = %p", page);
			gfp_t gfp_mask = (mapping_gfp_mask(mapping_input) & ~__GFP_FS) |
				__GFP_MOVABLE | __GFP_NOFAIL;

			/* Here we might have double counted memory usage.... minor issue */
			error = add_to_page_cache_lru(page, mapping, page_offset,
					gfp_mask & GFP_RECLAIM_MASK);
			if (unlikely(error)) {
				WARN_ON(1);
				goto skip;
			}

			lock_page(page);
			clear_page_dirty_for_io(page);
			unlock_page(page);
			set_page_dirty(page);
			wait_for_stable_page(page);

		} else {
			copy_page(page_address(page_original), page_address(page));
			/* Clean up write bit */
			clear_page_write(page_original);

			spin_lock(&mapping->private_lock);
			if (page_has_buffers(page)) {
				struct buffer_head *bh, *head;
				long offset = 0;

				bh = head = page_buffers(page);
				do {
					set_bh_page(bh, page_original, offset);
					offset += bh->b_size;
					bh = bh->b_this_page;
				} while (bh != head);

				if (!PagePrivate(page_original))
					attach_page_buffers(page_original, head);

				ClearPagePrivate(page);
				set_page_private(page, NULL);
				put_page(page);
			}
			spin_unlock(&mapping->private_lock);

			clear_page_dirty_for_io(page_original);

			if (!PageNeedjournal(page)) {
				set_page_dirty(page_original);
			} else {
				/* Journaled page, use ext4_journalled_set_page_dirty */
				SetPageChecked(page);
				__set_page_dirty_nobuffers(page);

				ClearPageNeedjournal(page);
			}
			if (PageDalloc(page))
				ClearPageDalloc(page);
			lock_page(page);
			clear_page_dirty_for_io(page);
			unlock_page(page);

			page_cache_release(page); /* page_cache_get_speculative */
			page_cache_release(page_original);

			/* For the conor case of non-cow-ed local page being released,
			 * we once got another reference at the end of
			 * pagecache_get_page_cow_memlog. */
			page_cache_release(page);
		}

		continue;

skip:
		page_cache_release(page); /* page_cache_get_speculative */
		page_cache_release(page); /* pagecache_get_page_cow_memlog */
	}
	rcu_read_unlock();
}

int inode_init_copy_memlog(struct super_block *sb, struct inode *inode,
				struct inode *input)
{
	struct address_space *const mapping = &inode->i_data,
				*const mapping_input = &input->i_data;

	spin_lock(&input->i_lock);

	/* From inode_init_always() */
	inode->i_mode = input->i_mode;
	inode->i_sb = sb;
	inode->i_blkbits = sb->s_blocksize_bits;
	inode->i_flags = input->i_flags;
	atomic_set(&inode->i_count, 1);
	inode->i_op = input->i_op;
	inode->i_fop = input->i_fop;
	inode->__i_nlink = 1;
	set_nlink(inode, input->__i_nlink);
	inode->i_opflags = input->i_opflags;
	inode->i_uid = input->i_uid;
	inode->i_gid = input->i_gid;
//	atomic_set(&inode->i_writecount, atomic_read(&input->i_writecount));
#if 1
fs_tx_debug("==== inode->i_writecount = %d", input->i_writecount);
atomic_set(&inode->i_writecount, 1);
#endif
	atomic_set(&inode->i_readcount, 1);

	inode->i_size = input->i_size;
	inode->i_blocks = input->i_blocks;
	inode->i_bytes = input->i_bytes;
	inode->i_generation = input->i_generation;
#ifdef CONFIG_QUOTA
	memcpy(&inode->i_dquot, input->i_dquot, sizeof(inode->i_dquot));
#endif
	inode->i_pipe = NULL;
	inode->i_bdev = input->i_bdev;
	inode->i_cdev = input->i_cdev;
	inode->i_rdev = input->i_rdev;
	inode->dirtied_when = input->dirtied_when;

	if (security_inode_alloc(inode))
		goto out;
	spin_lock_init(&inode->i_lock);
	lockdep_set_class(&inode->i_lock, &sb->s_type->i_lock_key);

	mutex_init(&inode->i_mutex);
	lockdep_set_class(&inode->i_mutex, &sb->s_type->i_mutex_key);

	atomic_set(&inode->i_dio_count, 0);

	mapping->a_ops = mapping_input->a_ops;
	mapping->host = inode;
	mapping->flags = mapping_input->flags;
	atomic_set(&mapping->i_mmap_writable, atomic_read(&mapping_input->i_mmap_writable));
	mapping_set_gfp_mask(mapping, GFP_HIGHUSER_MOVABLE);
	mapping->private_data = NULL;
	mapping->backing_dev_info = mapping_input->backing_dev_info;
	mapping->writeback_index = mapping_input->writeback_index;

	/*
	 * If the block_device provides a backing_dev_info for client
	 * inodes then use that.  Otherwise the inode share the bdev's
	 * backing_dev_info.
	 */
	if (sb->s_bdev) {
		struct backing_dev_info *bdi;

		bdi = sb->s_bdev->bd_inode->i_mapping->backing_dev_info;
		mapping->backing_dev_info = bdi;
	}
	inode->i_private = NULL;

	spin_unlock(&input->i_lock);

	inode->i_mapping = mapping;
	INIT_HLIST_HEAD(&inode->i_dentry);	/* buggered by rcu freeing */
#ifdef CONFIG_FS_POSIX_ACL
	inode->i_acl = inode->i_default_acl = ACL_NOT_CACHED;
#endif

#ifdef CONFIG_FSNOTIFY
	inode->i_fsnotify_mask = 0;
#endif
	inode->i_commit_pid = -1;

	INIT_LIST_HEAD(&inode->i_sb_list);

	return 0;
out:
	return -ENOMEM;
}

void inode_copy_back_memlog(struct inode *inode, struct inode *input)
{
	struct address_space *const mapping = &inode->i_data,
				*const mapping_input = &input->i_data;

	/* From inode_init_always() */
	inode->i_mode = input->i_mode;
	inode->i_flags = input->i_flags;
	//atomic_set(&inode->i_count, atomic_read(&input->i_count));
	inode->i_op = input->i_op;
	inode->i_fop = input->i_fop;
	set_nlink(inode, input->__i_nlink);
	inode->i_opflags = input->i_opflags;
	inode->i_uid = input->i_uid;
	inode->i_gid = input->i_gid;
//	atomic_set(&inode->i_writecount, atomic_read(&input->i_writecount));
#if 1
	fs_tx_debug("==== inode->i_writecount = %d, copy->i_writecount = %d",
			inode->i_writecount, input->i_writecount);
	atomic_add(atomic_read(&input->i_writecount) - 1, &inode->i_writecount);
#endif
	atomic_add(atomic_read(&input->i_readcount) - 1, &inode->i_readcount);

	inode->i_size = input->i_size;
	inode->i_blocks = input->i_blocks;
	inode->i_bytes = input->i_bytes;
#ifdef CONFIG_QUOTA
	memcpy(&inode->i_dquot, input->i_dquot, sizeof(inode->i_dquot));
#endif

	copy_back_radix_tree_memlog(mapping, mapping_input);
}

void abort_copy_dentry_inode(struct inode *inode, struct inode *inode_cp,
		bool lock_pages)
{
	struct radix_tree_iter iter;
	void **slot;
	struct page *page, *page_original;
	pgoff_t page_offset;
	struct address_space *const mapping = &inode->i_data,
				*const mapping_input = &inode_cp->i_data;

	BUG_ON(!(inode && inode_cp));
	fs_tx_debug("Copying back buffer_head-s, inode (%p, %d), local mapping %p\n",
			inode, inode->i_ino, mapping_input);

#if 1
	fs_tx_debug("==== inode->i_writecount = %d, copy->i_writecount = %d",
			inode->i_writecount, inode_cp->i_writecount);
	atomic_add(atomic_read(&inode_cp->i_writecount) - 1, &inode->i_writecount);
#endif
	atomic_add(atomic_read(&inode_cp->i_readcount) - 1, &inode->i_readcount);

	rcu_read_lock();
	radix_tree_for_each_slot(slot, &mapping_input->page_tree, &iter, 0) {
repeat:
		page = radix_tree_deref_slot(slot);
		if (unlikely(!page))
			continue;
		if (radix_tree_exception(page)) {
			fs_tx_debug("Warning: radix_tree_exception(page)!!!\n");
			if (radix_tree_deref_retry(page))
				break;
			else
				continue;
		}

		if (!page_cache_get_speculative(page))
			goto repeat;

		/* Has the page moved? */
		if (unlikely(page != *slot)) {
			page_cache_release(page);
			goto repeat;
		}

		if (unlikely(!PageUptodate(page) ||
				PageHWPoison(page))) {
			if (!PageUptodate(page)) printk("WARNING: !PageUptodate page %p\n", page);
			if (PageHWPoison(page)) printk("WARNING: PageHWPoison page %p\n", page);
			//WARN_ON(1);
		}
		if (unlikely(PageReadahead(page)))
			fs_tx_debug("WARNING: PageReadahead page %p", page);

		page_offset = page->index;
		page_original = find_get_entry(mapping, page_offset);
		if (radix_tree_exceptional_entry(page_original))
			page_original = NULL;
		if (unlikely(!page_original)) {
			printk("Warning: page_original not exists, page = %p", page);
			goto skip;
		}
		fs_tx_debug("Abort changes to a existing page %p, mapping_addr = %p\n",
				page, mapping);

		if (PageNeedjournal(page) || PageDalloc(page)) {
			/* Not a new and empty page. */
			spin_lock(&mapping->private_lock);
			if (page_has_buffers(page)) {
				struct buffer_head *bh, *head;
				long offset = 0;

				bh = head = page_buffers(page);
				do {
					set_bh_page(bh, page_original, offset);
					offset += bh->b_size;
					bh = bh->b_this_page;
				} while (bh != head);

				if (!PagePrivate(page_original))
					attach_page_buffers(page_original, head);

				ClearPagePrivate(page);
				set_page_private(page, NULL);
				put_page(page);
			}
			spin_unlock(&mapping->private_lock);

			if (lock_pages) lock_page(page_original);
			clear_page_write(page_original);
			clear_page_dirty_for_io(page_original);
			/* If the page_original was dirty before the tx,
			 * we need to dirty it back again since they were cleard
			 * during the COW. Otherwise, it won't be written back
			 * by BDI threads. */
			if (PageNeedredirty(page_original)) {
				set_page_dirty(page_original);
				ClearPageNeedredirty(page_original);
			}
			if (lock_pages) unlock_page(page_original);

		} else {
			/* Newly inserted unmapped page, need to be removed.
			 * Otherwise, the BDI threads will hang. */
			if (lock_pages) lock_page(page_original);
			clear_page_write(page_original);
			clear_page_dirty_for_io(page_original);
			truncate_inode_page(mapping, page_original);
			if (lock_pages) unlock_page(page_original);
		}

		lock_page(page);
		clear_page_dirty_for_io(page);
		unlock_page(page);
		if (PageNeedjournal(page))
			ClearPageNeedjournal(page);
		if (PageDalloc(page))
			ClearPageDalloc(page);

		page_cache_release(page); /* page_cache_get_speculative */
		page_cache_release(page_original);

		/* For the conor case of non-cow-ed local page being released,
		 * we once got another reference at the end of
		 * pagecache_get_page_cow_memlog. */
		page_cache_release(page);

		continue;

skip:
		page_cache_release(page); /* page_cache_get_speculative */
		page_cache_release(page); /* pagecache_get_page_cow_memlog */
	}
	rcu_read_unlock();
}

inline void lock_sb_list_lock(void) {
	spin_lock(&inode_sb_list_lock);
}

inline void unlock_sb_list_lock(void) {
	spin_unlock(&inode_sb_list_lock);
}

inline void lock_inode_hash_lock(void) {
	spin_lock(&inode_hash_lock);
}

inline void unlock_inode_hash_lock(void) {
	spin_unlock(&inode_hash_lock);
}
