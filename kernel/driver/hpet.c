#include "types.h"
#include "defs.h"
#include "memlayout.h"
#include "acpi.h"
#include "hpet.h"

#define HPET_GEN_CAP_ID       0x000
#define HPET_GEN_CONFIG       0x010
#define HPET_MAIN_COUNTER     0x0F0

#define HPET_CONF_ENABLE_CNF  0x001

static volatile uint *hpet_regs;
static struct acpi_hpet_info hpet_info;
static uint hpet_period_fs_cached;
static uint hpet_num_timers_cached;
static int hpet_counter_64bit_cached;
static int hpet_ready;

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

int
hpet_counter_is_64bit(void)
{
  return hpet_counter_64bit_cached;
}

void
hpet_stop(void)
{
  unsigned long long conf;

  if(!hpet_ready)
    return;

  conf = hpet_read64_reg(HPET_GEN_CONFIG);
  hpet_write64_reg(HPET_GEN_CONFIG, conf & ~((unsigned long long)HPET_CONF_ENABLE_CNF));
}
