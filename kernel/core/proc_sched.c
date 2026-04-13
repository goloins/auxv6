#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

// Provided by mp.c: reverse APIC-ID -> cpus[] index, built at mpinit().
extern uchar apic_cpu_map[256];

// Must be called with interrupts disabled
int
cpuid(void)
{
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
myproc(void)
{
  struct cpu *c;
  struct proc *p;

  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

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
  int i;
  int start;
  int found;

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