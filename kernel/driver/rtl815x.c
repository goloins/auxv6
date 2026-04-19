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
#include "net.h"

#define R815X_STUB_MAX     8

#define R815X_FAMILY_8152   1
#define R815X_FAMILY_8153   2
#define R815X_FAMILY_8153B  3

#define R815X_PHASE_INIT       0
#define R815X_PHASE_IDENTIFIED 1
#define R815X_PHASE_CONFIGURED 2
#define R815X_PHASE_RUNNING    3
#define R815X_PHASE_DEGRADED   4

#define R815X_BULK_BUF_MAX       512
#define R815X_BULK_QUEUE_DEPTH   4
#define R815X_BULK_RETRY_MAX     3
#define R815X_BULK_POLL_MAX      32

#define R815X_BULK_REQ_FREE      0
#define R815X_BULK_REQ_PENDING   1
#define R815X_BULK_REQ_INFLIGHT  2
#define R815X_BULK_REQ_DONE      3
#define R815X_BULK_REQ_FAILED    4

#define R815X_BULK_RC_TIMEOUT   -2
#define R815X_BULK_RC_SHORT     -3
#define R815X_BULK_RC_HCERR     -4

struct r815x_usb_id {
  ushort vendor_id;
  ushort product_id;
  uchar family;
  const char *model;
};

