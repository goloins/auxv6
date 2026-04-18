#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "wait.h"
#include "termios.h"
#include "stat.h"
#include "fs.h"
#include "file.h"
#include "traps.h"
#include "signal.h"
#include "syscall.h"

#ifndef WCONTINUED
#define WCONTINUED 0x0004
#endif

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

typedef char proc_table_size_guard[
  (sizeof(ptable.proc) <= PROC_TABLE_BYTES_MAX) ? 1 : -1
];

// Provided by mp.c: reverse APIC-ID -> cpus[] index, built at mpinit().
extern uchar apic_cpu_map[256];

// Count of processes that have active alarms (alarm_ticks != 0).
// Incremented when alarm_ticks is set, decremented when it fires or is
// cleared.  Checked without the ptable.lock in the timer ISR hotpath so
// proc_check_alarms() can skip the full table scan when no alarms exist.
static volatile int active_alarm_count;
volatile uint wakeup_calls;
volatile uint wakeup_scans;
volatile uint wakeup_hits;
volatile uint waitpid_loops;
volatile uint waitpid_scans;
volatile uint wakeup_ticks_calls;
volatile uint wakeup_proc_calls;
volatile uint wakeup_other_calls;
volatile int active_tick_sleepers;
static struct proc *tick_sleepq_head;

#define CHAN_WAIT_HASH_SIZE 256
static uint chan_wait_hash[CHAN_WAIT_HASH_SIZE];

static inline uint
chan_wait_hash_idx(void *chan)
{
  return (((uint)chan) >> 2) & (CHAN_WAIT_HASH_SIZE - 1);
}

extern struct proc *initproc;

// ticks is declared in trap.c; needed to record block timestamps.
extern uint ticks;

int valid_signo(int signo);
int signal_pick_stop(uint pending);
int signal_pick_fatal(uint pending);
void proc_note_signal_locked(struct proc *p, int signo);

// ptable.lock must be held.
static void
tick_sleepq_add_locked(struct proc *p)
{
  if(p == 0 || p->on_tickq)
    return;
  p->tick_next = tick_sleepq_head;
  tick_sleepq_head = p;
  p->on_tickq = 1;
  active_tick_sleepers++;
}

// ptable.lock must be held.
static void
tick_sleepq_remove_locked(struct proc *p)
{
  struct proc **pp;

  if(p == 0 || !p->on_tickq)
    return;

  for(pp = &tick_sleepq_head; *pp; pp = &(*pp)->tick_next){
    if(*pp != p)
      continue;
    *pp = p->tick_next;
    p->tick_next = 0;
    p->on_tickq = 0;
    if(active_tick_sleepers > 0)
      active_tick_sleepers--;
    return;
  }

  // Queue membership bit was set but node was not linked.
  p->tick_next = 0;
  p->on_tickq = 0;
  if(active_tick_sleepers > 0)
    active_tick_sleepers--;
}
static void wakeup1(void *chan);

#define WAIT_EVENT_NONE      0
#define WAIT_EVENT_STOPPED   1
#define WAIT_EVENT_CONTINUED 2

#define WSTATUS_EXIT(code)   (((code) & 0xff) << 8)
#define WSTATUS_SIG(sig)     ((sig) & 0x7f)
#define WSTATUS_STOP(sig)    ((((sig) & 0xff) << 8) | 0x7f)
#define WSTATUS_CONT         0xffff

// TODO(signal): add user-space signal frame/trampoline delivery.
// TODO(jobctl): add full shell-oriented job tables and fg/bg builtins.

// Set or cancel the SIGALRM deadline for p.
// deadline_ticks==0 cancels the timer and clears any reload interval.
void
proc_set_alarm_state(struct proc *p, uint deadline_ticks, uint interval_ticks)
{
  uint old = p->alarm_ticks;

  p->alarm_ticks = deadline_ticks;
  p->alarm_interval_ticks = (deadline_ticks != 0) ? interval_ticks : 0;
  if(old == 0 && deadline_ticks != 0)
    __sync_fetch_and_add(&active_alarm_count, 1);
  else if(old != 0 && deadline_ticks == 0)
    __sync_fetch_and_sub(&active_alarm_count, 1);
}

void
proc_set_alarm(struct proc *p, uint deadline_ticks)
{
  proc_set_alarm_state(p, deadline_ticks, 0);
}

