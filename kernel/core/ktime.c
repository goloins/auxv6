#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "date.h"
#include "sys/time.h"
#include "x86.h"

#define KTIME_NSEC_PER_SEC  1000000000U
#define KTIME_NSEC_PER_TICK 10000000ULL

static struct spinlock ktime_lock;
static uint            ktime_ticks;
static uint            ktime_tsc_per_tick;
static uint            ktime_hpet_period_fs;
static uint64_t        ktime_last_tick_tsc;
static uint64_t        ktime_last_tick_hpet;
static uint64_t        ktime_last_ns;
static int             ktime_realtime_ready;
static int             ktime_use_hpet;
static long long       ktime_realtime_offset_ns;

static int
ktime_is_leap_year(uint year)
{
  if((year % 4U) != 0U)
    return 0;
  if((year % 100U) != 0U)
    return 1;
  return (year % 400U) == 0U;
}

static int
ktime_rtc_to_epoch(const struct rtcdate *rtc, uint *out_sec)
{
  static const uint mdays[2][12] = {
    { 31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U },
    { 31U, 29U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U },
  };
  uint64_t days;
  uint year;
  uint month;

  if(rtc == 0 || out_sec == 0)
    return -1;
  if(rtc->year < 1970U)
    return -1;
  if(rtc->month < 1U || rtc->month > 12U)
    return -1;
  if(rtc->day < 1U || rtc->day > 31U)
    return -1;
  if(rtc->hour > 23U || rtc->minute > 59U || rtc->second > 59U)
    return -1;

  days = 0;
  for(year = 1970U; year < rtc->year; year++)
    days += ktime_is_leap_year(year) ? 366ULL : 365ULL;

  for(month = 1U; month < rtc->month; month++)
    days += (uint64_t)mdays[ktime_is_leap_year(rtc->year)][month - 1U];

  if(rtc->day > mdays[ktime_is_leap_year(rtc->year)][rtc->month - 1U])
    return -1;
  days += (uint64_t)(rtc->day - 1U);

  *out_sec = (uint)(days * 86400ULL +
                   (uint64_t)rtc->hour * 3600ULL +
                   (uint64_t)rtc->minute * 60ULL +
                   (uint64_t)rtc->second);
  return 0;
}

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

