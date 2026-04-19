/*
 * IEEE 802.15.4 (WPAN / ZigBee / 6LoWPAN) controller scaffold for auxv6.
 *
 * Status: stub / backburner.  See docs/ROADMAP.md.
 *
 * Why backburner:
 *   - Virtually all 802.15.4 hardware is USB-attached (TI CC2531/CC2652,
 *     Silicon Labs EM357, Atmel AT86RF2xx, Nordic Semiconductor nRF52840)
 *     or SPI/UART-attached (embedded modules).  No PCI-class 802.15.4 NICs
 *     exist in our x86 target environment.
 *   - Full 802.15.4 MAC/PHY, 6LoWPAN adaptation, ZigBee cluster library,
 *     and Thread/Matter upper layers all depend on a working USB
 *     (or future SPI) subordinate-device path that is not yet implemented.
 *
 * What this tranche provides:
 *   - Core data structures for WPAN controller descriptors and phase state.
 *   - A stub enumeration path that will hook into the USB device table once
 *     the USB class-driver attach model exists.
 *   - /proc/wpan observability so the subsystem is visible at boot even
 *     when no hardware is detected.
 *
 * Follow-on tranches (not in scope here):
 *   - USB subordinate-device attach and URB management.
 *   - IEEE 802.15.4 MAC: frame format, CSMA-CA, ACK, beacon/scan.
 *   - 6LoWPAN header compression (RFC 4944 / RFC 6282).
 *   - ZigBee stack and Thread/OpenThread integration.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"

/* --- limits ------------------------------------------------------------- */
#define WPAN_STUB_MAX  8

/* --- phase states ------------------------------------------------------- */
#define WPAN_PHASE_ABSENT     0   /* no hardware found yet */
#define WPAN_PHASE_INIT       1   /* hardware detected, not configured */
#define WPAN_PHASE_IDLE       2   /* configured, radio off */
#define WPAN_PHASE_RX         3   /* receiver enabled, not associated */
#define WPAN_PHASE_PAN        4   /* PAN coordinator or device associated */
#define WPAN_PHASE_DEGRADED   5   /* error / firmware fault */

/* --- transport type ----------------------------------------------------- */
#define WPAN_TRANSPORT_UNKNOWN  0
#define WPAN_TRANSPORT_USB      1   /* USB-attached dongle (most hardware) */
#define WPAN_TRANSPORT_SPI      2   /* SPI-attached module */
#define WPAN_TRANSPORT_UART     3   /* UART-attached module */

/* --- known USB vendor:product IDs for common 802.15.4 dongles ---------- */
struct wpan_usb_id {
  ushort vendor_id;
  ushort product_id;
  const char *model;
};

/*
 * Partial list of well-known USB 802.15.4 coordinator/router dongles.
 * This table is not yet acted upon; it serves as the authoritative
 * reference for when USB subordinate-device matching is implemented.
 */
static const struct wpan_usb_id wpan_usb_table[] = {
  /* Texas Instruments */
  { 0x0451, 0x16ae, "TI CC2531 USB Dongle (zigbee2mqtt)" },
  { 0x0451, 0x16c9, "TI CC2652R/P USB Dongle" },
  { 0x0451, 0xbef3, "TI CC2652RB USB (launchpad)" },
  /* Silicon Laboratories */
  { 0x10c4, 0x8a5e, "Silicon Labs HUSBZB-1 (ember/conbee-compat)" },
  { 0x10c4, 0x89fb, "Silicon Labs EM3588 USB stick" },
  /* dresden elektronik */
  { 0x1cf1, 0x0030, "ConBee II (deconz)" },
  { 0x1cf1, 0x0031, "ConBee III (deconz)" },
  /* Atmel */
  { 0x03eb, 0x210a, "Atmel AT86RF231 USB" },
  /* Nordic Semiconductor */
  { 0x1915, 0x0002, "Nordic nRF52840 USB Dongle" },
  /* Digi/MaxStream */
  { 0x0403, 0xf1e0, "Digi XBee USB adapter" },
};

#define WPAN_USB_TABLE_LEN (sizeof(wpan_usb_table) / sizeof(wpan_usb_table[0]))

/* --- per-controller descriptor ----------------------------------------- */
struct wpan_probe {
  uchar   active;
  uint    bind_id;
  ushort  vendor_id;
  ushort  product_id;
  uchar   transport;       /* WPAN_TRANSPORT_* */
  uchar   phase;           /* WPAN_PHASE_* */
  uchar   channel;         /* 802.15.4 channel (11-26 for 2.4 GHz band) */
  uchar   page;            /* channel page (0 = 2.4 GHz O-QPSK) */
  ushort  pan_id;          /* PAN identifier (0xffff = not set) */
  ushort  short_addr;      /* 16-bit assigned address (0xffff = not set) */
  uchar   ieee_addr[8];    /* EUI-64 extended address (zeroed until set) */
  uint    rx_frames;
  uint    tx_frames;
  uint    rx_errors;
  uint    tx_errors;
  uint    attach_ticks;
  uchar   init_failures;
  const char *model;
};

