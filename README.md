## The Texas Transactional File System (TxFS)

TxFS is a novel transactional file system that
builds upon the file system’s atomic-update mechanism
such as journaling. Though prior work has explored a
number of transactional file systems, TxFS has a unique
set of properties: a simple API, portability across different
hardware, high performance, low complexity (by building
on the journal), and full ACID transactions. 

Please
[cite](http://www.cs.utexas.edu/~vijay/bibtex/atc18-txfs.bib)
the following paper if you use TxFS: [TxFS: Leveraging File-System Crash Consistency to Provide ACID Transactions](http://www.cs.utexas.edu/~vijay/papers/atc18-txfs.pdf). Yige Hu, Zhiting Zhu, Ian Neal, Youngjin Kwon, Tianyu Cheng, Vijay Chidambaram, and Emmett Witchel [ATC
18](https://www.usenix.org/conference/atc18). [Bibtex](http://www.cs.utexas.edu/~vijay/bibtex/atc18-txfs.bib)

___

### In this repository

This repository includes two main components:

1. `linux-3.18.22-mod`. This contains the modified Linux kernel for TxFS. 
2. `benchmark`. Contains benchmarks used to evaluate TxFS.

Inside `linux-3.18.22-mod`, compiling the kernel follows the standard method of `configure` and `make`.  To see examples of TxFS in action, check out the examples inside `benchmark`.

___

### Example

We present an example where two files `file1` and `file2` are modified atomically. For the sake of brevity, we do not show error cases.

~~~~
ret = fs_tx_begin();
fd1 = open("file1.txt", O_RDWR | O_APPEND, 0644);
fd2 = open("file2.txt", O_RDWR | O_APPEND, 0644);
write(fd1, "foo\n", 4);
write(fd2, "bar\n", 4);
ret = ret = fs_tx_commit();
~~~~

The user does not have to add any other code apart from `fs_tx_begin()` and `fs_tx_commit()`. Once `fs_tx_commit` returns successfully, reads of both files will include both `foo` and `bar`.

`syscall_wrapper` in the `benchmmark` directory has the more complete version of this example. You can find it [here](https://github.com/ut-osa/txfs/blob/master/benchmark/syscall_wrapper/example.c).
___

### Contact

Please contact us at `yige@cs.utexas.edu` with any questions.  Drop
us a note if you would like to use TxFS in your research. 
