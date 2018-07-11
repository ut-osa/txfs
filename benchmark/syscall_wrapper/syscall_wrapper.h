#ifndef _FS_TX_WRAPPER_H
#define _FS_TX_WRAPPER_H

#include <unistd.h>
#include <sys/syscall.h>
#include <stdlib.h>

#define TX_DURABLE 0x00000001UL

/* Start a transaction */
int fs_tx_begin() {
	return syscall(322);
}

/* Commit a transaction */
int fs_tx_end(int flags) {
	int ret = syscall(323);
	if (flags & TX_DURABLE)
		sync();
	return ret;
}

/* Abort a transaction */
int fs_tx_abort() {
	return syscall(326);
}

/* Start printing kernel debugging info */
void fs_tx_dbg_begin() {
	syscall(324);
}

/* Stop printing kernel debugging info */
void fs_tx_dbg_end() {
	syscall(325);
}

#endif /* _FS_TX_WRAPPER_H */
