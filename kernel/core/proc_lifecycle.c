#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "wait.h"
#include "signal.h"

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

#define WAIT_EVENT_NONE      0
#define WAIT_EVENT_STOPPED   1
#define WAIT_EVENT_CONTINUED 2

#define WSTATUS_EXIT(code)   (((code) & 0xff) << 8)

#define WIFEXITED_INT(s)    (((s) & 0xff) == 0)
#define WEXITSTATUS_INT(s)  (((s) >> 8) & 0xff)
#define WIFSIGNALED_INT(s)  (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#define WTERMSIG_INT(s)     ((s) & 0x7f)
#define WIFSTOPPED_INT(s)   (((s) & 0xff) == 0x7f)
#define WSTOPSIG_INT(s)     (((s) >> 8) & 0xff)
#define WIFCONTINUED_INT(s) ((s) == 0xffff)

extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

extern volatile uint waitpid_loops;
extern volatile uint waitpid_scans;
extern void proc_wakeup1_locked(void *chan);

extern void forkret(void);
extern void trapret(void);

struct proc *initproc;
int nextpid = 1;

static uint
proc_kstack_npages(void)
{
  if((KSTACKSIZE % PGSIZE) != 0)
    panic("KSTACKSIZE align");
  return KSTACKSIZE / PGSIZE;
}