// Check all processes for expired alarms and post SIGALRM.
// Called from timer interrupt on CPU 0.
void
proc_check_alarms(uint current_ticks)
{
  struct proc *p;

  // Fast path: no alarms set at all.  Avoids acquiring ptable.lock
  // (and doing an O(NPROC) scan) on every timer tick when idle.
  if(active_alarm_count == 0)
    return;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    uint interval_ticks;

    if(p->state == UNUSED)
      continue;
    if(p->alarm_ticks == 0)
      continue;
    if(current_ticks >= p->alarm_ticks) {
      interval_ticks = p->alarm_interval_ticks;
      if(interval_ticks != 0)
        p->alarm_ticks = current_ticks + interval_ticks;
      else
        proc_set_alarm_state(p, 0, 0);
      p->sig_pending |= SIGBIT(SIGALRM);
      // Wake process if sleeping so it can receive the signal
      if(p->state == SLEEPING) {
        p->state = RUNNABLE;
        schedq_enqueue_locked(p, SCHED_ENQ_ALARM);
      }
    }
  }
  release(&ptable.lock);
}

// Attempt to grow the stack of process p downward to cover fault_addr.
// Called from the page-fault handler before delivering SIGSEGV.
// Returns 1 if the fault was in the guard band and a new page was made
// accessible; returns 0 if the fault is not a stack-growth candidate or the
// stack has already reached USER_STACK_MAX_PAGES.
//
// Implementation notes:
//   - For pre-mapped reserve pages, growth flips !PTE_U -> PTE_U.
//   - For sparse mappings (e.g. fork child where reserve pages were skipped),
//     growth allocates/maps the next page on demand.
//   - After changing PTE flags, the TLB for this process is invalidated via
//     switchuvm() so the next user instruction sees the updated mapping.
void
proc_apply_pending_signals(struct proc *p)
{
  uint pending;
  uint fatal;
  uint stopset;
  uint cont;
  int stopsig;
  int termsig;
  int signo;
  uint bit;

  if(p == 0)
    return;

  // Signals with default action: terminate
  fatal = SIGBIT(SIGHUP) | SIGBIT(SIGINT) | SIGBIT(SIGQUIT) |
          SIGBIT(SIGILL) | SIGBIT(SIGTRAP) | SIGBIT(SIGABRT) |
          SIGBIT(SIGBUS) | SIGBIT(SIGFPE) | SIGBIT(SIGKILL) |
          SIGBIT(SIGUSR1) | SIGBIT(SIGSEGV) | SIGBIT(SIGUSR2) |
          SIGBIT(SIGPIPE) | SIGBIT(SIGALRM) | SIGBIT(SIGTERM) |
          SIGBIT(SIGXCPU) | SIGBIT(SIGXFSZ) | SIGBIT(SIGSYS);
  // Signals with default action: stop
  stopset = SIGBIT(SIGSTOP) | SIGBIT(SIGTSTP) | SIGBIT(SIGTTIN) | SIGBIT(SIGTTOU);
  // Signals with default action: continue
  cont = SIGBIT(SIGCONT);
  // Signals with default action: ignore (SIGCHLD, SIGWINCH, SIGURG)

  acquire(&ptable.lock);
  pending = p->sig_pending & ~p->sig_mask;
  pending &= ~p->sig_ignored;

  // Consume user-handled signals into sig_caught; actual user trampoline
  // delivery is deferred to a later signal-frame implementation.
  for(signo = 1; signo < NSIG; signo++) {
    bit = SIGBIT(signo);
    if((pending & bit) == 0)
      continue;
    if(signo == SIGKILL || signo == SIGSTOP)
      continue;
    if(p->sig_handler[signo] > 1) {
      p->sig_caught |= bit;
      p->sig_pending &= ~bit;
      pending &= ~bit;
    }
  }

  if(pending & cont) {
    p->sig_pending &= ~cont;
    if(p->state == STOPPED) {
      p->state = RUNNABLE;
      schedq_enqueue_locked(p, SCHED_ENQ_SIGNAL);
      p->wait_event = WAIT_EVENT_CONTINUED;
      p->wait_status = WSTATUS_CONT;
    }
    pending &= ~cont;
  }

  if(pending & stopset) {
    stopsig = signal_pick_stop(pending);
    p->sig_pending &= ~stopset;
    if(p->state == RUNNING || p->state == RUNNABLE || p->state == SLEEPING) {
      p->state = STOPPED;
      p->wait_event = WAIT_EVENT_STOPPED;
      p->wait_status = WSTATUS_STOP(stopsig);
    }
    pending &= ~stopset;
  }

  if(pending & fatal){
    termsig = signal_pick_fatal(pending);
    p->killed = 1;
    if(valid_signo(termsig))
      p->xstatus = WSTATUS_SIG(termsig);
    if(p->state == STOPPED) {
      p->state = RUNNABLE;
      schedq_enqueue_locked(p, SCHED_ENQ_SIGNAL);
    }
    p->sig_pending &= ~fatal;
  }
  release(&ptable.lock);
}

void
proc_maybe_stop_current(void)
{
  struct proc *p;

  p = myproc();
  if(p == 0)
    return;

  acquire(&ptable.lock);
  if(p->state == STOPPED)
    sched();
  release(&ptable.lock);
}

