/*
 * PCI FireWire (IEEE 1394) OHCI scaffold for auxv6.
 *
 * Scope:
 * - Probe PCI class/subclass/progif for IEEE 1394 OHCI controllers.
 * - Perform minimal controller init (register sanity, soft-reset attempt,
 *   interrupt mask setup).
 * - Register IRQ handlers and track bus-reset/self-id events.
 * - Expose discovered controllers and telemetry via /proc/firewire.
 *
 * This tranche does not yet implement async transaction queues or SBP-2.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"

#define FIREWIRE_STUB_MAX 16
#define FIREWIRE_ASYNC_CTX_MAX 16
#define FIREWIRE_ASYNC_TIMEOUT_TICKS 500

/* OHCI-1394 register offsets */
#define OHCI_REG_VERSION           0x000
#define OHCI_REG_BUS_OPTIONS       0x020
#define OHCI_REG_HC_CONTROL_SET    0x050
#define OHCI_REG_HC_CONTROL_CLEAR  0x054
#define OHCI_REG_SELF_ID_COUNT     0x064
#define OHCI_REG_INT_EVENT_SET     0x080
#define OHCI_REG_INT_EVENT_CLEAR   0x084
#define OHCI_REG_INT_MASK_SET      0x088
#define OHCI_REG_INT_MASK_CLEAR    0x08C
#define OHCI_REG_NODE_ID           0x0E8

/* OHCI control bits */
#define OHCI_HCCTRL_SOFT_RESET     (1U << 16)
#define OHCI_HCCTRL_LINK_ENABLE    (1U << 17)

/* OHCI interrupt/event bits */
#define OHCI_INT_SELF_ID_COMPLETE  (1U << 16)
#define OHCI_INT_BUS_RESET         (1U << 17)
#define OHCI_INT_CYC64_SECONDS     (1U << 31)
#define OHCI_INT_CORE_MASK         (OHCI_INT_SELF_ID_COMPLETE | OHCI_INT_BUS_RESET)

#define FIREWIRE_PHASE_INIT        0
#define FIREWIRE_PHASE_READY       1
#define FIREWIRE_PHASE_RESETTING   2
#define FIREWIRE_PHASE_DEGRADED    3

struct firewire_async_ctx {
  uint token;
  uint submit_tick;
  uint complete_tick;
  uchar in_use;
  uchar generation;
  short status;
};

struct firewire_stub_probe {
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
  uint version;
  uint bus_options;
  uint node_id;
  uint self_id_count;
  uint irq_count;
  uint bus_reset_count;
  uint self_id_irq_count;
  uint last_event;
  uint init_failures;
  uint phase_changes;
  uint last_phase_change_tick;
  uint polled_event_count;
  uint reset_pending;
  uint async_head;
  uint async_tail;
  uint async_pending;
  uint async_next_token;
  uint async_submit_count;
  uint async_complete_count;
  uint async_stale_drop_count;
  uint async_timeout_count;
  uchar generation;
  uchar phase;
  uchar irq_registered;
  uchar attached;
  struct firewire_async_ctx async_ctx[FIREWIRE_ASYNC_CTX_MAX];
  volatile uint *regs;
};

static struct spinlock firewire_lock;
static int firewire_lock_ready;
static struct firewire_stub_probe firewire_stub_probes[FIREWIRE_STUB_MAX];
static uint firewire_stub_probe_count;
extern int ncpu;

static uint
firewire_current_ticks(void)
{
  uint now;

  acquire(&tickslock);
  now = ticks;
  release(&tickslock);
  return now;
}

static inline uint
firewire_read(struct firewire_stub_probe *sc, uint reg)
{
  volatile uint *base;

  if(!sc || !sc->regs)
    return 0xFFFFFFFF;
  base = sc->regs;
  return *(volatile uint *)((char *)base + reg);
}

static inline void
firewire_write(struct firewire_stub_probe *sc, uint reg, uint val)
{
  volatile uint *base;

  if(!sc || !sc->regs)
    return;
  base = sc->regs;
  *(volatile uint *)((char *)base + reg) = val;
}

static int
firewire_buf_putc(char *buf, uint max, uint *len, char c)
{
  if(*len >= max)
    return -1;
  buf[*len] = c;
  (*len)++;
  return 0;
}

