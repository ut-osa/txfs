#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include "log.h"

/* Page conflict detection in commit protocol */

/* slab cache for read list node */
static struct kmem_cache *read_page_info_cache;

int __init read_page_info_init(void)
{
	int retval = 0;

	L_ASSERT(read_page_info_cache == NULL);
	read_page_info_cache = kmem_cache_create("read_page_info_cache",
					     sizeof(struct read_page_info),
					     0,
					     (SLAB_RECLAIM_ACCOUNT|SLAB_PANIC|
					     SLAB_MEM_SPREAD), NULL);
	if (!read_page_info_cache) {
		retval = -ENOMEM;
		printk(KERN_EMERG "MEMLOG: no memory for read_page_info_cache\n");
	}
	return retval;
}

inline void memlog_destroy_read_page_info_cache(void)
{
	if (read_page_info_cache)
		kmem_cache_destroy(read_page_info_cache);
}

struct read_page_info *memlog_alloc_read_page_info(void)
{
	struct read_page_info *read_page;

	read_page = kmem_cache_alloc(read_page_info_cache, GFP_KERNEL);
	INIT_LIST_HEAD(&read_page->memlog_list);

	return read_page;
}

inline void memlog_free_read_page_info(struct read_page_info *read_page)
{
	kmem_cache_free(read_page_info_cache, read_page);
}

void memlog_clean_page_access(memlog_t *memlog)
{
	struct read_page_info *read_page, *read_page_next;
	struct page *page;
	struct list_head *item;

	list_for_each_entry_safe(read_page, read_page_next, &memlog->l_read_pages,
			memlog_list) {

		BUG_ON(read_page->memlog != memlog);
		page = read_page->page;

		item = &read_page->memlog_list;
		BUG_ON(list_empty(item));
		list_del_init(item);

		page->read_cnt--;
		if (!page->read_cnt) clear_page_read(page);

		memlog_free_read_page_info(read_page);
	}
	BUG_ON(!list_empty(&memlog->l_read_pages));
}

void memlog_clean_page_access_lock(memlog_t *memlog)
{
	struct read_page_info *read_page, *read_page_next;
	struct page *page;
	struct list_head *item;

	list_for_each_entry_safe(read_page, read_page_next, &memlog->l_read_pages,
			memlog_list) {

		BUG_ON(read_page->memlog != memlog);
		page = read_page->page;

		lock_page(page);

		item = &read_page->memlog_list;
		BUG_ON(list_empty(item));
		list_del_init(item);

		page->read_cnt--;
		if (!page->read_cnt) clear_page_read(page);

		unlock_page(page);
		memlog_free_read_page_info(read_page);
	}
	BUG_ON(!list_empty(&memlog->l_read_pages));
}



/* Page locking in commit protocol */
static struct kmem_cache *memlog_page_rec_cache;

int __init memlog_page_rec_init(void)
{
	int retval = 0;

	L_ASSERT(memlog_page_rec_cache == NULL);
	memlog_page_rec_cache = kmem_cache_create("memlog_page_rec_cache",
					     sizeof(struct page_rec),
					     0,
					     (SLAB_RECLAIM_ACCOUNT|SLAB_PANIC|
					     SLAB_MEM_SPREAD), NULL);
	if (!memlog_page_rec_cache) {
		retval = -ENOMEM;
		printk(KERN_EMERG "MEMLOG: no memory for memlog page_rec cache\n");
	}
	return retval;
}

void memlog_destroy_page_rec_cache(void)
{
	if (memlog_page_rec_cache)
		kmem_cache_destroy(memlog_page_rec_cache);
}

static inline void page_rec_lock_list_add(struct page_rec *record,
	struct list_head *list)
{
	struct list_head *item;

	item = &record->r_lock_list;
	list_add_tail(item, list);
}

static inline void page_rec_lock_list_del(struct page_rec *record)
{
	struct list_head *item;

	item = &record->r_lock_list;
	if (!list_empty(item))
		list_del(item);
}

void memlog_alloc_page_rec(struct page *page)
{
	struct page_rec *ret = kmem_cache_zalloc(memlog_page_rec_cache, GFP_KERNEL);
	memlog_t *memlog;

	memlog = current->memlog;
	L_ASSERT(memlog);

	if (!ret) return;
	ret->r_sort_key = page;
	get_page(page);
	INIT_LIST_HEAD(&ret->r_lock_list);

	page_rec_lock_list_add(ret, &memlog->l_page_lockset);
	//fs_tx_debug("Allocated page_rec for page %p\n", page);
}

inline void memlog_free_page_rec(struct page_rec *precord)
{
	if (!precord) return;
	page_rec_lock_list_del(precord);
	kmem_cache_free(memlog_page_rec_cache, precord);
}

static int page_rec_cmp(void *priv, struct list_head *a,
		struct list_head *b)
{
	struct page_rec *precord_a = container_of(a, struct page_rec, r_lock_list);
	struct page_rec *precord_b = container_of(b, struct page_rec, r_lock_list);

	return (precord_a->r_sort_key >= precord_b->r_sort_key);
}

void memlog_lock_pages(memlog_t *memlog)
{
	struct page_rec *precord, *precord_next;
	struct page *page;

	list_sort(NULL, &memlog->l_page_lockset, page_rec_cmp);

	/* Blocking locks, might cause the thread to sleep. */
	list_for_each_entry_safe(precord, precord_next, &memlog->l_page_lockset,
		r_lock_list) {

		page = (struct page *) precord->r_sort_key;
		lock_page(page);
		//fs_tx_debug("page %p locked.\n", page);
	}
}

void memlog_unlock_pages(memlog_t *memlog)
{
	struct page_rec *precord, *precord_next;
	struct page *page;

	list_for_each_entry_safe(precord, precord_next, &memlog->l_page_lockset,
		r_lock_list) {

		page = (struct page *) precord->r_sort_key;
		unlock_page(page);
	}
}

void memlog_unlock_pages_put_references(memlog_t *memlog)
{
	struct page_rec *precord, *precord_next;
	struct page *page;

	list_for_each_entry_safe(precord, precord_next, &memlog->l_page_lockset,
		r_lock_list) {

		page = (struct page *) precord->r_sort_key;
		unlock_page(page);
		put_page(page);
	}
}