struct r815x_bulk_req {
  uchar state;
  uchar attempts;
  uchar polls;
  short rc;
  ushort want_len;
  ushort done_len;
  uchar buf[R815X_BULK_BUF_MAX];
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
  uint bulk_retry_count;
  uint bulk_timeout_count;
  uint bulk_short_count;
  uint bulk_complete_count;
  uint bulk_polled_count;
  uint bulk_pulse_count;
  short bulk_last_rc;
  uchar bulk_qhead;
  uchar bulk_qtail;
  uchar bulk_qcount;
  uint rx_frames;
  uint rx_bytes;
  uint rx_harvest_frames;
  uint rx_harvest_bytes;
  uint rx_harvest_zero_len;
  uint rx_harvest_short_len;
  uint rx_last_len;
  uint rx_last_sig;
  uint rx_ifnet_attempts;
  uint rx_ifnet_deliver;
  uint rx_ifnet_drop;
  uint tx_frames;
  uint rx_errors;
  uint tx_errors;
  uchar if_slot;
  uchar if_registered;
  struct ifnet ifp;
  struct r815x_bulk_req bulkq[R815X_BULK_QUEUE_DEPTH];
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

static ushort r815x_probe_bulk_len(struct r815x_probe *sc);
static void r815x_bulk_queue_reset(struct r815x_probe *sc);
static const char *r815x_bulk_rc_name(int rc);
static const char *r815x_bulk_req_state_name(uchar state);
static int r815x_bulk_enqueue(struct r815x_probe *sc, const uchar *src, ushort len);
static int r815x_bulk_submit_once(struct r815x_probe *sc, struct r815x_bulk_req *req);
static int r815x_bulk_reap_once(struct r815x_probe *sc, struct r815x_bulk_req *req);
static int r815x_bulk_service(struct r815x_probe *sc);
static int r815x_service_runtime_probe(struct r815x_probe *sc);
static void r815x_rx_harvest_done(struct r815x_probe *sc, struct r815x_bulk_req *req);
static void r815x_rx_try_ifnet_input(struct r815x_probe *sc, const uchar *buf, uint len);
static int r815x_if_output(struct ifnet *ifp, struct mbuf *m);
static void r815x_if_poll(struct ifnet *ifp);
static int r815x_ifnet_register(struct r815x_probe *sc);

static struct ifnet_ops r815x_if_ops = {
  .if_output = r815x_if_output,
  .if_poll = r815x_if_poll,
};

static ushort
r815x_probe_bulk_len(struct r815x_probe *sc)
{
  uint bulk_len;

  if(!sc)
    return 0;

  bulk_len = sc->max_packet0 ? sc->max_packet0 : 64;
  if(bulk_len > R815X_BULK_BUF_MAX)
    bulk_len = R815X_BULK_BUF_MAX;
  return (ushort)bulk_len;
}

static void
r815x_bulk_queue_reset(struct r815x_probe *sc)
{
  if(!sc)
    return;

  memset(sc->bulkq, 0, sizeof(sc->bulkq));
  sc->bulk_qhead = 0;
  sc->bulk_qtail = 0;
  sc->bulk_qcount = 0;
  sc->bulk_last_len = 0;
  sc->bulk_last_rc = 0;
}

static const char *
r815x_bulk_rc_name(int rc)
{
  switch(rc){
  case 0:
    return "ok";
  case R815X_BULK_RC_TIMEOUT:
    return "timeout";
  case R815X_BULK_RC_SHORT:
    return "short";
  case R815X_BULK_RC_HCERR:
    return "hcerr";
  default:
    return "error";
  }
}

static const char *
r815x_bulk_req_state_name(uchar state)
{
  switch(state){
  case R815X_BULK_REQ_FREE:
    return "free";
  case R815X_BULK_REQ_PENDING:
    return "pending";
  case R815X_BULK_REQ_INFLIGHT:
    return "inflight";
  case R815X_BULK_REQ_DONE:
    return "done";
  case R815X_BULK_REQ_FAILED:
    return "failed";
  default:
    return "unknown";
  }
}

static int
r815x_bulk_enqueue(struct r815x_probe *sc, const uchar *src, ushort len)
{
  struct r815x_bulk_req *req;

  if(!sc || len == 0 || len > R815X_BULK_BUF_MAX)
    return -1;
  if(sc->bulk_qcount >= R815X_BULK_QUEUE_DEPTH)
    return -1;

  req = &sc->bulkq[sc->bulk_qtail];
  memset(req, 0, sizeof(*req));
  req->state = R815X_BULK_REQ_PENDING;
  req->rc = -1;
  req->want_len = len;
  if(src)
    memmove(req->buf, src, len);

  sc->bulk_qtail = (uchar)((sc->bulk_qtail + 1) % R815X_BULK_QUEUE_DEPTH);
  sc->bulk_qcount++;
  return 0;
}

static int
r815x_bulk_submit_once(struct r815x_probe *sc, struct r815x_bulk_req *req)
{
  int rc;

  if(!sc || !req)
    return -1;

  rc = usb_driver_bulk_submit(sc->bind_id,
                              sc->bulk_in_ep, sc->bulk_out_ep,
                              req->buf, req->want_len);
  if(rc < 0)
    rc = R815X_BULK_RC_HCERR;
  req->rc = (short)rc;
  return rc;
}

static int
r815x_bulk_reap_once(struct r815x_probe *sc, struct r815x_bulk_req *req)
{
  ushort done_len;
  int rc;

  if(!sc || !req)
    return -1;

  done_len = 0;
  rc = usb_driver_bulk_reap(sc->bind_id,
                            sc->bulk_in_ep, sc->bulk_out_ep,
                            &done_len);
  req->done_len = done_len;
  if(rc == 0 && done_len < req->want_len)
    rc = R815X_BULK_RC_SHORT;
  req->rc = (short)rc;
  return rc;
}

static int
r815x_bulk_service(struct r815x_probe *sc)
{
  struct r815x_bulk_req *req;
  int rc;

  if(!sc)
    return -1;

  while(sc->bulk_qcount){
    req = &sc->bulkq[sc->bulk_qhead];
    if(req->state == R815X_BULK_REQ_PENDING){
      sc->bulk_attempts++;
      req->attempts++;
      req->polls = 0;
      rc = r815x_bulk_submit_once(sc, req);
      if(rc == 0){
        req->state = R815X_BULK_REQ_INFLIGHT;
        return 0;
      }

      req->rc = R815X_BULK_RC_HCERR;
      if(req->attempts < R815X_BULK_RETRY_MAX){
        sc->bulk_retry_count++;
        microdelay(50 * req->attempts);
        continue;
      }

      req->state = R815X_BULK_REQ_FAILED;
      sc->bulk_last_len = 0;
      sc->bulk_last_rc = R815X_BULK_RC_HCERR;
      sc->bulk_failures++;
      sc->rx_errors++;
      sc->tx_errors++;
      sc->bulk_qhead = (uchar)((sc->bulk_qhead + 1) % R815X_BULK_QUEUE_DEPTH);
      sc->bulk_qcount--;
      return -1;
    }

    if(req->state != R815X_BULK_REQ_INFLIGHT)
      return -1;

    rc = r815x_bulk_reap_once(sc, req);
    if(rc == 1){
      req->polls++;
      if(req->polls < R815X_BULK_POLL_MAX)
        return 0;
      rc = R815X_BULK_RC_TIMEOUT;
      req->rc = (short)rc;
    }

    sc->bulk_last_len = req->done_len;
    sc->bulk_last_rc = (short)rc;
    if(rc == 0){
      req->state = R815X_BULK_REQ_DONE;
      sc->bulk_successes++;
      sc->bulk_complete_count++;
      sc->rx_frames++;
      sc->tx_frames++;
      sc->bulk_qhead = (uchar)((sc->bulk_qhead + 1) % R815X_BULK_QUEUE_DEPTH);
      sc->bulk_qcount--;
      continue;
    }

    if(rc == R815X_BULK_RC_TIMEOUT)
      sc->bulk_timeout_count++;
    else if(rc == R815X_BULK_RC_SHORT)
      sc->bulk_short_count++;

    if(req->attempts < R815X_BULK_RETRY_MAX){
      sc->bulk_retry_count++;
      req->state = R815X_BULK_REQ_PENDING;
      req->polls = 0;
      req->done_len = 0;
      microdelay(50 * req->attempts);
      continue;
    }

    req->state = R815X_BULK_REQ_FAILED;
    sc->bulk_failures++;
    sc->rx_errors++;
    sc->tx_errors++;
    sc->bulk_qhead = (uchar)((sc->bulk_qhead + 1) % R815X_BULK_QUEUE_DEPTH);
    sc->bulk_qcount--;
    return -1;
  }

  return 0;
}

static void
r815x_rx_harvest_done(struct r815x_probe *sc, struct r815x_bulk_req *req)
{
  uint sig;
  uint frame_len;

  if(!sc || !req)
    return;

  frame_len = req->done_len;
  sc->rx_last_len = frame_len;
  if(frame_len == 0){
    sc->rx_harvest_zero_len++;
    return;
  }

  sig = req->buf[0];
  if(frame_len > 1)
    sig |= ((uint)req->buf[1] << 8);
  if(frame_len > 2)
    sig |= ((uint)req->buf[2] << 16);
  if(frame_len > 3)
    sig |= ((uint)req->buf[3] << 24);
  sc->rx_last_sig = sig;

  if(frame_len < 14)
    sc->rx_harvest_short_len++;

  sc->rx_harvest_frames++;
  sc->rx_harvest_bytes += frame_len;
  sc->rx_frames++;
  sc->rx_bytes += frame_len;
}

static void
r815x_rx_try_ifnet_input(struct r815x_probe *sc, const uchar *buf, uint len)
{
  struct mbuf *m;
  ushort etype;

  if(!sc || !buf)
    return;

  acquire(&r815x_lock);
  sc->rx_ifnet_attempts++;
  release(&r815x_lock);

  if(len < 14 || len > MBUF_SIZE){
    acquire(&r815x_lock);
    sc->rx_ifnet_drop++;
    release(&r815x_lock);
    return;
  }

  etype = (ushort)(((uint)buf[12] << 8) | buf[13]);
  if(etype != NET_PROTO_IP && etype != 0x0806 && etype != 0x86dd){
    acquire(&r815x_lock);
    sc->rx_ifnet_drop++;
    release(&r815x_lock);
    return;
  }

  acquire(&r815x_lock);
  if(!sc->active || !sc->if_registered ||
     (sc->ifp.if_flags & IFF_UP) == 0 ||
     (sc->ifp.if_flags & IFF_RUNNING) == 0){
    sc->rx_ifnet_drop++;
    release(&r815x_lock);
    return;
  }
  release(&r815x_lock);

  m = mbuf_alloc();
  if(!m){
    acquire(&r815x_lock);
    sc->rx_ifnet_drop++;
    sc->rx_errors++;
    release(&r815x_lock);
    return;
  }

  memmove(m->data, buf, len);
  m->len = len;
  m->rcvif = &sc->ifp;
  if_input(&sc->ifp, m);

  acquire(&r815x_lock);
  sc->rx_ifnet_deliver++;
  release(&r815x_lock);
}

static int
r815x_if_output(struct ifnet *ifp, struct mbuf *m)
{
  struct r815x_probe *sc;

  if(m)
    mbuf_free(m);
  if(!ifp)
    return -1;

  sc = (struct r815x_probe*)ifp->if_softc;
  if(sc){
    acquire(&r815x_lock);
    sc->tx_errors++;
    release(&r815x_lock);
  }
  return -1;
}

static void
r815x_if_poll(struct ifnet *ifp)
{
  (void)ifp;
}

static int
r815x_ifnet_register(struct r815x_probe *sc)
{
  if(!sc)
    return -1;

  memset(&sc->ifp, 0, sizeof(sc->ifp));
  safestrcpy(sc->ifp.if_xname, "r815x0", sizeof(sc->ifp.if_xname));
  sc->ifp.if_xname[5] = (char)('0' + (sc->if_slot % 10));
  sc->ifp.if_xname[6] = 0;
  sc->ifp.if_mtu = 1500;
  sc->ifp.if_flags = IFF_UP | IFF_BROADCAST | IFF_RUNNING;
  sc->ifp.if_link_state = LINK_STATE_UP;
  sc->ifp.if_softc = sc;
  sc->ifp.if_input = ether_input;
  sc->ifp.if_ops = &r815x_if_ops;

  if(if_register(&sc->ifp) < 0)
    return -1;

  sc->if_registered = 1;
  return 0;
}

static int
r815x_service_runtime_probe(struct r815x_probe *sc)
{
  struct r815x_bulk_req *req;
  uchar rx_buf[R815X_BULK_BUF_MAX];
  uint bind_id;
  uint rx_len;
  uchar ep_in;
  uchar ep_out;
  ushort bulk_len;
  ushort done_len;
  int rc;

  if(!sc)
    return -1;

  rx_len = 0;

  acquire(&r815x_lock);
  if(!sc->active){
    release(&r815x_lock);
    return 0;
  }

  sc->bulk_polled_count++;

  if(sc->phase != R815X_PHASE_RUNNING){
    release(&r815x_lock);
    return 0;
  }

  if(sc->bulk_qcount == 0){
    bulk_len = r815x_probe_bulk_len(sc);
    if(bulk_len == 0){
      sc->phase = R815X_PHASE_DEGRADED;
      release(&r815x_lock);
      return -1;
    }
    if(r815x_bulk_enqueue(sc, 0, bulk_len) < 0){
      sc->phase = R815X_PHASE_DEGRADED;
      release(&r815x_lock);
      return -1;
    }
    sc->bulk_pulse_count++;
  }

  req = &sc->bulkq[sc->bulk_qhead];
  bind_id = sc->bind_id;
  ep_in = sc->bulk_in_ep;
  ep_out = sc->bulk_out_ep;

  if(req->state == R815X_BULK_REQ_PENDING){
    sc->bulk_attempts++;
    req->attempts++;
    req->polls = 0;
    release(&r815x_lock);

    rc = usb_driver_bulk_submit(bind_id, ep_in, ep_out,
                                req->buf, req->want_len);
    acquire(&r815x_lock);

    if(!sc->active || sc->bind_id != bind_id){
      release(&r815x_lock);
      return 0;
    }

    req = &sc->bulkq[sc->bulk_qhead];
    if(req->state != R815X_BULK_REQ_PENDING){
      release(&r815x_lock);
      return 0;
    }

    if(rc == 0){
      req->state = R815X_BULK_REQ_INFLIGHT;
      req->rc = 0;
      release(&r815x_lock);
      return 0;
    }

    req->rc = R815X_BULK_RC_HCERR;
    sc->bulk_last_len = 0;
    sc->bulk_last_rc = R815X_BULK_RC_HCERR;
    if(req->attempts < R815X_BULK_RETRY_MAX){
      sc->bulk_retry_count++;
      release(&r815x_lock);
      return 0;
    }

    req->state = R815X_BULK_REQ_FAILED;
    sc->bulk_failures++;
    sc->rx_errors++;
    sc->tx_errors++;
    sc->phase = R815X_PHASE_DEGRADED;
    sc->bulk_qhead = (uchar)((sc->bulk_qhead + 1) % R815X_BULK_QUEUE_DEPTH);
    sc->bulk_qcount--;
    release(&r815x_lock);
    return -1;
  }

  if(req->state != R815X_BULK_REQ_INFLIGHT){
    sc->phase = R815X_PHASE_DEGRADED;
    release(&r815x_lock);
    return -1;
  }

  release(&r815x_lock);

  done_len = 0;
  rc = usb_driver_bulk_reap(bind_id, ep_in, ep_out, &done_len);

  acquire(&r815x_lock);
  if(!sc->active || sc->bind_id != bind_id){
    release(&r815x_lock);
    return 0;
  }

  req = &sc->bulkq[sc->bulk_qhead];
  if(req->state != R815X_BULK_REQ_INFLIGHT){
    release(&r815x_lock);
    return 0;
  }

  req->done_len = done_len;
  if(rc == 1){
    req->polls++;
    if(req->polls < R815X_BULK_POLL_MAX){
      release(&r815x_lock);
      return 0;
    }
    rc = R815X_BULK_RC_TIMEOUT;
  } else if(rc == 0 && done_len < req->want_len){
    rc = R815X_BULK_RC_SHORT;
  } else if(rc < 0 && rc != R815X_BULK_RC_TIMEOUT && rc != R815X_BULK_RC_SHORT){
    rc = R815X_BULK_RC_HCERR;
  }

  req->rc = (short)rc;
  sc->bulk_last_len = req->done_len;
  sc->bulk_last_rc = (short)rc;
  if(rc == 0){
    req->state = R815X_BULK_REQ_DONE;
    sc->bulk_successes++;
    sc->bulk_complete_count++;
    r815x_rx_harvest_done(sc, req);
    if(req->done_len > 0 && req->done_len <= R815X_BULK_BUF_MAX){
      rx_len = req->done_len;
      memmove(rx_buf, req->buf, rx_len);
    }
    sc->tx_frames++;
    sc->bulk_qhead = (uchar)((sc->bulk_qhead + 1) % R815X_BULK_QUEUE_DEPTH);
    sc->bulk_qcount--;
    release(&r815x_lock);
    if(rx_len)
      r815x_rx_try_ifnet_input(sc, rx_buf, rx_len);
    return 0;
  }

  if(rc == R815X_BULK_RC_TIMEOUT)
    sc->bulk_timeout_count++;
  else if(rc == R815X_BULK_RC_SHORT)
    sc->bulk_short_count++;

  if(req->attempts < R815X_BULK_RETRY_MAX){
    sc->bulk_retry_count++;
    req->state = R815X_BULK_REQ_PENDING;
    req->polls = 0;
    req->done_len = 0;
    release(&r815x_lock);
    return 0;
  }

  req->state = R815X_BULK_REQ_FAILED;
  sc->bulk_failures++;
  sc->rx_errors++;
  sc->tx_errors++;
  sc->phase = R815X_PHASE_DEGRADED;
  sc->bulk_qhead = (uchar)((sc->bulk_qhead + 1) % R815X_BULK_QUEUE_DEPTH);
  sc->bulk_qcount--;
  release(&r815x_lock);
  return -1;
}

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
  ushort bulk_len;
  uchar probe_buf[R815X_BULK_BUF_MAX];

