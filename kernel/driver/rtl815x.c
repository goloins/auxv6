/*
 * Realtek RTL8152/RTL8153 USB Ethernet scaffold for auxv6.
 *
 * Current tranche:
 * - Keep an authoritative USB vendor:product table for RTL8152/RTL8153
 *   class adapters and common rebrands seen in Linux/OpenBSD/NetBSD.
 * - Provide a USB attach hook for the future subordinate-device model.
 * - Expose matched devices through /proc/r815x so the family is visible
 *   before the USB control/bulk transfer path lands.
 *
 * Not implemented here:
 * - Endpoint-0 control reads/writes
 * - PLA/USB/OCP register access
 * - MAC/PHY programming
 * - ifnet registration or RX/TX datapath
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"

#define R815X_STUB_MAX     8

#define R815X_FAMILY_8152   1
#define R815X_FAMILY_8153   2
#define R815X_FAMILY_8153B  3

#define R815X_PHASE_INIT       0
#define R815X_PHASE_IDENTIFIED 1
#define R815X_PHASE_CONFIGURED 2
#define R815X_PHASE_RUNNING    3
#define R815X_PHASE_DEGRADED   4

struct r815x_usb_id {
  ushort vendor_id;
  ushort product_id;
  uchar family;
  const char *model;
};

struct r815x_probe {
  uchar active;
  uint bind_id;
  ushort vendor_id;
  ushort product_id;
  uchar dev_speed;
  uchar max_packet0;
  uchar ifnum;
  uchar ifalt;
  uchar bulk_in_ep;
  uchar bulk_out_ep;
  ushort chip_version;
  uchar family;
  uchar phase;
  uint ctrl_attempts;
  uint ctrl_successes;
  uint ctrl_failures;
  uint bulk_attempts;
  uint bulk_successes;
  uint bulk_failures;
  uint bulk_last_len;
  uint rx_frames;
  uint tx_frames;
  uint rx_errors;
  uint tx_errors;
  const char *model;
};

static const struct r815x_usb_id r815x_usb_table[] = {
  { 0x0bda, 0x8152, R815X_FAMILY_8152,  "Realtek RTL8152 USB Ethernet" },
  { 0x0bda, 0x8153, R815X_FAMILY_8153,  "Realtek RTL8153 USB Ethernet" },
  { 0x17ef, 0x7205, R815X_FAMILY_8153,  "Lenovo RTL8153" },
  { 0x17ef, 0x720b, R815X_FAMILY_8153,  "Lenovo RTL8153" },
  { 0x17ef, 0x720c, R815X_FAMILY_8153,  "Lenovo USB-C Dongle (RTL8153)" },
  { 0x17ef, 0x721e, R815X_FAMILY_8153B, "Lenovo Powered USB-C Travel Hub (RTL8153B)" },
  { 0x17ef, 0xa359, R815X_FAMILY_8153B, "Lenovo Hybrid USB-C Dock (RTL8153B)" },
  { 0x2001, 0x7e34, R815X_FAMILY_8153,  "D-Link RTL8153" },
  { 0x2001, 0xa710, R815X_FAMILY_8153,  "D-Link RTL8153" },
  { 0x2357, 0x0601, R815X_FAMILY_8153,  "TP-Link UE300 (RTL8153)" },
  { 0x1d6b, 0x0000, 0, 0 }
};

static struct spinlock r815x_lock;
static int r815x_lock_ready;
static struct r815x_probe r815x_probes[R815X_STUB_MAX];
static uint r815x_probe_count;
static uint r815x_next_bind_id;

static int
r815x_real_control_probe(struct r815x_probe *sc)
{
  uchar desc18[18];

  if(!sc)
    return -1;

  sc->ctrl_attempts++;
  if(!sc->active){
    sc->ctrl_failures++;
    return -1;
  }
  if((sc->bulk_in_ep & 0x80) == 0 || (sc->bulk_out_ep & 0x80) != 0){
    sc->ctrl_failures++;
    return -1;
  }

  memset(desc18, 0, sizeof(desc18));
  if(usb_driver_ep0_probe_desc18(sc->bind_id, desc18) < 0){
    sc->ctrl_failures++;
    return -1;
  }
  if(desc18[0] < 18 || desc18[1] != 1){
    sc->ctrl_failures++;
    return -1;
  }

  sc->chip_version = (ushort)(desc18[12] | (desc18[13] << 8));

  sc->ctrl_successes++;
  return 0;
}

static int
r815x_stub_bulk_probe(struct r815x_probe *sc)
{
  uint bulk_len;
  uchar probe_buf[512];
  ushort done_len;

  if(!sc)
    return -1;

  sc->bulk_attempts++;
  if(!sc->active){
    sc->bulk_failures++;
    return -1;
  }
  if((sc->bulk_in_ep & 0x80) == 0 || (sc->bulk_out_ep & 0x80) != 0){
    sc->bulk_failures++;
    return -1;
  }

  /* First backend-routed bulk submission attempt; keep synthetic fallback. */
  bulk_len = sc->max_packet0 ? sc->max_packet0 : 64;
  if(bulk_len > 512)
    bulk_len = 512;
  done_len = 0;

  memset(probe_buf, 0, sizeof(probe_buf));
  if(usb_driver_bulk_probe_xfer(sc->bind_id,
                                sc->bulk_in_ep, sc->bulk_out_ep,
                                probe_buf, (ushort)bulk_len,
                                &done_len) < 0){
    sc->bulk_failures++;
    sc->bulk_last_len = 0;
    return -1;
  }

  sc->bulk_last_len = done_len;
  sc->rx_frames++;
  sc->tx_frames++;
  sc->bulk_successes++;
  return 0;
}

