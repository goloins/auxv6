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
#ifndef P_PID
#define P_PID 1
#define P_PGID 2
#define P_ALL 3
#endif
#ifndef CLD_EXITED
#define CLD_EXITED 1
#define CLD_KILLED 2
#define CLD_STOPPED 3
#define CLD_CONTINUED 4
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

static struct proc *initproc;

int nextpid = 1;
// Phase 1A: Check if any process has an open file on the given device.
// Called by fs.c when unmounting or checking device in-use status.
int
file_has_refs_on_dev(uint dev)
{
  int i;
  struct proc *p;
  struct file *f;

  acquire(&ptable.lock);
  
  // Walk all processes' fdtables to find open files on device
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED || p->fdtable == 0)
      continue;
    
    for(i = 0; i < p->fdtable->nfds; i++){
      f = p->fdtable->entries[i];
      if(f == 0)
        continue;
      if(f->ref < 1)
        continue;
      if(f->type != FD_INODE || f->ip == 0)
        continue;
      if(f->ip->dev == dev){
        release(&ptable.lock);
        return 1;
      }
    }
  }
  
  release(&ptable.lock);
  return 0;
}

extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

/*
 * Load average tracking — Linux-compatible fixed-point EMA.
 *
 * The load average is the number of RUNNABLE+RUNNING processes smoothed with
 * an exponential moving average sampled every 500 ticks (5 seconds at 100Hz).
 *
 * Coefficients (FSHIFT = 11, factor = 2048):
 *   1-min  exp(-5/60)   ≈ 1884/2048
 *   5-min  exp(-5/300)  ≈ 2014/2048
 *  15-min  exp(-5/900)  ≈ 2037/2048
 */
#define LAVG_FSHIFT  11
#define LAVG_FIXED1  (1 << LAVG_FSHIFT)   /* 2048 */
#define LAVG_EXP_1   1884
#define LAVG_EXP_5   2014
#define LAVG_EXP_15  2037

static uint lavg[3];   /* fixed-point 1-, 5-, 15-minute load averages */

/* Called from the timer ISR on CPU 0 every 500 ticks.  Scans ptable under
 * the ptable.lock to count active processes, then updates all three EMAs.
 * No dynamic allocation; safe to call from interrupt context. */
void
proc_tick_loadavg(void)
{
  struct proc *p;
  uint n;

  n = 0;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == RUNNABLE || p->state == RUNNING)
      n++;
  }
  release(&ptable.lock);

  /* EMA: new = old * coeff/2048 + n * (2048 - coeff)/2048
   * Written as: new = (old * coeff + n * (FIXED1-coeff)) >> FSHIFT  */
  lavg[0] = (lavg[0] * LAVG_EXP_1  + (n << LAVG_FSHIFT) * (LAVG_FIXED1 - LAVG_EXP_1)  / LAVG_FIXED1) >> LAVG_FSHIFT;
  lavg[1] = (lavg[1] * LAVG_EXP_5  + (n << LAVG_FSHIFT) * (LAVG_FIXED1 - LAVG_EXP_5)  / LAVG_FIXED1) >> LAVG_FSHIFT;
  lavg[2] = (lavg[2] * LAVG_EXP_15 + (n << LAVG_FSHIFT) * (LAVG_FIXED1 - LAVG_EXP_15) / LAVG_FIXED1) >> LAVG_FSHIFT;
}

/* Return fixed-point load averages (divisor = 2048). */
void
proc_get_loadavg(uint *la1, uint *la5, uint *la15)
{
  *la1  = lavg[0];
  *la5  = lavg[1];
  *la15 = lavg[2];
}

/* Count active processes.  nrunning receives RUNNABLE+RUNNING count;
 * ntotal receives all non-UNUSED+non-zombie processes. */
