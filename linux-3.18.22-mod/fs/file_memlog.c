/*
 * fs/file_memlog.c
 *
 * Complete reimplementation
 * (C) 2016 Yige Hu,
 */

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include "internal.h"
#include "dcache_memlog.h"

/* To support close() inside a tx, based on __close_fd().
 * The pre-commit part contains operations related to file descriptor removal;
 * the post-commit part does actual clean-up.
 */
/* Note: Currently, if a file is opened before the tx and closed inside the tx,
 * in abort phase I don't consider the re-opening of that file descriptor.
 * Usage on the file descriptor after the tx will be returned a 'bad file
 * descriptor' error message.
 */
int __close_fd_memlog(struct files_struct *files, unsigned fd)
{
	struct file *file;
	struct fdtable *fdt;

	spin_lock(&files->file_lock);
	fdt = files_fdtable(files);
	if (fd >= fdt->max_fds)
		goto out_unlock;
	file = fdt->fd[fd];
	if (!file)
		goto out_unlock;
	rcu_assign_pointer(fdt->fd[fd], NULL);

	/* Only record if it is the first time calling close() in tx */
	if (file->f_dentry->d_dentry_rec
		    && (file->f_dentry->d_dentry_rec->r_flags | REC_COPY)
		    && !file->f_dentry->d_dentry_rec->r_need_close) {
		fs_tx_debug("Recording close on file %p, fd = %d", file, fd);
		file->f_dentry->d_dentry_rec->r_need_close = true;
		file->f_dentry->d_dentry_rec->r_fd = fd;
	} else {
		__clear_close_on_exec(fd, fdt);
		__put_unused_fd(files, fd);
		spin_unlock(&files->file_lock);
		return filp_close(file, files);
	}

	spin_unlock(&files->file_lock);


	return 0;

out_unlock:
	spin_unlock(&files->file_lock);
	return -EBADF;
}

int __post_close_fd_memlog(struct files_struct *files, struct file *file,
		unsigned fd)
{
	struct fdtable *fdt;

	fs_tx_debug("Executing close on file %p, fd = %d", file, fd);
	spin_lock(&files->file_lock);
	fdt = files_fdtable(files);
	if (fd >= fdt->max_fds)
		goto out_unlock;
	__clear_close_on_exec(fd, fdt);
	__put_unused_fd(files, fd);
	spin_unlock(&files->file_lock);
	return filp_close(file, files);

out_unlock:
	spin_unlock(&files->file_lock);
	return -EBADF;
}

int __abort_close_fd_memlog(struct files_struct *files, struct file *file,
		unsigned fd)
{
	struct fdtable *fdt;

	file->f_dentry->d_dentry_rec->r_need_close = false;
	spin_lock(&files->file_lock);
	fdt = files_fdtable(files);
	if (fd >= fdt->max_fds)
		goto out_unlock;
	rcu_assign_pointer(fdt->fd[fd], file);
	spin_unlock(&files->file_lock);
	return 0;

out_unlock:
	spin_unlock(&files->file_lock);
	return -EBADF;
}
