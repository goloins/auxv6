// Mutual exclusion lock.
struct spinlock {
  uint locked;       // Is the lock held?

  // Lockdep metadata.
  int rank;          // Monotonic lock order rank (lower acquires before higher).
  char *class_name;  // Optional lock class label for diagnostics.

  // For debugging:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
  uint pcs[10];      // The call stack (an array of program counters)
                     // that locked the lock.
};

// Default rank for unannotated locks.
#define LOCK_RANK_DEFAULT 1000

// Initial lock rank map for core locks (lockdep-lite tranche).
#define LOCK_RANK_CONSOLE_INPUT    20
#define LOCK_RANK_CONSOLE_TTY      20
#define LOCK_RANK_CONSOLE_GFX      25
#define LOCK_RANK_FTABLE_INTERNAL  35
#define LOCK_RANK_TICKS            40
#define LOCK_RANK_PTABLE           2000
#define LOCK_RANK_KMEM             2100
#define LOCK_RANK_LOG              60

