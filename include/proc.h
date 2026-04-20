// Shared signal constants/types used by kernel and user ABI.
#include "types.h"
#include "param.h"
#include "stdint.h"
#include "signal.h"
#include "sys/types.h"
#include "mmu.h"     // For struct taskstate, NSEGS
// Forward declare struct taskstate to avoid double-inclusion

struct run;
struct spinlock;
struct address_space;

// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
  int sched_last;              // Index hint: start next scheduler scan here
  uint sched_passes;           // Number of scheduler outer-loop passes
  uint sched_idle_halts;       // Number of idle hlt transitions
  uint sched_picks;            // Number of RUNNABLE selections dispatched
  uint sched_q_dispatch[5];    // MLFQ: dispatches per queue level
  uint sched_q_empty_passes;   // MLFQ: scheduler passes with all queues empty
  struct run *kfree_cache[KALLOC_CPU_CACHE]; // Per-CPU free-page stash
  int kfree_cache_count;       // Number of pages in kfree_cache
#if KDEBUG_LOCKDEP
  int lockdep_depth;           // Number of currently held locks on this CPU
  struct spinlock *lockdep_locks[MAX_LOCKDEP_HELD];
  int lockdep_ranks[MAX_LOCKDEP_HELD];
#endif
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, STOPPED, ZOMBIE };

// Per-process file descriptor table (Phase 1A: dynamic replacement for fixed ofile[NOFILE])
struct fdtable {
  struct file **entries;       // Dynamic array of file pointers
  uint8_t     *fdflags;        // Per-fd flags (FD_CLOEXEC etc.), indexed by fd
  int capacity;                // Total allocated slots
  int nfds;                    // Number of valid fd entries (high water mark)
  int next_fd_hint;            // Next candidate index for fdalloc search
};

struct procinfo_k {
  int pid;
  int ppid;
  int pgid;
  int sid;
  int tty;
  int uid;
  int gid;
  int state;
  uint sz;
  uint cticks;          /* cumulative CPU ticks charged to this process */
  char name[16];
};

struct procfdinfo_k {
  int pid;
  int fd;
  int type;
  int readable;
  int writable;
  uint64_t off;  /* Current file offset — widened to match struct file.off */
  uint dev;
  uint inum;
  char name[16];
};

struct procfdlimitinfo_k {
  int pid;
  uint soft;
  uint hard;
  uint used;
  uint highwater;
  char name[16];
};

#define PROC_NGROUPS_MAX 16

// MLFQ scheduler flag bits (struct proc sched_flags)
#define SCHED_F_BUDGET_EXPIRED  0x01  // quantum exhausted; demote on next enqueue
#define SCHED_F_BOOST_APPLIED   0x02  // promoted in current boost epoch

// Enqueue reason codes passed to schedq_enqueue_locked()
#define SCHED_ENQ_FORK    1  // newly forked/userinit process
#define SCHED_ENQ_WAKE    2  // wakeup from sleep (I/O or event)
#define SCHED_ENQ_YIELD   3  // timer preemption or voluntary yield
#define SCHED_ENQ_SIGNAL  4  // resumed by SIGCONT or signal delivery
#define SCHED_ENQ_ALARM   5  // woken by alarm expiry
#define SCHED_ENQ_BOOST   6  // global anti-starvation boost

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  struct address_space *addrsp; // Authoritative address-space state when present
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  int ppid;                    // Parent process ID (cached)
  int pgid;                    // Process group ID
  int sid;                     // Session ID
  int tty;                     // Controlling terminal index (-1 means none)
  int uid;                     // Effective user ID
  int gid;                     // Effective group ID
  int ruid;                    // Real user ID
  int rgid;                    // Real group ID
  int suid;                    // Saved set-user-ID
  int sgid;                    // Saved set-group-ID
  int ngroups;                 // Number of supplementary groups
  gid_t groups[PROC_NGROUPS_MAX]; // Supplementary groups
  int umask;                   // File creation mask
  uint rlimit_nofile_cur;      // Soft RLIMIT_NOFILE for this process
  uint rlimit_nofile_max;      // Hard RLIMIT_NOFILE for this process
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  struct proc *tick_next;      // Next entry in tick-sleeper queue
  int on_tickq;                // 1 when queued on tick-sleeper queue
  int killed;                  // If non-zero, have been killed
  int xstatus;                 // Wait status consumed at reap
  int wait_event;              // One-shot event: stopped/continued
  int wait_status;             // Status payload for wait_event
  uint sig_pending;            // Pending signal bitmap (SIGBIT)
  uint sig_caught;             // Signals caught with user handlers (TODO trampoline)
  uint sig_mask;               // Blocked signal bitmap (SIGBIT)
  uint sig_ignored;            // Ignored signal bitmap (SIGBIT)
  uint sig_handler[NSIG];      // 0=default, 1=ignore, otherwise user handler PC
  uint sig_actmask[NSIG];      // Per-signal mask set by sigaction
  uint sig_actflags[NSIG];     // Per-signal flags set by sigaction
  uint alarm_ticks;            // Tick count when SIGALRM should fire (0=none)
  uint alarm_interval_ticks;   // Reload interval for ITIMER_REAL (0=one-shot)
  uint cticks;                 // Cumulative CPU ticks charged to this process
  // MLFQ scheduler metadata.  All fields protected by ptable.lock, except
  // sched_budget_left and sched_flags which are written lock-free by the
  // owning CPU while the process is RUNNING (safe: single owner at a time).
  uchar  sched_q;              // current queue level [0..MLFQ_NQUEUES-1]
  uchar  sched_flags;          // SCHED_F_* flag bits
  ushort sched_budget_left;    // ticks remaining in current quantum
  uint   sched_enq_tick;       // ticks when last enqueued RUNNABLE
  uint   sched_last_start_tick;// ticks when last dispatched onto a CPU
  uint   sched_last_block_tick;// ticks when last entered SLEEPING state
  uint   sched_last_wake_tick; // ticks when last woken to RUNNABLE
  uint   sched_cpu_burst_ticks;// CPU ticks charged this dispatch slot
  struct proc *sched_next;     // intrusive doubly-linked queue forward link
  struct proc *sched_prev;     // intrusive doubly-linked queue backward link
  uint stack_top;              // VA of top of user stack region (const after exec)
  uint stack_bot;              // VA of bottom of current accessible user stack
  struct fdtable *fdtable;     // Dynamic file descriptor table (Phase 1A)
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