static int
firewire_buf_puts(char *buf, uint max, uint *len, const char *s)
{
  uint i;

  if(!s)
    return firewire_buf_puts(buf, max, len, "?");

  for(i = 0; s[i]; i++){
    if(firewire_buf_putc(buf, max, len, s[i]) < 0)
      return -1;
  }
  return 0;
}

static uint
firewire_write_uint(char *tmp, uint value)
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
firewire_buf_putu(char *buf, uint max, uint *len, uint v)
{
  char tmp[16];
  uint n;
  uint i;

  n = firewire_write_uint(tmp, v);
  for(i = 0; i < n; i++){
    if(firewire_buf_putc(buf, max, len, tmp[i]) < 0)
      return -1;
  }
  return 0;
}

static int
firewire_buf_puthex8(char *buf, uint max, uint *len, uchar v)
{
  static const char hex[] = "0123456789abcdef";

  if(firewire_buf_putc(buf, max, len, hex[(v >> 4) & 0xF]) < 0) return -1;
  if(firewire_buf_putc(buf, max, len, hex[v & 0xF]) < 0) return -1;
  return 0;
}

static int
firewire_buf_puthex16(char *buf, uint max, uint *len, ushort v)
{
  static const char hex[] = "0123456789abcdef";

  if(firewire_buf_putc(buf, max, len, hex[(v >> 12) & 0xF]) < 0) return -1;
  if(firewire_buf_putc(buf, max, len, hex[(v >> 8) & 0xF]) < 0) return -1;
  if(firewire_buf_putc(buf, max, len, hex[(v >> 4) & 0xF]) < 0) return -1;
  if(firewire_buf_putc(buf, max, len, hex[v & 0xF]) < 0) return -1;
  return 0;
}

static int
firewire_buf_puthex32(char *buf, uint max, uint *len, uint v)
{
  static const char hex[] = "0123456789abcdef";
  int shift;

  for(shift = 28; shift >= 0; shift -= 4){
    if(firewire_buf_putc(buf, max, len, hex[(v >> shift) & 0xF]) < 0)
      return -1;
  }
  return 0;
}

static int
firewire_is_match(struct pci_dev *dev)
{
  if(!dev)
    return 0;
  if(dev->class_code != PCI_CLASS_SERIAL)
    return 0;
  if(dev->subclass != PCI_SUBCLASS_SERIAL_FIREWIRE)
    return 0;
  if(dev->prog_if != PCI_PROGIF_IEEE1394_OHCI)
    return 0;
  return 1;
}

static const char *
firewire_phase_name(uchar phase)
{
  switch(phase){
  case FIREWIRE_PHASE_INIT:
    return "init";
  case FIREWIRE_PHASE_READY:
    return "ready";
  case FIREWIRE_PHASE_RESETTING:
    return "resetting";
  case FIREWIRE_PHASE_DEGRADED:
    return "degraded";
  default:
    return "unknown";
  }
}

static void
firewire_set_phase_locked(struct firewire_stub_probe *sc, uchar phase, uint now)
{
  if(!sc)
    return;
  if(sc->phase == phase)
    return;

  sc->phase = phase;
  sc->phase_changes++;
  sc->last_phase_change_tick = now;
}

static void
firewire_async_reset_locked(struct firewire_stub_probe *sc)
{
  int i;

  if(!sc)
    return;

  for(i = 0; i < FIREWIRE_ASYNC_CTX_MAX; i++){
    sc->async_ctx[i].token = 0;
    sc->async_ctx[i].submit_tick = 0;
    sc->async_ctx[i].complete_tick = 0;
    sc->async_ctx[i].in_use = 0;
    sc->async_ctx[i].generation = sc->generation;
    sc->async_ctx[i].status = 0;
  }
  sc->async_head = 0;
  sc->async_tail = 0;
  sc->async_pending = 0;
}

static void
firewire_async_invalidate_generation_locked(struct firewire_stub_probe *sc)
{
  int i;

  if(!sc)
    return;

  for(i = 0; i < FIREWIRE_ASYNC_CTX_MAX; i++){
    if(sc->async_ctx[i].in_use){
      sc->async_stale_drop_count++;
      sc->async_ctx[i].in_use = 0;
      sc->async_ctx[i].status = -1;
    }
  }
  sc->async_head = 0;
  sc->async_tail = 0;
  sc->async_pending = 0;
}

