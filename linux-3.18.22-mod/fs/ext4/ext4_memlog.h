#ifndef __EXT4_MEMLOG_H
#define __EXT4_MEMLOG_H

#include <linux/fs.h>
#include <linux/memlog.h>
#if 0
#include <linux/pagemap.h>
#include <linux/jbd2.h>
#include <linux/time.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/quotaops.h>
#include <linux/buffer_head.h>
#include <linux/bio.h>
#endif
#include "ext4.h"
//#include "ext4_jbd2.h"

//#include "xattr.h"
//#include "acl.h"

#include <linux/memlog.h>

/* namei.c */
extern struct buffer_head * ext4_find_entry (struct inode *dir,
					const struct qstr *d_name,
					struct ext4_dir_entry_2 **res_dir,
					int *inlined);

extern int ext4_add_entry(handle_t *handle, struct dentry *dentry,
			  struct inode *inode);

/* super.c */
extern void ext4_i_callback(struct rcu_head *head);
extern struct inode *ext4_alloc_inode(struct super_block *sb);

/* ext4_memlog.c */

extern struct dentry *ext4_lookup_memlog(struct inode *dir,
	struct dentry *dentry, unsigned int flags);

extern struct inode *__ext4_new_inode_memlog(struct inode *dir,
			       umode_t mode, uid_t *owner);

extern int __ext4_post_new_inode_memlog(handle_t *handle, struct inode *inode,
			       struct inode *dir, const struct qstr *qstr,
			       __u32 goal, uid_t *owner, int handle_type,
			       unsigned int line_no, int nblocks);

#define ext4_new_inode_start_handle_memlog(inode, dir, qstr, goal, owner, \
				type, nblocks) \
	__ext4_post_new_inode_memlog(NULL, (inode), (dir), (qstr), \
				(goal), (owner), (type), __LINE__, (nblocks))

extern int ext4_create_memlog(struct inode *dir, struct dentry *dentry,
    umode_t mode, bool excl);
extern int ext4_post_create_memlog(struct dentry *dentry);

extern struct inode *ext4_copy_inode_memlog(struct inode *input);
extern void ext4_copy_back_inode_memlog(struct inode *inode, struct inode *copy);
extern void ext4_abort_copy_inode_memlog(struct inode *inode, struct inode *copy);

extern int inline ext4_lock_inode_bh(struct inode *inode);
extern int inline ext4_unlock_inode_bh(struct inode *inode);
extern int inline ext4_lock_dir_bh(struct inode *dir);
extern int inline ext4_unlock_dir_bh(struct inode *dir);

/* super_memlog.c */
extern struct inode_rec *ext4_alloc_inode_rec(struct super_block *sb, int flags);
extern void ext4_destroy_inode_rec(struct inode_rec *inode);

/* ialloc.c */
extern int find_group_orlov(struct super_block *sb, struct inode *parent,
			    ext4_group_t *group, umode_t mode,
			    const struct qstr *qstr);

extern int find_group_other(struct super_block *sb, struct inode *parent,
			    ext4_group_t *group, umode_t mode);

extern struct buffer_head *
ext4_read_inode_bitmap(struct super_block *sb, ext4_group_t block_group);

extern int recently_deleted(struct super_block *sb, ext4_group_t group, int ino);

// tmp:
extern void test_ext4_es_print_tree(struct inode *inode);
extern void ext4_print_dbg_info(struct inode *inode);

#endif  /* __EXT4_MEMLOG_H */
