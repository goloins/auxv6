#include "types.h"
#include "defs.h"
#include "memlayout.h"
#include "mmu.h"
#include "pci.h"
#include "usb_hcd.h"

#define XHCI_REG_CAPLENGTH   0x00
#define XHCI_REG_HCSPARAMS1  0x04

#define XHCI_OP_USBCMD       0x00
#define XHCI_OP_USBSTS       0x04

#define XHCI_USBCMD_RUNSTOP  (1U << 0)
#define XHCI_USBCMD_HCRST    (1U << 1)

#define XHCI_USBSTS_HCH      (1U << 0)
#define XHCI_USBSTS_CNR      (1U << 11)
#define XHCI_USBSTS_EINT     (1U << 3)
#define XHCI_USBSTS_PCD      (1U << 4)

#define XHCI_POLL_TRIES      4000
#define XHCI_POLL_DELAY_US   10

#define XHCI_REG_DBOFF       0x14
#define XHCI_REG_RTSOFF      0x18
#define XHCI_REG_HCCPARAMS1  0x10

#define XHCI_OP_PAGESIZE     0x08
#define XHCI_OP_CRCR         0x18
#define XHCI_OP_DCBAAP       0x30
#define XHCI_OP_CONFIG       0x38
#define XHCI_OP_PORTSC_BASE  0x400

#define XHCI_PORTSC_CCS      (1U << 0)    /* Current Connect Status */
#define XHCI_PORTSC_PED      (1U << 1)    /* Port Enabled/Disabled */
#define XHCI_PORTSC_PR       (1U << 4)    /* Port Reset */
#define XHCI_PORTSC_CSC      (1U << 17)   /* Connect Status Change (RW1C) */
#define XHCI_PORTSC_SPEED_SHIFT 10
#define XHCI_PORTSC_SPEED_MASK  (0xFU << XHCI_PORTSC_SPEED_SHIFT)

#define XHCI_PSIV_FULL       1U
#define XHCI_PSIV_LOW        2U
#define XHCI_PSIV_HIGH       3U
#define XHCI_PSIV_SUPER      4U

#define XHCI_RT_IR0          0x20
#define XHCI_RT_IMAN         0x00
#define XHCI_RT_ERSTSZ       0x08
#define XHCI_RT_ERSTBA       0x10
#define XHCI_RT_ERDP         0x18

#define XHCI_IMAN_IP         (1U << 0)
#define XHCI_IMAN_IE         (1U << 1)

#define XHCI_TRB_TYPE_LINK            6
#define XHCI_TRB_TYPE_ENABLE_SLOT     9
#define XHCI_TRB_TYPE_DISABLE_SLOT    10
#define XHCI_TRB_TYPE_ADDRESS_DEVICE  11
#define XHCI_TRB_TYPE_SETUP_STAGE     2
#define XHCI_TRB_TYPE_DATA_STAGE      3
#define XHCI_TRB_TYPE_STATUS_STAGE    4
#define XHCI_TRB_TYPE_NORMAL          1
#define XHCI_TRB_TYPE_CONFIG_ENDPOINT 12

#define XHCI_EVENT_TYPE_TRANSFER      32
#define XHCI_EVENT_TYPE_CMD_COMPLETE  33

#define XHCI_TRB_CYCLE      (1U << 0)
#define XHCI_TRB_ENT        (1U << 1)
#define XHCI_TRB_ISP        (1U << 2)
#define XHCI_TRB_IDT        (1U << 6)
#define XHCI_TRB_IOC        (1U << 5)
#define XHCI_TRB_CHAIN      (1U << 4)

#define XHCI_TRB_DIR_IN     (1U << 16)
#define XHCI_TRB_TRT_IN     (3U << 16)

#define XHCI_CRCR_RCS       (1U << 0)
#define XHCI_ERDP_EHB       (1U << 3)

#define XHCI_CTRL_MAX       USB_HC_MAX
#define XHCI_RING_TRBS      256

#define XHCI_CC_SUCCESS     1
#define XHCI_CC_SHORT_PKT   13

struct xhci_trb {
  uint d0;
  uint d1;
  uint d2;
  uint d3;
};

struct xhci_erst_ent {
  uint seg_base_lo;
  uint seg_base_hi;
  uint seg_size;
  uint rsvd;
};

struct xhci_bulk_state {
  uchar active;
  uchar in_flight;
  uchar port;
  uchar ep_in;
  uchar ep_out;
  ushort cur_len;
  uint slot_id;
  uint ep_dci;
  uint add_flags;
  char *input_ctx;
  char *output_ctx;
  struct xhci_trb *xfer_ring;
  uint input_ctx_pa;
  uint output_ctx_pa;
  uint xfer_ring_pa;
};

struct xhci_ctrl_state {
  uchar initialized;
  uchar csz64;
  uchar cmd_cycle;
  uchar evt_cycle;
  uint opbase;
  uint db_off;
  uint rt_off;
  uint max_slots;
  uint cmd_prod;
  uint evt_cons;
  volatile uint *regs;
  struct xhci_trb *cmd_ring;
  struct xhci_trb *evt_ring;
  struct xhci_erst_ent *erst;
  uint cmd_ring_pa;
  uint evt_ring_pa;
  uint erst_pa;
  uint dcbaa_pa;
  uint *dcbaa;
  struct xhci_bulk_state bulk;
};

static struct xhci_ctrl_state xhci_state[XHCI_CTRL_MAX];

static volatile uint* xhci_regs(struct pci_dev *dev);
static uint xhci_read(volatile uint *base, uint off);
static void xhci_write(volatile uint *base, uint off, uint val);
static void xhci_bulk_state_reset(struct xhci_ctrl_state *xs);
static int xhci_bulk_state_prepare(struct xhci_ctrl_state *xs, uchar port,
                                   uchar ep_in, uchar ep_out);
static int xhci_bulk_submit_async(struct xhci_ctrl_state *xs, uchar port,
                                  uchar ep_in, uchar ep_out,
                                  uchar *buf, ushort len);
static int xhci_bulk_reap_async(struct xhci_ctrl_state *xs, uchar port,
                                uchar ep_in, uchar ep_out,
                                ushort *out_len);