static void
r815x_ensure_lock(void)
{
  if(!r815x_lock_ready){
    initlock(&r815x_lock, "r815x");
    lockdep_set_rank(&r815x_lock, LOCK_RANK_DEFAULT, "r815x");
    r815x_lock_ready = 1;
  }
}

static const struct r815x_usb_id *
r815x_lookup(ushort vendor, ushort product)
{
  uint i;

  for(i = 0; r815x_usb_table[i].model; i++){
    if(r815x_usb_table[i].vendor_id == vendor &&
       r815x_usb_table[i].product_id == product)
      return &r815x_usb_table[i];
  }
  return 0;
}

static const char *
r815x_family_name(uchar family)
{
  switch(family){
  case R815X_FAMILY_8152:
    return "rtl8152";
  case R815X_FAMILY_8153:
    return "rtl8153";
  case R815X_FAMILY_8153B:
    return "rtl8153b";
  default:
    return "unknown";
  }
}

static const char *
r815x_phase_name(uchar phase)
{
  switch(phase){
  case R815X_PHASE_INIT:
    return "init";
  case R815X_PHASE_IDENTIFIED:
    return "identified";
  case R815X_PHASE_CONFIGURED:
    return "configured";
  case R815X_PHASE_RUNNING:
    return "running";
  case R815X_PHASE_DEGRADED:
    return "degraded";
  default:
    return "unknown";
  }
}

static int
r815x_buf_putc(char *buf, uint max, uint *len, char c)
{
  if(*len >= max)
    return -1;
  buf[(*len)++] = c;
  return 0;
}

static int
r815x_buf_puts(char *buf, uint max, uint *len, const char *s)
{
  if(!s)
    return 0;
  while(*s){
    if(r815x_buf_putc(buf, max, len, *s++) < 0)
      return -1;
  }
  return 0;
}

static int
r815x_buf_putu(char *buf, uint max, uint *len, uint v)
{
  char tmp[12];
  uint n;

  n = 0;
  do {
    tmp[n++] = '0' + (v % 10);
    v /= 10;
  } while(v);

  while(n--){
    if(r815x_buf_putc(buf, max, len, tmp[n]) < 0)
      return -1;
  }
  return 0;
}

static int
r815x_buf_puthex16(char *buf, uint max, uint *len, ushort v)
{
  static const char hex[] = "0123456789abcdef";
  int shift;

  for(shift = 12; shift >= 0; shift -= 4){
    if(r815x_buf_putc(buf, max, len, hex[(v >> shift) & 0xf]) < 0)
      return -1;
  }
  return 0;
}

int
rtl815x_usb_match(ushort vendor, ushort product)
{
  return r815x_lookup(vendor, product) ? 1 : 0;
}

