/*
 * PCI Thunderbolt/USB4 host-router scaffold for auxv6.
 *
 * Scope:
 * - Discover PCI Thunderbolt-class controllers.
 * - Capture stable probe metadata and minimal capability hints.
 * - Expose discovered controllers through /proc/thunderbolt.
 *
 * This tranche does not implement tunnel bring-up, hotplug state machines,
 * security levels, DMA guard policy, or retimer management.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"

#define THUNDERBOLT_STUB_MAX 16

/* PCI serial-bus subclass used for Thunderbolt host controllers. */
#define PCI_SUBCLASS_SERIAL_THUNDERBOLT 0x0c

#define TBT_PHASE_INIT     0
#define TBT_PHASE_READY    1
#define TBT_PHASE_DEGRADED 2

struct thunderbolt_probe {
  ushort vendor_id;
  ushort device_id;
  uchar class_code;
  uchar subclass;
  uchar prog_if;
  uchar bus;
  uchar slot;
  uchar func;
  uchar irq_line;
  uint bar0;
  uint bar0_size;
  uchar bar_mapped;
  uchar busmaster_ok;
  uchar phase;
  uchar init_failures;
  uint probe_attempts;
  uint probe_successes;
  uint probe_failures;
  uint flags;
};

static struct spinlock thunderbolt_lock;
static int thunderbolt_lock_ready;
static struct thunderbolt_probe thunderbolt_probes[THUNDERBOLT_STUB_MAX];
static uint thunderbolt_probe_count;

static int
thunderbolt_buf_putc(char *buf, uint max, uint *len, char c)
{
  if(*len >= max)
    return -1;
  buf[*len] = c;
  (*len)++;
  return 0;
}

static int
thunderbolt_buf_puts(char *buf, uint max, uint *len, const char *s)
{
  uint i;

  if(!s)
    return thunderbolt_buf_puts(buf, max, len, "?");

  for(i = 0; s[i]; i++){
    if(thunderbolt_buf_putc(buf, max, len, s[i]) < 0)
      return -1;
  }
  return 0;
}

static int
thunderbolt_buf_putu(char *buf, uint max, uint *len, uint v)
{
  char tmp[16];
  uint n;
  uint i;

  n = 0;
  do {
    tmp[n++] = (char)('0' + (v % 10));
    v /= 10;
  } while(v > 0);

  for(i = 0; i < n; i++){
    if(thunderbolt_buf_putc(buf, max, len, tmp[n - i - 1]) < 0)
      return -1;
  }
  return 0;
}

static int
thunderbolt_buf_puthex16(char *buf, uint max, uint *len, ushort v)
{
  static const char hex[] = "0123456789abcdef";

  if(thunderbolt_buf_putc(buf, max, len, hex[(v >> 12) & 0xf]) < 0) return -1;
  if(thunderbolt_buf_putc(buf, max, len, hex[(v >> 8) & 0xf]) < 0) return -1;
  if(thunderbolt_buf_putc(buf, max, len, hex[(v >> 4) & 0xf]) < 0) return -1;
  if(thunderbolt_buf_putc(buf, max, len, hex[v & 0xf]) < 0) return -1;
  return 0;
}

static int
thunderbolt_buf_puthex32(char *buf, uint max, uint *len, uint v)
{
  static const char hex[] = "0123456789abcdef";
  int shift;

  for(shift = 28; shift >= 0; shift -= 4){
    if(thunderbolt_buf_putc(buf, max, len, hex[(v >> shift) & 0xf]) < 0)
      return -1;
  }
  return 0;
}

static int
thunderbolt_is_match(struct pci_dev *dev)
{
  if(!dev)
    return 0;
  if(dev->class_code != PCI_CLASS_SERIAL)
    return 0;
  if(dev->subclass != PCI_SUBCLASS_SERIAL_THUNDERBOLT)
    return 0;
  return 1;
}

static const char*
thunderbolt_phase_name(uchar phase)
{
  switch(phase){
  case TBT_PHASE_INIT:
    return "init";
  case TBT_PHASE_READY:
    return "ready";
  case TBT_PHASE_DEGRADED:
    return "degraded";
  default:
    return "unknown";
  }
}