// Deliver one caught signal to userspace by setting up a signal frame
// on the user stack and redirecting execution to the signal handler.
// Returns 1 if a signal was delivered, 0 otherwise.
int
proc_deliver_signal(struct proc *p)
{
  int signo;
  uint handler;
  uint sp;
  struct sigframe sf;
  struct trapframe *tf;

  if(p == 0 || p->sig_caught == 0)
    return 0;

  tf = p->tf;
  if(tf == 0)
    return 0;

  // Find first caught signal
  acquire(&ptable.lock);
  for(signo = 1; signo < NSIG; signo++) {
    if(p->sig_caught & SIGBIT(signo))
      break;
  }
  if(signo >= NSIG) {
    release(&ptable.lock);
    return 0;
  }

  handler = p->sig_handler[signo];
  if(handler <= 1) {
    // SIG_DFL or SIG_IGN, shouldn't be in sig_caught
    p->sig_caught &= ~SIGBIT(signo);
    release(&ptable.lock);
    return 0;
  }

  // Clear this signal from caught set
  p->sig_caught &= ~SIGBIT(signo);

  // Save signal mask and apply handler's mask
  sf.sf_oldmask = p->sig_mask;
  p->sig_mask |= p->sig_actmask[signo];
  p->sig_mask |= SIGBIT(signo);  // Block delivered signal during handler
  p->sig_mask &= ~(SIGBIT(SIGKILL) | SIGBIT(SIGSTOP));  // Can't block these
  release(&ptable.lock);

  // Build signal frame with saved context
  sf.sf_signo = signo;
  sf.sf_edi = tf->edi;
  sf.sf_esi = tf->esi;
  sf.sf_ebp = tf->ebp;
  sf.sf_ebx = tf->ebx;
  sf.sf_edx = tf->edx;
  sf.sf_ecx = tf->ecx;
  sf.sf_eax = tf->eax;
  sf.sf_eip = tf->eip;
  sf.sf_eflags = tf->eflags;
  sf.sf_esp = tf->esp;

  // Build trampoline: mov $SYS_sigreturn, %eax; int $T_SYSCALL
  // b8 40 00 00 00       mov $64, %eax (SYS_sigreturn = 64)
  // cd 40                int $0x40 (T_SYSCALL)
  sf.sf_trampoline[0] = 0xb8;
  sf.sf_trampoline[1] = SYS_sigreturn;
  sf.sf_trampoline[2] = 0x00;
  sf.sf_trampoline[3] = 0x00;
  sf.sf_trampoline[4] = 0x00;
  sf.sf_trampoline[5] = 0xcd;
  sf.sf_trampoline[6] = T_SYSCALL;
  sf.sf_trampoline[7] = 0x00;  // padding

  // Push signal frame onto user stack
  sp = tf->esp;
  sp -= sizeof(sf);
  sp &= ~3;  // Align to 4 bytes

  // Verify user stack is accessible (simple bounds check)
  if(sp < PGSIZE || sp > p->sz) {
    STACKDBG("stack: pid %d signal delivery failed, bad stack 0x%x\n",
             p->pid, sp);
    // Match Unix behavior: if we cannot construct a handler frame
    // (e.g. exhausted stack and no alt stack), force default fatal action.
    if(valid_signo(signo))
      p->xstatus = WSTATUS_SIG(signo);
    p->killed = 1;
    return 0;
  }

  // Set return address to trampoline
  sf.sf_sigreturn = sp + ((uint)&((struct sigframe *)0)->sf_trampoline);

  // Copy signal frame to user stack
  if(copyout(proc_pgdir(p), sp, &sf, sizeof(sf)) < 0) {
    STACKDBG("stack: pid %d signal delivery copyout failed\n", p->pid);
    // Handler delivery could not be set up; terminate as signaled.
    if(valid_signo(signo))
      p->xstatus = WSTATUS_SIG(signo);
    p->killed = 1;
    return 0;
  }

  // Modify trap frame to "return" to signal handler
  tf->eip = handler;
  tf->esp = sp;

  return 1;
}

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  lockdep_set_rank(&ptable.lock, LOCK_RANK_PTABLE, "ptable");
  tick_sleepq_head = 0;
}

