/*
 * PCI modem discovery scaffold for auxv6.
 *
 * Scope of this tranche:
 * - Detect common modem-class devices and known vendor families.
 * - Provide per-vendor init/probe hooks for future driver expansion.
 * - Keep behavior strictly probe-only (no data path yet).
 *
 * References for structure and family coverage:
 * - Linux and BSD public driver trees for softmodem/controller modem IDs.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"

#define MODEM_STUB_MAX 32
#define MODEM_FAMILY_NAME_MAX 20

struct modem_stub_probe {
  char family[MODEM_FAMILY_NAME_MAX];
  ushort vendor_id;
  ushort device_id;
  uchar class_code;
  uchar subclass;
  uchar bus;
  uchar slot;
  uchar func;
  uchar irq_line;
};

static struct spinlock modem_lock;
static int modem_lock_ready;
static struct modem_stub_probe modem_stub_probes[MODEM_STUB_MAX];
static uint modem_stub_probe_count;

static void
modem_family_copy(char *dst, const char *src)
{
  uint i;

  if(!dst)
    return;
  if(!src)
    src = "unknown";

  for(i = 0; i + 1 < MODEM_FAMILY_NAME_MAX && src[i]; i++)
    dst[i] = src[i];
  dst[i] = 0;
}

static int
modem_buf_putc(char *buf, uint max, uint *len, char c)
{
  if(*len >= max)
    return -1;
  buf[*len] = c;
  (*len)++;
  return 0;
}

static int
modem_buf_puts(char *buf, uint max, uint *len, const char *s)
{
  uint i;

  if(!s)
    return modem_buf_puts(buf, max, len, "?");

  for(i = 0; s[i]; i++){
    if(modem_buf_putc(buf, max, len, s[i]) < 0)
      return -1;
  }
  return 0;
}

static uint
modem_write_uint(char *tmp, uint value)
{
  char rev[16];
  uint i;
  uint n;

  n = 0;
  do {
    rev[n++] = '0' + (value % 10);
    value /= 10;
  } while(value > 0);

  for(i = 0; i < n; i++)
    tmp[i] = rev[n - i - 1];
  return n;
}

static int
modem_buf_putu(char *buf, uint max, uint *len, uint v)
{
  char tmp[16];
  uint n;
  uint i;

  n = modem_write_uint(tmp, v);
  for(i = 0; i < n; i++){
    if(modem_buf_putc(buf, max, len, tmp[i]) < 0)
      return -1;
  }
  return 0;
}

static int
modem_buf_puthex16(char *buf, uint max, uint *len, ushort v)
{
  static const char hex[] = "0123456789abcdef";

  if(modem_buf_putc(buf, max, len, hex[(v >> 12) & 0xF]) < 0) return -1;
  if(modem_buf_putc(buf, max, len, hex[(v >> 8) & 0xF]) < 0) return -1;
  if(modem_buf_putc(buf, max, len, hex[(v >> 4) & 0xF]) < 0) return -1;
  if(modem_buf_putc(buf, max, len, hex[v & 0xF]) < 0) return -1;
  return 0;
}

int
modem_register_stub_probe(const char *family, struct pci_dev *dev)
{
  struct modem_stub_probe *entry;

  if(!dev)
    return -1;

  /* Keep parity with common PCI probe/attach sequences from BSD/Linux. */
  pci_enable_io(dev);
  pci_enable_mem(dev);
  pci_set_master(dev);

  if(!modem_lock_ready)
    return -1;

  acquire(&modem_lock);
  if(modem_stub_probe_count >= MODEM_STUB_MAX){
    release(&modem_lock);
    return -1;
  }

  entry = &modem_stub_probes[modem_stub_probe_count++];
  memset(entry, 0, sizeof(*entry));
  modem_family_copy(entry->family, family);
  entry->vendor_id = dev->vendor_id;
  entry->device_id = dev->device_id;
  entry->class_code = dev->class_code;
  entry->subclass = dev->subclass;
  entry->bus = dev->bus;
  entry->slot = dev->slot;
  entry->func = dev->func;
  entry->irq_line = dev->irq_line;
  release(&modem_lock);

  return 0;
}

int
modem_procfs_dump(char *buf, uint max)
{
  struct modem_stub_probe snap[MODEM_STUB_MAX];
  uint count;
  uint i;
  uint len;

  if(!buf || max == 0)
    return -1;

  if(!modem_lock_ready){
    if(max >= 1)
      buf[0] = 0;
    return 0;
  }

  acquire(&modem_lock);
  count = modem_stub_probe_count;
  if(count > MODEM_STUB_MAX)
    count = MODEM_STUB_MAX;
  memmove(snap, modem_stub_probes, sizeof(snap[0]) * count);
  release(&modem_lock);

  len = 0;
  if(modem_buf_puts(buf, max, &len,
                    "family ven:dev class/sub bus:slot.fn irq status\n") < 0)
    return -1;

  for(i = 0; i < count; i++){
    struct modem_stub_probe *m = &snap[i];

    if(modem_buf_puts(buf, max, &len, m->family) < 0) return -1;
    if(modem_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(modem_buf_puthex16(buf, max, &len, m->vendor_id) < 0) return -1;
    if(modem_buf_putc(buf, max, &len, ':') < 0) return -1;
    if(modem_buf_puthex16(buf, max, &len, m->device_id) < 0) return -1;
    if(modem_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(modem_buf_putu(buf, max, &len, m->class_code) < 0) return -1;
    if(modem_buf_putc(buf, max, &len, '/') < 0) return -1;
    if(modem_buf_putu(buf, max, &len, m->subclass) < 0) return -1;
    if(modem_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(modem_buf_putu(buf, max, &len, m->bus) < 0) return -1;
    if(modem_buf_putc(buf, max, &len, ':') < 0) return -1;
    if(modem_buf_putu(buf, max, &len, m->slot) < 0) return -1;
    if(modem_buf_putc(buf, max, &len, '.') < 0) return -1;
    if(modem_buf_putu(buf, max, &len, m->func) < 0) return -1;
    if(modem_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(modem_buf_putu(buf, max, &len, m->irq_line) < 0) return -1;
    if(modem_buf_puts(buf, max, &len, " probe-only\n") < 0) return -1;
  }

  if(modem_buf_puts(buf, max, &len, "summary total=") < 0) return -1;
  if(modem_buf_putu(buf, max, &len, count) < 0) return -1;
  if(modem_buf_puts(buf, max, &len, " datapath=0 probe_only=") < 0) return -1;
  if(modem_buf_putu(buf, max, &len, count) < 0) return -1;
  if(modem_buf_putc(buf, max, &len, '\n') < 0) return -1;

  return (int)len;
}

void
modem_init(void)
{
  if(!modem_lock_ready){
    initlock(&modem_lock, "modem");
    lockdep_set_rank(&modem_lock, LOCK_RANK_DEFAULT, "modem");
    modem_lock_ready = 1;
  }

  acquire(&modem_lock);
  modem_stub_probe_count = 0;
  release(&modem_lock);

  BOOTDBG("modem: probing PCI modem families (stub tranche)\n");
  conexant_hsf_init();
  agere_lt_init();
  smartlink_init();
  pctel_init();
  intel_softmodem_init();
  motorola_sm56_init();
}
