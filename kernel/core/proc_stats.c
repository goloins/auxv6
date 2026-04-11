#include "types.h"
#include "defs.h"
#include "param.h"
#include "proc.h"
#include "spinlock.h"

extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;
extern volatile uint wakeup_calls;
extern volatile uint wakeup_scans;
extern volatile uint wakeup_hits;
extern volatile uint waitpid_loops;
extern volatile uint waitpid_scans;
extern volatile uint wakeup_ticks_calls;
extern volatile uint wakeup_proc_calls;
extern volatile uint wakeup_other_calls;
extern volatile int active_tick_sleepers;

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
  int r;
  int t;

  r = 0;
  t = 0;
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

void
proc_get_sched_latency_stats(uint *wake_calls,
                             uint *wake_scanned,
                             uint *wake_matched,
                             uint *wait_loops,
                             uint *wait_scanned)
{
  *wake_calls = wakeup_calls;
  *wake_scanned = wakeup_scans;
  *wake_matched = wakeup_hits;
  *wait_loops = waitpid_loops;
  *wait_scanned = waitpid_scans;
}

void
proc_get_wakeup_class_stats(uint *ticks_calls, uint *proc_calls, uint *other_calls)
{
  *ticks_calls = wakeup_ticks_calls;
  *proc_calls = wakeup_proc_calls;
  *other_calls = wakeup_other_calls;
}

int
proc_has_tick_sleepers(void)
{
  return active_tick_sleepers > 0;
}