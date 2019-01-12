/*
 *  linux/fs/user_tx.c
 *
 *  Copyright (C) 2015 Yige Hu
 */

#include <linux/export.h>
#include <linux/syscalls.h>
#include <linux/jbd2.h>
#include <linux/memlog.h>

static bool fs_tx_dbg_trigger = false;

void __fs_tx_debug_emerg(const char *file, const char *func, unsigned int line,
    const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	if (!fs_tx_dbg_trigger) return;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	printk(KERN_EMERG "%d %s: (%s, %u): %pV\n", current->pid, file,
		func, line, &vaf);
	va_end(args);
}

#ifdef CONFIG_FS_TX_DEBUG
atomic_t tx_cnt;

void __fs_tx_debug(const char *file, const char *func, unsigned int line,
    const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	if (!fs_tx_dbg_trigger) return;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	printk(KERN_EMERG "%d %s: (%s, %u): %pV\n", current->pid, file,
		func, line, &vaf);
	va_end(args);
}

void __fs_tx_dump_stack(void)
{
	if (!fs_tx_dbg_trigger) return;
	dump_stack();
}
#endif

#ifdef CONFIG_FS_TX
int fs_txbegin(void)
{
	int ret = 0;

	if (current->in_fs_tx) {
		fs_tx_debug("Already in user tx.\n");
		return 0;
	}

	fs_tx_debug("--------------------- fs_txbegin ------------------\n");
	current->in_fs_tx = MEMLOG_IN_TX;
	current->memlog = alloc_memlog();

#ifdef CONFIG_FS_TX_DEBUG
	atomic_inc(&tx_cnt);
#endif
	return ret;
}
EXPORT_SYMBOL(fs_txbegin);

int fs_txabort(void)
{
	int ret = 0;
	memlog_t *memlog = current->memlog;

	fs_tx_debug("--------------------- fs_txabort ------------------\n");
	if (memlog) {
		ret = abort_memlog(memlog);
		free_memlog(memlog);
		current->memlog = NULL;
	}
	current->in_fs_tx = 0;
	jbd2_abort_current_user_tx();


#ifdef CONFIG_FS_TX_DEBUG
	atomic_dec(&tx_cnt);
#endif
	return ret;
}

int fs_txend(void)
{
	int ret = 0;
	memlog_t *memlog = current->memlog;

	fs_tx_debug("--------------------- fs_txend ------------------\n");
	if (!memlog) {
		BUG_ON(current->t_transaction);
		goto out;
	}

	if (memlog->l_aborted) {
		fs_tx_debug("User tx already aborted by eager conflict detection.\n");
		ret = fs_txabort();
		return -ECONFLICT;
	}

	/* Optimization: read-only tx can commit without conflict checking
	 *		- just abort local copies. */
	if (!memlog->l_write) {
		fs_tx_debug("User tx read-only, not need to commit and overwrite.\n");
		ret = fs_txabort();
		return RET_TX_READONLY;
	}

	ret = commit_memlog(memlog);
	if (ret == -ECONFLICT) {
		fs_tx_debug("User tx aborted by commit-time conflict detection.\n");
		goto out_abort;

	} else if (ret == -ENOJOURNAL) {
		fs_tx_debug("User tx aborted by full journal.\n");
		goto out_abort;

	} else if (ret == -ELOCK) {
		fs_tx_debug("User tx cannot get all i_mutex locks.\n");
		goto out_abort;

	} else if (unlikely(ret)) {
		fs_tx_debug("Error: commit_memlog() ret = %d....", ret);
#if 0
		// TODO: If do abort here, we will have duplicated commit and abort
		//	 on the same entry, leading to double ref-count decrements.

		current->in_fs_tx = MEMLOG_IN_ABORT;
		fs_txabort();
		return ret;
#endif
	}

out:
	current->in_fs_tx = 0;
	fs_tx_debug("--------------------- finished committing ------------------\n");
	/* jbd2_submit_user_tx moved inside commit_memlog. */
	current->memlog = NULL;
	free_memlog(memlog);

#ifdef CONFIG_FS_TX_DEBUG
	atomic_dec(&tx_cnt);
#endif
	return ret;

out_abort:
	current->in_fs_tx = MEMLOG_IN_ABORT;
	fs_txabort();
	return ret;
}
EXPORT_SYMBOL(fs_txend);
#endif

SYSCALL_DEFINE0(fs_txbegin)
{
#ifdef CONFIG_FS_TX
	if (current->in_fs_tx) {
		memlog_t *memlog = current->memlog;
		BUG_ON(!memlog);
		fs_tx_debug("Already in tx, l_nested = %d\n", memlog->l_nested);
		memlog->l_nested ++;
		return 0;
	}
	return fs_txbegin();
#else
	return -1;
#endif
}

SYSCALL_DEFINE0(fs_txend)
{
#ifdef CONFIG_FS_TX
	memlog_t *memlog = current->memlog;
	if (!memlog) {
		/* Not in a tx. */
		BUG_ON(current->t_transaction);
		return 0;
	} else if (memlog->l_nested) {
		/* Current tx is nested. */
		BUG_ON(!current->in_fs_tx);
		fs_tx_debug("In nested tx, l_nested = %d\n", memlog->l_nested);
		memlog->l_nested --;
		return 0;
	}
	return fs_txend();
#else
	return -1;
#endif
}

SYSCALL_DEFINE0(fs_txabort)
{
#ifdef CONFIG_FS_TX
	memlog_t *memlog = current->memlog;
	if (!memlog) {
		/* Not in a tx. */
		BUG_ON(current->t_transaction);
		return 0;
	} else if (memlog->l_nested) {
		/* Current tx is nested. */
		BUG_ON(!current->in_fs_tx);
		fs_tx_debug("In nested tx, l_nested = %d\n", memlog->l_nested);
		memlog->l_nested --;
		return 0;
	}
	return fs_txabort();
#else
	return -1;
#endif
}

SYSCALL_DEFINE0(fs_tx_dbg_begin)
{
	fs_tx_dbg_trigger = true;
	return 0;
}

SYSCALL_DEFINE0(fs_tx_dbg_end)
{
	fs_tx_dbg_trigger = false;
	return 0;
}
