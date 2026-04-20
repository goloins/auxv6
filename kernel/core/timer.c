#include "types.h"
#include "defs.h"
#include "proc.h"
#include "spinlock.h"

struct spinlock tickslock;
uint ticks;

static void
timer_tick_global(uint current_ticks)
{
  if(proc_has_tick_sleepers())
    wakeup(&ticks);

  ktime_tick(current_ticks);
  proc_check_alarms(current_ticks);
  mlfq_apply_global_boost(current_ticks);

  // Update load averages every 500 ticks (5 seconds at 100Hz).
  if((current_ticks % 500) == 0)
    proc_tick_loadavg();

  usb_runtime_service();

  // Poll network devices for RX/TX completions.
  netdev_poll();

  // TCP slow timer - every 10 ticks (100ms).
  if((current_ticks % 10) == 0)
    tcp_slowtimo();
}

void
timerinit(void)
{
  initlock(&tickslock, "time");
  lockdep_set_rank(&tickslock, LOCK_RANK_TICKS, "ticks");
  ticks = 0;
  ktime_init();

  // Best-effort Phase 1/2 probe: bring up the HPET counter if discovered,
  // while keeping LAPIC as the active interrupt source for scheduling.
  hpet_init();
}

void
timercpuinit(void)
{
  // Phase 0 backend: keep LAPIC as the active timer/interrupt source.
  lapicinit();
}

void
timerintr(void)
{
  uint current_ticks;

  if(cpuid() == 0){
    acquire(&tickslock);
    ticks++;
    current_ticks = ticks;
    release(&tickslock);

    timer_tick_global(current_ticks);
  }

  // Charge one CPU tick to the process running on this CPU (all CPUs).
  if(myproc()) {
    myproc()->cticks++;
    mlfq_timer_charge(myproc());
  }

  lapiceoi();
}
