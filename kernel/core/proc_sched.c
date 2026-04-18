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

// ticks is declared in trap.c.  We read it without tickslock in places where
// a stale-by-one-tick value is acceptable (scheduling heuristics only).
extern uint ticks;

// ---------------------------------------------------------------------------
// MLFQ queue state — all mutations require ptable.lock held, except
// mlfq_timer_charge() which runs from the timer ISR on the owning CPU.
// ---------------------------------------------------------------------------

static const ushort mlfq_quanta[MLFQ_NQUEUES] = {
  MLFQ_QUANTUM_Q0, MLFQ_QUANTUM_Q1, MLFQ_QUANTUM_Q2,
  MLFQ_QUANTUM_Q3, MLFQ_QUANTUM_Q4
};

struct mlfq_queue {
  struct proc *head;   // oldest entry (dequeue from here)
  struct proc *tail;   // newest entry (enqueue here)
  uint         count;  // number of procs currently queued
};

static struct mlfq_queue mlfq_qs[MLFQ_NQUEUES];
static uint   mlfq_last_boost_tick;
static volatile uint mlfq_boost_interval_ticks = MLFQ_BOOST_INTERVAL;

// MLFQ movement counters — written under ptable.lock (or single-CPU ISR);
// declared volatile so lockless procfs reads see up-to-date values.
volatile uint mlfq_stat_promotions;
volatile uint mlfq_stat_demotions;
volatile uint mlfq_stat_boosts;
volatile uint mlfq_stat_budget_expired;

// Append p to the tail of the given queue.  ptable.lock must be held.
static void
mlfq_list_append(struct mlfq_queue *q, struct proc *p)
{
  p->sched_next = 0;
  p->sched_prev = q->tail;
  if(q->tail)
    q->tail->sched_next = p;
  else
    q->head = p;
  q->tail = p;
  q->count++;
}

// Unlink p from the given queue.  ptable.lock must be held.
// Caller guarantees p is actually in this queue.
static void
mlfq_list_remove(struct mlfq_queue *q, struct proc *p)
{
  if(p->sched_prev)
    p->sched_prev->sched_next = p->sched_next;
  else
    q->head = p->sched_next;
  if(p->sched_next)
    p->sched_next->sched_prev = p->sched_prev;
  else
    q->tail = p->sched_prev;
  p->sched_next = 0;
  p->sched_prev = 0;
  if(q->count > 0)
    q->count--;
}

// Return 1 if p is currently linked into any MLFQ queue, 0 otherwise.
static int
mlfq_is_queued(struct proc *p)
{
  int q;
  if(p->sched_next || p->sched_prev)
    return 1;
  // Sole element: check whether p is the head of its declared queue.
  q = p->sched_q;
  if(q >= 0 && q < MLFQ_NQUEUES && mlfq_qs[q].head == p)
    return 1;
  return 0;
}

// ---------------------------------------------------------------------------
// Public MLFQ helpers (declared in defs.h).
// ---------------------------------------------------------------------------

// Enqueue p as RUNNABLE into its MLFQ queue, applying promotion/demotion
// policy based on reason.  p->state must already be set to RUNNABLE by the
// caller.  ptable.lock must be held.
void
schedq_enqueue_locked(struct proc *p, int reason)
{
  uint now;
  uint slept;
  int  q;
  int  orig_q;
  int  budget_was_expired;

  if(p == 0)
    return;

  // Hot path: SCHED_ENQ_YIELD comes from a RUNNING task that cannot already
  // be queued. Skip the queue-membership check there to keep per-tick
  // preemption overhead minimal.
  // For all other enqueue reasons, keep the defensive double-enqueue guard.
  if(reason != SCHED_ENQ_YIELD && mlfq_is_queued(p))
    mlfq_list_remove(&mlfq_qs[p->sched_q], p);

  now = ticks;   // approx — stale by at most 1 tick, fine for heuristics

  orig_q           = p->sched_q;
  budget_was_expired = (p->sched_flags & SCHED_F_BUDGET_EXPIRED) ? 1 : 0;

  // --- Demotion ---
  // Timer preemption exhausted this quantum: move down one level.
  if(budget_was_expired && reason == SCHED_ENQ_YIELD) {
    if(p->sched_q < MLFQ_NQUEUES - 1)
      p->sched_q++;
    mlfq_stat_demotions++;
  }
  p->sched_flags &= ~SCHED_F_BUDGET_EXPIRED;

  // --- Promotion ---
  // Process woke after a meaningful sleep (real I/O or event wait): move up.
  if(reason == SCHED_ENQ_WAKE || reason == SCHED_ENQ_ALARM ||
     reason == SCHED_ENQ_SIGNAL) {
    slept = now - p->sched_last_block_tick;
    if(slept >= MLFQ_PROMOTE_MIN_SLEEP && p->sched_q > 0) {
      p->sched_q--;
      mlfq_stat_promotions++;
    }
    p->sched_last_wake_tick = now;
  }

  // Clamp queue level to valid range.
  q = p->sched_q;
  if(q < 0) q = 0;
  if(q >= MLFQ_NQUEUES) q = MLFQ_NQUEUES - 1;
  p->sched_q = (uchar)q;

  // Reset the quantum budget when:
  //   - the queue level changed (promotion or demotion)
  //   - the budget was fully exhausted this run (expired yield at same level)
  //   - this is a fresh start (fork, wake, signal, alarm, boost)
  // Preserve the remaining budget on a plain timer-preemption yield at the
  // same level so that the process accumulates charges across consecutive
  // 1-tick dispatches rather than getting a fresh quantum on every tick.
  if(q != orig_q || budget_was_expired || reason != SCHED_ENQ_YIELD)
    p->sched_budget_left = mlfq_quanta[q];

  p->sched_enq_tick = now;

  mlfq_list_append(&mlfq_qs[q], p);
}