  if(!sc)
    return -1;
  if(!sc->active){
    sc->bulk_failures++;
    return -1;
  }
  if((sc->bulk_in_ep & 0x80) == 0 || (sc->bulk_out_ep & 0x80) != 0){
    sc->bulk_failures++;
    return -1;
  }

  bulk_len = r815x_probe_bulk_len(sc);
  if(bulk_len == 0)
    return -1;

  memset(probe_buf, 0, sizeof(probe_buf));
  if(r815x_bulk_enqueue(sc, probe_buf, bulk_len) < 0){
    sc->bulk_failures++;
    return -1;
  }

  return r815x_bulk_service(sc);
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
  int reg_ifnet;
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

  sc = &r815x_probes[r815x_probe_count];
  r815x_probe_count++;
  memset(sc, 0, sizeof(*sc));
  sc->active = 1;
  sc->if_slot = (uchar)(r815x_probe_count - 1);
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
  r815x_bulk_queue_reset(sc);

  if(r815x_real_control_probe(sc) < 0 ||
     r815x_stub_bulk_probe(sc) < 0)
    sc->phase = R815X_PHASE_DEGRADED;
  else
    sc->phase = R815X_PHASE_RUNNING;

  reg_ifnet = (sc->phase == R815X_PHASE_RUNNING) ? 1 : 0;