int
rtl815x_usb_attach(ushort vendor, ushort product,
                   uchar ifnum, uchar ifalt,
                   uchar bulk_in_ep, uchar bulk_out_ep,
                   uchar dev_speed, uchar mps0,
                   uint *bind_handle)
{
  const struct r815x_usb_id *id;
  struct r815x_probe *sc;
  uint bind_id;

  if(!bind_handle)
    return -1;
  if((bulk_in_ep & 0x80) == 0 || (bulk_out_ep & 0x80) != 0)
    return -1;
  id = r815x_lookup(vendor, product);
  if(!id)
    return -1;

  r815x_ensure_lock();

  acquire(&r815x_lock);
  if(r815x_probe_count >= R815X_STUB_MAX){
    release(&r815x_lock);
    return -1;
  }

  sc = &r815x_probes[r815x_probe_count++];
  memset(sc, 0, sizeof(*sc));
  sc->active = 1;
  bind_id = ++r815x_next_bind_id;
  if(bind_id == 0)
    bind_id = ++r815x_next_bind_id;
  sc->bind_id = bind_id;
  sc->vendor_id = vendor;
  sc->product_id = product;
  sc->dev_speed = dev_speed;
  sc->max_packet0 = mps0;
  sc->ifnum = ifnum;
  sc->ifalt = ifalt;
  sc->bulk_in_ep = bulk_in_ep;
  sc->bulk_out_ep = bulk_out_ep;
  sc->family = id->family;
  sc->phase = R815X_PHASE_CONFIGURED;
  sc->model = id->model;

  if(r815x_real_control_probe(sc) < 0 ||
     r815x_stub_bulk_probe(sc) < 0)
    sc->phase = R815X_PHASE_DEGRADED;
  else
    sc->phase = R815X_PHASE_RUNNING;

  *bind_handle = bind_id;
  release(&r815x_lock);

  cprintf("r815x: %s [%x:%x] attached via USB (bind=%d speed=%d mps0=%d if=%d/%d ep=0x%x/0x%x phase=%s)\n",
      id->model, (uint)vendor, (uint)product, bind_id,
      (uint)dev_speed, (uint)mps0,
      (uint)ifnum, (uint)ifalt, (uint)bulk_in_ep, (uint)bulk_out_ep,
      r815x_phase_name(sc->phase));
  return 0;
}

int
rtl815x_usb_detach(uint bind_handle)
{
  int i;

  if(bind_handle == 0)
    return -1;

  r815x_ensure_lock();

  acquire(&r815x_lock);
  for(i = (int)r815x_probe_count - 1; i >= 0; i--){
    struct r815x_probe *sc;

    sc = &r815x_probes[i];
    if(!sc->active)
      continue;
    if(sc->bind_id != bind_handle)
      continue;
    sc->active = 0;
    sc->phase = R815X_PHASE_DEGRADED;
    release(&r815x_lock);
    cprintf("r815x: bind=%d detached via USB retire\n", bind_handle);
    return 0;
  }
  release(&r815x_lock);
  return -1;
}

