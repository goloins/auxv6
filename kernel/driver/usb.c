/*
 * PCI USB host-controller discovery scaffold for auxv6.
 *
 * Scope of this tranche:
 * - Detect PCI USB controllers (UHCI/OHCI/EHCI/xHCI + unknown prog-if).
 * - Capture stable controller metadata for follow-on driver work.
 * - Expose the discovery snapshot through /proc/usb.
 *
 * This intentionally does not program host-controller operational registers yet.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "usb_hcd.h"

static struct spinlock usb_lock;
static struct usb_hc_probe usb_hc[USB_HC_MAX];
static uint usb_hc_count;

#define USB_DEV_CAND_MAX      64
#define USB_DEV_SPEED_UNKNOWN 0
#define USB_DEV_SPEED_LOW     1
#define USB_DEV_SPEED_FULL    2
#define USB_DEV_SPEED_HIGH    3
#define USB_DEV_SPEED_SUPER   4

struct usb_dev_candidate {
  uchar hc_index;
  uchar kind;
  uchar port;
  uchar present;
  uchar enabled;
  uchar speed;
};

static struct usb_dev_candidate usb_dev[USB_DEV_CAND_MAX];
static uint usb_dev_count;

static const struct usb_hc_ops usb_hc_ops_unknown = {
  .name = "unknown",
  .probe_regs = 0,
  .reset = 0,
  .halt = 0,
  .start = 0,
  .scan_ports = 0,
  .service_ports = 0,
};

static const struct usb_hc_ops usb_hc_ops_uhci = {
  .name = "uhci",
  .probe_regs = usb_probe_uhci_regs,
  .reset = usb_uhci_reset,
  .halt = usb_uhci_halt,
  .start = usb_uhci_start,
  .scan_ports = usb_uhci_scan_ports,
  .service_ports = usb_uhci_service_ports,
};

static const struct usb_hc_ops usb_hc_ops_ohci = {
  .name = "ohci",
  .probe_regs = usb_probe_ohci_regs,
  .reset = usb_ohci_reset,
  .halt = usb_ohci_halt,
  .start = usb_ohci_start,
  .scan_ports = usb_ohci_scan_ports,
  .service_ports = usb_ohci_service_ports,
};

static const struct usb_hc_ops usb_hc_ops_ehci = {
  .name = "ehci",
  .probe_regs = usb_probe_ehci_regs,
  .reset = usb_ehci_reset,
  .halt = usb_ehci_halt,
  .start = usb_ehci_start,
  .scan_ports = usb_ehci_scan_ports,
  .service_ports = usb_ehci_service_ports,
};

static const struct usb_hc_ops usb_hc_ops_xhci = {
  .name = "xhci",
  .probe_regs = usb_probe_xhci_regs,
  .reset = usb_xhci_reset,
  .halt = usb_xhci_halt,
  .start = usb_xhci_start,
  .scan_ports = usb_xhci_scan_ports,
  .service_ports = usb_xhci_service_ports,
};

static int
usb_buf_putc(char *buf, uint max, uint *len, char c)
{
  if(*len >= max)
    return -1;
  buf[*len] = c;
  (*len)++;
  return 0;
}

static int
usb_buf_puts(char *buf, uint max, uint *len, const char *s)
{
  uint i;

  for(i = 0; s[i]; i++){
    if(usb_buf_putc(buf, max, len, s[i]) < 0)
      return -1;
  }
  return 0;
}

static uint
usb_write_uint(char *tmp, uint value)
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
usb_buf_putu(char *buf, uint max, uint *len, uint v)
{
  char tmp[16];
  uint n;
  uint i;

  n = usb_write_uint(tmp, v);
  for(i = 0; i < n; i++){
    if(usb_buf_putc(buf, max, len, tmp[i]) < 0)
      return -1;
  }
  return 0;
}

static int
usb_buf_puthex8(char *buf, uint max, uint *len, uchar v)
{
  static const char hex[] = "0123456789abcdef";

  if(usb_buf_putc(buf, max, len, hex[(v >> 4) & 0xF]) < 0) return -1;
  if(usb_buf_putc(buf, max, len, hex[v & 0xF]) < 0) return -1;
  return 0;
}

static int
usb_buf_puthex16(char *buf, uint max, uint *len, ushort v)
{
  static const char hex[] = "0123456789abcdef";

  if(usb_buf_putc(buf, max, len, hex[(v >> 12) & 0xF]) < 0) return -1;
  if(usb_buf_putc(buf, max, len, hex[(v >> 8) & 0xF]) < 0) return -1;
  if(usb_buf_putc(buf, max, len, hex[(v >> 4) & 0xF]) < 0) return -1;
  if(usb_buf_putc(buf, max, len, hex[v & 0xF]) < 0) return -1;
  return 0;
}

static int
usb_buf_puthex32(char *buf, uint max, uint *len, uint v)
{
  static const char hex[] = "0123456789abcdef";
  int shift;

  for(shift = 28; shift >= 0; shift -= 4){
    if(usb_buf_putc(buf, max, len, hex[(v >> shift) & 0xF]) < 0)
      return -1;
  }
  return 0;
}

static uchar
usb_hc_kind_from_prog_if(uchar prog_if)
{
  switch(prog_if){
  case PCI_PROGIF_USB_UHCI:
    return USB_HC_KIND_UHCI;
  case PCI_PROGIF_USB_OHCI:
    return USB_HC_KIND_OHCI;
  case PCI_PROGIF_USB_EHCI:
    return USB_HC_KIND_EHCI;
  case PCI_PROGIF_USB_XHCI:
    return USB_HC_KIND_XHCI;
  default:
    return USB_HC_KIND_UNKNOWN;
  }
}

static const char*
usb_hc_kind_name(uchar kind)
{
  const struct usb_hc_ops *ops;

  switch(kind){
  case USB_HC_KIND_UHCI:
    ops = &usb_hc_ops_uhci;
    break;
  case USB_HC_KIND_OHCI:
    ops = &usb_hc_ops_ohci;
    break;
  case USB_HC_KIND_EHCI:
    ops = &usb_hc_ops_ehci;
    break;
  case USB_HC_KIND_XHCI:
    ops = &usb_hc_ops_xhci;
    break;
  default:
    ops = &usb_hc_ops_unknown;
    break;
  }

  return ops->name;
}

static const char*
usb_hc_phase_name(uchar phase)
{
  switch(phase){
  case USB_HC_PHASE_INIT:
    return "init";
  case USB_HC_PHASE_READY:
    return "ready";
  case USB_HC_PHASE_DEGRADED:
    return "degraded";
  default:
    return "unknown";
  }
}

static const char*
usb_dev_speed_name(uchar speed)
{
  switch(speed){
  case USB_DEV_SPEED_LOW:
    return "low";
  case USB_DEV_SPEED_FULL:
    return "full";
  case USB_DEV_SPEED_HIGH:
    return "high";
  case USB_DEV_SPEED_SUPER:
    return "super";
  case USB_DEV_SPEED_UNKNOWN:
  default:
    return "unknown";
  }
}

static void
usb_collect_candidates(uint hc_index, struct usb_hc_probe *sc)
{
  uint n;

  if(!sc || !sc->rh_present)
    return;

  for(n = 0; n < sc->rh_ports && n < 32; n++){
    struct usb_dev_candidate *dc;
    uint mask;
    uint present;
    uint enabled;

    if(usb_dev_count >= USB_DEV_CAND_MAX)
      break;

    mask = (1U << n);
    present = (sc->rh_connect_bits & mask) ? 1 : 0;
    enabled = (sc->rh_enabled_bits & mask) ? 1 : 0;
    if(!present && !enabled)
      continue;

    dc = &usb_dev[usb_dev_count++];
    memset(dc, 0, sizeof(*dc));
    dc->hc_index = (uchar)hc_index;
    dc->kind = sc->kind;
    dc->port = (uchar)(n + 1);
    dc->present = (uchar)present;
    dc->enabled = (uchar)enabled;
    if(sc->rh_super_bits & mask)
      dc->speed = USB_DEV_SPEED_SUPER;
    else if(sc->rh_high_bits & mask)
      dc->speed = USB_DEV_SPEED_HIGH;
    else if(sc->rh_full_bits & mask)
      dc->speed = USB_DEV_SPEED_FULL;
    else if(sc->rh_low_bits & mask)
      dc->speed = USB_DEV_SPEED_LOW;
    else
      dc->speed = USB_DEV_SPEED_UNKNOWN;
  }
}

static const char*
usb_hc_error_name(uint err)
{
  switch(err){
  case USB_HC_ERR_NONE:
    return "none";
  case USB_HC_ERR_NO_BACKEND:
    return "no_backend";
  case USB_HC_ERR_PROBE:
    return "probe";
  case USB_HC_ERR_RESET:
    return "reset";
  case USB_HC_ERR_HALT:
    return "halt";
  case USB_HC_ERR_START:
    return "start";
  default:
    return "unknown";
  }
}

static int
usb_is_match(struct pci_dev *dev)
{
  if(!dev)
    return 0;
  if(dev->class_code != PCI_CLASS_SERIAL)
    return 0;
  if(dev->subclass != PCI_SUBCLASS_SERIAL_USB)
    return 0;
  return 1;
}

static const struct usb_hc_ops*
usb_get_ops(uchar kind)
{
  switch(kind){
  case USB_HC_KIND_UHCI:
    return &usb_hc_ops_uhci;
  case USB_HC_KIND_OHCI:
    return &usb_hc_ops_ohci;
  case USB_HC_KIND_EHCI:
    return &usb_hc_ops_ehci;
  case USB_HC_KIND_XHCI:
    return &usb_hc_ops_xhci;
  default:
    return &usb_hc_ops_unknown;
  }
}

void
usb_init(void)
{
  int i;
  int ndev;

  initlock(&usb_lock, "usb");

  acquire(&usb_lock);
  memset(usb_hc, 0, sizeof(usb_hc));
  memset(usb_dev, 0, sizeof(usb_dev));
  usb_hc_count = 0;
  usb_dev_count = 0;

  ndev = pci_device_count();
  for(i = 0; i < ndev; i++){
    struct pci_dev *dev;
    struct usb_hc_probe *sc;
    const struct usb_hc_ops *ops;

    dev = pci_get_device(i);
    if(!usb_is_match(dev))
      continue;
    if(usb_hc_count >= USB_HC_MAX)
      break;

    sc = &usb_hc[usb_hc_count++];
    memset(sc, 0, sizeof(*sc));

    sc->vendor_id = dev->vendor_id;
    sc->device_id = dev->device_id;
    sc->bus = dev->bus;
    sc->slot = dev->slot;
    sc->func = dev->func;
    sc->class_code = dev->class_code;
    sc->subclass = dev->subclass;
    sc->prog_if = dev->prog_if;
    sc->irq_line = dev->irq_line;
    sc->kind = usb_hc_kind_from_prog_if(dev->prog_if);
    sc->phase = USB_HC_PHASE_INIT;
    sc->bar0 = pci_bar_base(dev, 0);
    sc->bar0_size = pci_bar_size(dev, 0);
    sc->bar0_is_io = (dev->bar[0] & PCI_BAR_IO) ? 1 : 0;
    sc->last_error = USB_HC_ERR_NONE;

    ops = usb_get_ops(sc->kind);
    if(!ops->probe_regs || !ops->reset || !ops->halt || !ops->start){
      sc->phase = USB_HC_PHASE_DEGRADED;
      sc->last_error = USB_HC_ERR_NO_BACKEND;
      sc->init_failures++;
    } else {
      sc->probe_attempts++;
      if(ops->probe_regs(sc, dev) < 0){
        sc->probe_failures++;
        sc->phase = USB_HC_PHASE_DEGRADED;
        sc->last_error = USB_HC_ERR_PROBE;
        sc->init_failures++;
      } else {
        sc->probe_successes++;

        sc->reset_attempts++;
        if(ops->reset(sc, dev) < 0){
          sc->reset_failures++;
          sc->phase = USB_HC_PHASE_DEGRADED;
          sc->last_error = USB_HC_ERR_RESET;
          sc->init_failures++;
        } else {
          sc->reset_successes++;

          sc->halt_attempts++;
          if(ops->halt(sc, dev) < 0){
            sc->halt_failures++;
            sc->phase = USB_HC_PHASE_DEGRADED;
            sc->last_error = USB_HC_ERR_HALT;
            sc->init_failures++;
          } else {
            sc->halt_successes++;

            sc->start_attempts++;
            if(ops->start(sc, dev) < 0){
              sc->start_failures++;
              sc->phase = USB_HC_PHASE_DEGRADED;
              sc->last_error = USB_HC_ERR_START;
              sc->init_failures++;
            } else {
              sc->start_successes++;
              if(ops->scan_ports){
                sc->scan_attempts++;
                if(ops->scan_ports(sc, dev) < 0)
                  sc->scan_failures++;
                else
                  sc->scan_successes++;
              }
              if(ops->service_ports){
                sc->service_attempts++;
                if(ops->service_ports(sc, dev) < 0)
                  sc->service_failures++;
                else
                  sc->service_successes++;
              }
              usb_collect_candidates(usb_hc_count - 1, sc);
            }
          }
        }
      }
    }

    BOOTDBG("usb: found %d:%d.%d %x:%x progif=%x kind=%s phase=%s err=%s irq=%d bar0=%x size=%x\n",
            sc->bus, sc->slot, sc->func,
            sc->vendor_id, sc->device_id,
            sc->prog_if, usb_hc_kind_name(sc->kind), usb_hc_phase_name(sc->phase),
            usb_hc_error_name(sc->last_error),
            sc->irq_line, sc->bar0, sc->bar0_size);
  }

  BOOTDBG("usb: host-controller scaffold discovered %d controller(s)\n", usb_hc_count);
  release(&usb_lock);
}

int
usb_procfs_dump(char *buf, uint max)
{
  uint len;
  uint i;
  uint uhci;
  uint ohci;
  uint ehci;
  uint xhci;
  uint unknown;
  uint ready;
  uint degraded;

  if(!buf || max == 0)
    return -1;

  acquire(&usb_lock);

  len = 0;
  uhci = 0;
  ohci = 0;
  ehci = 0;
  xhci = 0;
  unknown = 0;
  ready = 0;
  degraded = 0;

  for(i = 0; i < usb_hc_count; i++){
    switch(usb_hc[i].kind){
    case USB_HC_KIND_UHCI:
      uhci++;
      break;
    case USB_HC_KIND_OHCI:
      ohci++;
      break;
    case USB_HC_KIND_EHCI:
      ehci++;
      break;
    case USB_HC_KIND_XHCI:
      xhci++;
      break;
    default:
      unknown++;
      break;
    }
    if(usb_hc[i].phase == USB_HC_PHASE_READY)
      ready++;
    else if(usb_hc[i].phase == USB_HC_PHASE_DEGRADED)
      degraded++;
  }

  if(usb_buf_puts(buf, max, &len, "usb_hc_count ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, usb_hc_count) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "uhci ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, uhci) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "ohci ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, ohci) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "ehci ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, ehci) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "xhci ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, xhci) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "unknown ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, unknown) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "ready ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, ready) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "degraded ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, degraded) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "usb_dev_count ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, usb_dev_count) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  for(i = 0; i < usb_hc_count; i++){
    struct usb_hc_probe *sc = &usb_hc[i];

    if(usb_buf_puts(buf, max, &len, "hc") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, i) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " bus=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->bus) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " slot=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->slot) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " func=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->func) < 0) goto out;

    if(usb_buf_puts(buf, max, &len, " vendor=0x") < 0) goto out;
    if(usb_buf_puthex16(buf, max, &len, sc->vendor_id) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " device=0x") < 0) goto out;
    if(usb_buf_puthex16(buf, max, &len, sc->device_id) < 0) goto out;

    if(usb_buf_puts(buf, max, &len, " prog_if=0x") < 0) goto out;
    if(usb_buf_puthex8(buf, max, &len, sc->prog_if) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " kind=") < 0) goto out;
    if(usb_buf_puts(buf, max, &len, usb_hc_kind_name(sc->kind)) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " phase=") < 0) goto out;
    if(usb_buf_puts(buf, max, &len, usb_hc_phase_name(sc->phase)) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " err=") < 0) goto out;
    if(usb_buf_puts(buf, max, &len, usb_hc_error_name(sc->last_error)) < 0) goto out;

    if(usb_buf_puts(buf, max, &len, " irq=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->irq_line) < 0) goto out;

    if(usb_buf_puts(buf, max, &len, " bar0=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->bar0) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " bar0_size=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->bar0_size) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " bar0_type=") < 0) goto out;
    if(usb_buf_puts(buf, max, &len, sc->bar0_is_io ? "io" : "mmio") < 0) goto out;

    if(usb_buf_puts(buf, max, &len, " reg_probe=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->reg_probe_ok) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " caplen=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->cap_length) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " hciver=0x") < 0) goto out;
    if(usb_buf_puthex16(buf, max, &len, sc->hciversion) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " rh_present=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->rh_present) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " rh_ports=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->rh_ports) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " rh_change=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->rh_change_bits) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " reg0=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->reg0) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " reg1=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->reg1) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " p=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->probe_attempts) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->probe_successes) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->probe_failures) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " r=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->reset_attempts) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->reset_successes) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->reset_failures) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " h=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->halt_attempts) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->halt_successes) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->halt_failures) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " s=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->start_attempts) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->start_successes) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->start_failures) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " sc=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->scan_attempts) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->scan_successes) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->scan_failures) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " sv=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->service_attempts) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->service_successes) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->service_failures) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " rh_connect=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->rh_connect_bits) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " rh_enabled=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->rh_enabled_bits) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " rh_low=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->rh_low_bits) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " rh_full=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->rh_full_bits) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " rh_high=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->rh_high_bits) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " rh_super=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->rh_super_bits) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " failures=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->init_failures) < 0) goto out;

    if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;
  }

  for(i = 0; i < usb_dev_count; i++){
    struct usb_dev_candidate *dc = &usb_dev[i];

    if(usb_buf_puts(buf, max, &len, "dev") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, i) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " hc=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, dc->hc_index) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " kind=") < 0) goto out;
    if(usb_buf_puts(buf, max, &len, usb_hc_kind_name(dc->kind)) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " port=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, dc->port) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " present=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, dc->present) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " enabled=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, dc->enabled) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " speed=") < 0) goto out;
    if(usb_buf_puts(buf, max, &len, usb_dev_speed_name(dc->speed)) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " addr_ready=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, (dc->present && dc->enabled) ? 1 : 0) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;
  }

out:
  release(&usb_lock);
  return (int)len;
}