  *bind_handle = bind_id;
  release(&r815x_lock);

  if(reg_ifnet && r815x_ifnet_register(sc) < 0){
    acquire(&r815x_lock);
    if(sc->active && sc->bind_id == bind_id)
      sc->phase = R815X_PHASE_DEGRADED;
    release(&r815x_lock);
    cprintf("r815x: bind=%d ifnet registration failed\n", bind_id);
  }

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
  struct ifnet *ifp;
  int if_registered;

  if(bind_handle == 0)
    return -1;

  r815x_ensure_lock();

  acquire(&r815x_lock);
  ifp = 0;
  if_registered = 0;
  for(i = (int)r815x_probe_count - 1; i >= 0; i--){
    struct r815x_probe *sc;

    sc = &r815x_probes[i];
    if(!sc->active)
      continue;
    if(sc->bind_id != bind_handle)
      continue;
    if_registered = sc->if_registered;
    ifp = &sc->ifp;
    sc->if_registered = 0;
    sc->active = 0;
    sc->phase = R815X_PHASE_DEGRADED;
    release(&r815x_lock);
    if(if_registered)
      (void)if_unregister(ifp);
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
                    "# bind id speed mps0 if alt ep_in ep_out family phase chipver ctrl bulk blen brc retry tout short qdone qpoll qpulse qstate rxh rxb rxsig rxz rxs rxif if model\n") < 0)
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

    if(r815x_buf_puts(buf, max, &len, " brc=") < 0) return -1;
    if(r815x_buf_puts(buf, max, &len, r815x_bulk_rc_name(p->bulk_last_rc)) < 0) return -1;

    if(r815x_buf_puts(buf, max, &len, " retry=") < 0) return -1;
    if(r815x_buf_putu(buf, max, &len, p->bulk_retry_count) < 0) return -1;

    if(r815x_buf_puts(buf, max, &len, " tout=") < 0) return -1;
    if(r815x_buf_putu(buf, max, &len, p->bulk_timeout_count) < 0) return -1;

    if(r815x_buf_puts(buf, max, &len, " short=") < 0) return -1;
    if(r815x_buf_putu(buf, max, &len, p->bulk_short_count) < 0) return -1;

    if(r815x_buf_puts(buf, max, &len, " qdone=") < 0) return -1;
    if(r815x_buf_putu(buf, max, &len, p->bulk_complete_count) < 0) return -1;

    if(r815x_buf_puts(buf, max, &len, " qpoll=") < 0) return -1;
    if(r815x_buf_putu(buf, max, &len, p->bulk_polled_count) < 0) return -1;

    if(r815x_buf_puts(buf, max, &len, " qpulse=") < 0) return -1;
    if(r815x_buf_putu(buf, max, &len, p->bulk_pulse_count) < 0) return -1;

    if(r815x_buf_puts(buf, max, &len, " qstate=") < 0) return -1;
    if(p->bulk_qcount == 0){
      if(r815x_buf_puts(buf, max, &len, "idle") < 0) return -1;
    } else {
      if(r815x_buf_puts(buf, max, &len,
                        r815x_bulk_req_state_name(p->bulkq[p->bulk_qhead].state)) < 0)
        return -1;
    }

      if(r815x_buf_puts(buf, max, &len, " rxh=") < 0) return -1;
      if(r815x_buf_putu(buf, max, &len, p->rx_harvest_frames) < 0) return -1;

      if(r815x_buf_puts(buf, max, &len, " rxb=") < 0) return -1;
      if(r815x_buf_putu(buf, max, &len, p->rx_harvest_bytes) < 0) return -1;

      if(r815x_buf_puts(buf, max, &len, " rxsig=0x") < 0) return -1;
      if(r815x_buf_puthex16(buf, max, &len, (ushort)(p->rx_last_sig >> 16)) < 0) return -1;
      if(r815x_buf_puthex16(buf, max, &len, (ushort)(p->rx_last_sig & 0xffff)) < 0) return -1;

      if(r815x_buf_puts(buf, max, &len, " rxz=") < 0) return -1;
      if(r815x_buf_putu(buf, max, &len, p->rx_harvest_zero_len) < 0) return -1;

      if(r815x_buf_puts(buf, max, &len, " rxs=") < 0) return -1;
      if(r815x_buf_putu(buf, max, &len, p->rx_harvest_short_len) < 0) return -1;

      if(r815x_buf_puts(buf, max, &len, " rxif=") < 0) return -1;
      if(r815x_buf_putu(buf, max, &len, p->rx_ifnet_deliver) < 0) return -1;
      if(r815x_buf_putc(buf, max, &len, '/') < 0) return -1;
      if(r815x_buf_putu(buf, max, &len, p->rx_ifnet_drop) < 0) return -1;

      if(r815x_buf_puts(buf, max, &len, " if=") < 0) return -1;
      if(p->if_registered){
        if(r815x_buf_puts(buf, max, &len, p->ifp.if_xname) < 0) return -1;
      } else {
        if(r815x_buf_puts(buf, max, &len, "-") < 0) return -1;
      }

    if(r815x_buf_puts(buf, max, &len, " model=") < 0) return -1;
    if(r815x_buf_puts(buf, max, &len, p->model) < 0) return -1;
    if(r815x_buf_putc(buf, max, &len, '\n') < 0) return -1;
  }

  if(r815x_buf_puts(buf, max, &len, "summary active=") < 0) return -1;
  if(r815x_buf_putu(buf, max, &len, active) < 0) return -1;
  if(r815x_buf_puts(buf, max, &len, " seen=") < 0) return -1;
  if(r815x_buf_putu(buf, max, &len, count) < 0) return -1;
  if(r815x_buf_puts(buf, max, &len,
                    " note=usb-bind-context-ready control-probe-landed bulk-queue-retry-state-machine-landed timer-service-active datapath-unimplemented\n") < 0)
    return -1;

  return (int)len;
}

void
rtl815x_runtime_service(void)
{
  struct r815x_probe *active[R815X_STUB_MAX];
  uint count;
  uint i;

  r815x_ensure_lock();

  acquire(&r815x_lock);
  count = r815x_probe_count;
  if(count > R815X_STUB_MAX)
    count = R815X_STUB_MAX;
  for(i = 0; i < count; i++)
    active[i] = &r815x_probes[i];
  release(&r815x_lock);

  for(i = 0; i < count; i++)
    (void)r815x_service_runtime_probe(active[i]);
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