int
rtl815x_procfs_dump(char *buf, uint max)
{
  struct r815x_probe snap[R815X_STUB_MAX];
  uint count;
  uint active;
  uint len;
  uint i;

  r815x_ensure_lock();

  acquire(&r815x_lock);
  count = r815x_probe_count;
  if(count > R815X_STUB_MAX)
    count = R815X_STUB_MAX;
  for(i = 0; i < count; i++)
    snap[i] = r815x_probes[i];
  release(&r815x_lock);

  active = 0;

  len = 0;
  if(r815x_buf_puts(buf, max, &len,
                    "# Realtek RTL8152/RTL8153 USB Ethernet\n") < 0)
    return -1;
  if(r815x_buf_puts(buf, max, &len,
                    "# bind id speed mps0 if alt ep_in ep_out family phase chipver ctrl bulk blen model\n") < 0)
    return -1;

  for(i = 0; i < count; i++){
    struct r815x_probe *p;

    p = &snap[i];
    if(!p->active)
      continue;
    active++;
    if(r815x_buf_puts(buf, max, &len, "dev bind=") < 0) return -1;
    if(r815x_buf_putu(buf, max, &len, p->bind_id) < 0) return -1;
    if(r815x_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(r815x_buf_puts(buf, max, &len, "id=") < 0) return -1;
    if(r815x_buf_puthex16(buf, max, &len, p->vendor_id) < 0) return -1;
    if(r815x_buf_putc(buf, max, &len, ':') < 0) return -1;
    if(r815x_buf_puthex16(buf, max, &len, p->product_id) < 0) return -1;

    if(r815x_buf_puts(buf, max, &len, " speed=") < 0) return -1;
    if(r815x_buf_putu(buf, max, &len, p->dev_speed) < 0) return -1;
    if(r815x_buf_puts(buf, max, &len, " mps0=") < 0) return -1;
    if(r815x_buf_putu(buf, max, &len, p->max_packet0) < 0) return -1;

    if(r815x_buf_puts(buf, max, &len, " if=") < 0) return -1;
    if(r815x_buf_putu(buf, max, &len, p->ifnum) < 0) return -1;
    if(r815x_buf_putc(buf, max, &len, '/') < 0) return -1;
    if(r815x_buf_putu(buf, max, &len, p->ifalt) < 0) return -1;
    if(r815x_buf_puts(buf, max, &len, " ep=0x") < 0) return -1;
    if(r815x_buf_puthex16(buf, max, &len, p->bulk_in_ep) < 0) return -1;
    if(r815x_buf_putc(buf, max, &len, '/') < 0) return -1;
    if(r815x_buf_puts(buf, max, &len, "0x") < 0) return -1;
    if(r815x_buf_puthex16(buf, max, &len, p->bulk_out_ep) < 0) return -1;
    if(r815x_buf_puts(buf, max, &len, " family=") < 0) return -1;
    if(r815x_buf_puts(buf, max, &len, r815x_family_name(p->family)) < 0) return -1;

    if(r815x_buf_puts(buf, max, &len, " phase=") < 0) return -1;
    if(r815x_buf_puts(buf, max, &len, r815x_phase_name(p->phase)) < 0) return -1;

    if(r815x_buf_puts(buf, max, &len, " chipver=") < 0) return -1;
    if(p->chip_version){
      if(r815x_buf_puthex16(buf, max, &len, p->chip_version) < 0) return -1;
    } else {
      if(r815x_buf_putc(buf, max, &len, '?') < 0) return -1;
    }

    if(r815x_buf_puts(buf, max, &len, " ctrl=") < 0) return -1;
    if(r815x_buf_putu(buf, max, &len, p->ctrl_attempts) < 0) return -1;
    if(r815x_buf_putc(buf, max, &len, '/') < 0) return -1;
    if(r815x_buf_putu(buf, max, &len, p->ctrl_successes) < 0) return -1;
    if(r815x_buf_putc(buf, max, &len, '/') < 0) return -1;
    if(r815x_buf_putu(buf, max, &len, p->ctrl_failures) < 0) return -1;

    if(r815x_buf_puts(buf, max, &len, " bulk=") < 0) return -1;
    if(r815x_buf_putu(buf, max, &len, p->bulk_attempts) < 0) return -1;
    if(r815x_buf_putc(buf, max, &len, '/') < 0) return -1;
    if(r815x_buf_putu(buf, max, &len, p->bulk_successes) < 0) return -1;
    if(r815x_buf_putc(buf, max, &len, '/') < 0) return -1;
    if(r815x_buf_putu(buf, max, &len, p->bulk_failures) < 0) return -1;

    if(r815x_buf_puts(buf, max, &len, " blen=") < 0) return -1;
    if(r815x_buf_putu(buf, max, &len, p->bulk_last_len) < 0) return -1;

    if(r815x_buf_puts(buf, max, &len, " model=") < 0) return -1;
    if(r815x_buf_puts(buf, max, &len, p->model) < 0) return -1;
    if(r815x_buf_putc(buf, max, &len, '\n') < 0) return -1;
  }

  if(r815x_buf_puts(buf, max, &len, "summary active=") < 0) return -1;
  if(r815x_buf_putu(buf, max, &len, active) < 0) return -1;
  if(r815x_buf_puts(buf, max, &len, " seen=") < 0) return -1;
  if(r815x_buf_putu(buf, max, &len, count) < 0) return -1;
  if(r815x_buf_puts(buf, max, &len,
                    " note=usb-bind-context-ready control-bulk-probe-stubs-landed datapath-unimplemented\n") < 0)
    return -1;

  return (int)len;
}

void
rtl815x_init(void)
{
  r815x_ensure_lock();

  acquire(&r815x_lock);
  r815x_probe_count = 0;
  r815x_next_bind_id = 0;
  release(&r815x_lock);

  BOOTDBG("r815x: RTL8152/RTL8153 USB scaffold ready (USB attach pending)\n");
}