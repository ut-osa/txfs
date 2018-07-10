#include <linux/quotaops.h>
#include "ext4_jbd2.h"
#include "acl.h"
#include "xattr.h"
#include "ext4_memlog.h"

struct dentry *ext4_lookup_memlog(struct inode *dir,
	struct dentry *dentry, unsigned int flags)
{
	struct inode *inode;
	struct ext4_dir_entry_2 *de;
	struct buffer_head *bh;

fs_tx_debug("##### into ext4_lookup !!! #####\n");

	if (dentry->d_name.len > EXT4_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	bh = ext4_find_entry(dir, &dentry->d_name, &de, NULL);
fs_tx_debug("##### bh = ext4_find_entry() = %p #####", bh);
	if (IS_ERR(bh))
		return (struct dentry *) bh;
	inode = NULL;
	if (bh) {
fs_tx_debug("##### bh not ERR, not NULL");
		__u32 ino = le32_to_cpu(de->inode);
		brelse(bh);
		if (!ext4_valid_inum(dir->i_sb, ino)) {
			EXT4_ERROR_INODE(dir, "bad inode number: %u", ino);
			return ERR_PTR(-EIO);
		}
		if (unlikely(ino == dir->i_ino)) {
			EXT4_ERROR_INODE(dir, "'%pd' linked to parent dir",
					 dentry);
			return ERR_PTR(-EIO);
		}
		inode = ext4_iget_normal(dir->i_sb, ino);
		if (inode == ERR_PTR(-ESTALE)) {
			EXT4_ERROR_INODE(dir,
					 "deleted inode referenced: %u",
					 ino);
			//dump_stack();
			return ERR_PTR(-EIO);
		}
	}
	return d_splice_alias_memlog(inode, dentry);
}

struct inode *__ext4_new_inode_memlog(struct inode *dir, umode_t mode,
					uid_t *owner)
{
	struct super_block *sb;
	struct inode *inode;
	struct ext4_inode_info *ei;
	struct ext4_sb_info *sbi;
	int err = 0; //int ret2, err = 0;
	struct inode *ret;
	memlog_t *memlog;

	/* Cannot create files in a deleted directory */
	if (!dir || !dir->i_nlink)
		return ERR_PTR(-EPERM);

	sb = dir->i_sb;
	inode = new_inode_memlog(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	ei = EXT4_I(inode);
	sbi = EXT4_SB(sb);

	/*
	 * Initalize owners and quota early so that we don't have to account
	 * for quota initialization worst case in standard inode creating
	 * transaction
	 */
	if (owner) {
		inode->i_mode = mode;
		i_uid_write(inode, owner[0]);
		i_gid_write(inode, owner[1]);
	} else if (test_opt(sb, GRPID)) {
		inode->i_mode = mode;
		inode->i_uid = current_fsuid();
		inode->i_gid = dir->i_gid;
	} else
		inode_init_owner(inode, dir, mode);
	dquot_initialize(inode);

	/* Removed: group and group_desc_bh, inode_bitmap_bh ... */

	inode->i_ino = 12345;
	/* This is the optimal IO size (for stat), not the fs block size */
	inode->i_blocks = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = ei->i_crtime =
						       ext4_current_time(inode);

	memset(ei->i_data, 0, sizeof(ei->i_data));
	ei->i_dir_start_lookup = 0;
	ei->i_disksize = 0;

	/* Don't inherit extent flag from directory, amongst others. */
	ei->i_flags =
		ext4_mask_flags(mode, EXT4_I(dir)->i_flags & EXT4_FL_INHERITED);
	ei->i_file_acl = 0;
	ei->i_dtime = 0;
	ei->i_last_alloc_group = ~0;

	ext4_set_inode_flags(inode);

	spin_lock(&inode->i_lock);
	inode->i_state |= I_NEW;
	spin_unlock(&inode->i_lock);

	/* Precompute checksum seed for inode metadata */
	if (ext4_has_metadata_csum(sb)) {
		__u32 csum;
		__le32 inum = cpu_to_le32(inode->i_ino);
		__le32 gen = cpu_to_le32(inode->i_generation);
		csum = ext4_chksum(sbi, sbi->s_csum_seed, (__u8 *)&inum,
				   sizeof(inum));
		ei->i_csum_seed = ext4_chksum(sbi, csum, (__u8 *)&gen,
					      sizeof(gen));
	}

	ext4_clear_state_flags(ei); /* Only relevant on 32-bit archs */
	ext4_set_inode_state(inode, EXT4_STATE_NEW);

	ei->i_extra_isize = EXT4_SB(sb)->s_want_extra_isize;

	ei->i_inline_off = 0;
	if (EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_INLINE_DATA))
		ext4_set_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);