// Remove p from whatever MLFQ queue it currently occupies.
// ptable.lock must be held.
void
schedq_dequeue_locked(struct proc *p)
{
  if(p == 0 || !mlfq_is_queued(p))
    return;
  if(p->sched_q >= 0 && p->sched_q < MLFQ_NQUEUES)
    mlfq_list_remove(&mlfq_qs[p->sched_q], p);
}

// Pick and dequeue the highest-priority RUNNABLE process.
// Returns NULL when all queues are empty.  ptable.lock must be held.
struct proc*
schedq_pick_next_locked(void)
{
  int q;
  struct proc *p;

  for(q = 0; q < MLFQ_NQUEUES; q++) {
    p = mlfq_qs[q].head;
    if(p == 0)
      continue;
    mlfq_list_remove(&mlfq_qs[q], p);
    return p;
  }
  return 0;
}

// Charge one timer tick to the running process (no lock held).
// Decrements the quantum budget; marks SCHED_F_BUDGET_EXPIRED when it hits
// zero so that the subsequent yield() will demote the process one level.
// Safe to call without ptable.lock: only the owning CPU touches these two
// byte-sized fields while the process is RUNNING.
void
mlfq_timer_charge(struct proc *p)
{
  if(p == 0)
    return;
  if(p->sched_budget_left > 1) {
    p->sched_budget_left--;
  } else {
    p->sched_budget_left = 0;
    if(!(p->sched_flags & SCHED_F_BUDGET_EXPIRED)) {
      p->sched_flags |= SCHED_F_BUDGET_EXPIRED;
      mlfq_stat_budget_expired++;
    }
  }
}

// Apply the global anti-starvation boost on CPU0 every MLFQ_BOOST_INTERVAL
// ticks.  Moves all long-queued processes to Q1 so CPU-bound tasks cannot
// starve indefinitely behind interactive ones.
// Called from the timer ISR with interrupts disabled; acquires ptable.lock.
void
mlfq_apply_global_boost(uint now_ticks)
{
  uint interval;
  int i, q;
  struct proc *p;

  interval = mlfq_boost_interval_ticks;
  if(now_ticks - mlfq_last_boost_tick < interval)
    return;
  mlfq_last_boost_tick = now_ticks;

  acquire(&ptable.lock);
  for(i = 0; i < NPROC; i++) {
    p = &ptable.proc[i];
    if(p->state == UNUSED || p->state == ZOMBIE || p->state == EMBRYO)
      continue;
    if(p->state == RUNNING)   // managed by its own CPU right now
      continue;
    if(p->sched_q <= 1)       // already at Q0 or Q1
      continue;
    if(p->state == RUNNABLE) {
      // Move from current queue to Q1.
      q = p->sched_q;
      mlfq_list_remove(&mlfq_qs[q], p);
      p->sched_q = 1;
      p->sched_budget_left = mlfq_quanta[1];
      p->sched_flags &= ~(SCHED_F_BUDGET_EXPIRED | SCHED_F_BOOST_APPLIED);
      mlfq_list_append(&mlfq_qs[1], p);
    } else {
      // SLEEPING / STOPPED: reset metadata so next wakeup starts at Q1.
      p->sched_q = 1;
      p->sched_flags &= ~(SCHED_F_BUDGET_EXPIRED | SCHED_F_BOOST_APPLIED);
    }
  }
  mlfq_stat_boosts++;
  release(&ptable.lock);
}

uint
mlfq_get_boost_interval(void)
{
  return mlfq_boost_interval_ticks;
}

int
mlfq_set_boost_interval(uint interval_ticks)
{
  if(interval_ticks < MLFQ_BOOST_INTERVAL_MIN ||
     interval_ticks > MLFQ_BOOST_INTERVAL_MAX)
    return -1;

  acquire(&ptable.lock);
  mlfq_boost_interval_ticks = interval_ticks;
  mlfq_last_boost_tick = ticks;
  release(&ptable.lock);
  return 0;
}

// Return current MLFQ movement counters and per-queue lengths for procfs.
// Values are sampled locklessly; slight imprecision is acceptable.
void
mlfq_get_stats(uint *promotions, uint *demotions, uint *boosts,
               uint *budget_expired, uint *q_lens)
{
  int i;
  *promotions     = mlfq_stat_promotions;
  *demotions      = mlfq_stat_demotions;
  *boosts         = mlfq_stat_boosts;
  *budget_expired = mlfq_stat_budget_expired;
  for(i = 0; i < MLFQ_NQUEUES; i++)
    q_lens[i] = mlfq_qs[i].count;
}


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
  int found;
  int ql;

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

    acquire(&ptable.lock);
    p = schedq_pick_next_locked();
    if(p != 0) {
      ql = p->sched_q;
      c->sched_picks++;
      if(ql >= 0 && ql < MLFQ_NQUEUES)
        c->sched_q_dispatch[ql]++;
      // Record dispatch timestamp and reset burst counter.
      p->sched_last_start_tick = ticks;
      p->sched_cpu_burst_ticks = 0;

      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
      found = 1;
    } else {
      c->sched_q_empty_passes++;
    }

    if(!found){
      // Nothing runnable.  Release the lock and halt this CPU until
      // the next interrupt fires (timer, disk completion, etc.).
      release(&ptable.lock);
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
  struct proc *p;
  acquire(&ptable.lock);  //DOC: yieldlock
  p = myproc();
  p->state = RUNNABLE;
  schedq_enqueue_locked(p, SCHED_ENQ_YIELD);
  sched();
  release(&ptable.lock);
}