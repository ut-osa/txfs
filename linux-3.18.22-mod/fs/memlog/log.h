#ifndef _LOG_H
#define _LOG_H

#include <linux/fs.h>
#include <linux/jbd2.h>
#include <linux/memlog.h>
#include <linux/errno.h>
#include <linux/vmalloc.h>
#include <linux/list_sort.h>

/* dcache.c */
extern int __init memlog_dcache_init(void);
extern int __init memlog_dentry_rec_init(void);
extern int __init memlog_page_rec_init(void);
extern void memlog_destroy_dcache(void);
extern void memlog_destroy_page_rec_cache(void);
extern int memlog_init_hashtable(memlog_t *memlog);
extern void memlog_destroy_hashtable(memlog_t *memlog);
extern void memlog_unlink_del_dentry_rec(struct dentry_rec *record);
extern int memlog_commit_new_dentry_rec(struct dentry_rec *record);
extern int memlog_commit_copy_dentry_rec(struct dentry_rec *record);
extern int memlog_commit_del_dentry_rec(struct dentry_rec *record);
extern void memlog_lock_dentries(memlog_t *memlog);
extern void memlog_unlock_dentries(memlog_t *memlog);
extern int memlog_check_dentry_conflict(memlog_t *memlog);

// tmp
extern void memlog_get_dentry_references(memlog_t *memlog);
extern void memlog_put_dentry_references(memlog_t *memlog);

/* page.c */
extern int __init read_page_info_init(void);
extern inline void memlog_destroy_read_page_info_cache(void);
extern void memlog_clean_page_access(memlog_t *memlog);
extern void memlog_clean_page_access_lock(memlog_t *memlog);
extern void memlog_lock_pages(memlog_t *memlog);
extern void memlog_unlock_pages(memlog_t *memlog);
extern void memlog_unlock_pages_put_references(memlog_t *memlog);

#endif  /* _LOG_H */