	ret = inode;
	err = dquot_alloc_inode(inode);
	if (err)
		goto fail_drop;

	/* Removed: ext4_init_acl(), ext4_init_security(), ei->i_sync_tid ... */

	memlog = current->memlog;
	if (!memlog->l_journal)
		memlog->l_journal = EXT4_JOURNAL(ret);

	ext4_debug("allocating inode %lu\n", inode->i_ino);
	return ret;

fail_drop:
	clear_nlink(inode);
	unlock_new_inode(inode);
//out:
	dquot_drop(inode);
	inode->i_flags |= S_NOQUOTA;
	iput(inode);
	return ERR_PTR(err);
}

int __ext4_post_new_inode_memlog(handle_t *handle, struct inode *inode,
			       struct inode *dir, const struct qstr *qstr,
			       __u32 goal, uid_t *owner, int handle_type,
			       unsigned int line_no, int nblocks)
{
	struct super_block *sb;
	struct buffer_head *inode_bitmap_bh = NULL;
	struct buffer_head *group_desc_bh;
	ext4_group_t ngroups, group = 0;
	unsigned long ino = 0;
	struct ext4_group_desc *gdp = NULL;
	struct ext4_inode_info *ei;
	struct ext4_sb_info *sbi;
	int ret2, err = 0;
	ext4_group_t i;
	ext4_group_t flex_group;
	struct ext4_group_info *grp;

	umode_t mode = inode->i_mode;

	/* Cannot create files in a deleted directory */
	if (!dir || !dir->i_nlink)
		return -EPERM;

	sb = dir->i_sb;
	ngroups = ext4_get_groups_count(sb);
	ei = EXT4_I(inode);
	sbi = EXT4_SB(sb);

	if (!goal)
		goal = sbi->s_inode_goal;

	if (goal && goal <= le32_to_cpu(sbi->s_es->s_inodes_count)) {
		group = (goal - 1) / EXT4_INODES_PER_GROUP(sb);
		ino = (goal - 1) % EXT4_INODES_PER_GROUP(sb);
		ret2 = 0;
		goto got_group;
	}

	if (S_ISDIR(mode))
		ret2 = find_group_orlov(sb, dir, &group, mode, qstr);
	else
		ret2 = find_group_other(sb, dir, &group, mode);

got_group:
	EXT4_I(dir)->i_last_alloc_group = group;
	err = -ENOSPC;
	if (ret2 == -1)
		goto out;

	/*
	 * Normally we will only go through one pass of this loop,
	 * unless we get unlucky and it turns out the group we selected
	 * had its last inode grabbed by someone else.
	 */
	for (i = 0; i < ngroups; i++, ino = 0) {
		err = -EIO;

		gdp = ext4_get_group_desc(sb, group, &group_desc_bh);
		if (!gdp)
			goto out;

		/*
		 * Check free inodes count before loading bitmap.
		 */
		if (ext4_free_inodes_count(sb, gdp) == 0) {
			if (++group == ngroups)
				group = 0;
			continue;
		}

		grp = ext4_get_group_info(sb, group);
		/* Skip groups with already-known suspicious inode tables */
		if (EXT4_MB_GRP_IBITMAP_CORRUPT(grp)) {
			if (++group == ngroups)
				group = 0;
			continue;
		}

		brelse(inode_bitmap_bh);
		inode_bitmap_bh = ext4_read_inode_bitmap(sb, group);
		/* Skip groups with suspicious inode tables */
		if (EXT4_MB_GRP_IBITMAP_CORRUPT(grp) || !inode_bitmap_bh) {
			if (++group == ngroups)
				group = 0;
			continue;
		}

repeat_in_this_group:
		ino = ext4_find_next_zero_bit((unsigned long *)
					      inode_bitmap_bh->b_data,
					      EXT4_INODES_PER_GROUP(sb), ino);
		if (ino >= EXT4_INODES_PER_GROUP(sb))
			goto next_group;
		if (group == 0 && (ino+1) < EXT4_FIRST_INO(sb)) {
			ext4_error(sb, "reserved inode found cleared - "
				   "inode=%lu", ino + 1);
			continue;
		}
		if ((EXT4_SB(sb)->s_journal == NULL) &&
		    recently_deleted(sb, group, ino)) {
			ino++;
			goto next_inode;
		}
		if (!handle) {
			BUG_ON(nblocks <= 0);
			handle = __ext4_journal_start_sb(dir->i_sb, line_no,
							 handle_type, nblocks,
							 0);
			if (IS_ERR(handle)) {
				err = PTR_ERR(handle);
				ext4_std_error(sb, err);
				goto out;
			}
		}
		BUFFER_TRACE(inode_bitmap_bh, "get_write_access");
		err = ext4_journal_get_write_access(handle, inode_bitmap_bh);
		if (err) {
			ext4_std_error(sb, err);
			goto out;
		}
		ext4_lock_group(sb, group);
		ret2 = ext4_test_and_set_bit(ino, inode_bitmap_bh->b_data);
		ext4_unlock_group(sb, group);
		ino++;		/* the inode bitmap is zero-based */
		if (!ret2)
			goto got; /* we grabbed the inode! */
next_inode:
		if (ino < EXT4_INODES_PER_GROUP(sb))
			goto repeat_in_this_group;
next_group:
		if (++group == ngroups)
			group = 0;
	}
	err = -ENOSPC;
	goto out;

got:
	BUFFER_TRACE(inode_bitmap_bh, "call ext4_handle_dirty_metadata");
	err = ext4_handle_dirty_metadata(handle, NULL, inode_bitmap_bh);
	if (err) {
		ext4_std_error(sb, err);
		goto out;
	}

	BUFFER_TRACE(group_desc_bh, "get_write_access");
	err = ext4_journal_get_write_access(handle, group_desc_bh);
	if (err) {
		ext4_std_error(sb, err);  // Here got an error if no dir i_last_alloc_group update
		goto out;
	}

	/* We may have to initialize the block bitmap if it isn't already */
	if (ext4_has_group_desc_csum(sb) &&
	    gdp->bg_flags & cpu_to_le16(EXT4_BG_BLOCK_UNINIT)) {
		struct buffer_head *block_bitmap_bh;

		block_bitmap_bh = ext4_read_block_bitmap(sb, group);
		if (!block_bitmap_bh) {
			err = -EIO;
			goto out;
		}
		BUFFER_TRACE(block_bitmap_bh, "get block bitmap access");
		err = ext4_journal_get_write_access(handle, block_bitmap_bh);
		if (err) {
			brelse(block_bitmap_bh);
			ext4_std_error(sb, err);
			goto out;
		}

		BUFFER_TRACE(block_bitmap_bh, "dirty block bitmap");
		err = ext4_handle_dirty_metadata(handle, NULL, block_bitmap_bh);

		/* recheck and clear flag under lock if we still need to */
		ext4_lock_group(sb, group);
		if (gdp->bg_flags & cpu_to_le16(EXT4_BG_BLOCK_UNINIT)) {
			gdp->bg_flags &= cpu_to_le16(~EXT4_BG_BLOCK_UNINIT);
			ext4_free_group_clusters_set(sb, gdp,
				ext4_free_clusters_after_init(sb, group, gdp));
			ext4_block_bitmap_csum_set(sb, group, gdp,
						   block_bitmap_bh);
			ext4_group_desc_csum_set(sb, group, gdp);
		}
		ext4_unlock_group(sb, group);
		brelse(block_bitmap_bh);

		if (err) {
			ext4_std_error(sb, err);
			goto out;
		}
	}

	/* Update the relevant bg descriptor fields */
	if (ext4_has_group_desc_csum(sb)) {
		int free;
		struct ext4_group_info *grp = ext4_get_group_info(sb, group);

		down_read(&grp->alloc_sem); /* protect vs itable lazyinit */
		ext4_lock_group(sb, group); /* while we modify the bg desc */
		free = EXT4_INODES_PER_GROUP(sb) -
			ext4_itable_unused_count(sb, gdp);
		if (gdp->bg_flags & cpu_to_le16(EXT4_BG_INODE_UNINIT)) {
			gdp->bg_flags &= cpu_to_le16(~EXT4_BG_INODE_UNINIT);
			free = 0;
		}
		/*
		 * Check the relative inode number against the last used
		 * relative inode number in this group. if it is greater
		 * we need to update the bg_itable_unused count
		 */
		if (ino > free)
			ext4_itable_unused_set(sb, gdp,
					(EXT4_INODES_PER_GROUP(sb) - ino));
		up_read(&grp->alloc_sem);
	} else {
		ext4_lock_group(sb, group);
	}

	ext4_free_inodes_set(sb, gdp, ext4_free_inodes_count(sb, gdp) - 1);
	if (S_ISDIR(mode)) {
		ext4_used_dirs_set(sb, gdp, ext4_used_dirs_count(sb, gdp) + 1);
		if (sbi->s_log_groups_per_flex) {
			ext4_group_t f = ext4_flex_group(sbi, group);

			atomic_inc(&sbi->s_flex_groups[f].used_dirs);
		}
	}
	if (ext4_has_group_desc_csum(sb)) {
		ext4_inode_bitmap_csum_set(sb, group, gdp, inode_bitmap_bh,
					   EXT4_INODES_PER_GROUP(sb) / 8);
		ext4_group_desc_csum_set(sb, group, gdp);
	}
	ext4_unlock_group(sb, group);

	BUFFER_TRACE(group_desc_bh, "call ext4_handle_dirty_metadata");
	err = ext4_handle_dirty_metadata(handle, NULL, group_desc_bh);
	if (err) {
		ext4_std_error(sb, err);
		goto out;
	}

	percpu_counter_dec(&sbi->s_freeinodes_counter);
	if (S_ISDIR(mode))
		percpu_counter_inc(&sbi->s_dirs_counter);

	if (sbi->s_log_groups_per_flex) {
		flex_group = ext4_flex_group(sbi, group);
		atomic_dec(&sbi->s_flex_groups[flex_group].free_inodes);
	}

	inode->i_ino = ino + group * EXT4_INODES_PER_GROUP(sb);
	/* This is the optimal IO size (for stat), not the fs block size */
//	inode->i_blocks = 0;
//	inode->i_mtime = inode->i_atime = inode->i_ctime = ei->i_crtime =
//						       ext4_current_time(inode);
	ei->i_block_group = group;

	/* Change i_state so that the inode can be marked dirty and be added
	 * to &bdi->wb.b_dirty
	 */
	inode->i_state &= ~I_IN_MEM;

	if (IS_DIRSYNC(inode))
		ext4_handle_sync(handle);
	if (insert_inode_locked(inode) < 0) {
		/*
		 * Likely a bitmap corruption causing inode to be allocated
		 * twice.
		 */
		err = -EIO;
		ext4_error(sb, "failed to insert inode %lu: doubly allocated?",
			   inode->i_ino);
		goto out;
	}
	spin_lock(&sbi->s_next_gen_lock);
	inode->i_generation = sbi->s_next_generation++;
	spin_unlock(&sbi->s_next_gen_lock);

	err = ext4_init_acl(handle, inode, dir);
	if (err)
		goto fail_free_drop;

	err = ext4_init_security(handle, inode, dir, qstr);
	if (err)
		goto fail_free_drop;

	if (EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_EXTENTS)) {
		/* set extent flag only for directory, file and normal symlink*/
		if (S_ISDIR(mode) || S_ISREG(mode) || S_ISLNK(mode)) {
			ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);
			ext4_ext_tree_init(handle, inode);
		}
	}

	if (ext4_handle_valid(handle)) {
		ei->i_sync_tid = handle->h_transaction->t_tid;
		ei->i_datasync_tid = handle->h_transaction->t_tid;
	}

	err = ext4_mark_inode_dirty(handle, inode);
	if (err) {
		ext4_std_error(sb, err);
		goto fail_free_drop;
	}

	ext4_debug("allocating inode %lu\n", inode->i_ino);
	//trace_ext4_allocate_inode(inode, dir, mode);
	brelse(inode_bitmap_bh);
	return 0;

