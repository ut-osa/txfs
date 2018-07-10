#include "ext4_memlog.h"

struct inode_rec *ext4_alloc_inode_rec(struct super_block *sb, int flags)
{
	struct inode_rec *record;
	struct inode *inode;

	inode = ext4_alloc_inode(sb);
	/* (a) New file inode: flags = REC_NEW_REF,
	 *	- from inode_memlog.c: alloc_inode_memlog;
	 * (b) Copied file inode: flags = REC_COPY,
	 *	- from ext4/ext4_memlog.c: ext4_copy_inode_memlog;
	 * (c) Unlinked inode to be locked when commit: REC_LOCK,
	 *	- from dcache_memlog.c: __d_alloc_del_memlog;
	 * (d) Dir with new inodes, to be locked: REC_LOCK | REC_DIR,
	 *	- from namei_memlog.c: vfs_create_memlog. (necessary?)
	 * */
	record = memlog_alloc_inode_rec(inode, flags);
	return record;
}

void ext4_destroy_inode_rec(struct inode_rec *record)
{
	struct inode *inode = record->r_inode_copy;

	if (!list_empty(&(EXT4_I(inode)->i_orphan))) {
		ext4_msg(inode->i_sb, KERN_ERR,
			 "Inode %lu (%p): orphan list check failed!",
			 inode->i_ino, EXT4_I(inode));
		print_hex_dump(KERN_INFO, "", DUMP_PREFIX_ADDRESS, 16, 4,
				EXT4_I(inode), sizeof(struct ext4_inode_info),
				true);
		dump_stack();
	}
	call_rcu(&inode->i_rcu, ext4_i_callback);
	record->r_inode_copy = NULL;
	memlog_free_inode_rec(record);
}

void ext4_print_dbg_info(struct inode *inode)
{
	test_ext4_es_print_tree(inode);
}