static uint64_t
ktime_monotonic_now_locked(uint64_t now)
{
  uint64_t base_ns;
  uint64_t ns;
  uint64_t delta_cycles;
  uint64_t scaled_ns;
  uint64_t delta_ns;

  base_ns = (uint64_t)ktime_ticks * KTIME_NSEC_PER_TICK;
  ns = base_ns;

  if(ktime_use_hpet) {
    if(ktime_hpet_period_fs != 0 && ktime_last_tick_hpet != 0 && now > ktime_last_tick_hpet) {
      delta_cycles = now - ktime_last_tick_hpet;
      scaled_ns = delta_cycles * (uint64_t)ktime_hpet_period_fs;
      ktime_u64_divmod_u32(scaled_ns, 1000000U, &delta_ns, 0);
      if(delta_ns >= KTIME_NSEC_PER_TICK)
        delta_ns = KTIME_NSEC_PER_TICK - 1ULL;
      ns += delta_ns;
    }
  } else if(ktime_tsc_per_tick != 0 && ktime_last_tick_tsc != 0 && now > ktime_last_tick_tsc) {
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

  return ns;
}

void
ktime_init(void)
{
  struct rtcdate rtc;
  uint rtc_sec;

  initlock(&ktime_lock, "ktime");
  lockdep_set_rank(&ktime_lock, LOCK_RANK_DEFAULT, "ktime");
  ktime_ticks = 0;
  ktime_tsc_per_tick = 0;
  ktime_hpet_period_fs = 0;
  ktime_last_tick_tsc = 0;
  ktime_last_tick_hpet = 0;
  ktime_last_ns = 0;
  ktime_realtime_ready = 0;
  ktime_use_hpet = 0;
  ktime_realtime_offset_ns = 0;

  cmostime(&rtc);
  if(ktime_rtc_to_epoch(&rtc, &rtc_sec) == 0) {
    ktime_realtime_ready = 1;
    ktime_realtime_offset_ns = (long long)((uint64_t)rtc_sec * KTIME_NSEC_PER_SEC);
  }
}

void
ktime_tick(uint current_ticks)
{
  uint64_t now;
  uint64_t sample;
  uint64_t base_ns;

  acquire(&ktime_lock);

  if(!ktime_use_hpet && hpet_available() && hpet_period_fs() != 0) {
    ktime_use_hpet = 1;
    ktime_hpet_period_fs = hpet_period_fs();
    ktime_last_tick_hpet = hpet_read_counter();
    BOOTDBG("ktime: clocksource=hpet period_fs=%u\n", ktime_hpet_period_fs);
  }

  if(ktime_use_hpet) {
    now = hpet_read_counter();
    ktime_last_tick_hpet = now;
  } else {
    now = rdtsc();
    if(ktime_last_tick_tsc != 0 && now > ktime_last_tick_tsc) {
      sample = now - ktime_last_tick_tsc;
      if(sample > 0 && sample <= 0xffffffffULL) {
        if(ktime_tsc_per_tick == 0)
          ktime_tsc_per_tick = (uint)sample;
        else
          ktime_tsc_per_tick = (uint)((((uint64_t)ktime_tsc_per_tick * 7ULL) + sample) >> 3);
      }
    }
    ktime_last_tick_tsc = now;
  }

  ktime_ticks = current_ticks;
  base_ns = (uint64_t)current_ticks * KTIME_NSEC_PER_TICK;
  if(ktime_last_ns < base_ns)
    ktime_last_ns = base_ns;
  release(&ktime_lock);
}

int
ktime_uses_hpet(void)
{
  int enabled;

  acquire(&ktime_lock);
  enabled = ktime_use_hpet;
  release(&ktime_lock);
  return enabled;
}

void
ktime_get_monotonic(struct timespec *ts)
{
  uint64_t now;
  uint64_t ns;
  uint64_t seconds;
  uint nanoseconds;

  if(ts == 0)
    return;

  acquire(&ktime_lock);
  now = ktime_use_hpet ? hpet_read_counter() : rdtsc();
  ns = ktime_monotonic_now_locked(now);

  ktime_u64_divmod_u32(ns, KTIME_NSEC_PER_SEC, &seconds, &nanoseconds);
  release(&ktime_lock);

  ts->tv_sec = (time_t)seconds;
  ts->tv_nsec = (long)nanoseconds;
}

void
ktime_get_realtime(struct timespec *ts)
{
  uint64_t now;
  uint64_t mono_ns;
  long long realtime_ns;
  uint64_t ns;
  uint64_t seconds;
  uint nanoseconds;

  if(ts == 0)
    return;

  acquire(&ktime_lock);
  now = ktime_use_hpet ? hpet_read_counter() : rdtsc();
  mono_ns = ktime_monotonic_now_locked(now);
  if(ktime_realtime_ready) {
    realtime_ns = (long long)mono_ns + ktime_realtime_offset_ns;
    if(realtime_ns < 0)
      ns = 0;
    else
      ns = (uint64_t)realtime_ns;
  } else {
    ns = mono_ns;
  }

  ktime_u64_divmod_u32(ns, KTIME_NSEC_PER_SEC, &seconds, &nanoseconds);
  release(&ktime_lock);

  ts->tv_sec = (time_t)seconds;
  ts->tv_nsec = (long)nanoseconds;
}

int
ktime_set_realtime(const struct timespec *ts)
{
  uint64_t now;
  uint64_t mono_ns;
  uint64_t target_ns;

  if(ts == 0)
    return -1;
  /* RFC 5280 compliance: reject timestamps outside [0, Y9999-12-31 23:59:59 UTC].
   * This ensures file/certificate timestamps remain representable without overflow.
   */
  if(ts->tv_sec < 0 || ts->tv_sec > 253402300799LL)
    return -1;
  if(ts->tv_nsec < 0 || ts->tv_nsec >= (long)KTIME_NSEC_PER_SEC)
    return -1;

  target_ns = (uint64_t)ts->tv_sec * KTIME_NSEC_PER_SEC + (uint64_t)ts->tv_nsec;

  acquire(&ktime_lock);
  now = ktime_use_hpet ? hpet_read_counter() : rdtsc();
  mono_ns = ktime_monotonic_now_locked(now);
  ktime_realtime_offset_ns = (long long)target_ns - (long long)mono_ns;
  ktime_realtime_ready = 1;
  release(&ktime_lock);

  return 0;
}