// Combined signal dispatch called at every trap/syscall return to userspace.
// A single lockless precheck avoids acquiring ptable.lock at all when there
// is nothing to do -- the common case for well-behaved processes.
void
proc_handle_signals_on_return(struct proc *p)
{
  if(p == 0)
    return;
  // Lockless read: stale zeros just mean we process on the next return.
  if(!(p->sig_pending || p->sig_caught || p->state == STOPPED))
    return;
  proc_apply_pending_signals(p);
  proc_deliver_signal(p);
  proc_maybe_stop_current();
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();
  pde_t *pgdir;

  sz = curproc->sz;
  pgdir = proc_pgdir(curproc);
  if(n > 0){
    if(curproc->addrsp){
      if((sz = allocuvm_as(curproc->addrsp, sz, sz + n)) == 0)
        return -1;
    } else {
      if(pgdir == 0 || (sz = allocuvm(pgdir, sz, sz + n)) == 0)
        return -1;
    }
  } else if(n < 0){
    if(curproc->addrsp){
      if((sz = deallocuvm_as(curproc->addrsp, sz, sz + n)) == 0)
        return -1;
    } else {
      if(pgdir == 0 || (sz = deallocuvm(pgdir, sz, sz + n)) == 0)
        return -1;
    }
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

int
proc_sigaction(int signo, uint act_addr, uint oldact_addr)
{
  struct proc *p;
  pde_t *pgdir;
  uint act_local[3];
  uint oldact_local[3];
  int have_act;
  int have_oldact;

  if(!valid_signo(signo))
    return -1;

  p = myproc();
  pgdir = proc_pgdir(p);
  if(p == 0 || pgdir == 0)
    return -1;

  have_act = (act_addr != 0);
  have_oldact = (oldact_addr != 0);

  if(have_act && copyin(pgdir, act_local, act_addr, sizeof(act_local)) < 0)
    return -1;

  acquire(&ptable.lock);

  if(have_oldact) {
    oldact_local[0] = p->sig_handler[signo];
    oldact_local[1] = p->sig_actmask[signo];
    oldact_local[2] = p->sig_actflags[signo];
  }

  // SIGKILL/SIGSTOP keep fixed default actions.
  if(have_act && (signo == SIGKILL || signo == SIGSTOP)) {
    release(&ptable.lock);
    return -1;
  }

  if(have_act) {
    p->sig_handler[signo] = act_local[0];
    p->sig_actmask[signo] = act_local[1];
    p->sig_actflags[signo] = act_local[2];

    if(act_local[0] == 1)
      p->sig_ignored |= SIGBIT(signo);
    else
      p->sig_ignored &= ~SIGBIT(signo);

  }

  release(&ptable.lock);

  if(have_oldact && copyout(pgdir, oldact_addr, oldact_local, sizeof(oldact_local)) < 0)
    return -1;

  return 0;
}

int
proc_sigprocmask(int how, uint set_addr, uint oldset_addr)
{
  struct proc *p;
  pde_t *pgdir;
  sigset_t set;
  sigset_t newmask;
  sigset_t oldmask;
  sigset_t unblockable;

  p = myproc();
  pgdir = proc_pgdir(p);
  if(p == 0 || pgdir == 0)
    return -1;

  unblockable = SIGBIT(SIGKILL) | SIGBIT(SIGSTOP);
  set = 0;
  if(set_addr != 0 && copyin(pgdir, &set, set_addr, sizeof(set)) < 0)
    return -1;

  acquire(&ptable.lock);
  oldmask = p->sig_mask;

  if(set_addr != 0) {
    if(how == SIG_BLOCK)
      newmask = oldmask | set;
    else if(how == SIG_UNBLOCK)
      newmask = oldmask & ~set;
    else if(how == SIG_SETMASK)
      newmask = set;
    else {
      release(&ptable.lock);
      return -1;
    }

    p->sig_mask = newmask & ~unblockable;
  }

  release(&ptable.lock);

  if(oldset_addr != 0 && copyout(pgdir, oldset_addr, &oldmask, sizeof(oldmask)) < 0)
    return -1;

  return 0;
}

int
proc_tcsetpgrp(int pgid)
{
  struct proc *curproc;
  struct proc *p;
  int found;

  curproc = myproc();
  if(curproc == 0 || pgid <= 0)
    return -1;

  acquire(&ptable.lock);
  if(curproc->tty < 0) {
    release(&ptable.lock);
    return -1;
  }

  found = 0;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state == UNUSED)
      continue;
    if(p->pgid != pgid)
      continue;
    if(p->sid != curproc->sid)
      continue;
    if(p->tty != curproc->tty)
      continue;
    found = 1;
    break;
  }
  release(&ptable.lock);

  if(!found)
    return -1;

  console_set_foreground_pgid(curproc->tty, pgid);
  return 0;
}

int
proc_tcgetpgrp(void)
{
  struct proc *curproc;

  curproc = myproc();
  if(curproc == 0)
    return -1;
  if(curproc->tty < 0)
    return -1;
  return console_get_foreground_pgid(curproc->tty);
}

int
proc_is_tty_fd(int fd)
{
  struct proc *curproc;
  struct file *f;

  curproc = myproc();
  if(curproc == 0)
    return 0;
  if(fd < 0 || curproc->fdtable == 0 || fd >= curproc->fdtable->nfds)
    return 0;

  f = curproc->fdtable->entries[fd];
  if(f == 0 || f->type != FD_INODE || f->ip == 0)
    return 0;
  if(f->ip->type != T_DEV)
    return 0;
  if(f->ip->major != CONSOLE && f->ip->major != PTYDEV && f->ip->major != SERIALDEV)
    return 0;

  return 1;
}

