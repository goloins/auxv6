/*
 * Apple Lightning accessory scaffold for auxv6.
 *
 * Scope:
 * - Provide a kernel-visible planning anchor for Lightning/iAP2 work.
 * - Track lightweight probe/attach telemetry for future USB bridge hooks.
 * - Expose current scaffold state via /proc/lightning.
 *
 * This tranche does not implement iAP2 transport, MFi authentication,
 * usbmuxd, or serial-over-lightning data paths.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"

#define LIGHTNING_PHASE_INIT     0
#define LIGHTNING_PHASE_SCaffold 1
#define LIGHTNING_PHASE_ACTIVE   2
#define LIGHTNING_PHASE_DEGRADED 3

struct lightning_state {
  uchar phase;
  uint observed_usb;
  uint apple_vid_matches;
  uint iap2_candidates;
  uint attach_attempts;
  uint attach_successes;
  uint attach_failures;
  ushort last_vendor;
  ushort last_product;
};

static struct spinlock lightning_lock;
static int lightning_lock_ready;
static struct lightning_state lightning_st;

static int
lightning_buf_putc(char *buf, uint max, uint *len, char c)
{
  if(*len >= max)
    return -1;
  buf[*len] = c;
  (*len)++;
  return 0;
}

static int
lightning_buf_puts(char *buf, uint max, uint *len, const char *s)
{
  uint i;

  for(i = 0; s[i]; i++){
    if(lightning_buf_putc(buf, max, len, s[i]) < 0)
      return -1;
  }
  return 0;
}

static int
lightning_buf_putu(char *buf, uint max, uint *len, uint v)
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
    if(lightning_buf_putc(buf, max, len, tmp[n - i - 1]) < 0)
      return -1;
  }
  return 0;
}

static int
lightning_buf_puthex16(char *buf, uint max, uint *len, ushort v)
{
  static const char hex[] = "0123456789abcdef";

  if(lightning_buf_putc(buf, max, len, hex[(v >> 12) & 0xf]) < 0) return -1;
  if(lightning_buf_putc(buf, max, len, hex[(v >> 8) & 0xf]) < 0) return -1;
  if(lightning_buf_putc(buf, max, len, hex[(v >> 4) & 0xf]) < 0) return -1;
  if(lightning_buf_putc(buf, max, len, hex[v & 0xf]) < 0) return -1;
  return 0;
}

static const char*
lightning_phase_name(uchar phase)
{
  switch(phase){
  case LIGHTNING_PHASE_INIT:
    return "init";
  case LIGHTNING_PHASE_SCaffold:
    return "scaffold";
  case LIGHTNING_PHASE_ACTIVE:
    return "active";
  case LIGHTNING_PHASE_DEGRADED:
    return "degraded";
  default:
    return "unknown";
  }
}

void
lightning_usb_observe(ushort vendor, ushort product,
                      uchar ifclass, uchar ifsubclass, uchar ifproto)
{
  acquire(&lightning_lock);
  lightning_st.observed_usb++;
  lightning_st.last_vendor = vendor;
  lightning_st.last_product = product;

  if(vendor == 0x05ac){
    lightning_st.apple_vid_matches++;
    if(ifclass == 0xff || ifsubclass == 0xf0 || ifproto == 0x01)
      lightning_st.iap2_candidates++;
  }
  release(&lightning_lock);
}

int
lightning_procfs_dump(char *buf, uint max)
{
  struct lightning_state snap;
  uint len;

  len = 0;
  acquire(&lightning_lock);
  snap = lightning_st;
  release(&lightning_lock);

  if(lightning_buf_puts(buf, max, &len,
                        "# Apple Lightning/iAP2 scaffold\n") < 0)
    return -1;
  if(lightning_buf_puts(buf, max, &len, "phase ") < 0) return -1;
  if(lightning_buf_puts(buf, max, &len, lightning_phase_name(snap.phase)) < 0) return -1;
  if(lightning_buf_putc(buf, max, &len, '\n') < 0) return -1;

  if(lightning_buf_puts(buf, max, &len, "observed_usb ") < 0) return -1;
  if(lightning_buf_putu(buf, max, &len, snap.observed_usb) < 0) return -1;
  if(lightning_buf_putc(buf, max, &len, '\n') < 0) return -1;

  if(lightning_buf_puts(buf, max, &len, "apple_vid ") < 0) return -1;
  if(lightning_buf_putu(buf, max, &len, snap.apple_vid_matches) < 0) return -1;
  if(lightning_buf_putc(buf, max, &len, '\n') < 0) return -1;

  if(lightning_buf_puts(buf, max, &len, "iap2_candidates ") < 0) return -1;
  if(lightning_buf_putu(buf, max, &len, snap.iap2_candidates) < 0) return -1;
  if(lightning_buf_putc(buf, max, &len, '\n') < 0) return -1;

  if(lightning_buf_puts(buf, max, &len, "attach ") < 0) return -1;
  if(lightning_buf_putu(buf, max, &len, snap.attach_attempts) < 0) return -1;
  if(lightning_buf_putc(buf, max, &len, '/') < 0) return -1;
  if(lightning_buf_putu(buf, max, &len, snap.attach_successes) < 0) return -1;
  if(lightning_buf_putc(buf, max, &len, '/') < 0) return -1;
  if(lightning_buf_putu(buf, max, &len, snap.attach_failures) < 0) return -1;
  if(lightning_buf_putc(buf, max, &len, '\n') < 0) return -1;

  if(lightning_buf_puts(buf, max, &len, "last_vidpid 0x") < 0) return -1;
  if(lightning_buf_puthex16(buf, max, &len, snap.last_vendor) < 0) return -1;
  if(lightning_buf_putc(buf, max, &len, ':') < 0) return -1;
  if(lightning_buf_puthex16(buf, max, &len, snap.last_product) < 0) return -1;
  if(lightning_buf_putc(buf, max, &len, '\n') < 0) return -1;

  if(lightning_buf_puts(buf, max, &len,
                        "summary transport=none mux=none auth=none\n") < 0)
    return -1;

  return (int)len;
}

void
lightning_init(void)
{
  if(!lightning_lock_ready){
    initlock(&lightning_lock, "lightning");
    lockdep_set_rank(&lightning_lock, LOCK_RANK_DEFAULT, "lightning");
    lightning_lock_ready = 1;
  }

  acquire(&lightning_lock);
  memset(&lightning_st, 0, sizeof(lightning_st));
  lightning_st.phase = LIGHTNING_PHASE_SCaffold;
  release(&lightning_lock);

  BOOTDBG("lightning: scaffold initialized (USB hook pending)\n");
}
