/*
 * fs/memlog/page-writeback.c
 *
 * Copyright (C) 2016, Yige Hu.
 *
 * Contains functions related to user transaction conflict detection with
 * page level granuality.
 *
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/memlog.h>
#include <linux/slab.h>

#define PageNonTxWrite(page) (page->non_tx_write_cnt > 0)

int set_page_read(struct page *page)
{
	BUG_ON(!current->in_fs_tx);

	if (!PageRead(page)) {
		return !TestSetPageRead(page);
	}
	return 0;
}

/*
 * set_page_read() is racy if the caller has no reference against
 * page->mapping->host, and if the page is unlocked.  This is because another
 * CPU could truncate the page off the mapping and then free the mapping.
 *
 * Usually, the page _is_ locked, or the caller is a user-space process which
 * holds a reference on the inode by having an open file.
 *
 * In other cases, the page should be locked before running set_page_read().
 */
int set_page_read_lock(struct page *page)
{
	int ret;

	lock_page(page);
	ret = set_page_read(page);
	unlock_page(page);
	return ret;
}

int inline clear_page_read(struct page *page)
{
	return TestClearPageRead(page);
}

int clear_page_read_lock(struct page *page)
{
	int ret;

	lock_page(page);
	ret = TestClearPageRead(page);
	unlock_page(page);
	return ret;
}

int set_page_write(struct page *page)
{
	if (!PageWrite(page)) {
		return !TestSetPageWrite(page);
	}
	return 0;
}

int set_page_write_lock(struct page *page)
{
	int ret;

	lock_page(page);
	ret = set_page_write(page);
	unlock_page(page);
	return ret;
}

int inline clear_page_write(struct page *page)
{
	return TestClearPageWrite(page);
}

int clear_page_write_lock(struct page *page)
{
	int ret;

	lock_page(page);
	ret = TestClearPageWrite(page);
	unlock_page(page);
	return ret;
}

int try_get_tx_page_read_access(struct page *page)
{
	struct read_page_info *read_page, *read_page_next;
	memlog_t *memlog = current->memlog;
	int ret = 0;

	/* Detect conflict with transactional, non-transactional writes. */
	if (PageWrite(page) || PageNonTxWrite(page)) {
		fs_tx_debug("Conflict on read: abort transactional read on "
				"page %p.\n", page);
		ret = -ECONFLICT;
		return ret;
	}

	if (PageRead(page)) {
		list_for_each_entry_safe(read_page, read_page_next,
			    &memlog->l_read_pages, memlog_list) {

			/* The thread already has read access */
			if (read_page->page == page)
				return ret;
		}
	}

	/* The first time this thread getting read access to the page. */
	set_page_read(page);
	page->read_cnt ++;

	read_page = memlog_alloc_read_page_info();
	read_page->page = page;
	list_add_tail(&read_page->memlog_list, &memlog->l_read_pages);
	read_page->memlog = memlog;

	/* Page needs to be locked when memlog_clean_page_access
	 * is called by commit */
	memlog_alloc_page_rec(page);

	return ret;
}

int try_get_tx_page_read_access_lock(struct page *page)
{
	int ret;

	lock_page(page);
	ret = try_get_tx_page_read_access(page);
	unlock_page(page);
	return ret;
}

int try_get_tx_page_write_access(struct page *page)
{
	struct read_page_info *read_page, *read_page_next;
	memlog_t *memlog = current->memlog;
	struct list_head *item;
	int ret = 0;
	
	if (PageWrite(page) || PageNonTxWrite(page)) {
		fs_tx_debug("Conflict on write: abort new transactional write.\n");
		if (PageNonTxWrite(page)) fs_tx_debug("PageNonTxWrite(page), addr = %p\n", page);
		else fs_tx_debug("PageWrite(page), addr = %p\n", page);

		ret = -ECONFLICT;
		goto out;
	}

	if (PageRead(page)) {
		if (page->read_cnt <= 1) {
			/* If only current tx have read access, upgrade it into write */
			BUG_ON(page->read_cnt != 1);

			list_for_each_entry_safe(read_page, read_page_next,
				    &memlog->l_read_pages, memlog_list) {

				if (read_page->page == page) {
					item = &read_page->memlog_list;
					BUG_ON(list_empty(item));
					list_del_init(item);

					page->read_cnt = 0;
					clear_page_read(page);
					memlog_free_read_page_info(read_page);

					goto succeed;
				}
			}
		}
		fs_tx_debug("Write conflicts with read: abort transactional write "
			    "on page %p\n", page);
		ret = -ECONFLICT;
		goto out;
	}

	/* The original page needs to be locked in commit protocol. */
	memlog_alloc_page_rec(page);

succeed:
	set_page_write(page);

out:
	return ret;
}