static uint
firewire_async_submit_locked(struct firewire_stub_probe *sc, uint now)
{
  struct firewire_async_ctx *ctx;

  if(!sc)
    return 0;
  if(sc->async_pending >= FIREWIRE_ASYNC_CTX_MAX)
    return 0;

  ctx = &sc->async_ctx[sc->async_tail];
  if(ctx->in_use)
    return 0;

  sc->async_next_token++;
  if(sc->async_next_token == 0)
    sc->async_next_token = 1;

  ctx->token = sc->async_next_token;
  ctx->submit_tick = now;
  ctx->complete_tick = 0;
  ctx->in_use = 1;
  ctx->generation = sc->generation;
  ctx->status = 0;

  sc->async_tail = (sc->async_tail + 1) % FIREWIRE_ASYNC_CTX_MAX;
  sc->async_pending++;
  sc->async_submit_count++;

  return ctx->token;
}

static void
firewire_async_complete_one_locked(struct firewire_stub_probe *sc, uint now, short status)
{
  struct firewire_async_ctx *ctx;

  if(!sc)
    return;
  if(sc->async_pending == 0)
    return;

  ctx = &sc->async_ctx[sc->async_head];
  if(!ctx->in_use){
    sc->async_head = (sc->async_head + 1) % FIREWIRE_ASYNC_CTX_MAX;
    if(sc->async_pending > 0)
      sc->async_pending--;
    return;
  }

  ctx->complete_tick = now;
  ctx->status = status;

  if(ctx->generation != sc->generation)
    sc->async_stale_drop_count++;
  else
    sc->async_complete_count++;

  ctx->in_use = 0;
  sc->async_head = (sc->async_head + 1) % FIREWIRE_ASYNC_CTX_MAX;
  sc->async_pending--;
}

static void
firewire_async_reap_timeouts_locked(struct firewire_stub_probe *sc, uint now)
{
  uint guard;

  if(!sc)
    return;

  for(guard = 0; guard < FIREWIRE_ASYNC_CTX_MAX && sc->async_pending > 0; guard++){
    struct firewire_async_ctx *ctx = &sc->async_ctx[sc->async_head];
    uint age;

    if(!ctx->in_use){
      sc->async_head = (sc->async_head + 1) % FIREWIRE_ASYNC_CTX_MAX;
      sc->async_pending--;
      continue;
    }

    age = now - ctx->submit_tick;
    if(age < FIREWIRE_ASYNC_TIMEOUT_TICKS)
      break;

    ctx->complete_tick = now;
    ctx->status = -2;
    ctx->in_use = 0;
    sc->async_timeout_count++;
    sc->async_head = (sc->async_head + 1) % FIREWIRE_ASYNC_CTX_MAX;
    sc->async_pending--;
  }
}

static uint
firewire_async_oldest_age_snapshot(struct firewire_stub_probe *sc, uint now)
{
  struct firewire_async_ctx *ctx;

  if(!sc)
    return 0;
  if(sc->async_pending == 0)
    return 0;

  ctx = &sc->async_ctx[sc->async_head];
  if(!ctx->in_use)
    return 0;
  return now - ctx->submit_tick;
}

static void
firewire_apply_events_locked(struct firewire_stub_probe *sc, uint evt, uint now, int polled)
{
  if(!sc)
    return;

  sc->last_event = evt;
  if(polled)
    sc->polled_event_count++;

  if(evt & OHCI_INT_BUS_RESET){
    sc->bus_reset_count++;
    sc->generation++;
    sc->reset_pending = 1;
    firewire_async_invalidate_generation_locked(sc);
    firewire_set_phase_locked(sc, FIREWIRE_PHASE_RESETTING, now);
    sc->node_id = firewire_read(sc, OHCI_REG_NODE_ID);
  }
  if(evt & OHCI_INT_SELF_ID_COMPLETE){
    sc->self_id_irq_count++;
    sc->self_id_count = firewire_read(sc, OHCI_REG_SELF_ID_COUNT);
    if(sc->reset_pending)
      sc->reset_pending = 0;
    firewire_set_phase_locked(sc, FIREWIRE_PHASE_READY, now);
    firewire_async_submit_locked(sc, now);
    firewire_async_complete_one_locked(sc, now, 0);
  }
}