fail_free_drop:
	dquot_free_inode(inode);
//fail_drop:
	clear_nlink(inode);
	//unlock_new_inode(inode);
	fs_tx_debug("fail_free_drop: __ext4_post_new_inode_memlog()\n");
out:
	dquot_drop(inode);
	inode->i_flags |= S_NOQUOTA;
	//iput(inode);
	atomic_dec(&inode->i_count); // TODO: Clear inode w/o affecting parent
	brelse(inode_bitmap_bh);
	return err;
}

static int ext4_add_nondir_memlog(struct dentry *dentry, struct inode *inode)
{
	int err = 0;
	if (!err) {
		unlock_new_inode(inode);
		d_instantiate(dentry, inode);
		return 0;
	}
	drop_nlink(inode);
	unlock_new_inode(inode);
	iput(inode);
	return err;
}

static inline int ext4_post_add_nondir_memlog(handle_t *handle,
			struct dentry *dentry, struct inode *inode)
{
	int err = ext4_add_entry(handle, dentry, inode);
	if (!err) {
		unlock_new_inode(inode);
		//inode->i_state &= ~I_NEW;
		ext4_mark_inode_dirty(handle, inode);
		return 0;
	}
	drop_nlink(inode);
	unlock_new_inode(inode);
	fs_tx_debug("failed in ext4_add_entry(), errcode = %d\n", err);
	iput(inode);
	return err;
}

