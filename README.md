TxFS is a novel transactional file system that
builds upon a file system’s atomic-update mechanism
such as journaling. Though prior work has explored a
number of transactional file systems, TxFS has a unique
set of properties: a simple API, portability across different
hardware, high performance, low complexity (by building
on the journal), and full ACID transactions. We port
SQLite and Git to use TxFS, and experimentally show that
TxFS provides strong crash consistency while providing
equal or better performance.