static void
xhci_write64(volatile uint *base, uint off, uint val)
{
  xhci_write(base, off, val);
  xhci_write(base, off + 4, 0);
}

static int
xhci_event_ack(struct xhci_ctrl_state *xs)
{
  uint off;
  uint ptr;

  if(!xs || !xs->regs)
    return -1;

  off = xs->rt_off + XHCI_RT_IR0 + XHCI_RT_ERDP;
  ptr = xs->evt_ring_pa + (xs->evt_cons * sizeof(struct xhci_trb));
  xhci_write64(xs->regs, off, ptr | XHCI_ERDP_EHB);
  return 0;
}

static int
xhci_wait_event(struct xhci_ctrl_state *xs, uint type, uint *ev0, uint *ev2,
                uint *ev3, uint timeout_tries)
{
  uint tries;

  if(!xs || !xs->evt_ring)
    return -1;

  for(tries = 0; tries < timeout_tries; tries++){
    struct xhci_trb *ev;
    uint cyc;
    uint evtype;

    ev = &xs->evt_ring[xs->evt_cons];
    cyc = ev->d3 & XHCI_TRB_CYCLE;
    if((xs->evt_cycle ? XHCI_TRB_CYCLE : 0) != cyc){
      microdelay(10);
      continue;
    }

    evtype = (ev->d3 >> 10) & 0x3f;
    if(evtype == type){
      if(ev0)
        *ev0 = ev->d0;
      if(ev2)
        *ev2 = ev->d2;
      if(ev3)
        *ev3 = ev->d3;
    }

    xs->evt_cons++;
    if(xs->evt_cons >= XHCI_RING_TRBS){
      xs->evt_cons = 0;
      xs->evt_cycle ^= 1;
    }
    xhci_event_ack(xs);

    if(evtype == type)
      return 0;
  }

  return -1;
}

static int
xhci_cmd_submit(struct xhci_ctrl_state *xs, uint type, uint d0, uint d1,
                uint d2, uint d3_extra)
{
  struct xhci_trb *trb;
  uint cyc;

  if(!xs || !xs->cmd_ring)
    return -1;

  if(xs->cmd_prod >= (XHCI_RING_TRBS - 1))
    return -1;

  trb = &xs->cmd_ring[xs->cmd_prod];
  cyc = xs->cmd_cycle ? XHCI_TRB_CYCLE : 0;

  trb->d0 = d0;
  trb->d1 = d1;
  trb->d2 = d2;
  trb->d3 = cyc | (type << 10) | d3_extra;

  xs->cmd_prod++;
  if(xs->cmd_prod == (XHCI_RING_TRBS - 1)){
    struct xhci_trb *link = &xs->cmd_ring[xs->cmd_prod];
    link->d0 = xs->cmd_ring_pa;
    link->d1 = 0;
    link->d2 = 0;
    link->d3 = (xs->cmd_cycle ? XHCI_TRB_CYCLE : 0) |
               (XHCI_TRB_TYPE_LINK << 10) | XHCI_TRB_ENT;
    xs->cmd_prod = 0;
    xs->cmd_cycle ^= 1;
  }

  xhci_write(xs->regs, xs->db_off + 0, 0);
  return 0;
}

static int
xhci_cmd_submit_wait(struct xhci_ctrl_state *xs, uint type, uint d0, uint d1,
                     uint d2, uint d3_extra, uint *slot_id)
{
  uint ev2;
  uint ev3;
  uint cc;

  if(xhci_cmd_submit(xs, type, d0, d1, d2, d3_extra) < 0)
    return -1;
  if(xhci_wait_event(xs, XHCI_EVENT_TYPE_CMD_COMPLETE, 0, &ev2, &ev3,
                     200000) < 0)
    return -1;

  cc = (ev2 >> 24) & 0xff;
  if(cc != XHCI_CC_SUCCESS)
    return -1;

  if(slot_id)
    *slot_id = (ev3 >> 24) & 0xff;
  return 0;
}

static uint
xhci_ctx_size(struct xhci_ctrl_state *xs)
{
  return xs->csz64 ? 64U : 32U;
}

static uint *
xhci_ctx_dwords(char *base, uint off)
{
  return (uint *)(base + off);
}