int ext4_create_memlog(struct inode *dir, struct dentry *dentry, umode_t mode,
		       bool excl)
{
	//handle_t *handle;
	struct inode *inode;
	int err, retries = 0;

	dquot_initialize(dir);

retry:
	inode = __ext4_new_inode_memlog(dir, mode, NULL);

	err = PTR_ERR(inode);
	if (!IS_ERR(inode)) {
		inode->i_op = &ext4_file_inode_operations;
		inode->i_fop = &ext4_file_operations;
		ext4_set_aops(inode);
		err = ext4_add_nondir_memlog(dentry, inode);
	}
	if (err == -ENOSPC && ext4_should_retry_alloc(dir->i_sb, &retries))
		goto retry;
	return err;
}

int ext4_post_create_memlog(struct dentry *dentry)
{
	handle_t *handle = NULL;
	int err, credits, retries = 0;
	struct inode *inode = dentry->d_inode;
	struct inode *dir = dentry->d_parent->d_inode;

	dquot_initialize(dir);

	credits = (EXT4_DATA_TRANS_BLOCKS(dir->i_sb) +
		   EXT4_INDEX_EXTRA_TRANS_BLOCKS + 3);
retry:
	/* ext4_new_inode_start_handle() -- __ext4_new_inode() */
	err = ext4_new_inode_start_handle_memlog(inode, dir,
					&dentry->d_name, 0,
					NULL, EXT4_HT_DIR, credits);
	if (err) goto out;

	handle = ext4_journal_current_handle();
	if (!IS_ERR(inode)) {
		/* ext4_add_nondir() */
		err = ext4_post_add_nondir_memlog(handle, dentry, inode);
		if (!err && IS_DIRSYNC(dir))
			ext4_handle_sync(handle);
	}

out:
	if (handle)
		ext4_journal_stop(handle);
	if (err == -ENOSPC && ext4_should_retry_alloc(dir->i_sb, &retries))
		goto retry;
	return err;
}

