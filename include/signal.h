#ifndef XV6_SIGNAL_H
#define XV6_SIGNAL_H

#include "types.h"

// POSIX signal numbering (Linux x86 compatible)
#define SIGHUP    1   // Hangup (terminal closed)
#define SIGINT    2   // Interrupt (Ctrl+C)
#define SIGQUIT   3   // Quit (Ctrl+\)
#define SIGILL    4   // Illegal instruction
#define SIGTRAP   5   // Trace/breakpoint trap
#define SIGABRT   6   // Abort
#define SIGBUS    7   // Bus error (alignment fault)
#define SIGFPE    8   // Floating point exception
#define SIGKILL   9   // Kill (uncatchable)
#define SIGUSR1  10   // User-defined signal 1
#define SIGSEGV  11   // Segmentation fault
#define SIGUSR2  12   // User-defined signal 2
#define SIGPIPE  13   // Broken pipe
#define SIGALRM  14   // Alarm clock
#define SIGTERM  15   // Termination
#define SIGSTKFLT 16  // Stack fault (unused on Linux)
#define SIGCHLD  17   // Child status changed
#define SIGCONT  18   // Continue if stopped
#define SIGSTOP  19   // Stop (uncatchable)
#define SIGTSTP  20   // Terminal stop (Ctrl+Z)
#define SIGTTIN  21   // Background read from tty
#define SIGTTOU  22   // Background write to tty
#define SIGURG   23   // Urgent data on socket
#define SIGXCPU  24   // CPU time limit exceeded
#define SIGXFSZ  25   // File size limit exceeded
#define SIGVTALRM 26  // Virtual timer expired
#define SIGPROF  27   // Profiling timer expired
#define SIGWINCH 28   // Window size change
#define SIGIO    29   // I/O possible
#define SIGPWR   30   // Power failure
#define SIGSYS   31   // Bad system call

#define NSIG             32
#define SIGBIT(sig)      (1U << ((sig) - 1))

typedef uint sigset_t;

/* sig_atomic_t: type that can be read/written atomically on i386 */
typedef int sig_atomic_t;

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#define SIG_DFL ((void(*)(int))0)
#define SIG_IGN ((void(*)(int))1)
#define SIG_ERR ((void(*)(int))-1)

struct sigaction {
  void (*sa_handler)(int);
  sigset_t sa_mask;
  int sa_flags;
};

// Signal frame pushed onto user stack during signal delivery.
// The trampoline code at the top returns to sigreturn.
struct sigframe {
  uint sf_sigreturn;         // Return address: points to trampoline below
  int sf_signo;              // Signal number (arg to handler)
  uint sf_oldmask;           // Signal mask to restore
  // Saved trap frame registers (for sigreturn to restore)
  uint sf_edi;
  uint sf_esi;
  uint sf_ebp;
  uint sf_ebx;
  uint sf_edx;
  uint sf_ecx;
  uint sf_eax;
  uint sf_eip;               // Original return address
  uint sf_eflags;            // Original flags
  uint sf_esp;               // Original stack pointer
  // Trampoline code: mov $SYS_sigreturn, %eax; int $0x40; (5 bytes + padding)
  uchar sf_trampoline[8];
};

/* POSIX sigset manipulation — inline, locale-safe, no header dependency */
static inline void sigemptyset(sigset_t *s)              { *s = 0; }
static inline void sigfillset(sigset_t *s)               { *s = ~0U; }
static inline void sigaddset(sigset_t *s, int sig)       { *s |= SIGBIT(sig); }
static inline void sigdelset(sigset_t *s, int sig)       { *s &= ~SIGBIT(sig); }
static inline int  sigismember(const sigset_t *s, int sig) { return !!(*s & SIGBIT(sig)); }

/* sigprocmask is declared in unistd.h / user.h; prototype here for kernel use */
#ifndef KERNEL
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
int sigsuspend(const sigset_t *mask);
void (*signal(int signum, void (*handler)(int)))(int);
int raise(int sig);
#endif

#endif