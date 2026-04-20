#include "types.h"
#include "defs.h"
#include "memlayout.h"
#include "traps.h"
#include "acpi.h"
#include "hpet.h"

#define HPET_GEN_CAP_ID        0x000
#define HPET_GEN_CONFIG        0x010
#define HPET_GEN_INT_STATUS    0x020
#define HPET_MAIN_COUNTER      0x0F0
#define HPET_TIMER_STRIDE      0x020
#define HPET_TIMER_CONFIG(n)   (0x100 + ((n) * HPET_TIMER_STRIDE))
#define HPET_TIMER_COMPARATOR(n) (0x108 + ((n) * HPET_TIMER_STRIDE))

#define HPET_CONF_ENABLE_CNF   0x001

#define HPET_TN_INT_TYPE_CNF   (1ULL << 1)
#define HPET_TN_INT_ENB_CNF    (1ULL << 2)
#define HPET_TN_TYPE_CNF       (1ULL << 3)
#define HPET_TN_PER_INT_CAP    (1ULL << 4)
#define HPET_TN_VAL_SET_CNF    (1ULL << 6)
#define HPET_TN_32MODE_CNF     (1ULL << 8)
#define HPET_TN_ROUTE_SHIFT    9
#define HPET_TN_ROUTE_MASK     (0x1FULL << HPET_TN_ROUTE_SHIFT)

static volatile uint *hpet_regs;
static struct acpi_hpet_info hpet_info;
static uint hpet_period_fs_cached;
static uint hpet_num_timers_cached;
static int hpet_counter_64bit_cached;
static int hpet_ready;
static int hpet_test_timer = -1;
static int hpet_test_irq = -1;
static uint hpet_test_irq_count;
static uint hpet_test_route_cap_cached;
static int hpet_test_armed;

static volatile uint*
hpet_map_regs(unsigned long long pa64)
{
  uint pa;

  if((pa64 >> 32) != 0)
    return 0;
  pa = (uint)pa64;

  if(pa >= DEVSPACE)
    return (volatile uint*)pa;
  if(pa >= PHYSTOP)
    return 0;
  return (volatile uint*)P2V(pa);
}

static uint
hpet_read32(uint reg)
{
  return hpet_regs[reg >> 2];
}

static void
hpet_write32(uint reg, uint value)
{
  hpet_regs[reg >> 2] = value;
  (void)hpet_regs[0];
}

static unsigned long long
hpet_read64_reg(uint reg)
{
  uint hi1;
  uint lo;
  uint hi2;

  do {
    hi1 = hpet_read32(reg + 4);
    lo = hpet_read32(reg + 0);
    hi2 = hpet_read32(reg + 4);
  } while(hi1 != hi2);

  return ((unsigned long long)hi2 << 32) | (unsigned long long)lo;
}

static void
hpet_write64_reg(uint reg, unsigned long long value)
{
  hpet_write32(reg + 0, (uint)value);
  hpet_write32(reg + 4, (uint)(value >> 32));
}

static int
hpet_irq_reserved(int irq)
{
  switch(irq){
  case IRQ_TIMER:
  case IRQ_KBD:
  case IRQ_COM1:
  case IRQ_MOUSE:
  case IRQ_IDE:
  case IRQ_IDE + 1:
  case IRQ_ERROR:
  case IRQ_SPURIOUS:
    return 1;
  }
  return 0;
}

static int
hpet_pick_irq(uint route_cap)
{
  int irq;
  for(irq = 16; irq < 32; irq++){
    if((route_cap & (1U << irq)) == 0 || hpet_irq_reserved(irq))
      continue;
    if(irq == 2)
      continue;
    return irq;
  }
  for(irq = 0; irq < 32; irq++){
    if((route_cap & (1U << irq)) == 0 || hpet_irq_reserved(irq))
      continue;
    if(irq == 2)
      continue;
    return irq;
  }
  return -1;
}

static unsigned long long
hpet_calc_interval(uint freq_hz)
{
  unsigned long long denom;

  if(freq_hz == 0 || hpet_period_fs_cached == 0)
    return 0ULL;
  denom = (unsigned long long)hpet_period_fs_cached * (unsigned long long)freq_hz;
  if(denom == 0)
    return 0ULL;
  return 1000000000000000ULL / denom;
}

static void
hpet_irq_handler(int irq, void *arg)
{
  int timer;
  uint mask;
  uint status;

  timer = (int)(uint)arg;
  if(timer < 0 || timer >= 32)
    return;

  mask = 1U << timer;
  status = hpet_read32(HPET_GEN_INT_STATUS);
  if(status & mask)
    hpet_write32(HPET_GEN_INT_STATUS, mask);

  /* For edge-triggered HPET delivery the status bit may remain clear, but the
   * IRQ has still arrived at the CPU. Count the interrupt unconditionally. */
  hpet_test_irq_count++;
  if((hpet_test_irq_count & 0x3fU) == 1)
    BOOTDBG("hpet: periodic irq=%d timer=%d count=%u status=%x\n",
            irq, timer, hpet_test_irq_count, status);
}