struct inode *ext4_copy_inode_memlog(struct inode *input)
{
	int ret;
	struct inode_rec *record;
	struct inode *inode;
	struct ext4_inode_info *ei, *ei_input;
	memlog_t *memlog;

	record = ext4_alloc_inode_rec(input->i_sb, REC_COPY);
	spin_lock(&input->i_lock);
	record->r_inode_addr = input;
	record->r_sort_key = input;
	__iget(input);
	inode = record->r_inode_copy;
	if (!inode)
		return NULL;

	WARN_ON(input->i_state & I_IN_COMMIT);
	inode->i_state = input->i_state | I_IN_MEM;
	inode->i_ino = input->i_ino;

	ei = EXT4_I(inode);
	ei_input = EXT4_I(input);
	spin_unlock(&input->i_lock);

	spin_lock(&ei_input->i_raw_lock);

	memcpy(ei->i_data, ei_input->i_data, sizeof(ei_input->i_data));
	ei->i_dtime = ei_input->i_dtime;
	ei->i_file_acl = ei_input->i_file_acl;
	ei->i_block_group = ei_input->i_block_group;
	ei->i_dir_start_lookup = ei_input->i_dir_start_lookup;
#if (BITS_PER_LONG < 64)
	ei->i_state_flags = ei_input->i_state_flags;
#endif
	ei->i_flags = ei_input->i_flags;
	ei->i_disksize = ei_input->i_disksize;
	ei->jinode = ei_input->jinode;
	ei->i_crtime = ei_input->i_crtime;

