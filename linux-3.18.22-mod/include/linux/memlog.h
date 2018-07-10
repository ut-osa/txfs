#ifndef _LINUX_MEMLOG_H
#define _LINUX_MEMLOG_H

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/dcache.h>

#define L_ASSERT(assert)        BUG_ON(!(assert))

#define REC_COPY	0x00000001UL
#define REC_NEW		0x00000002UL
#define REC_NEW_REF	0x00000004UL
#define REC_DEL		0x00000008UL
#define REC_LOCK	0x00000010UL
#define REC_DIR		0x00000020UL

#define MEMLOG_IN_TX		0x00000001UL
#define MEMLOG_IN_COMMIT	0x00000002UL
#define MEMLOG_IN_ABORT		0x00000004UL
#define MEMLOG_IN_DIRTY_INODES	0x00000008UL

#define ELOCK			533
#define RET_TX_READONLY		600

struct files_struct;
struct memlog_s;
typedef struct memlog_s		memlog_t;

void __fs_tx_debug_emerg(const char *file, const char *func, unsigned int line,
		const char *fmt, ...);
#define fs_tx_debug_emerg(fmt, a...) \
	__fs_tx_debug_emerg(__FILE__, __func__, __LINE__, (fmt), ##a)

#ifdef CONFIG_FS_TX_DEBUG
extern atomic_t tx_cnt;
#endif

/* page record in memlog, only used by l_page_lockset.
 */
struct page_rec {
	void			*r_sort_key;
	struct list_head	r_lock_list;
};

/* inode record in memlog, currently only used by l_inode_lockset.
 */
struct inode_rec {
	struct inode		*r_inode_addr;
	struct inode		*r_inode_copy;
//	struct list_head	r_log_list;

	void			*r_sort_key;
	struct list_head	r_lock_list;

	int			r_flags;
};

/* dentry record in memlog, smallest unit of info storage for COW in tx;
 * also used in l_dentry_lockset.
 */
struct dentry_rec {
	struct dentry		*r_dentry_addr;
	struct dentry		*r_dentry_copy;
	/* In case no dentry exists, but inode exists */
	struct inode            *r_inode_addr;
	struct list_head	r_log_list;

	void			*r_sort_key;
	struct list_head	r_lock_list;

	bool			r_new_d_inode;
	struct file		*r_file;
	/* file descriptor; also used for dfd param in unlink record */
	int			r_fd;
	int			r_flags;
	/* Used by close() syscall, need delayed close clean-up */
	bool			r_need_close;

	/* Used by unlink. Newly created REC_NEW entry after a pre-existing
	 * file being marked with REC_DEL.
	 * The REC_DEL entry will have this pointing to the new dentry,
	 * so that in commit phase we will know about this new dentry;
	 * while the new dentry will have this pointing to the REC_DEL entry,
	 * which is used as an indicator in dentry allocation. */
	struct dentry_rec	*r_dentry_rec;

	/* global dentry's d_ctime recorded when tx starts */
	unsigned long		r_dentry_start_ctime;
	/* global inode's i_ctime recorded when tx starts */
	struct timespec		r_inode_start_ctime;

	memlog_t		*r_memlog;
};

/*
 * per-process memory log.
 */
struct memlog_s
{
	/* lock set in commit protocol. */
	struct list_head        l_page_lockset;
	struct list_head	l_inode_lockset;
	struct list_head	l_dentry_lockset;

	struct list_head	l_dentry_recs;
	struct list_head	l_dentry_new;
	struct list_head	l_dentry_delete;

	/* per-process local dentry. */
	struct hlist_bl_head	*l_dentry_hashtable;

	/* For conflict detection, reclaim read access */
//	spinlock_t		l_lock;		// TODO: memlog shared by multi threads
	struct list_head	l_read_pages;
	bool			l_aborted;
	bool			l_write;

	/* Accumulated credits for a user tx, including selective data journaling
	 * credits, and pre-calculated over-estimation on metadata credits.
	 * Later, if we change to enable tx shared by threads, this needs to be
	 * made into atomic_t. */
	int			l_overestimated_credits;
	void			*l_journal;

	/* To support nested transactions. */
	int			l_nested;
};

/* For conflict detection, list node for tx-s with read access to a page */
struct read_page_info {
	struct page		*page;
	struct list_head	memlog_list;	/* memlog->l_read_pages */

	memlog_t		*memlog;
};



/* log.c */
extern memlog_t * alloc_memlog(void);
extern int abort_memlog(memlog_t *memlog);
extern int commit_memlog(memlog_t *memlog);
extern int free_memlog(memlog_t *memlog);

/* dcache_memlog.c */
extern void dput_nolock(struct dentry *dentry);
extern void dput_and_kill_isolated_nolock(struct dentry *dentry);
extern int do_remaining_new_dentry(struct dentry *dentry, bool new_d_inode,
					memlog_t *memlog);
extern int do_remaining_copy_dentry(struct dentry *dentry, struct dentry *copy,
					bool new_d_inode);

extern struct inode_rec *memlog_alloc_inode_rec(struct inode *inode, int flags);
extern inline void memlog_free_inode_rec(struct inode_rec *record);
extern void memlog_destroy_inode_rec(struct inode_rec *record);

/* log.c - conflict detection */
extern struct read_page_info *memlog_alloc_read_page_info(void);
extern inline void memlog_free_read_page_info(struct read_page_info *read_page);

/* dcache.c */
extern struct dentry_rec *memlog_alloc_dentry_rec(struct dentry *dentry, int flags);
extern inline void memlog_free_dentry_rec(struct dentry_rec *drecord);
extern void memlog_unlink_copy_dentry_rec(struct dentry_rec *record, bool lock_pages);
extern void memlog_unlink_new_dentry_rec(struct dentry_rec *record, bool lock_pages);
//extern inline struct dentry *memlog_alloc_dentry_new(void);
//extern struct dentry *memlog_alloc_dentry(struct dentry *dentry);
//extern inline void memlog__d_free(struct dentry *dentry);
//extern struct dentry *memlog__d_alloc(struct super_block *sb, const struct qstr *name);

extern void memlog_d_rehash(struct dentry *dentry);
extern void memlog__d_drop(struct dentry *dentry, memlog_t *memlog);
extern struct dentry *memlog__d_lookup(const struct dentry *parent,
	const struct qstr *name);

extern inline void dentry_rec_log_list_del(struct dentry_rec *record);

/* page.c */
extern void memlog_alloc_page_rec(struct page *page);
extern inline void memlog_free_page_rec(struct page_rec *precord);
extern inline void page_rec_lock_list_add(struct page_rec *record,
	struct list_head *list);
extern inline void page_rec_lock_list_del(struct page_rec *record);

/* read_write.c */
extern inline struct fd fdget_memlog(int fd);

/* file_memlog.c */
extern int __close_fd_memlog(struct files_struct *files, unsigned fd);
extern int __post_close_fd_memlog(struct files_struct *files, struct file *file,
		unsigned fd);
extern int __abort_close_fd_memlog(struct files_struct *files, struct file *file,
		unsigned fd);

/* page_conflict.c, conflict detection */
extern int set_page_read(struct page *page);
extern int set_page_read_lock(struct page *page);
extern int inline clear_page_read(struct page *page);
extern int clear_page_read_lock(struct page *page);
extern int inline set_page_write(struct page *page);
extern int set_page_write_lock(struct page *page);
extern int inline clear_page_write(struct page *page);
extern int clear_page_write_lock(struct page *page);
extern int try_get_tx_page_read_access(struct page *page);
extern int try_get_tx_page_read_access_lock(struct page *page);
extern int try_get_tx_page_write_access(struct page *page);

/* inode_memlog.c */
extern inline void lock_sb_list_lock(void);
extern inline void unlock_sb_list_lock(void);
extern inline void lock_inode_hash_lock(void);
extern inline void unlock_inode_hash_lock(void);

static inline bool is_std_fd(int fd)
{
	return fd == 0 || fd == 1 || fd == 2;
}

#endif  /* _LINUX_MEMLOG_H */
