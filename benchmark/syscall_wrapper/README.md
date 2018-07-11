The example shows how to use the system call wrapper to group file system
operations in a TxFS transaction. The flag passed into fs_tx_end()
decides whether a trnsaction is a durable one.