	ei->i_touch_when = ei_input->i_touch_when;

	ei->i_last_alloc_group = ei_input->i_last_alloc_group;


	ei->i_extra_isize = ei_input->i_extra_isize;
	ei->i_inline_off = ei_input->i_inline_off;
	ei->i_inline_size = ei_input->i_inline_size;

#ifdef CONFIG_QUOTA
	ei->i_reserved_quota = ei_input->i_reserved_quota;
#endif

//	ei->i_ioend_count = ei_input->i_ioend_count;
	ei->i_unwritten = ei_input->i_unwritten;
	ei->i_csum_seed = ei_input->i_csum_seed;

	spin_unlock(&ei_input->i_raw_lock);

	ret = inode_init_copy_memlog(input->i_sb, inode, input);
	if (unlikely(ret)) {
		memlog_destroy_inode_rec(record);
		return NULL;
	}

	/* From __ext4_new_inode_memlog() */

	/*
	 * Initalize owners and quota early so that we don't have to account
	 * for quota initialization worst case in standard inode creating
	 * transaction
	 */
#if 0
	if (owner) {
		inode->i_mode = mode;
		i_uid_write(inode, owner[0]);
		i_gid_write(inode, owner[1]);
	} else if (test_opt(sb, GRPID)) {
		inode->i_mode = mode;
		inode->i_uid = current_fsuid();
		inode->i_gid = dir->i_gid;
	} else
		inode_init_owner(inode, dir, mode);
	dquot_initialize(inode);
#endif

	inode->i_mtime = inode->i_atime = inode->i_ctime = ei->i_crtime;

#if 0
	ret = inode;
	err = dquot_alloc_inode(inode);
	if (err)
		goto fail_drop;
	return ret;

fail_drop:
	clear_nlink(inode);
	unlock_new_inode(inode);
//out:
	dquot_drop(inode);
	inode->i_flags |= S_NOQUOTA;
	iput(inode);
	//brelse(inode_bitmap_bh);
	return ERR_PTR(err);
#endif

	memlog = current->memlog;
	if (!memlog->l_journal)
		memlog->l_journal = EXT4_JOURNAL(input);

	return inode;
}

void ext4_copy_back_inode_memlog(struct inode *inode, struct inode *copy)
{
	struct ext4_inode_info *ei, *ei_copy;

	ei = EXT4_I(inode);
	ei_copy = EXT4_I(copy);

	spin_lock(&ei->i_raw_lock);

	memcpy(ei->i_data, ei_copy->i_data, sizeof(ei_copy->i_data));
	ei->i_dtime = ei_copy->i_dtime;
	ei->i_file_acl = ei_copy->i_file_acl;
#if (BITS_PER_LONG < 64)
	ei->i_state_flags = ei_copy->i_state_flags;
#endif
	ei->i_flags = ei_copy->i_flags;
	ei->i_disksize = ei_copy->i_disksize;
	ei->i_crtime = ei_copy->i_crtime;

	//memcpy(ei->i_block, ei_copy->i_block, sizeof(ei_copy->i_block));

	ei->i_touch_when = ei_copy->i_touch_when;

	ei->i_last_alloc_group = ei_copy->i_last_alloc_group;

	ei->i_reserved_data_blocks += ei_copy->i_reserved_data_blocks;
	ei->i_reserved_meta_blocks += ei_copy->i_reserved_meta_blocks;
	ei->i_allocated_meta_blocks += ei_copy->i_allocated_meta_blocks;
	ei->i_da_metadata_calc_last_lblock += ei_copy->i_da_metadata_calc_last_lblock;
	ei->i_da_metadata_calc_len += ei_copy->i_da_metadata_calc_len;

	ei->i_extra_isize = ei_copy->i_extra_isize;
	ei->i_inline_off = ei_copy->i_inline_off;
	ei->i_inline_size = ei_copy->i_inline_size;

#ifdef CONFIG_QUOTA
	ei->i_reserved_quota = ei_copy->i_reserved_quota;
#endif

//	ei->i_ioend_count = ei_copy->i_ioend_count;
	ei->i_unwritten = ei_copy->i_unwritten;

	spin_unlock(&ei->i_raw_lock);

	inode_copy_back_memlog(inode, copy);
	ei_copy->jinode = NULL;

	/* This is the optimal IO size (for stat), not the fs block size */
	inode->i_mtime = inode->i_atime = inode->i_ctime = ei->i_crtime;
}

