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

// Minimal POSIX-like signal numbering used by kernel bookkeeping.
#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGCHLD  17
#define SIGTERM  15
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20
#define SIGKILL  9

// Signals are tracked in a 32-bit pending/mask bitmap.
#define NSIG             32
#define SIGBIT(sig)      (1U << ((sig) - 1))

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  int ppid;                    // Parent process ID (cached)
  int pgid;                    // Process group ID
  int sid;                     // Session ID
  int tty;                     // Controlling terminal index (-1 means none)
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
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
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
