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

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

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

// Check all processes for expired alarms and post SIGALRM.
// Called from timer interrupt on CPU 0.
void
proc_check_alarms(uint current_ticks)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state == UNUSED)
      continue;
    if(p->alarm_ticks == 0)
      continue;
    if(current_ticks >= p->alarm_ticks) {
      p->alarm_ticks = 0;  // One-shot, clear the alarm
      p->sig_pending |= SIGBIT(SIGALRM);
      // Wake process if sleeping so it can receive the signal
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
    }
  }
  release(&ptable.lock);
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
    cprintf("pid %d: signal delivery failed, bad stack 0x%x\n", p->pid, sp);
    p->killed = 1;
    return 0;
  }

  // Set return address to trampoline
  sf.sf_sigreturn = sp + ((uint)&((struct sigframe *)0)->sf_trampoline);

  // Copy signal frame to user stack
  if(copyout(p->pgdir, sp, &sf, sizeof(sf)) < 0) {
    cprintf("pid %d: signal delivery copyout failed\n", p->pid);
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
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
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
  p->pid = nextpid++;
  p->ppid = 0;
  p->pgid = p->pid;
  p->sid = p->pid;
  p->tty = -1;
  p->uid = 0;
  p->gid = 0;
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
  np->sz = curproc->sz;
  np->parent = curproc;
  np->ppid = curproc->pid;
  np->pgid = curproc->pgid;
  np->sid = curproc->sid;
  np->tty = curproc->tty;
  np->uid = curproc->uid;
  np->gid = curproc->gid;
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

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
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
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
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
    curproc->xstatus = WSTATUS_EXIT(0);
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
  uint *act;
  uint *oldact;

  if(!valid_signo(signo))
    return -1;

  p = myproc();
  if(p == 0)
    return -1;

  acquire(&ptable.lock);

  if(oldact_addr != 0) {
    oldact = (uint*)oldact_addr;
    oldact[0] = p->sig_handler[signo];
    oldact[1] = p->sig_actmask[signo];
    oldact[2] = p->sig_actflags[signo];
  }

  // SIGKILL/SIGSTOP keep fixed default actions.
  if(act_addr != 0 && (signo == SIGKILL || signo == SIGSTOP)) {
    release(&ptable.lock);
    return -1;
  }

  if(act_addr != 0) {
    act = (uint*)act_addr;
    p->sig_handler[signo] = act[0];
    p->sig_actmask[signo] = act[1];
    p->sig_actflags[signo] = act[2];

    if(act[0] == 1)
      p->sig_ignored |= SIGBIT(signo);
    else
      p->sig_ignored &= ~SIGBIT(signo);

  }

  release(&ptable.lock);
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
  if(p == 0)
    return -1;

  unblockable = SIGBIT(SIGKILL) | SIGBIT(SIGSTOP);
  set = 0;
  if(set_addr != 0)
    set = *(sigset_t*)set_addr;

  acquire(&ptable.lock);
  oldmask = p->sig_mask;

  if(oldset_addr != 0)
    *(sigset_t*)oldset_addr = oldmask;

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

  console_set_foreground_pgid(pgid);
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
  return console_get_foreground_pgid();
}

int
proc_tcgetattr(int fd, uint termios_addr)
{
  struct proc *curproc;

  curproc = myproc();
  if(curproc == 0 || termios_addr == 0)
    return -1;
  if(curproc->tty < 0)
    return -1;
  if(fd < 0 || fd > 2)
    return -1;
  return console_get_termios((struct termios*)termios_addr);
}

int
proc_tcsetattr(int fd, int optional_actions, uint termios_addr)
{
  struct proc *curproc;

  curproc = myproc();
  if(curproc == 0 || termios_addr == 0)
    return -1;
  if(curproc->tty < 0)
    return -1;
  if(fd < 0 || fd > 2)
    return -1;
  return console_set_termios((const struct termios*)termios_addr, optional_actions);
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
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

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
    if(p == initproc)
      continue;

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
