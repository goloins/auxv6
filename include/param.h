#define NPROC       128  // maximum number of processes
// PID_MAX: highest assignable process ID.  PIDs wrap back to 2 (preserving
// PID 1 for init) and skip any slot already in use.  Chosen to match the
// traditional POSIX lower bound (32767 = 2^15-1) while giving headroom well
// above NPROC=128.  Raise if NPROC is ever increased significantly.
#define PID_MAX     32767
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
// Per-process FD limit policy:
//   NOFILE_DEFAULT — soft limit inherited by each new process.
//   NOFILE_HARD    — system hard ceiling; setrlimit(RLIMIT_NOFILE) cannot exceed this.
//   NOFILE         — backward-compat alias for NOFILE_HARD.
#define NOFILE_DEFAULT  256  // starting soft limit for new processes
#define NOFILE_HARD     512  // absolute per-process ceiling (setrlimit upper bound)
#define NOFILE          NOFILE_HARD
// Deprecated: global open-file ceiling is no longer enforced by a fixed table.
// Keep NFILE defined for compatibility/documentation only.
#define NFILE      NOFILE_HARD
#define NINODE      200  // maximum number of active i-nodes
#define NDEV         64  // maximum block/char device number
#define ROOTDEV       1  // device number of file system root disk
// Exec argument policy:
// - EXEC_ARGC_MAX: hard argv[] entry cap (including argv[0], excluding NULL).
// - EXEC_ARG_BYTES_MAX: total bytes copied for argument strings, including
//   trailing NUL for each arg.  Keep this aligned with user-visible ARG_MAX.
#define EXEC_ARGC_MAX      128
#define EXEC_ARG_BYTES_MAX 4096
#define MAXARG             EXEC_ARGC_MAX  // compatibility alias
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGSIZE      (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF        128  // size of disk block cache (independent of log size)
#define FSSIZE       3000  // size of file system in blocks

// Pipe capacity policy (kernel-internal ring buffer bytes).
// Keep >= 512 to preserve POSIX PIPE_BUF atomic-write expectation while
// allowing larger throughput than the historical xv6-sized pipe.
#define PIPE_CAPACITY 2048

#if PIPE_CAPACITY < 512
#error "PIPE_CAPACITY must be >= 512 (PIPE_BUF atomicity floor)"
#endif

// Growth-budget guardrails (build-time): keep static tables bounded so
// seemingly small constant bumps do not silently bloat kernel image/runtime.
#define PROC_TABLE_BYTES_MAX   (2 * 1024 * 1024)
#define FILE_TABLE_BYTES_MAX   (512 * 1024)
#define KPAGE_META_BYTES_MAX   (4 * 1024 * 1024)

/* NFILE global-table invariant removed with dynamic per-process fdtable. */

// User stack policy for exec(): one guard region plus usable stack pages.
// USER_STACK_MAX_PAGES is the hard ceiling on demand growth; the initial
// allocation is USER_STACK_PAGES usable pages with a single guard page below.
#define USER_STACK_GUARD_PAGES  1
#define USER_STACK_PAGES        4
#define USER_STACK_MAX_PAGES   64

// Spinlock locking system modernization: Phase 1 safety nets.
// SPINLOCK_TIMEOUT_ITERS: iterations before acquire() panics on deadlock.
// At ~1GHz per CPU, 100M iterations ≈ 100ms timeout. Adjust per platform.
#define SPINLOCK_TIMEOUT_ITERS 100000000  // ~100ms at 1GHz, panic on exceed
// KDEBUG_SPINLOCK_LOCKFAIL: emit lock-name/owner diagnostics before panic on
// bad release, nested acquire, and timeout. Keep enabled while lock refactors
// are active; set to 0 for quieter production output.
#define KDEBUG_SPINLOCK_LOCKFAIL 1
// KDEBUG_LOCKDEP: enable lock-order validation (lockdep-lite) in spinlock
// acquire/release paths. Panics when a lower-rank lock is acquired while
// holding a higher-rank lock or when release order mismatches.
#define KDEBUG_LOCKDEP 1
#define MAX_LOCKDEP_HELD 32

#define NSOCKET 64              /* max open sockets system-wide */