/* --- module state ------------------------------------------------------- */
static struct spinlock wpan_lock;
static int wpan_lock_ready;
static struct wpan_probe wpan_probes[WPAN_STUB_MAX];
static uint wpan_probe_count;
static uint wpan_next_bind_id;

/* --- helpers ------------------------------------------------------------ */

static const char *
wpan_phase_name(uchar phase)
{
  switch(phase){
  case WPAN_PHASE_ABSENT:   return "absent";
  case WPAN_PHASE_INIT:     return "init";
  case WPAN_PHASE_IDLE:     return "idle";
  case WPAN_PHASE_RX:       return "rx";
  case WPAN_PHASE_PAN:      return "pan";
  case WPAN_PHASE_DEGRADED: return "degraded";
  default:                  return "unknown";
  }
}

static const char *
wpan_transport_name(uchar t)
{
  switch(t){
  case WPAN_TRANSPORT_USB:  return "usb";
  case WPAN_TRANSPORT_SPI:  return "spi";
  case WPAN_TRANSPORT_UART: return "uart";
  default:                  return "unknown";
  }
}

/* --- procfs output helpers ---------------------------------------------- */

static int
wpan_buf_putc(char *buf, uint max, uint *len, char c)
{
  if(*len >= max)
    return -1;
  buf[(*len)++] = c;
  return 0;
}

static int
wpan_buf_puts(char *buf, uint max, uint *len, const char *s)
{
  if(!s)
    return 0;
  while(*s){
    if(wpan_buf_putc(buf, max, len, *s++) < 0)
      return -1;
  }
  return 0;
}

static int
wpan_buf_putu(char *buf, uint max, uint *len, uint v)
{
  char tmp[12];
  uint n = 0;

  do {
    tmp[n++] = '0' + (v % 10);
    v /= 10;
  } while(v);
  while(n--){
    if(wpan_buf_putc(buf, max, len, tmp[n]) < 0)
      return -1;
  }
  return 0;
}

/* --- stub USB attach hook (no-op until USB class-driver model lands) --- */

/*
 * wpan_usb_attach() will be called by the USB core when a subordinate device
 * matching wpan_usb_table is enumerated.  For now it is a no-op that simply
 * registers a probe record so /proc/wpan reflects the find.
 */
int
wpan_usb_match(ushort vendor, ushort product)
{
  uint i;

  for(i = 0; i < WPAN_USB_TABLE_LEN; i++){
    if(wpan_usb_table[i].vendor_id == vendor &&
       wpan_usb_table[i].product_id == product)
      return 1;
  }
  return 0;
}

int
wpan_usb_attach(ushort vendor, ushort product, uint *bind_handle)
{
  uint i;
  struct wpan_probe *sc;
  const char *model = 0;
  uint bind_id;

  if(!bind_handle)
    return -1;

  for(i = 0; i < WPAN_USB_TABLE_LEN; i++){
    if(wpan_usb_table[i].vendor_id == vendor &&
       wpan_usb_table[i].product_id == product){
      model = wpan_usb_table[i].model;
      break;
    }
  }
  if(!model)
    return -1;  /* not a known 802.15.4 device */

  acquire(&wpan_lock);
  if(wpan_probe_count >= WPAN_STUB_MAX){
    release(&wpan_lock);
    return -1;
  }
  sc = &wpan_probes[wpan_probe_count++];
  release(&wpan_lock);

  bind_id = ++wpan_next_bind_id;
  if(bind_id == 0)
    bind_id = ++wpan_next_bind_id;

  sc->active     = 1;
  sc->bind_id    = bind_id;
  sc->vendor_id  = vendor;
  sc->product_id = product;
  sc->transport  = WPAN_TRANSPORT_USB;
  sc->phase      = WPAN_PHASE_INIT;
  sc->channel    = 11;    /* default start channel */
  sc->page       = 0;
  sc->pan_id     = 0xffff;
  sc->short_addr = 0xffff;
  sc->model      = model;
  *bind_handle   = bind_id;

  cprintf("ieee802154: %s [%x:%x] attached via USB (stub, bind=%d)\n",
    model, (uint)vendor, (uint)product, bind_id);
  return 0;
}