int
proc_tty_major(int fd)
{
  struct proc *curproc;
  struct file *f;

  curproc = myproc();
  if(curproc == 0)
    return -1;
  if(fd < 0 || curproc->fdtable == 0 || fd >= curproc->fdtable->nfds)
    return -1;

  f = curproc->fdtable->entries[fd];
  if(f == 0 || f->type != FD_INODE || f->ip == 0)
    return -1;
  if(f->ip->type != T_DEV)
    return -1;

  return f->ip->major;
}

int
proc_tcgetattr(int fd, uint termios_addr)
{
  struct proc *curproc;
  struct file *f;
  struct termios kt;
  pde_t *pgdir;
  int rc;

  curproc = myproc();
  pgdir = proc_pgdir(curproc);
  if(curproc == 0 || pgdir == 0 || termios_addr == 0)
    return -1;
  if(!proc_is_tty_fd(fd)) {
    return -1;
  }

  f = curproc->fdtable->entries[fd];
  if(f == 0 || f->ip == 0)
    return -1;

  if(f->ip->major == CONSOLE) {
    if(curproc->tty < 0)
      return -1;
    rc = console_get_termios(curproc->tty, &kt);
  } else if(f->ip->major == PTYDEV) {
    rc = pty_get_termios_file(f, &kt);
  } else if(f->ip->major == SERIALDEV) {
    rc = serial_get_termios_file(f, &kt);
  } else {
    return -1;
  }

  if(rc < 0)
    return rc;
  if(copyout(pgdir, termios_addr, &kt, sizeof(kt)) < 0)
    return -1;
  return 0;
}

int
proc_tcsetattr(int fd, int optional_actions, uint termios_addr)
{
  struct proc *curproc;
  struct file *f;
  struct termios kt;
  pde_t *pgdir;

  curproc = myproc();
  pgdir = proc_pgdir(curproc);
  if(curproc == 0 || pgdir == 0 || termios_addr == 0)
    return -1;
  if(!proc_is_tty_fd(fd)) {
    return -1;
  }

  if(copyin(pgdir, &kt, termios_addr, sizeof(kt)) < 0)
    return -1;

  f = curproc->fdtable->entries[fd];
  if(f == 0 || f->ip == 0)
    return -1;

  if(f->ip->major == CONSOLE) {
    if(curproc->tty < 0)
      return -1;
    return console_set_termios(curproc->tty, &kt, optional_actions);
  }

  if(f->ip->major == PTYDEV)
    return pty_set_termios_file(f, &kt, optional_actions);

  if(f->ip->major == SERIALDEV)
    return serial_set_termios_file(f, &kt, optional_actions);

  return -1;
}

int
proc_signal_pgid(int pgid, int signo)
{
  struct proc *p;
  int delivered;

  if(pgid <= 0 || !valid_signo(signo))
    return -1;

  delivered = 0;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state == UNUSED || p == initproc)
      continue;
    if(p->pgid != pgid)
      continue;
    proc_note_signal_locked(p, signo);
    delivered++;
  }
  release(&ptable.lock);

  return delivered > 0 ? 0 : -1;
}