static int
thunderbolt_attach_probe(struct pci_dev *dev)
{
  struct thunderbolt_probe *sc;
  volatile uint *regs;

  acquire(&thunderbolt_lock);
  if(thunderbolt_probe_count >= THUNDERBOLT_STUB_MAX){
    release(&thunderbolt_lock);
    return -1;
  }

  sc = &thunderbolt_probes[thunderbolt_probe_count++];
  memset(sc, 0, sizeof(*sc));
  sc->vendor_id = dev->vendor_id;
  sc->device_id = dev->device_id;
  sc->class_code = dev->class_code;
  sc->subclass = dev->subclass;
  sc->prog_if = dev->prog_if;
  sc->bus = dev->bus;
  sc->slot = dev->slot;
  sc->func = dev->func;
  sc->irq_line = dev->irq_line;
  sc->bar0 = pci_bar_base(dev, 0);
  sc->bar0_size = pci_bar_size(dev, 0);
  sc->phase = TBT_PHASE_INIT;
  sc->probe_attempts = 1;

  regs = 0;
  if(sc->bar0 && sc->bar0_size && (dev->bar[0] & PCI_BAR_IO) == 0)
    regs = (volatile uint*)pci_map_bar(dev, 0);
  if(regs)
    sc->bar_mapped = 1;

  pci_set_master(dev);
  sc->busmaster_ok = 1;

  if(sc->bar_mapped){
    sc->phase = TBT_PHASE_READY;
    sc->probe_successes = 1;
    sc->flags = 1U; /* mmio-visible */
  } else {
    sc->phase = TBT_PHASE_DEGRADED;
    sc->probe_failures = 1;
    sc->init_failures = 1;
  }

  release(&thunderbolt_lock);
  return 0;
}

int
thunderbolt_procfs_dump(char *buf, uint max)
{
  struct thunderbolt_probe snap[THUNDERBOLT_STUB_MAX];
  uint count;
  uint len;
  uint i;

  len = 0;
  acquire(&thunderbolt_lock);
  count = thunderbolt_probe_count;
  if(count > THUNDERBOLT_STUB_MAX)
    count = THUNDERBOLT_STUB_MAX;
  for(i = 0; i < count; i++)
    snap[i] = thunderbolt_probes[i];
  release(&thunderbolt_lock);

  if(thunderbolt_buf_puts(buf, max, &len,
                          "# Thunderbolt/USB4 host-router scaffold\n") < 0)
    return -1;
  if(thunderbolt_buf_puts(buf, max, &len,
                          "# vendor:device bus:slot.fn phase bar0 irq flags\n") < 0)
    return -1;

  for(i = 0; i < count; i++){
    struct thunderbolt_probe *p;

    p = &snap[i];
    if(thunderbolt_buf_puts(buf, max, &len, "dev ") < 0) return -1;
    if(thunderbolt_buf_puthex16(buf, max, &len, p->vendor_id) < 0) return -1;
    if(thunderbolt_buf_putc(buf, max, &len, ':') < 0) return -1;
    if(thunderbolt_buf_puthex16(buf, max, &len, p->device_id) < 0) return -1;
    if(thunderbolt_buf_puts(buf, max, &len, " bus=") < 0) return -1;
    if(thunderbolt_buf_putu(buf, max, &len, p->bus) < 0) return -1;
    if(thunderbolt_buf_putc(buf, max, &len, ':') < 0) return -1;
    if(thunderbolt_buf_putu(buf, max, &len, p->slot) < 0) return -1;
    if(thunderbolt_buf_putc(buf, max, &len, '.') < 0) return -1;
    if(thunderbolt_buf_putu(buf, max, &len, p->func) < 0) return -1;
    if(thunderbolt_buf_puts(buf, max, &len, " phase=") < 0) return -1;
    if(thunderbolt_buf_puts(buf, max, &len, thunderbolt_phase_name(p->phase)) < 0) return -1;
    if(thunderbolt_buf_puts(buf, max, &len, " bar0=0x") < 0) return -1;
    if(thunderbolt_buf_puthex32(buf, max, &len, p->bar0) < 0) return -1;
    if(thunderbolt_buf_puts(buf, max, &len, " irq=") < 0) return -1;
    if(thunderbolt_buf_putu(buf, max, &len, p->irq_line) < 0) return -1;
    if(thunderbolt_buf_puts(buf, max, &len, " flags=") < 0) return -1;
    if(thunderbolt_buf_putu(buf, max, &len, p->flags) < 0) return -1;
    if(thunderbolt_buf_putc(buf, max, &len, '\n') < 0) return -1;
  }

  if(thunderbolt_buf_puts(buf, max, &len, "summary total=") < 0) return -1;
  if(thunderbolt_buf_putu(buf, max, &len, count) < 0) return -1;
  if(thunderbolt_buf_puts(buf, max, &len,
                          " tunnels=0 hotplug=0 security=0\n") < 0)
    return -1;

  return (int)len;
}

void
thunderbolt_init(void)
{
  int i;
  uint found;

  if(!thunderbolt_lock_ready){
    initlock(&thunderbolt_lock, "thunderbolt");
    lockdep_set_rank(&thunderbolt_lock, LOCK_RANK_DEFAULT, "thunderbolt");
    thunderbolt_lock_ready = 1;
  }

  acquire(&thunderbolt_lock);
  thunderbolt_probe_count = 0;
  release(&thunderbolt_lock);

  found = 0;
  BOOTDBG("thunderbolt: probing host routers\n");
  for(i = 0; i < pci_device_count(); i++){
    struct pci_dev *dev;

    dev = pci_get_device(i);
    if(!thunderbolt_is_match(dev))
      continue;
    if(thunderbolt_attach_probe(dev) == 0)
      found++;
  }
  BOOTDBG("thunderbolt: discovered %d controller(s)\n", found);
}