static void
firewire_service_polled(struct firewire_stub_probe *sc)
{
  uint evt;
  uint now;

  if(!sc || !sc->attached || !sc->regs)
    return;
  if(sc->irq_registered)
    return;

  evt = firewire_read(sc, OHCI_REG_INT_EVENT_SET);
  if(evt == 0 || evt == 0xFFFFFFFF)
    return;

  firewire_write(sc, OHCI_REG_INT_EVENT_CLEAR, evt);
  now = firewire_current_ticks();

  acquire(&firewire_lock);
  firewire_async_reap_timeouts_locked(sc, now);
  firewire_apply_events_locked(sc, evt, now, 1);
  release(&firewire_lock);
}

static int
firewire_reset_controller(struct firewire_stub_probe *sc)
{
  int i;

  firewire_write(sc, OHCI_REG_HC_CONTROL_SET, OHCI_HCCTRL_SOFT_RESET);
  for(i = 0; i < 1000; i++){
    uint hc = firewire_read(sc, OHCI_REG_HC_CONTROL_CLEAR);
    if((hc & OHCI_HCCTRL_SOFT_RESET) == 0)
      return 0;
    microdelay(10);
  }
  return -1;
}

static void
firewire_irq_handler(int irq, void *arg)
{
  struct firewire_stub_probe *sc;
  uint evt;
  uint now;

  (void)irq;
  sc = (struct firewire_stub_probe *)arg;
  if(!sc || !sc->attached || !sc->regs)
    return;

  evt = firewire_read(sc, OHCI_REG_INT_EVENT_SET);
  if(evt == 0 || evt == 0xFFFFFFFF)
    return;

  firewire_write(sc, OHCI_REG_INT_EVENT_CLEAR, evt);
  now = firewire_current_ticks();

  acquire(&firewire_lock);
  firewire_async_reap_timeouts_locked(sc, now);
  sc->irq_count++;
  firewire_apply_events_locked(sc, evt, now, 0);
  release(&firewire_lock);
}

