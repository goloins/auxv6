#define NPROC       128  // maximum number of processes
#define KSTACKSIZE 4096  // size of per-process kernel stack
#define NCPU          8  // maximum number of CPUs
#define KALLOC_CPU_CACHE 32  // per-CPU cached free pages before global flush
#define NOFILE       32  // open files per process
#define NFILE       256  // open files per system
#define NINODE      200  // maximum number of active i-nodes
#define NDEV         64  // maximum block/char device number
#define ROOTDEV       1  // device number of file system root disk
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGSIZE      (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF        128  // size of disk block cache (independent of log size)
#define FSSIZE       3000  // size of file system in blocks

// User stack policy for exec(): one guard region plus usable stack pages.
#define USER_STACK_GUARD_PAGES 1
#define USER_STACK_PAGES       4

