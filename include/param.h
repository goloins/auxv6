#define NPROC       128  // maximum number of processes
#define KSTACKSIZE 4096  // size of per-process kernel stack
#define NCPU          8  // maximum number of CPUs
#define KALLOC_CPU_CACHE 32  // per-CPU cached free pages before global flush
// Phase 2 allocator policy: explicit per-CPU watermarks + batched global moves.
#define KALLOC_PCPU_LOW_WATER   8   // try to keep at least this many local pages
#define KALLOC_PCPU_HIGH_WATER 28   // start draining when local cache reaches this
#define KALLOC_PCPU_REFILL_TRIGGER 4 // preemptive refill threshold (Phase 2c)
#define KALLOC_REFILL_BATCH    16   // pages pulled from global on local refill
#define KALLOC_DRAIN_BATCH     16   // max pages returned to global per drain
#define KALLOC_GLOBAL_RESERVE 256   // pages to leave globally available when possible
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
// USER_STACK_MAX_PAGES is the hard ceiling on demand growth; the initial
// allocation is USER_STACK_PAGES usable pages with a single guard page below.
#define USER_STACK_GUARD_PAGES  1
#define USER_STACK_PAGES        4
#define USER_STACK_MAX_PAGES   64

#define NSOCKET 64              /* max open sockets system-wide */