static int
firewire_attach_probe(struct pci_dev *dev)
{
  struct firewire_stub_probe *entry;
  void *bar0v;

  if(!dev)
    return -1;

  pci_enable_io(dev);
  pci_enable_mem(dev);
  pci_set_master(dev);

  bar0v = pci_map_bar(dev, 0);
  if(!bar0v)
    return -1;

  {
    uint now = firewire_current_ticks();

    acquire(&firewire_lock);
    if(firewire_stub_probe_count >= FIREWIRE_STUB_MAX){
      release(&firewire_lock);
      return -1;
    }

    entry = &firewire_stub_probes[firewire_stub_probe_count++];
    memset(entry, 0, sizeof(*entry));
    entry->vendor_id = dev->vendor_id;
    entry->device_id = dev->device_id;
    entry->class_code = dev->class_code;
    entry->subclass = dev->subclass;
    entry->prog_if = dev->prog_if;
    entry->bus = dev->bus;
    entry->slot = dev->slot;
    entry->func = dev->func;
    entry->irq_line = dev->irq_line;
    entry->bar0 = pci_bar_base(dev, 0);
    entry->bar0_size = pci_bar_size(dev, 0);
    entry->regs = (volatile uint *)bar0v;
    entry->version = firewire_read(entry, OHCI_REG_VERSION);
    entry->bus_options = firewire_read(entry, OHCI_REG_BUS_OPTIONS);
    entry->node_id = firewire_read(entry, OHCI_REG_NODE_ID);
    entry->self_id_count = firewire_read(entry, OHCI_REG_SELF_ID_COUNT);
    entry->phase = FIREWIRE_PHASE_INIT;
    entry->phase_changes = 1;
    entry->last_phase_change_tick = now;
    firewire_async_reset_locked(entry);
    entry->attached = 1;
    release(&firewire_lock);
  }

  if(entry->version == 0xFFFFFFFF || entry->version == 0){
    uint now = firewire_current_ticks();
    acquire(&firewire_lock);
    entry->init_failures++;
    firewire_set_phase_locked(entry, FIREWIRE_PHASE_DEGRADED, now);
    release(&firewire_lock);
    return -1;
  }

  if(firewire_reset_controller(entry) < 0){
    uint now = firewire_current_ticks();
    acquire(&firewire_lock);
    entry->init_failures++;
    firewire_set_phase_locked(entry, FIREWIRE_PHASE_DEGRADED, now);
    release(&firewire_lock);
  }

  firewire_write(entry, OHCI_REG_INT_MASK_CLEAR, 0xFFFFFFFFU);
  firewire_write(entry, OHCI_REG_INT_EVENT_CLEAR, 0xFFFFFFFFU);
  firewire_write(entry, OHCI_REG_INT_MASK_SET, OHCI_INT_CORE_MASK);
  firewire_write(entry, OHCI_REG_HC_CONTROL_SET, OHCI_HCCTRL_LINK_ENABLE);

  entry->irq_registered = 0;
  int irq = -1;
  if (pci_irq_alloc_vectors(dev, 1, 1, PCI_IRQ_F_ALL) >= 1) {
    irq = pci_irq_vector(dev, 0);
    if (irq >= 0 && irq_register(irq, firewire_irq_handler, entry, "firewire") == 0) {
      uint now = firewire_current_ticks();
      if (pci_irq_mode(dev) == PCI_IRQ_MODE_INTX)
        pci_enable_irq(dev, ncpu - 1);
      acquire(&firewire_lock);
      entry->irq_registered = 1;
      if(entry->phase != FIREWIRE_PHASE_DEGRADED)
        firewire_set_phase_locked(entry, FIREWIRE_PHASE_READY, now);
      if(entry->phase != FIREWIRE_PHASE_DEGRADED)
        firewire_async_submit_locked(entry, now);
      release(&firewire_lock);
    } else {
      if (irq >= 0)
        pci_irq_free_vectors(dev);
      uint now = firewire_current_ticks();
      acquire(&firewire_lock);
      entry->init_failures++;
      firewire_set_phase_locked(entry, FIREWIRE_PHASE_DEGRADED, now);
      release(&firewire_lock);
    }
  } else {
    uint now = firewire_current_ticks();
    acquire(&firewire_lock);
    entry->init_failures++;
    firewire_set_phase_locked(entry, FIREWIRE_PHASE_DEGRADED, now);
    release(&firewire_lock);
  }

  return 0;
}