static int
wait_status_has_valid_signal(int status)
{
  if(!WIFSIGNALED_INT(status))
    return 1;
  return valid_signo(WTERMSIG_INT(status));
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
  p->tick_next = 0;
  p->on_tickq = 0;

  // Assign a unique PID with wrap/collision avoidance.
  {
    int candidate;
    int found_pid;
    int tries;
    struct proc *q;

    found_pid = 0;
    for(tries = 0; tries < PID_MAX && !found_pid; tries++){
      candidate = nextpid;
      if(nextpid >= PID_MAX)
        nextpid = 2;
      else
        nextpid++;

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
  proc_clear_addrspace(p);
  p->pgid = p->pid;
  p->sid = p->pid;
  p->tty = -1;
  p->uid = 0;
  p->gid = 0;
  p->ruid = 0;
  p->rgid = 0;
  p->suid = 0;
  p->sgid = 0;
  p->ngroups = 1;
  p->groups[0] = 0;
  for(i = 1; i < PROC_NGROUPS_MAX; i++)
    p->groups[i] = 0;
  p->umask = 022;
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
  p->alarm_ticks = 0;
  p->alarm_interval_ticks = 0;
  // MLFQ scheduler fields — zeroed at EMBRYO allocation.
  p->sched_q = MLFQ_FORK_START_Q;
  p->sched_flags = 0;
  p->sched_budget_left = MLFQ_QUANTUM_Q1;
  p->sched_enq_tick = 0;
  p->sched_last_start_tick = 0;
  p->sched_last_block_tick = 0;
  p->sched_last_wake_tick = 0;
  p->sched_cpu_burst_ticks = 0;
  p->sched_next = 0;
  p->sched_prev = 0;

  release(&ptable.lock);

  if(p->kstack == 0){
    p->kstack = kalloc_contiguous(proc_kstack_npages());
    if(p->kstack == 0){
      p->state = UNUSED;
      return 0;
    }
  }

  if(p->fdtable == 0)
    p->fdtable = fdtable_alloc();
  if(p->fdtable == 0){
    p->state = UNUSED;
    return 0;
  }

  sp = p->kstack + KSTACKSIZE;
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

void
userinit(void)
{
  int i;
  struct address_space *initas;
  struct proc *p;

  p = allocproc();
  initproc = p;
  initas = address_space_create();
  proc_bind_addrspace(p, initas);
  if(initas == 0)
    panic("userinit: out of memory?");
  if((p->sz = allocuvm_as(p->addrsp, 0, PGSIZE)) == 0)
    panic("userinit: allocuvm");
  clearpteu(proc_pgdir(p), (char*)(p->sz - PGSIZE));

  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;

  safestrcpy(p->name, "init", sizeof(p->name));
  p->cwd = 0;
  p->ppid = 0;
  p->pgid = p->pid;
  p->sid = p->pid;
  p->tty = 0;
  p->uid = 0;
  p->gid = 0;
  p->ruid = 0;
  p->rgid = 0;
  p->suid = 0;
  p->sgid = 0;
  p->ngroups = 1;
  p->groups[0] = 0;
  for(i = 1; i < PROC_NGROUPS_MAX; i++)
    p->groups[i] = 0;
  p->umask = 022;
  p->rlimit_nofile_cur = NOFILE_DEFAULT;
  p->rlimit_nofile_max = NOFILE_HARD;
  p->xstatus = 0;
  p->wait_event = WAIT_EVENT_NONE;
  p->wait_status = 0;
  p->sig_caught = 0;

  acquire(&ptable.lock);
  p->sched_q = MLFQ_FORK_START_Q;
  p->state = RUNNABLE;
  schedq_enqueue_locked(p, SCHED_ENQ_FORK);
  release(&ptable.lock);
}

int
fork(void)
{
  int i;
  int pid;
  struct proc *np;
  struct proc *curproc = myproc();
  struct address_space *newas;
  struct address_space *srcas;

  if((np = allocproc()) == 0)
    return -1;

  srcas = curproc->addrsp;
  if(srcas == 0)
    panic("fork: no addrspace");

  newas = address_space_dup_cow(srcas);
  if(newas == 0){
    np->state = UNUSED;
    return -1;
  }
  proc_bind_addrspace(np, newas);
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
  np->ruid = curproc->ruid;
  np->rgid = curproc->rgid;
  np->suid = curproc->suid;
  np->sgid = curproc->sgid;
  np->ngroups = curproc->ngroups;
  for(i = 0; i < PROC_NGROUPS_MAX; i++)
    np->groups[i] = curproc->groups[i];
  np->umask = curproc->umask;
  np->rlimit_nofile_cur = curproc->rlimit_nofile_cur;
  np->rlimit_nofile_max = curproc->rlimit_nofile_max;
  np->xstatus = 0;
  np->wait_event = WAIT_EVENT_NONE;
  np->wait_status = 0;
  np->sig_pending = 0;
  np->sig_caught = 0;
  np->sig_mask = curproc->sig_mask;
  np->sig_ignored = curproc->sig_ignored;
  np->alarm_ticks = 0;
  np->alarm_interval_ticks = 0;
  for(i = 0; i < NSIG; i++) {
    np->sig_handler[i] = curproc->sig_handler[i];
    np->sig_actmask[i] = curproc->sig_actmask[i];
    np->sig_actflags[i] = curproc->sig_actflags[i];
  }
  *np->tf = *curproc->tf;
  np->tf->eax = 0;

  if(fdtable_dup(curproc->fdtable, np->fdtable) < 0){
    fdtable_free(np->fdtable);
    np->fdtable = 0;
    if(newas)
      address_space_destroy(newas);
    proc_clear_addrspace(np);
    np->state = UNUSED;
    return -1;
  }

  np->cwd = idup(curproc->cwd);
  safestrcpy(np->name, curproc->name, sizeof(curproc->name));
  pid = np->pid;

  acquire(&ptable.lock);
  np->sched_q = MLFQ_FORK_START_Q;
  np->state = RUNNABLE;
  schedq_enqueue_locked(np, SCHED_ENQ_FORK);
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
  proc_wakeup1_locked(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      p->ppid = initproc->pid;
      if(p->state == ZOMBIE)
        proc_wakeup1_locked(initproc);
    }
  }

  if(curproc->xstatus != 0 && !wait_status_has_valid_signal(curproc->xstatus))
    curproc->xstatus = 0;
  if(curproc->xstatus == 0)
    curproc->xstatus = WSTATUS_EXIT(status);
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

int
wait(void)
{
  return proc_waitpid(-1, 0, 0);
}

int
proc_waitpid(int pid, int *status, int options)
{
  int i;
  struct proc *p;
  int havekids;
  int foundpid;
  struct proc *curproc = myproc();
  int st;

  if((options & ~(WNOHANG | WUNTRACED | WCONTINUED)) != 0)
    return -1;

  acquire(&ptable.lock);
  for(;;){
    havekids = 0;
    waitpid_loops++;
    waitpid_scans += NPROC;
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
        foundpid = p->pid;
        st = p->xstatus;
        if(p->addrsp){
          address_space_destroy(p->addrsp);
        } else if(proc_pgdir(p)){
          freevm(proc_pgdir(p));
        }
        proc_clear_addrspace(p);
        p->pid = 0;
        p->ppid = 0;
        p->pgid = 0;
        p->sid = 0;
        p->tty = -1;
        p->uid = 0;
        p->gid = 0;
        p->ruid = 0;
        p->rgid = 0;
        p->suid = 0;
        p->sgid = 0;
        p->ngroups = 0;
        for(i = 0; i < PROC_NGROUPS_MAX; i++)
          p->groups[i] = 0;
        p->umask = 022;
        p->rlimit_nofile_cur = 0;
        p->rlimit_nofile_max = 0;
        p->xstatus = 0;
        p->wait_event = WAIT_EVENT_NONE;
        p->wait_status = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->tick_next = 0;
        p->on_tickq = 0;
        p->killed = 0;
        p->sig_pending = 0;
        p->sig_caught = 0;
        p->sig_mask = 0;
        p->sig_ignored = 0;
        memset(p->sig_handler, 0, sizeof(p->sig_handler));
        memset(p->sig_actmask, 0, sizeof(p->sig_actmask));
        memset(p->sig_actflags, 0, sizeof(p->sig_actflags));
        // Zero MLFQ fields so reused slot starts clean.
        p->sched_q = 0;
        p->sched_flags = 0;
        p->sched_budget_left = 0;
        p->sched_enq_tick = 0;
        p->sched_last_start_tick = 0;
        p->sched_last_block_tick = 0;
        p->sched_last_wake_tick = 0;
        p->sched_cpu_burst_ticks = 0;
        p->sched_next = 0;
        p->sched_prev = 0;
        p->state = UNUSED;
        if(status)
          *status = st;
        release(&ptable.lock);
        return foundpid;
      }
    }

    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    if(options & WNOHANG){
      release(&ptable.lock);
      return 0;
    }

    sleep(curproc, &ptable.lock);
  }
}

int
proc_wait4(int pid, int *status, int options, uint rusage_addr)
{
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

  if(idtype == P_PID)
    pid = id;
  else if(idtype == P_PGID)
    pid = (id == 0) ? 0 : -id;
  else if(idtype == P_ALL)
    pid = -1;
  else
    return -1;

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