#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "sys/time.h"
#include "x86.h"

#define KTIME_NSEC_PER_SEC  1000000000U
#define KTIME_NSEC_PER_TICK 10000000ULL

static struct spinlock ktime_lock;
static uint            ktime_ticks;
static uint            ktime_tsc_per_tick;
static uint64_t        ktime_last_tick_tsc;
static uint64_t        ktime_last_ns;

static void
ktime_u64_divmod_u32(uint64_t n, uint d, uint64_t *q_out, uint *r_out)
{
  uint64_t q;
  uint64_t r;
  int i;

  q = 0;
  r = 0;
  if(d == 0) {
    if(q_out)
      *q_out = 0;
    if(r_out)
      *r_out = 0;
    return;
  }

  for(i = 63; i >= 0; i--) {
    r = (r << 1) | ((n >> i) & 1ULL);
    if(r >= (uint64_t)d) {
      r -= (uint64_t)d;
      q |= (1ULL << i);
    }
  }

  if(q_out)
    *q_out = q;
  if(r_out)
    *r_out = (uint)r;
}

void
ktime_init(void)
{
  initlock(&ktime_lock, "ktime");
  ktime_ticks = 0;
  ktime_tsc_per_tick = 0;
  ktime_last_tick_tsc = 0;
  ktime_last_ns = 0;
}

void
ktime_tick(uint current_ticks)
{
  uint64_t now;
  uint64_t sample;
  uint64_t base_ns;

  now = rdtsc();

  acquire(&ktime_lock);
  if(ktime_last_tick_tsc != 0 && now > ktime_last_tick_tsc) {
    sample = now - ktime_last_tick_tsc;
    if(sample > 0 && sample <= 0xffffffffULL) {
      if(ktime_tsc_per_tick == 0)
        ktime_tsc_per_tick = (uint)sample;
      else
        ktime_tsc_per_tick = (uint)((((uint64_t)ktime_tsc_per_tick * 7ULL) + sample) >> 3);
    }
  }

  ktime_ticks = current_ticks;
  ktime_last_tick_tsc = now;
  base_ns = (uint64_t)current_ticks * KTIME_NSEC_PER_TICK;
  if(ktime_last_ns < base_ns)
    ktime_last_ns = base_ns;
  release(&ktime_lock);
}

void
ktime_get_monotonic(struct timespec *ts)
{
  uint64_t now;
  uint64_t base_ns;
  uint64_t ns;
  uint64_t delta_cycles;
  uint64_t scaled_ns;
  uint64_t delta_ns;
  uint64_t seconds;
  uint nanoseconds;

  if(ts == 0)
    return;

  now = rdtsc();

  acquire(&ktime_lock);
  base_ns = (uint64_t)ktime_ticks * KTIME_NSEC_PER_TICK;
  ns = base_ns;

  if(ktime_tsc_per_tick != 0 && ktime_last_tick_tsc != 0 && now > ktime_last_tick_tsc) {
    delta_cycles = now - ktime_last_tick_tsc;
    scaled_ns = delta_cycles * KTIME_NSEC_PER_TICK;
    ktime_u64_divmod_u32(scaled_ns, ktime_tsc_per_tick, &delta_ns, 0);
    if(delta_ns >= KTIME_NSEC_PER_TICK)
      delta_ns = KTIME_NSEC_PER_TICK - 1ULL;
    ns += delta_ns;
  }

  if(ns < ktime_last_ns)
    ns = ktime_last_ns;
  else
    ktime_last_ns = ns;

  ktime_u64_divmod_u32(ns, KTIME_NSEC_PER_SEC, &seconds, &nanoseconds);
  release(&ktime_lock);

  ts->tv_sec = (time_t)seconds;
  ts->tv_nsec = (long)nanoseconds;
}