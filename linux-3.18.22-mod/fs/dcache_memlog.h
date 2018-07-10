#ifndef _DCACHE_MEMLOG_H
#define _DCACHE_MEMLOG_H

#include <linux/dcache.h>
#include <linux/buffer_head.h>
#include <linux/memlog.h>

#include "internal.h"

extern long nr_dentry;
extern long nr_dentry_unused;

struct external_name {
	union {
		atomic_t count;
		struct rcu_head head;
	} u;
	unsigned char name[];
};

/* In dcache.c */
extern inline int d_revalidate(struct dentry *dentry, unsigned int flags);
extern inline void __dget_dlock(struct dentry *dentry);

/* namei.c */
extern inline int may_create(struct inode *dir, struct dentry *child);

/* In dcache_memlog.c */
extern struct dentry *d_alloc_memlog(struct dentry * parent, const struct qstr *name,
					struct file *file);
extern struct dentry *d_alloc_copy_memlog(struct dentry *parent, const struct qstr *name,
					struct dentry *input, struct file *file);
extern struct dentry *d_alloc_del_memlog(struct dentry *input, int dfd);
extern void d_copy_inode_memlog(struct dentry *input, struct file *file);

/* In namei_memlog.c */
extern struct dentry *d_lookup_read_memlog(const struct dentry *, const struct qstr *,
						bool *is_unlinked);
extern struct dentry *d_lookup_write_memlog(const struct dentry *, const struct qstr *);
extern struct dentry *lookup_dcache_memlog(struct qstr *name, struct dentry *dir,
				    unsigned int flags, bool *need_lookup, struct file *file);
extern struct dentry *lookup_real_memlog(struct inode *dir, struct dentry *dentry,
				  unsigned int flags);
extern int lookup_open_memlog(struct nameidata *nd, struct path *path,
			struct file *file,
			const struct open_flags *op,
			bool got_write, int *opened);

extern long do_unlinkat_memlog(int dfd, const char __user *pathname);

/* In inode_memlog.c */
//extern struct inode *new_inode_pseudo_memlog(struct super_block *sb);
extern struct inode *new_inode_memlog(struct super_block *sb);
#endif  /* _DCACHE_MEMLOG_H */