// Try to exec init from standard paths: /sbin/init, /bin/init, /init
// Called from forkret() after vfs_init() is complete.
static void
kinit_exec(void)
{
  char *init_paths[] = {"/sbin/init", "/bin/init", "/init", 0};
  char *argv[] = {0, 0};  // argv[0] will be set to the path
  int i;

  // Try each standard init path
  for(i = 0; init_paths[i] != 0; i++){
    argv[0] = init_paths[i];
    if(exec(init_paths[i], argv) >= 0){
      // In kernel context, exec() returns 0 on success and the caller
      // must return to trapret to enter userspace at the new image.
      return;
    }
  }

  // If we get here, no init executable was found
  panic("kinit_exec: cannot find init (/sbin/init, /bin/init, or /init)");
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    // icache lock/sleeplocks are used by generic inode ref paths even when
    // xv6fs is not the selected root filesystem.
    icache_init();
    // iinit/initlog are xv6fs-specific; only call for xv6-root
    if(ROOTFS_TYPE == ROOTFS_TYPE_XV6FS){
      iinit(ROOTDEV);
      initlog(ROOTDEV);
    }
    vfs_init();
    
    // If this is the init process, exec the init program
    if(myproc() == initproc){
      kinit_exec();
      // kinit_exec() panics on failure, so we never get here
    }
  }

  if(myproc()->cwd == 0){
    myproc()->cwd = vfs_namei("/");
    if(myproc()->cwd == 0)
      panic("forkret: cwd");
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  int slept_on_ticks;
  uint hidx;
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  slept_on_ticks = (chan == &ticks);
  hidx = chan_wait_hash_idx(chan);
  chan_wait_hash[hidx]++;
  if(slept_on_ticks)
    tick_sleepq_add_locked(p);
  p->chan = chan;
  p->state = SLEEPING;
  p->sched_last_block_tick = ticks;  // MLFQ: record when we block for promotion heuristic

  sched();

  if(slept_on_ticks)
    tick_sleepq_remove_locked(p);
  if(chan_wait_hash[hidx] > 0)
    chan_wait_hash[hidx]--;

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;
  char *base;
  char *end;
  char *c;
  struct proc *next;
  uint scanned;

  wakeup_calls++;
  if(chan == &ticks)
    wakeup_ticks_calls++;

  if(chan == &ticks){
    scanned = 0;
    p = tick_sleepq_head;
    while(p){
      next = p->tick_next;
      scanned++;
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
        schedq_enqueue_locked(p, SCHED_ENQ_WAKE);
        wakeup_hits++;
      }
      // Whether matched or stale, remove from tick queue.
      tick_sleepq_remove_locked(p);
      p = next;
    }
    wakeup_scans += scanned;
    return;
  }

  base = (char*)ptable.proc;
  end = (char*)&ptable.proc[NPROC];
  c = (char*)chan;

  // Fast reject: when no sleepers hash to this channel bucket, a full
  // table scan cannot find a match.
  if(chan_wait_hash[chan_wait_hash_idx(chan)] == 0)
    return;

  // wait()/waitpid() sleeps on the current proc pointer as channel.
  // If chan is exactly a proc-table slot address, wake that slot directly
  // and avoid an O(NPROC) full-table walk.
  if(c >= base && c < end && ((uint)(c - base) % sizeof(struct proc)) == 0){
    wakeup_proc_calls++;
    p = (struct proc*)chan;
    wakeup_scans++;
    if(p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
      schedq_enqueue_locked(p, SCHED_ENQ_WAKE);
      wakeup_hits++;
    }
    return;
  }

  if(chan != &ticks)
    wakeup_other_calls++;
  wakeup_scans += NPROC;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
      schedq_enqueue_locked(p, SCHED_ENQ_WAKE);
      wakeup_hits++;
    }
}

// Internal helper for callers that already hold ptable.lock.
void
proc_wakeup1_locked(void *chan)
{
  wakeup1(chan);
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid, int sig)
{
  return proc_kill_with_signal(pid, sig);
}

int
proc_getppid(void)
{
  struct proc *p;

  p = myproc();
  if(p == 0)
    return 0;
  return p->ppid;
}

int
proc_getpgrp(void)
{
  struct proc *p;

  p = myproc();
  if(p == 0)
    return 0;
  return p->pgid;
}

int
proc_getsid(int pid)
{
  struct proc *curproc;
  struct proc *p;

  curproc = myproc();
  if(curproc == 0)
    return -1;

  if(pid == 0)
    return curproc->sid;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state != UNUSED && p->pid == pid) {
      int sid = p->sid;
      release(&ptable.lock);
      return sid;
    }
  }
  release(&ptable.lock);
  return -1;
}

int
proc_getuid(void)
{
  struct proc *p;

  p = myproc();
  if(p == 0)
    return 0;
  return p->ruid;
}

int
proc_getgid(void)
{
  struct proc *p;

  p = myproc();
  if(p == 0)
    return 0;
  return p->rgid;
}

int
proc_geteuid(void)
{
  struct proc *p;

  p = myproc();
  if(p == 0)
    return 0;
  return p->uid;
}

int
proc_getegid(void)
{
  struct proc *p;

  p = myproc();
  if(p == 0)
    return 0;
  return p->gid;
}

int
proc_setpgid(int pid, int pgid)
{
  struct proc *curproc;
  struct proc *p;
  struct proc *target;

  curproc = myproc();
  if(curproc == 0)
    return -1;

  if(pid == 0)
    pid = curproc->pid;
  if(pgid == 0)
    pgid = pid;
  if(pid < 0 || pgid < 0)
    return -1;

  acquire(&ptable.lock);
  target = 0;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->pid == pid && p->state != UNUSED) {
      target = p;
      break;
    }
  }
  if(target == 0) {
    release(&ptable.lock);
    return -1;
  }

  // Session leaders must not change process group.
  if(target->pid == target->sid) {
    release(&ptable.lock);
    return -1;
  }

  // Keep process-group moves within current session.
  if(target->sid != curproc->sid) {
    release(&ptable.lock);
    return -1;
  }

  // Keep initial policy narrow and safe: allow self or direct child only.
  if(target != curproc && target->parent != curproc) {
    release(&ptable.lock);
    return -1;
  }

  target->pgid = pgid;
  release(&ptable.lock);
  return 0;
}