static uint
xhci_mps0_for_port(struct xhci_ctrl_state *xs, uchar port)
{
  uint off;
  uint portsc;
  uint psiv;

  if(!xs || !xs->regs || port == 0)
    return 8;

  off = xs->opbase + XHCI_OP_PORTSC_BASE + ((uint)(port - 1) * 0x10);
  portsc = xhci_read(xs->regs, off);
  psiv = (portsc & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;

  switch(psiv){
  case XHCI_PSIV_LOW:
    return 8;
  case XHCI_PSIV_FULL:
    return 64;
  case XHCI_PSIV_HIGH:
    return 64;
  case XHCI_PSIV_SUPER:
    return 512;
  default:
    return 8;
  }
}

static uint
xhci_slot_speed_for_port(struct xhci_ctrl_state *xs, uchar port)
{
  uint off;
  uint portsc;
  uint psiv;

  if(!xs || !xs->regs || port == 0)
    return 1;

  off = xs->opbase + XHCI_OP_PORTSC_BASE + ((uint)(port - 1) * 0x10);
  portsc = xhci_read(xs->regs, off);
  psiv = (portsc & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
  if(psiv == 0)
    return 1;
  return psiv & 0x0f;
}

static void
xhci_bulk_state_reset(struct xhci_ctrl_state *xs)
{
  struct xhci_bulk_state *bs;

  if(!xs)
    return;

  bs = &xs->bulk;
  if(bs->active && bs->slot_id != 0 && xs->initialized && xs->regs)
    (void)xhci_cmd_submit_wait(xs, XHCI_TRB_TYPE_DISABLE_SLOT,
                               0, 0, 0, (bs->slot_id << 24), 0);

  if(bs->xfer_ring)
    kfree((char*)bs->xfer_ring);
  if(bs->output_ctx)
    kfree(bs->output_ctx);
  if(bs->input_ctx)
    kfree(bs->input_ctx);
  memset(bs, 0, sizeof(*bs));
}

static int
xhci_bulk_state_prepare(struct xhci_ctrl_state *xs, uchar port,
                        uchar ep_in, uchar ep_out)
{
  struct xhci_bulk_state *bs;
  uint *ctrl_ctx;
  uint *slot_ctx;
  uint *ep0_ctx;
  uint ctxsz;
  uint ep_num;
  uint mps0;
  uint slot_speed;

  if(!xs || !xs->initialized || port == 0)
    return -1;
  if((ep_out & 0x80) != 0 || (ep_in & 0x80) == 0)
    return -1;

  ep_num = ep_out & 0x0f;
  if(ep_num == 0)
    return -1;

  bs = &xs->bulk;
  if(bs->active && bs->port == port && bs->ep_in == ep_in && bs->ep_out == ep_out)
    return 0;

  xhci_bulk_state_reset(xs);

  bs->input_ctx = kalloc();
  bs->output_ctx = kalloc();
  bs->xfer_ring = (struct xhci_trb *)kalloc();
  if(!bs->input_ctx || !bs->output_ctx || !bs->xfer_ring){
    xhci_bulk_state_reset(xs);
    return -1;
  }

  memset(bs->input_ctx, 0, PGSIZE);
  memset(bs->output_ctx, 0, PGSIZE);
  memset(bs->xfer_ring, 0, PGSIZE);
  bs->input_ctx_pa = V2P(bs->input_ctx);
  bs->output_ctx_pa = V2P(bs->output_ctx);
  bs->xfer_ring_pa = V2P(bs->xfer_ring);
  bs->port = port;
  bs->ep_in = ep_in;
  bs->ep_out = ep_out;
  bs->ep_dci = ep_num * 2;
  bs->add_flags = (1U << 0) | (1U << 1) | (1U << bs->ep_dci);

  if(xhci_cmd_submit_wait(xs, XHCI_TRB_TYPE_ENABLE_SLOT, 0, 0, 0, 0,
                          &bs->slot_id) < 0){
    xhci_bulk_state_reset(xs);
    return -1;
  }
  if(bs->slot_id == 0 || bs->slot_id >= 256){
    xhci_bulk_state_reset(xs);
    return -1;
  }

  xs->dcbaa[bs->slot_id * 2] = bs->output_ctx_pa;
  xs->dcbaa[bs->slot_id * 2 + 1] = 0;

  ctxsz = xhci_ctx_size(xs);
  ctrl_ctx = xhci_ctx_dwords(bs->input_ctx, 0);
  slot_ctx = xhci_ctx_dwords(bs->input_ctx, ctxsz);
  ep0_ctx = xhci_ctx_dwords(bs->input_ctx, ctxsz * 2);

  mps0 = xhci_mps0_for_port(xs, port);
  slot_speed = xhci_slot_speed_for_port(xs, port);

  ctrl_ctx[1] = (1U << 0) | (1U << 1);
  slot_ctx[0] = ((slot_speed & 0x0f) << 20) | (1U << 27);
  slot_ctx[1] = ((uint)port << 16);

  ep0_ctx[1] = (3U << 1) | (4U << 3) | ((mps0 & 0xffff) << 16);
  ep0_ctx[2] = bs->xfer_ring_pa | 1U;
  ep0_ctx[4] = 8;

  if(xhci_cmd_submit_wait(xs, XHCI_TRB_TYPE_ADDRESS_DEVICE,
                          bs->input_ctx_pa, 0, 0,
                          (bs->slot_id << 24), 0) < 0){
    xhci_bulk_state_reset(xs);
    return -1;
  }

  bs->active = 1;
  return 0;
}

static int
xhci_runtime_init(struct usb_hc_probe *sc, struct pci_dev *dev,
                  struct xhci_ctrl_state **out)
{
  struct xhci_ctrl_state *xs;
  volatile uint *regs;
  uint db_off;
  uint rt_off;
  uint hccparams1;
  uint hcsp1;

  if(!sc || !dev || !out)
    return -1;
  if(sc->pci_index >= XHCI_CTRL_MAX)
    return -1;

  xs = &xhci_state[sc->pci_index];
  if(xs->initialized){
    *out = xs;
    return 0;
  }

  pci_enable_mem(dev);
  pci_set_master(dev);

  regs = xhci_regs(dev);
  if(!regs)
    return -1;

  db_off = xhci_read(regs, XHCI_REG_DBOFF) & ~0x3U;
  rt_off = xhci_read(regs, XHCI_REG_RTSOFF) & ~0x1fU;
  hccparams1 = xhci_read(regs, XHCI_REG_HCCPARAMS1);
  hcsp1 = xhci_read(regs, XHCI_REG_HCSPARAMS1);

  memset(xs, 0, sizeof(*xs));
  xs->regs = regs;
  xs->opbase = (uint)sc->cap_length;
  xs->db_off = db_off;
  xs->rt_off = rt_off;
  xs->csz64 = (hccparams1 & (1U << 2)) ? 1 : 0;
  xs->max_slots = hcsp1 & 0xff;
  if(xs->max_slots == 0)
    xs->max_slots = 8;

  xs->cmd_ring = (struct xhci_trb *)kalloc();
  xs->evt_ring = (struct xhci_trb *)kalloc();
  xs->erst = (struct xhci_erst_ent *)kalloc();
  xs->dcbaa = (uint *)kalloc();
  if(!xs->cmd_ring || !xs->evt_ring || !xs->erst || !xs->dcbaa)
    return -1;

  memset(xs->cmd_ring, 0, PGSIZE);
  memset(xs->evt_ring, 0, PGSIZE);
  memset(xs->erst, 0, PGSIZE);
  memset(xs->dcbaa, 0, PGSIZE);

  xs->cmd_ring_pa = V2P(xs->cmd_ring);
  xs->evt_ring_pa = V2P(xs->evt_ring);
  xs->erst_pa = V2P(xs->erst);
  xs->dcbaa_pa = V2P(xs->dcbaa);

  xs->cmd_cycle = 1;
  xs->evt_cycle = 1;
  xs->cmd_prod = 0;
  xs->evt_cons = 0;

  xs->cmd_ring[XHCI_RING_TRBS - 1].d0 = xs->cmd_ring_pa;
  xs->cmd_ring[XHCI_RING_TRBS - 1].d1 = 0;
  xs->cmd_ring[XHCI_RING_TRBS - 1].d2 = 0;
  xs->cmd_ring[XHCI_RING_TRBS - 1].d3 = XHCI_TRB_CYCLE |
                                        (XHCI_TRB_TYPE_LINK << 10) |
                                        XHCI_TRB_ENT;

  xs->erst[0].seg_base_lo = xs->evt_ring_pa;
  xs->erst[0].seg_base_hi = 0;
  xs->erst[0].seg_size = XHCI_RING_TRBS;
  xs->erst[0].rsvd = 0;

  xhci_write64(xs->regs, xs->opbase + XHCI_OP_DCBAAP, xs->dcbaa_pa);
  xhci_write64(xs->regs, xs->opbase + XHCI_OP_CRCR, xs->cmd_ring_pa | XHCI_CRCR_RCS);
  xhci_write(xs->regs, xs->opbase + XHCI_OP_CONFIG, xs->max_slots & 0xff);

  xhci_write(xs->regs, xs->rt_off + XHCI_RT_IR0 + XHCI_RT_ERSTSZ, 1);
  xhci_write64(xs->regs, xs->rt_off + XHCI_RT_IR0 + XHCI_RT_ERSTBA, xs->erst_pa);
  xhci_write64(xs->regs, xs->rt_off + XHCI_RT_IR0 + XHCI_RT_ERDP,
               xs->evt_ring_pa | XHCI_ERDP_EHB);
  xhci_write(xs->regs, xs->rt_off + XHCI_RT_IR0 + XHCI_RT_IMAN,
             XHCI_IMAN_IE | XHCI_IMAN_IP);

  xs->initialized = 1;
  *out = xs;
  return 0;
}

static int
xhci_get_descriptor_n(struct xhci_ctrl_state *xs, uchar port,
                      uchar desc_type, uchar desc_index,
                      uchar *out, uint dlen)
{
  char *input_ctx;
  char *output_ctx;
  struct xhci_trb *xfer_ring;
  uchar *buf;
  uint input_ctx_pa;
  uint output_ctx_pa;
  uint xfer_ring_pa;
  uint buf_pa;
  uint slot_id;
  uint ctxsz;
  uint *ctrl_ctx;
  uint *slot_ctx;
  uint *ep0_ctx;
  uint mps0;
  uint slot_speed;
  int slot_enabled;
  int ret;
  uint ev2;
  uint ev3;

  if(!xs || !xs->initialized || !out || port == 0)
    return -1;
  if(dlen == 0 || dlen > PGSIZE)
    return -1;

  ret = -1;
  slot_enabled = 0;
  slot_id = 0;
  input_ctx = 0;
  output_ctx = 0;
  xfer_ring = 0;
  buf = 0;

  input_ctx = kalloc();
  output_ctx = kalloc();
  xfer_ring = (struct xhci_trb *)kalloc();
  buf = (uchar *)kalloc();
  if(!input_ctx || !output_ctx || !xfer_ring || !buf)
    goto out;

  memset(input_ctx, 0, PGSIZE);
  memset(output_ctx, 0, PGSIZE);
  memset(xfer_ring, 0, PGSIZE);
  memset(buf, 0, PGSIZE);

  input_ctx_pa = V2P(input_ctx);
  output_ctx_pa = V2P(output_ctx);
  xfer_ring_pa = V2P(xfer_ring);
  buf_pa = V2P(buf);

  if(xhci_cmd_submit_wait(xs, XHCI_TRB_TYPE_ENABLE_SLOT, 0, 0, 0, 0,
                          &slot_id) < 0)
    goto out;
  slot_enabled = 1;
  if(slot_id == 0 || slot_id >= 256)
    goto out;

  xs->dcbaa[slot_id * 2] = output_ctx_pa;
  xs->dcbaa[slot_id * 2 + 1] = 0;

  ctxsz = xhci_ctx_size(xs);
  ctrl_ctx = xhci_ctx_dwords(input_ctx, 0);
  slot_ctx = xhci_ctx_dwords(input_ctx, ctxsz);
  ep0_ctx = xhci_ctx_dwords(input_ctx, ctxsz * 2);

  mps0 = xhci_mps0_for_port(xs, port);
  slot_speed = xhci_slot_speed_for_port(xs, port);

  ctrl_ctx[1] = (1U << 0) | (1U << 1);
  slot_ctx[0] = ((slot_speed & 0x0f) << 20) | (1U << 27);
  slot_ctx[1] = ((uint)port << 16);

  ep0_ctx[1] = (3U << 1) | (4U << 3) | ((mps0 & 0xffff) << 16);
  ep0_ctx[2] = xfer_ring_pa | 1U;
  ep0_ctx[4] = 8;

  if(xhci_cmd_submit_wait(xs, XHCI_TRB_TYPE_ADDRESS_DEVICE,
                          input_ctx_pa, 0, 0, (slot_id << 24), 0) < 0)
    goto out;

  xfer_ring[0].d0 = ((uint)desc_index << 8) |
                    ((uint)desc_type << 16) |
                    (0x80U << 24) |
                    6U;
  xfer_ring[0].d1 = dlen;
  xfer_ring[0].d2 = 8;
  xfer_ring[0].d3 = XHCI_TRB_CYCLE |
                    (XHCI_TRB_TYPE_SETUP_STAGE << 10) |
                    XHCI_TRB_IDT | XHCI_TRB_TRT_IN;

  xfer_ring[1].d0 = buf_pa;
  xfer_ring[1].d1 = 0;
  xfer_ring[1].d2 = dlen;
  xfer_ring[1].d3 = XHCI_TRB_CYCLE |
                    (XHCI_TRB_TYPE_DATA_STAGE << 10) | XHCI_TRB_DIR_IN;

  xfer_ring[2].d0 = 0;
  xfer_ring[2].d1 = 0;
  xfer_ring[2].d2 = 0;
  xfer_ring[2].d3 = XHCI_TRB_CYCLE |
                    (XHCI_TRB_TYPE_STATUS_STAGE << 10) |
                    XHCI_TRB_IOC;

  xfer_ring[3].d0 = xfer_ring_pa;
  xfer_ring[3].d1 = 0;
  xfer_ring[3].d2 = 0;
  xfer_ring[3].d3 = XHCI_TRB_CYCLE |
                    (XHCI_TRB_TYPE_LINK << 10) |
                    XHCI_TRB_ENT;

  xhci_write(xs->regs, xs->db_off + (slot_id * 4), 1);

  if(xhci_wait_event(xs, XHCI_EVENT_TYPE_TRANSFER, 0, &ev2, &ev3,
                     200000) < 0)
    goto out;
  if(((ev2 >> 24) & 0xff) != XHCI_CC_SUCCESS)
    goto out;
  if(((ev3 >> 24) & 0xff) != slot_id)
    goto out;

  memmove(out, buf, dlen);
  ret = 0;

out:
  if(slot_enabled)
    (void)xhci_cmd_submit_wait(xs, XHCI_TRB_TYPE_DISABLE_SLOT,
                               0, 0, 0, (slot_id << 24), 0);
  if(buf)
    kfree((char*)buf);
  if(xfer_ring)
    kfree((char*)xfer_ring);
  if(output_ctx)
    kfree(output_ctx);
  if(input_ctx)
    kfree(input_ctx);
  return ret;
}

static int
xhci_get_desc8(struct xhci_ctrl_state *xs, uchar port, uchar *out8)
{
  return xhci_get_descriptor_n(xs, port, 1, 0, out8, 8);
}

static int
xhci_get_desc18(struct xhci_ctrl_state *xs, uchar port, uchar *out18)
{
  return xhci_get_descriptor_n(xs, port, 1, 0, out18, 18);
}

static int
xhci_get_cfg_desc(struct xhci_ctrl_state *xs, uchar port, uchar *out,
                  ushort len)
{
  return xhci_get_descriptor_n(xs, port, 2, 0, out, len);
}

static int
xhci_set_configuration_req(struct xhci_ctrl_state *xs, uchar port, uchar cfg)
{
  char *input_ctx;
  char *output_ctx;
  struct xhci_trb *xfer_ring;
  uint input_ctx_pa;
  uint output_ctx_pa;
  uint xfer_ring_pa;
  uint slot_id;
  uint ctxsz;
  uint *ctrl_ctx;
  uint *slot_ctx;
  uint *ep0_ctx;
  uint mps0;
  uint slot_speed;
  int slot_enabled;
  int ret;
  uint ev2;
  uint ev3;

  if(!xs || !xs->initialized || port == 0)
    return -1;

  ret = -1;
  slot_enabled = 0;
  slot_id = 0;
  input_ctx = 0;
  output_ctx = 0;
  xfer_ring = 0;

  input_ctx = kalloc();
  output_ctx = kalloc();
  xfer_ring = (struct xhci_trb *)kalloc();
  if(!input_ctx || !output_ctx || !xfer_ring)
    goto out;

  memset(input_ctx, 0, PGSIZE);
  memset(output_ctx, 0, PGSIZE);
  memset(xfer_ring, 0, PGSIZE);

  input_ctx_pa = V2P(input_ctx);
  output_ctx_pa = V2P(output_ctx);
  xfer_ring_pa = V2P(xfer_ring);

  if(xhci_cmd_submit_wait(xs, XHCI_TRB_TYPE_ENABLE_SLOT, 0, 0, 0, 0,
                          &slot_id) < 0)
    goto out;
  slot_enabled = 1;
  if(slot_id == 0 || slot_id >= 256)
    goto out;

  xs->dcbaa[slot_id * 2] = output_ctx_pa;
  xs->dcbaa[slot_id * 2 + 1] = 0;

  ctxsz = xhci_ctx_size(xs);
  ctrl_ctx = xhci_ctx_dwords(input_ctx, 0);
  slot_ctx = xhci_ctx_dwords(input_ctx, ctxsz);
  ep0_ctx = xhci_ctx_dwords(input_ctx, ctxsz * 2);

  mps0 = xhci_mps0_for_port(xs, port);
  slot_speed = xhci_slot_speed_for_port(xs, port);

  ctrl_ctx[1] = (1U << 0) | (1U << 1);
  slot_ctx[0] = ((slot_speed & 0x0f) << 20) | (1U << 27);
  slot_ctx[1] = ((uint)port << 16);

  ep0_ctx[1] = (3U << 1) | (4U << 3) | ((mps0 & 0xffff) << 16);
  ep0_ctx[2] = xfer_ring_pa | 1U;
  ep0_ctx[4] = 8;

  if(xhci_cmd_submit_wait(xs, XHCI_TRB_TYPE_ADDRESS_DEVICE,
                          input_ctx_pa, 0, 0, (slot_id << 24), 0) < 0)
    goto out;

  /* bmRequestType=0x00, bRequest=SET_CONFIGURATION(9), wValue=cfg, wIndex=0, wLength=0 */
  xfer_ring[0].d0 = ((uint)cfg << 16) | (9U << 8) | 0x00U;
  xfer_ring[0].d1 = 0;
  xfer_ring[0].d2 = 8;
  xfer_ring[0].d3 = XHCI_TRB_CYCLE |
                    (XHCI_TRB_TYPE_SETUP_STAGE << 10);

  xfer_ring[1].d0 = 0;
  xfer_ring[1].d1 = 0;
  xfer_ring[1].d2 = 0;
  xfer_ring[1].d3 = XHCI_TRB_CYCLE |
                    (XHCI_TRB_TYPE_STATUS_STAGE << 10) |
                    XHCI_TRB_DIR_IN | XHCI_TRB_IOC;

  xfer_ring[2].d0 = xfer_ring_pa;
  xfer_ring[2].d1 = 0;
  xfer_ring[2].d2 = 0;
  xfer_ring[2].d3 = XHCI_TRB_CYCLE |
                    (XHCI_TRB_TYPE_LINK << 10) |
                    XHCI_TRB_ENT;

  xhci_write(xs->regs, xs->db_off + (slot_id * 4), 1);

  if(xhci_wait_event(xs, XHCI_EVENT_TYPE_TRANSFER, 0, &ev2, &ev3,
                     200000) < 0)
    goto out;
  if(((ev2 >> 24) & 0xff) != XHCI_CC_SUCCESS)
    goto out;
  if(((ev3 >> 24) & 0xff) != slot_id)
    goto out;

  ret = 0;

out:
  if(slot_enabled)
    (void)xhci_cmd_submit_wait(xs, XHCI_TRB_TYPE_DISABLE_SLOT,
                               0, 0, 0, (slot_id << 24), 0);
  if(xfer_ring)
    kfree((char*)xfer_ring);
  if(output_ctx)
    kfree(output_ctx);
  if(input_ctx)
    kfree(input_ctx);
  return ret;
}

static int
xhci_bulk_submit_async(struct xhci_ctrl_state *xs, uchar port,
                       uchar ep_in, uchar ep_out,
                       uchar *buf, ushort len)
{
  struct xhci_bulk_state *bs;
  uint buf_pa;
  uint ctxsz;
  uint *ctrl_ctx;
  uint *ep_ctx;
  if(!xs || !xs->initialized || !buf || len == 0 || port == 0)
    return -1;
  if((ep_out & 0x80) != 0 || (ep_in & 0x80) == 0)
    return -1;

  if(xhci_bulk_state_prepare(xs, port, ep_in, ep_out) < 0)
    return -1;

  bs = &xs->bulk;
  if(bs->in_flight)
    return 1;
  buf_pa = V2P(buf);

  ctxsz = xhci_ctx_size(xs);
  ctrl_ctx = xhci_ctx_dwords(bs->input_ctx, 0);
  ep_ctx = xhci_ctx_dwords(bs->input_ctx, ctxsz * (bs->ep_dci + 1));
  memset(bs->xfer_ring, 0, PGSIZE);

  ctrl_ctx[1] = bs->add_flags;
  ep_ctx[1] = (3U << 1) | (2U << 3) | ((512U & 0xffff) << 16);
  ep_ctx[2] = bs->xfer_ring_pa | 1U;
  ep_ctx[4] = 512;

  if(xhci_cmd_submit_wait(xs, XHCI_TRB_TYPE_CONFIG_ENDPOINT,
                          bs->input_ctx_pa, 0, 0,
                          (bs->slot_id << 24), 0) < 0){
    xhci_bulk_state_reset(xs);
    return -1;
  }

  bs->xfer_ring[0].d0 = buf_pa;
  bs->xfer_ring[0].d1 = 0;
  bs->xfer_ring[0].d2 = len;
  bs->xfer_ring[0].d3 = XHCI_TRB_CYCLE |
                       (XHCI_TRB_TYPE_NORMAL << 10) |
                       XHCI_TRB_IOC;

  bs->xfer_ring[1].d0 = bs->xfer_ring_pa;
  bs->xfer_ring[1].d1 = 0;
  bs->xfer_ring[1].d2 = 0;
  bs->xfer_ring[1].d3 = XHCI_TRB_CYCLE |
                       (XHCI_TRB_TYPE_LINK << 10) |
                       XHCI_TRB_ENT;

  bs->cur_len = len;
  bs->in_flight = 1;
  xhci_write(xs->regs, xs->db_off + (bs->slot_id * 4), bs->ep_dci);

  return 0;
}

static int
xhci_bulk_reap_async(struct xhci_ctrl_state *xs, uchar port,
                     uchar ep_in, uchar ep_out,
                     ushort *out_len)
{
  struct xhci_bulk_state *bs;
  uint cc;
  uint ev2;
  uint ev3;

  if(!xs || !xs->initialized || port == 0)
    return -1;
  if((ep_out & 0x80) != 0 || (ep_in & 0x80) == 0)
    return -1;

  bs = &xs->bulk;
  if(!bs->active || !bs->in_flight)
    return -1;
  if(bs->port != port || bs->ep_in != ep_in || bs->ep_out != ep_out)
    return -1;

  if(out_len)
    *out_len = 0;

  if(xhci_wait_event(xs, XHCI_EVENT_TYPE_TRANSFER, 0, &ev2, &ev3, 1) < 0)
    return 1;

  bs->in_flight = 0;
  cc = (ev2 >> 24) & 0xff;
  if(cc != XHCI_CC_SUCCESS && cc != XHCI_CC_SHORT_PKT){
    xhci_bulk_state_reset(xs);
    return -4;
  }
  if(((ev3 >> 24) & 0xff) != bs->slot_id){
    xhci_bulk_state_reset(xs);
    return -4;
  }

  if(out_len){
    uint residual;

    residual = ev2 & 0x00ffffffU;
    if(residual > bs->cur_len)
      residual = bs->cur_len;
    *out_len = (ushort)(bs->cur_len - residual);
  }

  if(cc == XHCI_CC_SHORT_PKT || (out_len && *out_len < bs->cur_len))
    return -3;
  return 0;
}

static volatile uint*
xhci_regs(struct pci_dev *dev)
{
  return (volatile uint*)pci_map_bar(dev, 0);
}

static uint
xhci_read(volatile uint *base, uint off)
{
  return *(volatile uint *)((char*)base + off);
}

static void
xhci_write(volatile uint *base, uint off, uint val)
{
  *(volatile uint *)((char*)base + off) = val;
}

static int
xhci_wait_bits(volatile uint *base, uint off, uint mask, uint expect_set)
{
  int i;

  for(i = 0; i < XHCI_POLL_TRIES; i++){
    uint v = xhci_read(base, off);
    if(expect_set){
      if((v & mask) == mask)
        return 0;
    } else {
      if((v & mask) == 0)
        return 0;
    }
    microdelay(XHCI_POLL_DELAY_US);
  }

  return -1;
}

int
usb_probe_xhci_regs(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  volatile uint *regs;
  uint r0;
  uint r1;

  if(!sc || !dev)
    return -1;
  if(sc->bar0 == 0 || sc->bar0_size == 0)
    return -1;
  if(sc->bar0_is_io)
    return -1;

  regs = xhci_regs(dev);
  if(!regs)
    return -1;

  r0 = xhci_read(regs, XHCI_REG_CAPLENGTH);
  r1 = xhci_read(regs, XHCI_REG_HCSPARAMS1);

  sc->reg0 = r0;
  sc->reg1 = r1;
  sc->cap_length = (uchar)(r0 & 0xFF);
  sc->hciversion = (ushort)((r0 >> 16) & 0xFFFF);
  sc->rh_present = 1;
  sc->rh_ports = (uchar)((r1 >> 24) & 0xFF);
  sc->rh_change_bits = 0;
  sc->reg_probe_ok = 1;
  sc->phase = USB_HC_PHASE_READY;
  return 0;
}

int
usb_xhci_reset(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  volatile uint *regs;
  uint opbase;
  uint cmd;

  if(!sc || !sc->reg_probe_ok)
    return -1;

  if(sc->pci_index < XHCI_CTRL_MAX)
    xhci_bulk_state_reset(&xhci_state[sc->pci_index]);

  regs = xhci_regs(dev);
  if(!regs)
    return -1;

  opbase = (uint)sc->cap_length;
  if(opbase < 0x10 || opbase > 0x80)
    return -1;

  cmd = xhci_read(regs, opbase + XHCI_OP_USBCMD);
  cmd &= ~XHCI_USBCMD_RUNSTOP;
  xhci_write(regs, opbase + XHCI_OP_USBCMD, cmd);
  if(xhci_wait_bits(regs, opbase + XHCI_OP_USBSTS, XHCI_USBSTS_HCH, 1) < 0)
    return -1;

  cmd = xhci_read(regs, opbase + XHCI_OP_USBCMD);
  cmd |= XHCI_USBCMD_HCRST;
  xhci_write(regs, opbase + XHCI_OP_USBCMD, cmd);
  if(xhci_wait_bits(regs, opbase + XHCI_OP_USBCMD, XHCI_USBCMD_HCRST, 0) < 0)
    return -1;

  if(xhci_wait_bits(regs, opbase + XHCI_OP_USBSTS, XHCI_USBSTS_CNR, 0) < 0)
    return -1;

  return 0;
}

int
usb_xhci_halt(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  volatile uint *regs;
  uint opbase;
  uint cmd;

  if(!sc || !sc->reg_probe_ok)
    return -1;

  if(sc->pci_index < XHCI_CTRL_MAX)
    xhci_bulk_state_reset(&xhci_state[sc->pci_index]);

  regs = xhci_regs(dev);
  if(!regs)
    return -1;

  opbase = (uint)sc->cap_length;
  if(opbase < 0x10 || opbase > 0x80)
    return -1;

  cmd = xhci_read(regs, opbase + XHCI_OP_USBCMD);
  cmd &= ~XHCI_USBCMD_RUNSTOP;
  xhci_write(regs, opbase + XHCI_OP_USBCMD, cmd);

  if(xhci_wait_bits(regs, opbase + XHCI_OP_USBSTS, XHCI_USBSTS_HCH, 1) < 0)
    return -1;

  return 0;
}

int
usb_xhci_start(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  volatile uint *regs;
  uint opbase;
  uint cmd;

  if(!sc || !sc->reg_probe_ok)
    return -1;

  regs = xhci_regs(dev);
  if(!regs)
    return -1;

  opbase = (uint)sc->cap_length;
  if(opbase < 0x10 || opbase > 0x80)
    return -1;

  cmd = xhci_read(regs, opbase + XHCI_OP_USBCMD);
  cmd |= XHCI_USBCMD_RUNSTOP;
  xhci_write(regs, opbase + XHCI_OP_USBCMD, cmd);

  if(xhci_wait_bits(regs, opbase + XHCI_OP_USBSTS, XHCI_USBSTS_HCH, 0) < 0)
    return -1;

  return 0;
}

static void
xhci_apply_speed(struct usb_hc_probe *sc, uint n, uint portsc)
{
  uint psiv;

  psiv = (portsc & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
  switch(psiv){
  case XHCI_PSIV_LOW:
    sc->rh_low_bits |= (1U << n);
    break;
  case XHCI_PSIV_FULL:
    sc->rh_full_bits |= (1U << n);
    break;
  case XHCI_PSIV_HIGH:
    sc->rh_high_bits |= (1U << n);
    break;
  case XHCI_PSIV_SUPER:
    sc->rh_super_bits |= (1U << n);
    break;
  default:
    break;
  }
}

int
usb_xhci_scan_ports(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  volatile uint *regs;
  uint opbase;
  uint n;

  if(!sc || !sc->reg_probe_ok || sc->rh_ports == 0)
    return 0;

  regs = xhci_regs(dev);
  if(!regs)
    return -1;

  opbase = (uint)sc->cap_length;
  if(opbase < 0x10 || opbase > 0x80)
    return -1;

  sc->rh_connect_bits = 0;
  sc->rh_change_bits = 0;
  sc->rh_low_bits = 0;
  sc->rh_full_bits = 0;
  sc->rh_high_bits = 0;
  sc->rh_super_bits = 0;

  for(n = 0; n < sc->rh_ports && n < 32; n++){
    uint portsc = xhci_read(regs, opbase + XHCI_OP_PORTSC_BASE + n * 0x10);
    if(portsc & XHCI_PORTSC_CCS)
      sc->rh_connect_bits |= (1U << n);
    if(portsc & XHCI_PORTSC_CSC)
      sc->rh_change_bits |= (1U << n);
  }
  return 0;
}

int
usb_xhci_service_ports(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  volatile uint *regs;
  uint opbase;
  uint n;

  if(!sc || !sc->reg_probe_ok || sc->rh_ports == 0)
    return 0;

  regs = xhci_regs(dev);
  if(!regs)
    return -1;

  opbase = (uint)sc->cap_length;
  if(opbase < 0x10 || opbase > 0x80)
    return -1;

  sc->rh_enabled_bits = 0;
  sc->rh_low_bits = 0;
  sc->rh_full_bits = 0;
  sc->rh_high_bits = 0;
  sc->rh_super_bits = 0;

  for(n = 0; n < sc->rh_ports && n < 32; n++){
    uint off = opbase + XHCI_OP_PORTSC_BASE + n * 0x10;
    uint portsc = xhci_read(regs, off);

    if(!(portsc & XHCI_PORTSC_CCS))
      continue;

    portsc |= XHCI_PORTSC_CSC;
    portsc |= XHCI_PORTSC_PR;
    xhci_write(regs, off, portsc);

    if(xhci_wait_bits(regs, off, XHCI_PORTSC_PR, 0) < 0)
      continue;

    microdelay(2000);
    portsc = xhci_read(regs, off);
    xhci_apply_speed(sc, n, portsc);
    if(portsc & XHCI_PORTSC_PED)
      sc->rh_enabled_bits |= (1U << n);
  }

  return 0;
}

int
usb_xhci_consume_events(struct usb_hc_probe *sc, struct pci_dev *dev,
                        uint *change_bits)
{
  volatile uint *regs;
  uint opbase;
  uint n;
  uint changes;
  uint st;

  if(change_bits)
    *change_bits = 0;
  if(!sc || !sc->reg_probe_ok || sc->rh_ports == 0)
    return 0;

  regs = xhci_regs(dev);
  if(!regs)
    return -1;

  opbase = (uint)sc->cap_length;
  if(opbase < 0x10 || opbase > 0x80)
    return -1;

  st = xhci_read(regs, opbase + XHCI_OP_USBSTS);
  st &= (XHCI_USBSTS_EINT | XHCI_USBSTS_PCD);
  if(st)
    xhci_write(regs, opbase + XHCI_OP_USBSTS, st);

  changes = 0;
  for(n = 0; n < sc->rh_ports && n < 32; n++){
    uint off;
    uint portsc;

    off = opbase + XHCI_OP_PORTSC_BASE + n * 0x10;
    portsc = xhci_read(regs, off);
    if(!(portsc & XHCI_PORTSC_CSC))
      continue;
    changes |= (1U << n);
    xhci_write(regs, off, portsc | XHCI_PORTSC_CSC);
  }

  if(change_bits)
    *change_bits = changes;
  return 0;
}

int
usb_xhci_get_device_desc8(struct usb_hc_probe *sc, struct pci_dev *dev,
                          uchar port, uchar address, uchar *out8)
{
  struct xhci_ctrl_state *xs;

  (void)address;

  if(!sc || !dev || !out8)
    return -1;
  if(port == 0)
    return -1;

  if(xhci_runtime_init(sc, dev, &xs) < 0)
    return -1;
  if(!xs)
    return -1;

  if(xhci_get_desc8(xs, port, out8) < 0)
    return -1;

  if(out8[0] < 8 || out8[1] != 1 || out8[7] == 0)
    return -1;

  return 0;
}

int
usb_xhci_get_device_desc18(struct usb_hc_probe *sc, struct pci_dev *dev,
                           uchar port, uchar address, uchar *out18)
{
  struct xhci_ctrl_state *xs;

  (void)address;

  if(!sc || !dev || !out18)
    return -1;
  if(port == 0)
    return -1;

  if(xhci_runtime_init(sc, dev, &xs) < 0)
    return -1;
  if(!xs)
    return -1;

  if(xhci_get_desc18(xs, port, out18) < 0)
    return -1;

  if(out18[0] < 18 || out18[1] != 1)
    return -1;

  return 0;
}

int
usb_xhci_get_config_desc(struct usb_hc_probe *sc, struct pci_dev *dev,
                         uchar port, uchar address, ushort length,
                         uchar *outbuf)
{
  struct xhci_ctrl_state *xs;

  (void)address;

  if(!sc || !dev || !outbuf)
    return -1;
  if(port == 0 || length == 0 || length > PGSIZE)
    return -1;

  if(xhci_runtime_init(sc, dev, &xs) < 0)
    return -1;
  if(!xs)
    return -1;

  if(xhci_get_cfg_desc(xs, port, outbuf, length) < 0)
    return -1;

  if(length >= 2 && outbuf[1] != 2)
    return -1;

  return 0;
}

int
usb_xhci_set_configuration(struct usb_hc_probe *sc, struct pci_dev *dev,
                           uchar port, uchar address, uchar cfg_value)
{
  struct xhci_ctrl_state *xs;

  (void)address;

  if(!sc || !dev)
    return -1;
  if(port == 0)
    return -1;

  if(xhci_runtime_init(sc, dev, &xs) < 0)
    return -1;
  if(!xs)
    return -1;

  return xhci_set_configuration_req(xs, port, cfg_value);
}

int
usb_xhci_bulk_submit(struct usb_hc_probe *sc, struct pci_dev *dev,
                     uchar port, uchar address,
                     uchar ep_in, uchar ep_out,
                     uchar *buf, ushort len)
{
  struct xhci_ctrl_state *xs;

  (void)address;

  if(!sc || !dev || !buf)
    return -1;
  if(port == 0 || len == 0)
    return -1;

  if(xhci_runtime_init(sc, dev, &xs) < 0)
    return -1;
  if(!xs)
    return -1;

  return xhci_bulk_submit_async(xs, port, ep_in, ep_out, buf, len);
}

int
usb_xhci_bulk_reap(struct usb_hc_probe *sc, struct pci_dev *dev,
                   uchar port, uchar address,
                   uchar ep_in, uchar ep_out,
                   ushort *out_len)
{
  struct xhci_ctrl_state *xs;

  (void)address;

  if(!sc || !dev)
    return -1;
  if(port == 0)
    return -1;

  if(xhci_runtime_init(sc, dev, &xs) < 0)
    return -1;
  if(!xs)
    return -1;

  return xhci_bulk_reap_async(xs, port, ep_in, ep_out, out_len);
}

int
usb_xhci_bulk_probe_xfer(struct usb_hc_probe *sc, struct pci_dev *dev,
                         uchar port, uchar address,
                         uchar ep_in, uchar ep_out,
                         uchar *buf, ushort len,
                         ushort *out_len)
{
  int tries;
  int ret;

  if(usb_xhci_bulk_submit(sc, dev, port, address, ep_in, ep_out,
                          buf, len) < 0)
    return -1;

  for(tries = 0; tries < 200000; tries++){
    ret = usb_xhci_bulk_reap(sc, dev, port, address, ep_in, ep_out, out_len);
    if(ret != 1)
      return ret;
    microdelay(10);
  }

  return -2;
}