int
firewire_procfs_dump(char *buf, uint max)
{
  struct firewire_stub_probe snap[FIREWIRE_STUB_MAX];
  uint count;
  uint i;
  uint len;
  uint now;

  if(!buf || max == 0)
    return -1;

  if(!firewire_lock_ready){
    if(max >= 1)
      buf[0] = 0;
    return 0;
  }

  now = firewire_current_ticks();

  for(i = 0; i < FIREWIRE_STUB_MAX; i++)
    firewire_service_polled(&firewire_stub_probes[i]);

  acquire(&firewire_lock);
  for(i = 0; i < firewire_stub_probe_count; i++)
    firewire_async_reap_timeouts_locked(&firewire_stub_probes[i], now);
  release(&firewire_lock);

  acquire(&firewire_lock);
  count = firewire_stub_probe_count;
  if(count > FIREWIRE_STUB_MAX)
    count = FIREWIRE_STUB_MAX;
  memmove(snap, firewire_stub_probes, sizeof(snap[0]) * count);
  release(&firewire_lock);

  len = 0;
  if(firewire_buf_puts(buf, max, &len,
                       "ven:dev class/sub/pif bus:slot.fn irq gen nodeid version resets selfid irqev last phase phchg phtick polled apend asub acomp astale atimeout aage status\n") < 0)
    return -1;

  for(i = 0; i < count; i++){
    struct firewire_stub_probe *p = &snap[i];

    if(firewire_buf_puthex16(buf, max, &len, p->vendor_id) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, ':') < 0) return -1;
    if(firewire_buf_puthex16(buf, max, &len, p->device_id) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(firewire_buf_puthex8(buf, max, &len, p->class_code) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, '/') < 0) return -1;
    if(firewire_buf_puthex8(buf, max, &len, p->subclass) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, '/') < 0) return -1;
    if(firewire_buf_puthex8(buf, max, &len, p->prog_if) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(firewire_buf_putu(buf, max, &len, p->bus) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, ':') < 0) return -1;
    if(firewire_buf_putu(buf, max, &len, p->slot) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, '.') < 0) return -1;
    if(firewire_buf_putu(buf, max, &len, p->func) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(firewire_buf_putu(buf, max, &len, p->irq_line) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(firewire_buf_putu(buf, max, &len, p->generation) < 0) return -1;
    if(firewire_buf_puts(buf, max, &len, " 0x") < 0) return -1;
    if(firewire_buf_puthex32(buf, max, &len, p->node_id) < 0) return -1;
    if(firewire_buf_puts(buf, max, &len, " 0x") < 0) return -1;
    if(firewire_buf_puthex32(buf, max, &len, p->version) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(firewire_buf_putu(buf, max, &len, p->bus_reset_count) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(firewire_buf_putu(buf, max, &len, p->self_id_irq_count) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(firewire_buf_putu(buf, max, &len, p->irq_count) < 0) return -1;
    if(firewire_buf_puts(buf, max, &len, " 0x") < 0) return -1;
    if(firewire_buf_puthex32(buf, max, &len, p->last_event) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(firewire_buf_puts(buf, max, &len, firewire_phase_name(p->phase)) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(firewire_buf_putu(buf, max, &len, p->phase_changes) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(firewire_buf_putu(buf, max, &len, p->last_phase_change_tick) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(firewire_buf_putu(buf, max, &len, p->polled_event_count) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(firewire_buf_putu(buf, max, &len, p->async_pending) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(firewire_buf_putu(buf, max, &len, p->async_submit_count) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(firewire_buf_putu(buf, max, &len, p->async_complete_count) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(firewire_buf_putu(buf, max, &len, p->async_stale_drop_count) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(firewire_buf_putu(buf, max, &len, p->async_timeout_count) < 0) return -1;
    if(firewire_buf_putc(buf, max, &len, ' ') < 0) return -1;
    if(firewire_buf_putu(buf, max, &len, firewire_async_oldest_age_snapshot(p, now)) < 0) return -1;
    if(p->irq_registered){
      if(firewire_buf_puts(buf, max, &len, " irq\n") < 0) return -1;
    } else if(p->init_failures){
      if(firewire_buf_puts(buf, max, &len, " degraded\n") < 0) return -1;
    } else {
      if(firewire_buf_puts(buf, max, &len, " polled\n") < 0) return -1;
    }
  }

  if(firewire_buf_puts(buf, max, &len, "summary total=") < 0) return -1;
  if(firewire_buf_putu(buf, max, &len, count) < 0) return -1;
  if(firewire_buf_puts(buf, max, &len, " transport=0 bar=0x") < 0) return -1;
  if(count){
    if(firewire_buf_puthex32(buf, max, &len, snap[0].bar0) < 0) return -1;
  } else {
    if(firewire_buf_puthex32(buf, max, &len, 0) < 0) return -1;
  }
  if(firewire_buf_puts(buf, max, &len, " bar_size=") < 0) return -1;
  if(count){
    if(firewire_buf_putu(buf, max, &len, snap[0].bar0_size) < 0) return -1;
  } else {
    if(firewire_buf_putu(buf, max, &len, 0) < 0) return -1;
  }
  if(firewire_buf_putc(buf, max, &len, '\n') < 0) return -1;

  return (int)len;
}

void
firewire_init(void)
{
  int i;
  uint found;

  if(!firewire_lock_ready){
    initlock(&firewire_lock, "firewire");
    lockdep_set_rank(&firewire_lock, LOCK_RANK_DEFAULT, "firewire");
    firewire_lock_ready = 1;
  }

  acquire(&firewire_lock);
  firewire_stub_probe_count = 0;
  release(&firewire_lock);

  found = 0;
  BOOTDBG("firewire: probing IEEE1394 OHCI controllers\n");
  for(i = 0; i < pci_device_count(); i++){
    struct pci_dev *dev = pci_get_device(i);
    if(!firewire_is_match(dev))
      continue;
    if(firewire_attach_probe(dev) == 0)
      found++;
  }

  BOOTDBG("firewire: discovered %d controller(s)\n", found);
}