void
proc_count_active(int *nrunning, int *ntotal)
{
  struct proc *p;
  int r, t;

  r = 0; t = 0;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED || p->pid <= 0)
      continue;
    t++;
    if(p->state == RUNNABLE || p->state == RUNNING)
      r++;
  }
  release(&ptable.lock);
  *nrunning = r;
  *ntotal   = t;
}

/* Return best-effort scheduler activity totals across all CPUs.
 * Values are monotonic and sampled locklessly for low overhead. */
void
proc_get_sched_stats(uint *passes, uint *idle_halts, uint *picks)
{
  int i;
  uint p;
  uint h;
  uint k;

  p = 0;
  h = 0;
  k = 0;
  for(i = 0; i < ncpu; i++){
    p += cpus[i].sched_passes;
    h += cpus[i].sched_idle_halts;
    k += cpus[i].sched_picks;
  }

  *passes = p;
  *idle_halts = h;
  *picks = k;
}

static int
valid_signo(int signo)
{
  return signo > 0 && signo < NSIG;
}

#define WAIT_EVENT_NONE      0
#define WAIT_EVENT_STOPPED   1
#define WAIT_EVENT_CONTINUED 2

#define WSTATUS_EXIT(code)   (((code) & 0xff) << 8)
#define WSTATUS_SIG(sig)     ((sig) & 0x7f)
#define WSTATUS_STOP(sig)    ((((sig) & 0xff) << 8) | 0x7f)
#define WSTATUS_CONT         0xffff

#define WIFEXITED_INT(s)    (((s) & 0xff) == 0)
#define WEXITSTATUS_INT(s)  (((s) >> 8) & 0xff)
#define WIFSIGNALED_INT(s)  (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#define WTERMSIG_INT(s)     ((s) & 0x7f)
#define WIFSTOPPED_INT(s)   (((s) & 0xff) == 0x7f)
#define WSTOPSIG_INT(s)     (((s) >> 8) & 0xff)
#define WIFCONTINUED_INT(s) ((s) == WSTATUS_CONT)

// TODO(signal): add user-space signal frame/trampoline delivery.
// TODO(jobctl): add full shell-oriented job tables and fg/bg builtins.

static int
signal_pick_stop(uint pending)
{
  if(pending & SIGBIT(SIGSTOP))
    return SIGSTOP;
  if(pending & SIGBIT(SIGTSTP))
    return SIGTSTP;
  if(pending & SIGBIT(SIGTTIN))
    return SIGTTIN;
  if(pending & SIGBIT(SIGTTOU))
    return SIGTTOU;
  return 0;
}

static int
signal_pick_fatal(uint pending)
{
  // Check in priority order
  if(pending & SIGBIT(SIGKILL))
    return SIGKILL;
  if(pending & SIGBIT(SIGSEGV))
    return SIGSEGV;
  if(pending & SIGBIT(SIGBUS))
    return SIGBUS;
  if(pending & SIGBIT(SIGILL))
    return SIGILL;
  if(pending & SIGBIT(SIGFPE))
    return SIGFPE;
  if(pending & SIGBIT(SIGABRT))
    return SIGABRT;
  if(pending & SIGBIT(SIGTERM))
    return SIGTERM;
  if(pending & SIGBIT(SIGINT))
    return SIGINT;
  if(pending & SIGBIT(SIGQUIT))
    return SIGQUIT;
  if(pending & SIGBIT(SIGHUP))
    return SIGHUP;
  if(pending & SIGBIT(SIGTRAP))
    return SIGTRAP;
  if(pending & SIGBIT(SIGPIPE))
    return SIGPIPE;
  if(pending & SIGBIT(SIGALRM))
    return SIGALRM;
  if(pending & SIGBIT(SIGUSR1))
    return SIGUSR1;
  if(pending & SIGBIT(SIGUSR2))
    return SIGUSR2;
  if(pending & SIGBIT(SIGXCPU))
    return SIGXCPU;
  if(pending & SIGBIT(SIGXFSZ))
    return SIGXFSZ;
  if(pending & SIGBIT(SIGSYS))
    return SIGSYS;
  return 0;
}