int
proc_setsid(void)
{
  struct proc *p;
  struct proc *q;

  p = myproc();
  if(p == 0)
    return -1;

  acquire(&ptable.lock);
  if(p->pid == p->pgid){
    release(&ptable.lock);
    return -1;
  }
  for(q = ptable.proc; q < &ptable.proc[NPROC]; q++) {
    if(q->state != UNUSED && q != p && q->pgid == p->pid) {
      release(&ptable.lock);
      return -1;
    }
  }
  p->sid = p->pid;
  p->pgid = p->pid;
  p->tty = -1;
  release(&ptable.lock);
  return p->sid;
}

int
proc_setuid(int uid)
{
  int euid;

  if(uid < 0)
    return -1;
  euid = proc_geteuid();
  if(euid == 0)
    return proc_setresuid(uid, uid, uid);
  return proc_setresuid(-1, uid, -1);
}

int
proc_setgid(int gid)
{
  int euid;

  if(gid < 0)
    return -1;
  euid = proc_geteuid();
  if(euid == 0)
    return proc_setresgid(gid, gid, gid);
  return proc_setresgid(-1, gid, -1);
}

static int
cred_matches_current_uid(struct proc *p, int id)
{
  return (id == p->ruid || id == p->uid || id == p->suid);
}

static int
cred_matches_current_gid(struct proc *p, int id)
{
  return (id == p->rgid || id == p->gid || id == p->sgid);
}

int
proc_setresuid(int ruid, int euid, int suid)
{
  struct proc *p;
  int new_ruid;
  int new_euid;
  int new_suid;

  p = myproc();
  if(p == 0)
    return -1;

  if(ruid < -1 || euid < -1 || suid < -1)
    return -1;

  acquire(&ptable.lock);
  new_ruid = (ruid == -1) ? p->ruid : ruid;
  new_euid = (euid == -1) ? p->uid : euid;
  new_suid = (suid == -1) ? p->suid : suid;

  if(p->uid != 0) {
    if(!cred_matches_current_uid(p, new_ruid) ||
       !cred_matches_current_uid(p, new_euid) ||
       !cred_matches_current_uid(p, new_suid)) {
      release(&ptable.lock);
      return -1;
    }
  }

  p->ruid = new_ruid;
  p->uid = new_euid;
  p->suid = new_suid;
  release(&ptable.lock);
  return 0;
}

int
proc_setresgid(int rgid, int egid, int sgid)
{
  struct proc *p;
  int new_rgid;
  int new_egid;
  int new_sgid;

  p = myproc();
  if(p == 0)
    return -1;

  if(rgid < -1 || egid < -1 || sgid < -1)
    return -1;

  acquire(&ptable.lock);
  new_rgid = (rgid == -1) ? p->rgid : rgid;
  new_egid = (egid == -1) ? p->gid : egid;
  new_sgid = (sgid == -1) ? p->sgid : sgid;

  if(p->uid != 0) {
    if(!cred_matches_current_gid(p, new_rgid) ||
       !cred_matches_current_gid(p, new_egid) ||
       !cred_matches_current_gid(p, new_sgid)) {
      release(&ptable.lock);
      return -1;
    }
  }

  p->rgid = new_rgid;
  p->gid = new_egid;
  p->sgid = new_sgid;
  if(p->ngroups > 0)
    p->groups[0] = (gid_t)p->gid;
  release(&ptable.lock);
  return 0;
}

int
proc_getgroups(gid_t *groups, int max)
{
  struct proc *p;
  int i;
  int n;

  p = myproc();
  if(p == 0)
    return -1;

  acquire(&ptable.lock);
  n = p->ngroups;
  if(groups != 0) {
    if(max < n) {
      release(&ptable.lock);
      return -1;
    }
    for(i = 0; i < n; i++)
      groups[i] = p->groups[i];
  }
  release(&ptable.lock);

  return n;
}

int
proc_setgroups(const gid_t *groups, int n)
{
  struct proc *p;
  int i;

  if(n < 0 || n > PROC_NGROUPS_MAX)
    return -1;

  p = myproc();
  if(p == 0)
    return -1;

  acquire(&ptable.lock);
  if(p->uid != 0) {
    release(&ptable.lock);
    return -1;
  }

  if(n == 0) {
    p->ngroups = 1;
    p->groups[0] = (gid_t)p->gid;
    for(i = 1; i < PROC_NGROUPS_MAX; i++)
      p->groups[i] = 0;
    release(&ptable.lock);
    return 0;
  }

  p->ngroups = n;
  for(i = 0; i < n; i++)
    p->groups[i] = groups[i];
  for(i = n; i < PROC_NGROUPS_MAX; i++)
    p->groups[i] = 0;
  release(&ptable.lock);
  return 0;
}