int
wpan_usb_detach(uint bind_handle)
{
  int i;

  if(bind_handle == 0)
    return -1;

  acquire(&wpan_lock);
  for(i = (int)wpan_probe_count - 1; i >= 0; i--){
    struct wpan_probe *sc;

    sc = &wpan_probes[i];
    if(!sc->active)
      continue;
    if(sc->bind_id != bind_handle)
      continue;
    sc->active = 0;
    sc->phase = WPAN_PHASE_ABSENT;
    release(&wpan_lock);
    cprintf("ieee802154: bind=%d detached via USB retire\n", bind_handle);
    return 0;
  }
  release(&wpan_lock);
  return -1;
}

/* --- /proc/wpan dump ---------------------------------------------------- */

int
ieee802154_procfs_dump(char *buf, uint max)
{
  struct wpan_probe snap[WPAN_STUB_MAX];
  uint count;
  uint active = 0;
  uint len = 0;
  uint i;

  acquire(&wpan_lock);
  count = wpan_probe_count;
  if(count > WPAN_STUB_MAX)
    count = WPAN_STUB_MAX;
  for(i = 0; i < count; i++)
    snap[i] = wpan_probes[i];
  release(&wpan_lock);

  if(wpan_buf_puts(buf, max, &len, "# IEEE 802.15.4 WPAN controllers\n") < 0) return -1;
  if(wpan_buf_puts(buf, max, &len, "# bind transport vendor:product phase channel pan_id\n") < 0) return -1;

  for(i = 0; i < count; i++){
    struct wpan_probe *p = &snap[i];
    static const char hx[] = "0123456789abcdef";
    int shift;

    if(!p->active)
      continue;
    active++;

    if(wpan_buf_puts(buf, max, &len, "dev bind=") < 0) return -1;
    if(wpan_buf_putu(buf, max, &len, p->bind_id) < 0) return -1;
    if(wpan_buf_putc(buf, max, &len, ' ') < 0) return -1;

    if(wpan_buf_puts(buf, max, &len, "dev transport=") < 0) return -1;
    if(wpan_buf_puts(buf, max, &len, wpan_transport_name(p->transport)) < 0) return -1;

    if(wpan_buf_puts(buf, max, &len, " id=") < 0) return -1;
    for(shift = 12; shift >= 0; shift -= 4){
      if(wpan_buf_putc(buf, max, &len, hx[(p->vendor_id >> shift) & 0xf]) < 0) return -1;
    }
    if(wpan_buf_putc(buf, max, &len, ':') < 0) return -1;
    for(shift = 12; shift >= 0; shift -= 4){
      if(wpan_buf_putc(buf, max, &len, hx[(p->product_id >> shift) & 0xf]) < 0) return -1;
    }

    if(wpan_buf_puts(buf, max, &len, " phase=") < 0) return -1;
    if(wpan_buf_puts(buf, max, &len, wpan_phase_name(p->phase)) < 0) return -1;

    if(wpan_buf_puts(buf, max, &len, " channel=") < 0) return -1;
    if(wpan_buf_putu(buf, max, &len, p->channel) < 0) return -1;

    if(wpan_buf_puts(buf, max, &len, " pan=0x") < 0) return -1;
    for(shift = 12; shift >= 0; shift -= 4){
      if(wpan_buf_putc(buf, max, &len, hx[(p->pan_id >> shift) & 0xf]) < 0) return -1;
    }

    if(wpan_buf_puts(buf, max, &len, " rx=") < 0) return -1;
    if(wpan_buf_putu(buf, max, &len, p->rx_frames) < 0) return -1;
    if(wpan_buf_puts(buf, max, &len, " tx=") < 0) return -1;
    if(wpan_buf_putu(buf, max, &len, p->tx_frames) < 0) return -1;
    if(wpan_buf_putc(buf, max, &len, '\n') < 0) return -1;
  }

  if(wpan_buf_puts(buf, max, &len, "summary active=") < 0) return -1;
  if(wpan_buf_putu(buf, max, &len, active) < 0) return -1;
  if(wpan_buf_puts(buf, max, &len, " seen=") < 0) return -1;
  if(wpan_buf_putu(buf, max, &len, count) < 0) return -1;
  if(wpan_buf_puts(buf, max, &len,
    " note=usb-attach-pending pending-usb-class-driver\n") < 0) return -1;

  return (int)len;
}

/* --- init --------------------------------------------------------------- */

void
ieee802154_init(void)
{
  if(!wpan_lock_ready){
    initlock(&wpan_lock, "wpan");
    lockdep_set_rank(&wpan_lock, LOCK_RANK_DEFAULT, "wpan");
    wpan_lock_ready = 1;
  }

  acquire(&wpan_lock);
  wpan_probe_count = 0;
  wpan_next_bind_id = 0;
  release(&wpan_lock);

  /*
   * No PCI-class 802.15.4 hardware exists; discovery will be driven by
   * the USB core once the subordinate-device attach path is implemented.
   * Log a single boot note so the subsystem is visible.
   */
  BOOTDBG("ieee802154: WPAN scaffold ready (USB attach pending)\n");
}