static int
proc_wait_target_match(struct proc *curproc, struct proc *child, int pid)
{
  if(pid > 0)
    return child->pid == pid;
  if(pid == 0)
    return child->pgid == curproc->pgid;
  if(pid == -1)
    return 1;
  return child->pgid == -pid;
}

static void
proc_note_signal_locked(struct proc *p, int signo)
{
  if(p == 0 || !valid_signo(signo))
    return;

  p->sig_pending |= SIGBIT(signo);

  if(signo == SIGCONT) {
    // Continue clears pending stop intents and resumes stopped tasks.
    p->sig_pending &= ~(SIGBIT(SIGSTOP) | SIGBIT(SIGTSTP));
    if(p->state == STOPPED)
      p->state = RUNNABLE;
  }

  // Preserve existing semantics: SIGKILL marks process as killed.
  if(signo == SIGKILL)
    p->killed = 1;

  // Signals should wake a sleeping process so delivery can progress.
  if(p->state == SLEEPING)
    p->state = RUNNABLE;
}

// Set (or cancel) the alarm for p.  deadline_ticks==0 cancels.
// Keeps active_alarm_count in sync so the timer ISR hotpath can skip
// the ptable scan when no alarms are pending.
void
proc_set_alarm(struct proc *p, uint deadline_ticks)
{
  uint old = p->alarm_ticks;
  p->alarm_ticks = deadline_ticks;
  if(old == 0 && deadline_ticks != 0)
    __sync_fetch_and_add(&active_alarm_count, 1);
  else if(old != 0 && deadline_ticks == 0)
    __sync_fetch_and_sub(&active_alarm_count, 1);
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
    if(p->state == UNUSED)
      continue;
    if(p->alarm_ticks == 0)
      continue;
    if(current_ticks >= p->alarm_ticks) {
      p->alarm_ticks = 0;  // One-shot, clear the alarm
      __sync_fetch_and_sub(&active_alarm_count, 1);
      p->sig_pending |= SIGBIT(SIGALRM);
      // Wake process if sleeping so it can receive the signal
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
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
int
proc_try_grow_stack(struct proc *p, uint fault_addr)
{
  uint stack_guard;
  uint pages_used;
  int pst;

  if(p->stack_top == 0 || p->stack_bot == 0)
    return 0;
  if(p->stack_bot >= p->stack_top)
    return 0;

  stack_guard = p->stack_bot - PGSIZE;

  // The fault must land in the current single-page guard zone.
  if(fault_addr < stack_guard || fault_addr >= p->stack_bot)
    return 0;

  pages_used = (p->stack_top - p->stack_bot) / PGSIZE;
  if(pages_used >= USER_STACK_MAX_PAGES) {
    STACKDBG("stack: pid %d tried to grow beyond max (%d pages)\n",
             p->pid, USER_STACK_MAX_PAGES);
    return 0;  // Hard ceiling: deliver SIGSEGV
  }

  pst = user_page_state(p->pgdir, (char*)stack_guard);
  if(pst == 1){
    // Pre-mapped but inaccessible reserve page.
    setpteu(p->pgdir, (char*)stack_guard);
  } else if(pst == 0){
    // Sparse child mapping: allocate one stack page on demand.
    if(allocuvm(p->pgdir, stack_guard, stack_guard + PGSIZE) == 0)
      return 0;
  } else {
    // Already user-accessible: not a growth fault candidate.
    return 0;
  }

  p->stack_bot = stack_guard;

  STACKDBG("stack: pid %d grew stack to 0x%x (%d/%d pages used)\n",
           p->pid, p->stack_bot, pages_used + 1, USER_STACK_MAX_PAGES);

  // Flush the TLB so the newly writable page is visible to userspace.
  switchuvm(p);
  return 1;
}

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
    if(termsig)
      p->xstatus = WSTATUS_SIG(termsig);
    if(p->state == STOPPED)
      p->state = RUNNABLE;
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
    p->xstatus = WSTATUS_SIG(signo);
    p->killed = 1;
    return 0;
  }

  // Set return address to trampoline
  sf.sf_sigreturn = sp + ((uint)&((struct sigframe *)0)->sf_trampoline);

  // Copy signal frame to user stack
  if(copyout(p->pgdir, sp, &sf, sizeof(sf)) < 0) {
    STACKDBG("stack: pid %d signal delivery copyout failed\n", p->pid);
    // Handler delivery could not be set up; terminate as signaled.
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

// Must be called with interrupts disabled
int
cpuid() {
  uchar apicid;
  uchar idx;

  if(ncpu <= 1)
    return 0;

  apicid = cpu_apicid_cpuid();
  idx = apic_cpu_map[apicid];
  if(idx != 0xff && (int)idx < ncpu && (uchar)cpus[idx].apicid == apicid)
    return idx;

  cprintf("cpuid: apic lookup failed apicid=%d ncpu=%d\n", apicid, ncpu);
  panic("unknown apicid\n");
  return 0;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  uchar apicid;
  uchar idx;

  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");

  // Early bootstrap path.
  if(ncpu <= 1)
    return &cpus[0];

  // Strict reverse-map lookup built at mpinit().
  // Fail fast on any inconsistency rather than returning a potentially wrong CPU.
  apicid = cpu_apicid_cpuid();
  idx = apic_cpu_map[apicid];
  if(idx != 0xff && (int)idx < ncpu && (uchar)cpus[idx].apicid == apicid)
    return &cpus[idx];

  cprintf("mycpu: apic lookup failed apicid=%d ncpu=%d\n", apicid, ncpu);
  panic("unknown apicid\n");
  return &cpus[0];
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

int
proc_has_cwd_on_dev(uint dev)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(inode_on_dev(p->cwd, dev)){
      release(&ptable.lock);
      return 1;
    }
  }
  release(&ptable.lock);
  return 0;
}

uint
proc_cwd_dev(void)
{
  struct proc *p;
  uint dev;

  dev = 0;
  acquire(&ptable.lock);
  p = myproc();
  if(p && p->cwd)
    dev = inode_get_dev(p->cwd);
  release(&ptable.lock);

  return dev;
}

struct inode*
proc_cwd_idup(void)
{
  struct proc *p;
  struct inode *ip;

  ip = 0;
  acquire(&ptable.lock);
  p = myproc();
  if(p && p->cwd)
    ip = idup(p->cwd);
  release(&ptable.lock);

  return ip;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  int i;
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;

  // Assign a unique PID.  nextpid is bumped monotonically; when it would
  // exceed PID_MAX we wrap back to 2 (preserving PID 1 for init) and scan
  // forward to skip any PID still in use.  With NPROC=128 and PID_MAX=32767
  // collisions after wrap are extremely rare but must be handled correctly.
  // ptable.lock is already held so the in-use scan is safe.
  {
    int candidate;
    int found_pid;
    int tries;
    struct proc *q;

    found_pid = 0;
    for(tries = 0; tries < PID_MAX && !found_pid; tries++){
      candidate = nextpid;
      if(nextpid >= PID_MAX)
        nextpid = 2;   /* wrap: skip 0 (invalid) and 1 (init) */
      else
        nextpid++;

      /* Collision check: is any live process already using 'candidate'? */
      found_pid = 1;
      for(q = ptable.proc; q < &ptable.proc[NPROC]; q++){
        if(q != p && q->state != UNUSED && q->pid == candidate){
          found_pid = 0;
          break;
        }
      }
    }
    if(!found_pid)
      panic("allocproc: PID space exhausted");
    p->pid = candidate;
  }
  p->ppid = 0;
  p->pgid = p->pid;
  p->sid = p->pid;
  p->tty = -1;
  p->uid = 0;
  p->gid = 0;
  p->rlimit_nofile_cur = NOFILE_DEFAULT;
  p->rlimit_nofile_max = NOFILE_HARD;
  p->xstatus = 0;
  p->wait_event = WAIT_EVENT_NONE;
  p->wait_status = 0;
  p->sig_pending = 0;
  p->sig_caught = 0;
  p->sig_mask = 0;
  p->sig_ignored = 0;
  for(i = 0; i < NSIG; i++) {
    p->sig_handler[i] = 0;
    p->sig_actmask[i] = 0;
    p->sig_actflags[i] = 0;
  }

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }

  // Phase 1A: Allocate dynamic file descriptor table
  p->fdtable = fdtable_alloc();
  if(p->fdtable == 0){
    kfree(p->kstack);
    p->kstack = 0;
    p->state = UNUSED;
    return 0;
  }

  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  // Allocate initial stack page
  if((p->sz = allocuvm(p->pgdir, 0, PGSIZE)) == 0)
    panic("userinit: allocuvm");
  clearpteu(p->pgdir, (char*)(p->sz - PGSIZE));

  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  // eip will be set by kinit_exec() after vfs_init()

  safestrcpy(p->name, "init", sizeof(p->name));
  p->cwd = 0;
  p->ppid = 0;
  p->pgid = p->pid;
  p->sid = p->pid;
  p->tty = 0;
  p->uid = 0;
  p->gid = 0;
  p->rlimit_nofile_cur = NOFILE_DEFAULT;
  p->rlimit_nofile_max = NOFILE_HARD;
  p->xstatus = 0;
  p->wait_event = WAIT_EVENT_NONE;
  p->wait_status = 0;
  p->sig_caught = 0;

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  // copyuvm may COW-write-protect parent PTEs; flush current TLB view.
  switchuvm(curproc);
  np->sz = curproc->sz;
  np->stack_top = curproc->stack_top;
  np->stack_bot = curproc->stack_bot;
  np->parent = curproc;
  np->ppid = curproc->pid;
  np->pgid = curproc->pgid;
  np->sid = curproc->sid;
  np->tty = curproc->tty;
  np->uid = curproc->uid;
  np->gid = curproc->gid;
  np->rlimit_nofile_cur = curproc->rlimit_nofile_cur;
  np->rlimit_nofile_max = curproc->rlimit_nofile_max;
  np->xstatus = 0;
  np->wait_event = WAIT_EVENT_NONE;
  np->wait_status = 0;
  np->sig_pending = 0;
  np->sig_caught = 0;
  np->sig_mask = curproc->sig_mask;
  np->sig_ignored = curproc->sig_ignored;
  for(i = 0; i < NSIG; i++) {
    np->sig_handler[i] = curproc->sig_handler[i];
    np->sig_actmask[i] = curproc->sig_actmask[i];
    np->sig_actflags[i] = curproc->sig_actflags[i];
  }
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  // Phase 1A: Duplicate file descriptor table from parent
  if(fdtable_dup(curproc->fdtable, np->fdtable) < 0){
    kfree(np->kstack);
    np->kstack = 0;
    fdtable_free(np->fdtable);
    np->fdtable = 0;
    freevm(np->pgdir);
    np->pgdir = 0;
    np->state = UNUSED;
    return -1;
  }

  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(int status)
{
  struct proc *curproc = myproc();
  struct proc *p;

  if(curproc == initproc)
    panic("init exiting");

  // Phase 1A: Close all files via fdtable_free
  if(curproc->fdtable){
    fdtable_free(curproc->fdtable);
    curproc->fdtable = 0;
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      p->ppid = initproc->pid;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  if(curproc->xstatus == 0)
    curproc->xstatus = WSTATUS_EXIT(status);
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  return proc_waitpid(-1, 0, 0);
}

int
proc_waitpid(int pid, int *status, int options)
{
  struct proc *p;
  int havekids;
  int foundpid;
  struct proc *curproc = myproc();
  int st;

  if((options & ~(WNOHANG | WUNTRACED | WCONTINUED)) != 0)
    return -1;
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      if(!proc_wait_target_match(curproc, p, pid))
        continue;
      havekids = 1;

      if((options & WUNTRACED) && p->wait_event == WAIT_EVENT_STOPPED){
        st = p->wait_status;
        p->wait_event = WAIT_EVENT_NONE;
        p->wait_status = 0;
        foundpid = p->pid;
        if(status)
          *status = st;
        release(&ptable.lock);
        return foundpid;
      }

      if((options & WCONTINUED) && p->wait_event == WAIT_EVENT_CONTINUED){
        st = p->wait_status;
        p->wait_event = WAIT_EVENT_NONE;
        p->wait_status = 0;
        foundpid = p->pid;
        if(status)
          *status = st;
        release(&ptable.lock);
        return foundpid;
      }

      if(p->state == ZOMBIE){
        // Found one.
        foundpid = p->pid;
        st = p->xstatus;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->ppid = 0;
        p->pgid = 0;
        p->sid = 0;
        p->tty = -1;
        p->uid = 0;
        p->gid = 0;
        p->rlimit_nofile_cur = 0;
        p->rlimit_nofile_max = 0;
        p->xstatus = 0;
        p->wait_event = WAIT_EVENT_NONE;
        p->wait_status = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->sig_pending = 0;
        p->sig_caught = 0;
        p->sig_mask = 0;
        p->sig_ignored = 0;
        memset(p->sig_handler, 0, sizeof(p->sig_handler));
        memset(p->sig_actmask, 0, sizeof(p->sig_actmask));
        memset(p->sig_actflags, 0, sizeof(p->sig_actflags));
        p->state = UNUSED;
        if(status)
          *status = st;
        release(&ptable.lock);
        return foundpid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    if(options & WNOHANG){
      release(&ptable.lock);
      return 0;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

int
proc_wait4(int pid, int *status, int options, uint rusage_addr)
{
  // TODO(wait4): populate rusage once accounting fields are available.
  (void)rusage_addr;
  return proc_waitpid(pid, status, options);
}

int
proc_waitid(int idtype, int id, int *infop, int options)
{
  int pid;
  int st;
  int ret;

  if(options & ~(WNOHANG | WUNTRACED | WCONTINUED))
    return -1;

  if(idtype == P_PID) {
    pid = id;
  } else if(idtype == P_PGID) {
    pid = (id == 0) ? 0 : -id;
  } else if(idtype == P_ALL) {
    pid = -1;
  } else {
    return -1;
  }

  ret = proc_waitpid(pid, &st, options);
  if(ret < 0)
    return ret;

  if(ret == 0) {
    if(infop) {
      infop[0] = 0;
      infop[1] = 0;
      infop[2] = 0;
      infop[3] = 0;
    }
    return 0;
  }

  if(infop) {
    infop[0] = SIGCHLD;
    if(WIFSTOPPED_INT(st)) {
      infop[1] = CLD_STOPPED;
      infop[2] = WSTOPSIG_INT(st);
      infop[3] = ret;
    } else if(WIFCONTINUED_INT(st)) {
      infop[1] = CLD_CONTINUED;
      infop[2] = SIGCONT;
      infop[3] = ret;
    } else if(WIFSIGNALED_INT(st)) {
      infop[1] = CLD_KILLED;
      infop[2] = WTERMSIG_INT(st);
      infop[3] = ret;
    } else {
      infop[1] = CLD_EXITED;
      infop[2] = WEXITSTATUS_INT(st);
      infop[3] = ret;
    }
  }

  return 0;
}

int
proc_sigaction(int signo, uint act_addr, uint oldact_addr)
{
  struct proc *p;
  uint act_local[3];
  uint oldact_local[3];
  int have_act;
  int have_oldact;

  if(!valid_signo(signo))
    return -1;

  p = myproc();
  if(p == 0 || p->pgdir == 0)
    return -1;

  have_act = (act_addr != 0);
  have_oldact = (oldact_addr != 0);

  if(have_act && copyin(p->pgdir, act_local, act_addr, sizeof(act_local)) < 0)
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

  if(have_oldact && copyout(p->pgdir, oldact_addr, oldact_local, sizeof(oldact_local)) < 0)
    return -1;

  return 0;
}

int
proc_sigprocmask(int how, uint set_addr, uint oldset_addr)
{
  struct proc *p;
  sigset_t set;
  sigset_t newmask;
  sigset_t oldmask;
  sigset_t unblockable;

  p = myproc();
  if(p == 0 || p->pgdir == 0)
    return -1;

  unblockable = SIGBIT(SIGKILL) | SIGBIT(SIGSTOP);
  set = 0;
  if(set_addr != 0 && copyin(p->pgdir, &set, set_addr, sizeof(set)) < 0)
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

  if(oldset_addr != 0 && copyout(p->pgdir, oldset_addr, &oldmask, sizeof(oldmask)) < 0)
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
  int rc;

  curproc = myproc();
  if(curproc == 0 || curproc->pgdir == 0 || termios_addr == 0)
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
  if(copyout(curproc->pgdir, termios_addr, &kt, sizeof(kt)) < 0)
    return -1;
  return 0;
}

int
proc_tcsetattr(int fd, int optional_actions, uint termios_addr)
{
  struct proc *curproc;
  struct file *f;
  struct termios kt;

  curproc = myproc();
  if(curproc == 0 || curproc->pgdir == 0 || termios_addr == 0)
    return -1;
  if(!proc_is_tty_fd(fd)) {
    return -1;
  }

  if(copyin(curproc->pgdir, &kt, termios_addr, sizeof(kt)) < 0)
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

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  int i, start, found;
  c->proc = 0;
  c->sched_last = 0;
  c->sched_passes = 0;
  c->sched_idle_halts = 0;
  c->sched_picks = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();
    c->sched_passes++;

    found = 0;

    // Loop over process table looking for a runnable process.
    // Each CPU starts its scan from where it last found work, so
    // multiple CPUs naturally spread across the table instead of
    // all racing for the same low-indexed slots.
    acquire(&ptable.lock);
    start = c->sched_last;
    for(i = 0; i < NPROC; i++){
      p = &ptable.proc[(start + i) % NPROC];
      if(p->state != RUNNABLE)
        continue;

      // Advance hint past this process so the next trip starts after it.
      c->sched_last = ((start + i + 1) % NPROC);
      c->sched_picks++;

      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
      found = 1;
    }

    if(!found){
      // Nothing runnable.  Release the lock and halt this CPU until
      // the next interrupt fires (timer, disk completion, etc.).
      // This avoids burning all CPUs spinning on ptable.lock when the
      // system is idle, which was a significant source of unnecessary
      // lock contention in the original xv6 design.
      release(&ptable.lock);
      // sti was called above; hlt suspends the CPU until the next
      // interrupt, at which point the outer loop resumes.
      c->sched_idle_halts++;
      asm volatile("hlt");
    } else {
      release(&ptable.lock);
    }

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
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
  p->chan = chan;
  p->state = SLEEPING;

  sched();

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

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
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
  return p->uid;
}

int
proc_getgid(void)
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
  struct proc *p;

  if(uid < 0)
    return -1;

  p = myproc();
  if(p == 0)
    return -1;

  acquire(&ptable.lock);
  if(p->uid != 0 && uid != p->uid) {
    release(&ptable.lock);
    return -1;
  }
  p->uid = uid;
  release(&ptable.lock);
  return 0;
}

int
proc_setgid(int gid)
{
  struct proc *p;

  if(gid < 0)
    return -1;

  p = myproc();
  if(p == 0)
    return -1;

  acquire(&ptable.lock);
  if(p->uid != 0 && gid != p->gid) {
    release(&ptable.lock);
    return -1;
  }
  p->gid = gid;
  release(&ptable.lock);
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