int
hpet_init(void)
{
  unsigned long long caps;
  unsigned long long conf;

  if(hpet_ready)
    return 0;

  if(acpi_get_hpet_info(&hpet_info) < 0)
    return -1;

  hpet_regs = hpet_map_regs(hpet_info.address);
  if(hpet_regs == 0)
    return -1;

  caps = hpet_read64_reg(HPET_GEN_CAP_ID);
  hpet_period_fs_cached = (uint)(caps >> 32);
  hpet_num_timers_cached = (uint)(((caps >> 8) & 0x1f) + 1ULL);
  hpet_counter_64bit_cached = (caps & (1ULL << 13)) ? 1 : 0;

  if(hpet_period_fs_cached == 0)
    return -1;

  conf = hpet_read64_reg(HPET_GEN_CONFIG);
  hpet_write64_reg(HPET_GEN_CONFIG, conf & ~((unsigned long long)HPET_CONF_ENABLE_CNF));
  hpet_write64_reg(HPET_MAIN_COUNTER, 0ULL);
  hpet_write64_reg(HPET_GEN_CONFIG, conf | (unsigned long long)HPET_CONF_ENABLE_CNF);

  hpet_ready = 1;

  BOOTDBG("hpet: base=%x period_fs=%u timers=%u 64bit=%d legacy=%d\n",
          (uint)hpet_info.address,
          hpet_period_fs_cached,
          hpet_num_timers_cached,
          hpet_counter_64bit_cached,
          hpet_info.legacy_replacement);

  return 0;
}

int
hpet_available(void)
{
  return hpet_ready;
}

int
hpet_start_periodic_test(uint freq_hz)
{
  uint i;

  if(!hpet_ready)
    return -1;
  if(hpet_test_armed)
    return 0;

  for(i = 0; i < hpet_num_timers_cached; i++){
    unsigned long long cfg;
    unsigned long long now;
    unsigned long long interval;
    int irq;
    uint route_cap;

    cfg = hpet_read64_reg(HPET_TIMER_CONFIG(i));
    if((cfg & HPET_TN_PER_INT_CAP) == 0)
      continue;

    route_cap = (uint)(cfg >> 32);
    irq = hpet_pick_irq(route_cap);
    if(irq < 0)
      continue;

    if(irq_register(irq, hpet_irq_handler, (void*)i, "hpet") < 0)
      continue;

    interval = hpet_calc_interval(freq_hz);
    if(interval == 0ULL){
      irq_unregister(irq, "hpet");
      return -1;
    }

    now = hpet_read_counter();
    cfg &= ~(HPET_TN_INT_TYPE_CNF | HPET_TN_INT_ENB_CNF |
             HPET_TN_TYPE_CNF | HPET_TN_VAL_SET_CNF |
             HPET_TN_32MODE_CNF | HPET_TN_ROUTE_MASK);
    if(!hpet_counter_64bit_cached)
      cfg |= HPET_TN_32MODE_CNF;
    cfg |= HPET_TN_TYPE_CNF | HPET_TN_VAL_SET_CNF | HPET_TN_INT_ENB_CNF;
    cfg |= ((unsigned long long)(irq & 0x1f) << HPET_TN_ROUTE_SHIFT);
    hpet_write64_reg(HPET_TIMER_CONFIG(i), cfg);
    hpet_write32(HPET_GEN_INT_STATUS, 1U << i);
    hpet_write64_reg(HPET_TIMER_COMPARATOR(i), now + interval);
    hpet_write64_reg(HPET_TIMER_COMPARATOR(i), interval);
    ioapicenable(irq, 0);

    hpet_test_timer = (int)i;
    hpet_test_irq = irq;
    hpet_test_irq_count = 0;
    hpet_test_route_cap_cached = route_cap;
    hpet_test_armed = 1;

    BOOTDBG("hpet: armed test timer=%d irq=%d hz=%u\n", hpet_test_timer, hpet_test_irq, freq_hz);
    return 0;
  }

  return -1;
}

unsigned long long
hpet_read_counter(void)
{
  if(!hpet_ready)
    return 0ULL;
  return hpet_read64_reg(HPET_MAIN_COUNTER);
}

uint
hpet_period_fs(void)
{
  return hpet_period_fs_cached;
}

uint
hpet_num_timers(void)
{
  return hpet_num_timers_cached;
}

uint
hpet_irq_count(void)
{
  return hpet_test_irq_count;
}

int
hpet_irq_line(void)
{
  return hpet_test_irq;
}

uint
hpet_test_route_cap(void)
{
  return hpet_test_route_cap_cached;
}

int
hpet_test_timer_index(void)
{
  return hpet_test_timer;
}

int
hpet_test_enabled(void)
{
  return hpet_test_armed;
}

int
hpet_counter_is_64bit(void)
{
  return hpet_counter_64bit_cached;
}

void
hpet_stop(void)
{
  unsigned long long conf;
  unsigned long long cfg;

  if(!hpet_ready)
    return;

  if(hpet_test_armed && hpet_test_timer >= 0){
    cfg = hpet_read64_reg(HPET_TIMER_CONFIG(hpet_test_timer));
    cfg &= ~HPET_TN_INT_ENB_CNF;
    hpet_write64_reg(HPET_TIMER_CONFIG(hpet_test_timer), cfg);
    hpet_write32(HPET_GEN_INT_STATUS, 1U << hpet_test_timer);
    hpet_test_armed = 0;
  }

  conf = hpet_read64_reg(HPET_GEN_CONFIG);
  hpet_write64_reg(HPET_GEN_CONFIG, conf & ~((unsigned long long)HPET_CONF_ENABLE_CNF));
}