int
proc_in_group(struct proc *p, gid_t gid)
{
  int i;

  if(p == 0)
    return 0;
  if(p->gid == gid)
    return 1;
  for(i = 0; i < p->ngroups && i < PROC_NGROUPS_MAX; i++) {
    if(p->groups[i] == gid)
      return 1;
  }
  return 0;
}

int
proc_kill_with_signal(int pid, int signo)
{
  struct proc *curproc;
  struct proc *p;
  int delivered;
  int target_pgid;
  int match;

  if(!valid_signo(signo))
    return -1;

  curproc = myproc();
  delivered = 0;
  target_pgid = -pid;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED || p->pid == 0)
      continue;
    if(p == initproc){
      int allow_init_hup;

      // Keep init protected from arbitrary signals, but allow root to
      // request a runlevel transition via SIGHUP to PID 1.
      allow_init_hup = (pid > 0 && p->pid == pid && signo == SIGHUP &&
                        curproc != 0 && (curproc == initproc || curproc->uid == 0));
      if(!allow_init_hup)
        continue;
    }

    match = 0;
    if(pid > 0)
      match = (p->pid == pid);
    else if(pid == 0)
      match = (curproc != 0 && p->pgid == curproc->pgid);
    else if(pid == -1)
      match = 1;
    else
      match = (p->pgid == target_pgid);

    if(match) {
      proc_note_signal_locked(p, signo);
      delivered++;
      if(pid > 0)
        break;
    }
  }
  release(&ptable.lock);
  return delivered > 0 ? 0 : -1;
}

int
proc_snapshot(struct procinfo_k *out, int max)
{
  struct proc *p;
  int n;

  if(out == 0 || max <= 0)
    return -1;

  n = 0;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC] && n < max; p++){
    if(p->state == UNUSED || p->pid <= 0)
      continue;
    out[n].pid = p->pid;
    out[n].ppid = p->ppid;
    out[n].pgid = p->pgid;
    out[n].sid = p->sid;
    out[n].tty = p->tty;
    out[n].uid = p->uid;
    out[n].gid = p->gid;
    out[n].state = p->state;
    out[n].sz = p->sz;
    out[n].cticks = p->cticks;
    safestrcpy(out[n].name, p->name, sizeof(out[n].name));
    n++;
  }
  release(&ptable.lock);

  return n;
}

int
proc_fd_snapshot(struct procfdinfo_k *out, int max, int skip)
{
  struct proc *p;
  int n;
  int fd;

  if(out == 0 || max <= 0 || skip < 0)
    return -1;

  n = 0;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC] && n < max; p++){
    if(p->state == UNUSED || p->pid <= 0)
      continue;

    for(fd = 0; p->fdtable && fd < p->fdtable->nfds && n < max; fd++){
      struct file *f;

      f = p->fdtable->entries[fd];
      if(f == 0)
        continue;

      if(skip > 0){
        skip--;
        continue;
      }

      out[n].pid = p->pid;
      out[n].fd = fd;
      out[n].type = f->type;
      out[n].readable = f->readable;
      out[n].writable = f->writable;
      out[n].off = f->off;
      out[n].dev = 0;
      out[n].inum = 0;
      if(f->type == FD_INODE && f->ip){
        out[n].dev = f->ip->dev;
        out[n].inum = f->ip->inum;
      }
      safestrcpy(out[n].name, p->name, sizeof(out[n].name));
      n++;
    }
  }
  release(&ptable.lock);

  return n;
}

int
proc_fd_limits_snapshot(struct procfdlimitinfo_k *out, int max, int skip)
{
  struct proc *p;
  int n;

  if(out == 0 || max <= 0 || skip < 0)
    return -1;

  n = 0;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC] && n < max; p++){
    int fd;
    uint used;
    uint highwater;

    if(p->state == UNUSED || p->pid <= 0)
      continue;

    if(skip > 0){
      skip--;
      continue;
    }

    used = 0;
    highwater = 0;
    if(p->fdtable){
      highwater = (uint)p->fdtable->nfds;
      for(fd = 0; fd < p->fdtable->nfds; fd++){
        if(p->fdtable->entries[fd])
          used++;
      }
    }

    out[n].pid = p->pid;
    out[n].soft = p->rlimit_nofile_cur;
    out[n].hard = p->rlimit_nofile_max;
    out[n].used = used;
    out[n].highwater = highwater;
    safestrcpy(out[n].name, p->name, sizeof(out[n].name));
    n++;
  }
  release(&ptable.lock);

  return n;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [STOPPED]   "stop  ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
        cprintf("%d %s %s pgid=%d sid=%d sigpend=%x",
          p->pid, state, p->name, p->pgid, p->sid, p->sig_pending);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