void ext4_abort_copy_inode_memlog(struct inode *inode, struct inode *copy)
{
	EXT4_I(copy)->jinode = NULL;
}

int inline ext4_lock_inode_bh(struct inode *inode)
{
	struct ext4_iloc iloc;
	struct buffer_head *bh;
	int err;

	//if (inode->i_state & I_IN_MEM) return 0;
	if (inode->i_ino == 12345) return 0; // TODO: set another flag

	err = ext4_get_inode_loc(inode, &iloc);
	if (!err) {
		bh = iloc.bh;
		if ((bh->b_commit_pid != current->pid)) {
			fs_tx_debug("locking bh %p\n", bh);
			//jbd_lock_bh_state(bh);
			//set_buffer_state_locked(bh);
			lock_buffer(bh);
			bh->b_commit_pid = current->pid;
		}
	}
	ext4_std_error(inode->i_sb, err);
	return err;
}

int inline ext4_unlock_inode_bh(struct inode *inode)
{
	struct ext4_iloc iloc;
	struct buffer_head *bh;
	int err;

	err = ext4_get_inode_loc(inode, &iloc);
	if (!err) {
		bh = iloc.bh;
		//if (buffer_state_locked(bh) && (bh->b_commit_pid == current->pid)) {
		if (bh->b_commit_pid == current->pid) {
			fs_tx_debug("unlocking bh %p", bh);
			//clear_buffer_state_locked(bh);
			//jbd_unlock_bh_state(bh);
			bh->b_commit_pid = -1;
			unlock_buffer(bh);
		}
	}
	ext4_std_error(inode->i_sb, err);
	return err;
}

int inline ext4_lock_dir_bh(struct inode *dir) {
	struct buffer_head *bh;
	ext4_lblk_t block, blocks;
	int err = 0;

	fs_tx_debug("Locking buffers for dir inode (%p, %d)", dir, dir->i_ino);
#if 1
	blocks = dir->i_size >> dir->i_sb->s_blocksize_bits;
	for (block = 0; block < blocks; block++) {
//		bh = ext4_read_dirblock(dir, block, DIRENT);
		bh = ext4_bread(NULL, dir, block, 0);
		if (IS_ERR(bh)) {
			err = PTR_ERR(bh);
			continue;
		}

		if ((bh->b_commit_pid != current->pid)) {
			lock_buffer(bh);
			bh->b_commit_pid = current->pid;
		}
	}
#endif

	return err;
}

int inline ext4_unlock_dir_bh(struct inode *dir) {
	struct buffer_head *bh;
	ext4_lblk_t block, blocks;
	int err = 0;

	fs_tx_debug("Unlocking buffers for dir inode (%p, %d)", dir, dir->i_ino);
#if 1
	blocks = dir->i_size >> dir->i_sb->s_blocksize_bits;
	for (block = 0; block < blocks; block++) {
		//bh = ext4_read_dirblock(dir, block, DIRENT);
		bh = ext4_bread(NULL, dir, block, 0);
		if (IS_ERR(bh)) {
			err = PTR_ERR(bh);
			continue;
		}

		if (bh->b_commit_pid == current->pid) {
			bh->b_commit_pid = -1;
			unlock_buffer(bh);
		}
	}
#endif

	return err;
}
