/*
 * Broadcom BCM57412/BCM57416 10GbE Ethernet Driver for auxv6  (bnxt_en)
 *
 * Covers Broadcom NetXtreme-E NX3 (Thor) generation:
 *   BCM57412  (14E4:16D6) — 10G SFP+, dual-port
 *   BCM57416  (14E4:16D8) — 10G Base-T RJ45, dual-port
 *
 * Architecture differs fundamentally from the older tg3 family:
 *   - Firmware-based configuration via HWRM (Hardware Resource Manager).
 *     Commands are written to a PCI MMIO mailbox; responses are DMA'd back
 *     into a host-side response buffer.
 *   - Per-queue ring model: TX ring, TX completion ring (CP ring), and
 *     RX ring + RX completion ring.  A separate NQ (Notification Queue)
 *     delivers MSI-X interrupt coalescing summaries.
 *   - All ring creation / MAC configuration / link management is done via
 *     HWRM commands, not direct register writes.
 *
 * Current tranche: PCI probe, BAR map, HWRM identification, ifnet skeleton.
 *   HWRM ring-create / link-qcfg / TX/RX path deferred to a later tranche.
 *
 * Reference: Broadcom BNXT HWRM Specification (publicly available)
 *            Linux drivers/net/ethernet/broadcom/bnxt/bnxt.c
 *            FreeBSD sys/dev/bnxt/bnxt_rxr.c, bnxt_txr.c
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

/* PCI IDs */
#define BNXT_VENDOR         0x14E4   /* Broadcom */
#define BNXT_DEV_BCM57412   0x16D6
#define BNXT_DEV_BCM57416   0x16D8

/*
 * HWRM (Hardware Resource Manager) mailbox registers — BAR0 offsets.
 * The host writes a 16-byte command header + variable payload to the
 * DMA request buffer, then writes the DMA address to the mailbox register
 * to trigger firmware processing.
 */

/* BAR0 base register map */
#define BNXT_BAR0_SIZE          0x100000UL   /* 1 MB MMIO */

/* Doorbell registers (BAR1 / legacy mode) */
#define BNXT_DOORBELL_KEY_TX    0x1
#define BNXT_DOORBELL_KEY_RX    0x2

/* HWRM register offsets within BAR0 */
#define BNXT_HWRM_REQ_ADDR_LO   0x0100  /* HWRM request DMA address (lo) */
#define BNXT_HWRM_REQ_ADDR_HI   0x0104  /* HWRM request DMA address (hi) */
#define BNXT_HWRM_REQ_LEN       0x0108  /* HWRM request length */
#define BNXT_HWRM_RESP_ADDR_LO  0x010C  /* HWRM response DMA address (lo) */
#define BNXT_HWRM_RESP_ADDR_HI  0x0110  /* HWRM response DMA address (hi) */
#define BNXT_HWRM_RESP_LEN      0x0114  /* HWRM response length */

/* Legacy HWRM channel doorbell */
#define BNXT_HWRM_CMD_REQ_LEN   0x0104  /* Alternative request length reg */

/* Misc config */
#define BNXT_HWRM_MAX_REQ_LEN   128
#define BNXT_HWRM_SHORT_REQ_LEN 16

/*
 * HWRM command codes (subset).
 * See bnxt_hsi.h in Linux / HWRM spec appendix A.
 */
#define HWRM_VER_GET                    0x0000
#define HWRM_FUNC_RESET                 0x0011
#define HWRM_FUNC_QCAPS                 0x0015  /* Query function capabilities */
#define HWRM_FUNC_QCFG                  0x0016  /* Query function config */
#define HWRM_FUNC_CFG                   0x0013
#define HWRM_PORT_PHY_QCFG             0x0027  /* Query PHY config/link */
#define HWRM_RING_ALLOC                 0x0050
#define HWRM_RING_FREE                  0x0051
#define HWRM_RING_GRP_ALLOC             0x0060
#define HWRM_VNIC_ALLOC                 0x0040
#define HWRM_VNIC_CFG                   0x0041
#define HWRM_QUEUE_TX_ALLOC             0x009C
#define HWRM_QUEUE_RX_ALLOC             0x009D

/* HWRM response error codes */
#define HWRM_ERR_CODE_SUCCESS           0x0000
#define HWRM_ERR_CODE_FAIL              0x0001
#define HWRM_ERR_CODE_UNSUPPORTED_FEAT  0x000E

/*
 * Generic HWRM command header.
 * All HWRM requests begin with this 16-byte header.
 */
struct hwrm_cmd_hdr {
    uint16_t req_type;
    uint16_t cmpl_ring;    /* 0xFFFF = no completion ring */
    uint16_t seq_id;
    uint16_t target_id;    /* 0xFFFF = firmware default */
    uint64_t resp_addr;    /* host DMA address for response */
} __attribute__((packed));

/*
 * Generic HWRM response header (first 8 bytes of every response).
 */
struct hwrm_resp_hdr {
    uint16_t error_code;
    uint16_t req_type;
    uint16_t seq_id;
    uint16_t resp_len;
} __attribute__((packed));

/*
 * HWRM_VER_GET response (partial) — firmware version info.
 */
struct hwrm_ver_get_output {
    struct hwrm_resp_hdr hdr;
    uint8_t  hwrm_intf_maj;
    uint8_t  hwrm_intf_min;
    uint8_t  hwrm_intf_upd;
    uint8_t  hwrm_intf_rsv;
    uint8_t  hwrm_fw_maj;
    uint8_t  hwrm_fw_min;
    uint8_t  hwrm_fw_bld;
    uint8_t  hwrm_fw_rsv;
    uint8_t  mgmt_fw_maj;
    uint8_t  mgmt_fw_min;
    uint8_t  mgmt_fw_bld;
    uint8_t  mgmt_fw_rsv;
    uint8_t  netctrl_fw_maj;
    uint8_t  netctrl_fw_min;
    uint8_t  netctrl_fw_bld;
    uint8_t  netctrl_fw_rsv;
    uint32_t dev_caps_cfg;
    uint8_t  roce_fw_maj;
    uint8_t  roce_fw_min;
    uint8_t  roce_fw_bld;
    uint8_t  roce_fw_rsv;
    uint8_t  hwrm_fw_name[16];
    uint8_t  mgmt_fw_name[16];
    uint8_t  netctrl_fw_name[16];
    uint8_t  reserved[16];
    uint32_t max_req_win_len;
    uint16_t max_resp_len;
    uint16_t def_req_timeout;
    uint8_t  flags;
    uint8_t  unused[3];
    uint8_t  valid;
} __attribute__((packed));

/*
 * HWRM_FUNC_QCAPS response (partial) — MAC address reported here.
 */
struct hwrm_func_qcaps_output {
    struct hwrm_resp_hdr hdr;
    uint16_t fid;
    uint16_t port_id;
    uint32_t capabilities;
    uint16_t max_rsscos_ctx;
    uint16_t max_cmpl_rings;
    uint16_t max_tx_rings;
    uint16_t max_rx_rings;
    uint16_t max_l2_ctxs;
    uint16_t max_vnics;
    uint16_t first_vf_id;
    uint16_t max_vfs;
    uint16_t max_stat_ctx;
    uint32_t max_encap_records;
    uint32_t max_decap_records;
    uint32_t max_tx_em_flows;
    uint32_t max_tx_wm_flows;
    uint32_t max_rx_em_flows;
    uint32_t max_rx_wm_flows;
    uint32_t max_mcast_filters;
    uint32_t max_flow_id;
    uint32_t max_hw_ring_grps;
    uint16_t max_sp_tx_rings;
    uint8_t  unused_0[1];
    uint8_t  valid;
    uint8_t  mac_address[6];
    uint8_t  pad[2];
} __attribute__((packed));

/* Ring types for HWRM_RING_ALLOC */
#define RING_ALLOC_REQ_RING_TYPE_TX         0x0
#define RING_ALLOC_REQ_RING_TYPE_RX         0x1
#define RING_ALLOC_REQ_RING_TYPE_L2_CMPL   0x2
#define RING_ALLOC_REQ_RING_TYPE_NQ         0x7

#define BNXT_TX_RING_SIZE   512
#define BNXT_RX_RING_SIZE   512
#define BNXT_CQ_SIZE        (BNXT_TX_RING_SIZE + BNXT_RX_RING_SIZE)
#define BNXT_RX_BUF_SIZE    2048

/* 16-byte TX BD (buffer descriptor) */
struct bnxt_tx_bd {
    uint64_t addr;
    uint32_t len_flags;  /* [31:16]=len [15:0]=flags */
    uint32_t opaque;
} __attribute__((packed));

#define BNXT_TX_BD_FLAGS_END    0x40    /* Last BD in packet */
#define BNXT_TX_BD_FLAGS_START  0x10    /* First BD in packet */
#define BNXT_TX_BD_TYPE_ST      0x00    /* Short TX BD type */

/* 16-byte RX BD */
struct bnxt_rx_bd {
    uint64_t addr;
    uint32_t opaque;
    uint32_t flags_type;
} __attribute__((packed));

/* 16-byte completion record (TX/RX share the same completion ring) */
struct bnxt_cq_entry {
    uint32_t type_flags;
    uint32_t opaque;
    uint32_t agg_len;
    uint32_t errors_v2;
} __attribute__((packed));

#define BNXT_CQ_TYPE_MASK       0x03F
#define BNXT_CQ_TYPE_TX         0x00
#define BNXT_CQ_TYPE_RX         0x11
#define BNXT_CQ_VALID_FLAG      0x01   /* Valid bit alternates each wrap */

#define MAX_BNXT 4

struct bnxt_softc {
    struct pci_dev   *pci;
    struct spinlock   lock;
    struct ifnet      ifn;

    volatile uint8_t *bar0;   /* 1 MB MMIO */
    volatile uint8_t *bar2;   /* doorbell MMIO (for TX/RX push) */
    uint8_t           mac[6];
    uint16_t          seq_id;  /* HWRM sequence counter */

    /* HWRM request / response buffers (DMA) */
    char             *hwrm_req;
    char             *hwrm_resp;

    /* TX ring */
    struct bnxt_tx_bd    *tx_ring;
    char                 *tx_bufs[BNXT_TX_RING_SIZE];
    struct mbuf          *tx_mbufs[BNXT_TX_RING_SIZE];
    uint32_t              tx_prod;
    uint32_t              tx_cons;

    /* RX ring */
    struct bnxt_rx_bd    *rx_ring;
    char                 *rx_bufs[BNXT_RX_RING_SIZE];
    uint32_t              rx_prod;

    /* Completion ring */
    struct bnxt_cq_entry *cq_ring;
    uint32_t              cq_cons;
    uint32_t              cq_valid; /* expected valid bit (toggles on wrap) */
};

static struct bnxt_softc bnxt_devices[MAX_BNXT];
static int bnxt_count;
extern int ncpu;

static int  bnxt_output(struct ifnet *ifp, struct mbuf *m);
static void bnxt_poll(struct ifnet *ifp);

static struct ifnet_ops bnxt_ifnet_ops = {
    .if_output = bnxt_output,
    .if_poll   = bnxt_poll,
};

static int
bnxt_match(struct pci_dev *dev)
{
    if (!dev || dev->vendor_id != BNXT_VENDOR)
        return 0;
    switch (dev->device_id) {
    case BNXT_DEV_BCM57412:
    case BNXT_DEV_BCM57416:
        return 1;
    default:
        return 0;
    }
}

static uint32_t
bnxt_read32(struct bnxt_softc *sc, uint32_t reg) __attribute__((unused));
static uint32_t
bnxt_read32(struct bnxt_softc *sc, uint32_t reg)
{
    return *(volatile uint32_t *)(sc->bar0 + reg);
}

static void
bnxt_write32(struct bnxt_softc *sc, uint32_t reg, uint32_t val)
{
    *(volatile uint32_t *)(sc->bar0 + reg) = val;
}

/*
 * Issue an HWRM command to firmware.
 *   req      - pre-filled hwrm_cmd_hdr + payload in sc->hwrm_req
 *   req_len  - total bytes in request
 *   resp_len - maximum bytes firmware may write to sc->hwrm_resp
 *
 * Returns 0 on success, -1 on firmware error or timeout.
 *
 * TODO: add a proper polling loop with a timeout counter instead of
 * a fixed microdelay; real chips need up to 1000ms for some commands.
 */
static int
bnxt_hwrm_send(struct bnxt_softc *sc, uint16_t cmd,
               void *payload, uint16_t plen)
{
    struct hwrm_cmd_hdr *hdr = (struct hwrm_cmd_hdr *)sc->hwrm_req;
    struct hwrm_resp_hdr *resp;

    memset(sc->hwrm_req, 0, BNXT_HWRM_MAX_REQ_LEN);
    hdr->req_type  = cmd;
    hdr->cmpl_ring = 0xFFFF;
    hdr->seq_id    = sc->seq_id++;
    hdr->target_id = 0xFFFF;
    hdr->resp_addr = V2P(sc->hwrm_resp);

    if (payload && plen)
        memmove(sc->hwrm_req + sizeof(*hdr), payload, plen);

    memset(sc->hwrm_resp, 0, BNXT_HWRM_MAX_REQ_LEN);

    /* Post request to firmware */
    bnxt_write32(sc, BNXT_HWRM_REQ_ADDR_HI, 0);
    bnxt_write32(sc, BNXT_HWRM_REQ_ADDR_LO, V2P(sc->hwrm_req));
    bnxt_write32(sc, BNXT_HWRM_REQ_LEN,
                 sizeof(*hdr) + plen);
    bnxt_write32(sc, BNXT_HWRM_RESP_ADDR_HI, 0);
    bnxt_write32(sc, BNXT_HWRM_RESP_ADDR_LO, hdr->resp_addr);

    /* Poll for response (valid byte at end of response is non-zero) */
    microdelay(5000);

    resp = (struct hwrm_resp_hdr *)sc->hwrm_resp;
    if (resp->error_code != HWRM_ERR_CODE_SUCCESS) {
        BOOTDBG("bnxt: HWRM cmd=%04x error=%04x\n",
                cmd, resp->error_code);
        return -1;
    }
    return 0;
}

static int
bnxt_get_ver(struct bnxt_softc *sc)
{
    struct hwrm_ver_get_output *resp;
    if (bnxt_hwrm_send(sc, HWRM_VER_GET, 0, 0) < 0)
        return -1;
    resp = (struct hwrm_ver_get_output *)sc->hwrm_resp;
    BOOTDBG("bnxt: HWRM v%d.%d.%d FW v%d.%d.%d\n",
            resp->hwrm_intf_maj, resp->hwrm_intf_min, resp->hwrm_intf_upd,
            resp->hwrm_fw_maj, resp->hwrm_fw_min, resp->hwrm_fw_bld);
    return 0;
}

static int
bnxt_get_mac(struct bnxt_softc *sc)
{
    struct hwrm_func_qcaps_output *resp;
    if (bnxt_hwrm_send(sc, HWRM_FUNC_QCAPS, 0, 0) < 0)
        return -1;
    resp = (struct hwrm_func_qcaps_output *)sc->hwrm_resp;
    memmove(sc->mac, resp->mac_address, 6);
    return 0;
}

static int
bnxt_alloc_rings(struct bnxt_softc *sc)
{
    int i;

    sc->tx_ring = (struct bnxt_tx_bd *)kalloc();
    if (!sc->tx_ring) return -1;
    memset(sc->tx_ring, 0, sizeof(struct bnxt_tx_bd) * BNXT_TX_RING_SIZE);

    for (i = 0; i < BNXT_TX_RING_SIZE; i++) {
        sc->tx_bufs[i] = kalloc();
        if (!sc->tx_bufs[i]) return -1;
    }
    sc->tx_prod = 0;
    sc->tx_cons = 0;

    sc->rx_ring = (struct bnxt_rx_bd *)kalloc();
    if (!sc->rx_ring) return -1;
    memset(sc->rx_ring, 0, sizeof(struct bnxt_rx_bd) * BNXT_RX_RING_SIZE);

    for (i = 0; i < BNXT_RX_RING_SIZE; i++) {
        sc->rx_bufs[i] = kalloc();
        if (!sc->rx_bufs[i]) return -1;
        sc->rx_ring[i].addr   = V2P(sc->rx_bufs[i]);
        sc->rx_ring[i].opaque = i;
    }
    sc->rx_prod = BNXT_RX_RING_SIZE - 1;

    sc->cq_ring = (struct bnxt_cq_entry *)kalloc();
    if (!sc->cq_ring) return -1;
    memset(sc->cq_ring, 0, sizeof(struct bnxt_cq_entry) * BNXT_CQ_SIZE);
    sc->cq_cons  = 0;
    sc->cq_valid = 1;   /* first pass: valid bit = 1 */

    return 0;
}

/*
 * Process completion ring entries.  A valid entry has (errors_v2 & 1) ==
 * sc->cq_valid (the bit toggles each time the ring wraps).
 */
static void
bnxt_cq_drain(struct bnxt_softc *sc)
{
    struct bnxt_cq_entry *ce;
    struct mbuf *m;
    int processed = 0;

    while (processed < 64) {
        ce = &sc->cq_ring[sc->cq_cons];

        if ((ce->errors_v2 & BNXT_CQ_VALID_FLAG) != sc->cq_valid)
            break;

        switch (ce->type_flags & BNXT_CQ_TYPE_MASK) {
        case BNXT_CQ_TYPE_TX: {
            uint32_t opaque = ce->opaque & (BNXT_TX_RING_SIZE - 1);
            if (sc->tx_mbufs[opaque]) {
                mbuf_free(sc->tx_mbufs[opaque]);
                sc->tx_mbufs[opaque] = 0;
            }
            sc->tx_cons = (sc->tx_cons + 1) & (BNXT_TX_RING_SIZE - 1);
            break;
        }
        case BNXT_CQ_TYPE_RX: {
            uint32_t opaque = ce->opaque & (BNXT_RX_RING_SIZE - 1);
            uint16_t len    = (uint16_t)(ce->agg_len & 0xFFFF);
            if (len > 0 && len <= BNXT_RX_BUF_SIZE) {
                m = mbuf_alloc();
                if (m) {
                    memmove(m->data, sc->rx_bufs[opaque], len);
                    m->len   = len;
                    m->rcvif = &sc->ifn;
                    release(&sc->lock);
                    if_input(&sc->ifn, m);
                    acquire(&sc->lock);
                }
            }
            sc->rx_prod = (sc->rx_prod + 1) & (BNXT_RX_RING_SIZE - 1);
            break;
        }
        default:
            break;
        }

        sc->cq_cons++;
        if (sc->cq_cons >= BNXT_CQ_SIZE) {
            sc->cq_cons  = 0;
            sc->cq_valid ^= 1;   /* Toggle valid bit on ring wrap */
        }
        processed++;
    }
}

static void
bnxt_poll(struct ifnet *ifp)
{
    struct bnxt_softc *sc = (struct bnxt_softc *)ifp->if_softc;
    if (!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;
    acquire(&sc->lock);
    bnxt_cq_drain(sc);
    release(&sc->lock);
}

static int
bnxt_output(struct ifnet *ifp, struct mbuf *m)
{
    struct bnxt_softc *sc = (struct bnxt_softc *)ifp->if_softc;
    struct bnxt_tx_bd *bd;
    uint32_t idx, next;

    if (!sc || !m || m->len == 0)
        return -1;

    acquire(&sc->lock);

    if ((ifp->if_flags & IFF_RUNNING) == 0) {
        release(&sc->lock);
        return -1;
    }

    idx  = sc->tx_prod;
    next = (idx + 1) & (BNXT_TX_RING_SIZE - 1);
    if (next == sc->tx_cons) {
        release(&sc->lock);
        return -1;
    }

    memmove(sc->tx_bufs[idx], m->data, m->len);
    sc->tx_mbufs[idx] = m;

    bd = &sc->tx_ring[idx];
    bd->addr      = V2P(sc->tx_bufs[idx]);
    bd->len_flags = ((uint32_t)m->len << 16) |
                    BNXT_TX_BD_FLAGS_START | BNXT_TX_BD_FLAGS_END;
    bd->opaque    = idx;

    sc->tx_prod = next;

    /*
     * TODO: ring the TX doorbell on BAR2 once HWRM ring allocation is
     * implemented and the ring ID is known.  Example (BAR2 doorbell write):
     *   *(volatile uint32_t *)(sc->bar2 + tx_ring_id * 8) = sc->tx_prod;
     */

    release(&sc->lock);
    return 0;
}

static int
bnxt_probe(struct pci_dev *dev)
{
    struct bnxt_softc *sc;

    if (bnxt_count >= MAX_BNXT)
        return -1;

    sc = &bnxt_devices[bnxt_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "bnxt");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "bnxt");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    /* BAR0 = 1 MB MMIO for registers + HWRM mailbox */
    sc->bar0 = pci_map_bar(dev, 0);
    if (!sc->bar0) {
        cprintf("bnxt: failed to map BAR0 at %d:%d.%d\n",
                dev->bus, dev->slot, dev->func);
        return -1;
    }

    /* BAR2 = doorbell MMIO (optional if BAR2 not present) */
    sc->bar2 = pci_map_bar(dev, 2);

    BOOTDBG("bnxt: found %04x at %d:%d.%d irq=%d bar0=%p\n",
            dev->device_id, dev->bus, dev->slot, dev->func,
            dev->irq_line, sc->bar0);

    /* Allocate HWRM mailbox buffers (one page each) */
    sc->hwrm_req  = kalloc();
    sc->hwrm_resp = kalloc();
    if (!sc->hwrm_req || !sc->hwrm_resp) {
        cprintf("bnxt: failed to allocate HWRM buffers\n");
        return -1;
    }

    /* Identify firmware */
    if (bnxt_get_ver(sc) < 0) {
        cprintf("bnxt: HWRM_VER_GET failed — firmware may not be ready\n");
        /* Non-fatal for stub: continue with zero MAC */
    }

    /* Get MAC address via HWRM_FUNC_QCAPS */
    if (bnxt_get_mac(sc) < 0) {
        /* Fall back to a zeroed MAC; production code would fail here */
        cprintf("bnxt: HWRM_FUNC_QCAPS failed — using zero MAC\n");
    }

    /* Allocate rings */
    if (bnxt_alloc_rings(sc) < 0) {
        cprintf("bnxt: ring allocation failed\n");
        return -1;
    }

    /*
     * TODO: issue HWRM_RING_ALLOC for CP ring, TX ring, RX ring;
     *       HWRM_RING_GRP_ALLOC; HWRM_VNIC_ALLOC + HWRM_VNIC_CFG;
     *       HWRM_PORT_PHY_QCFG to check link state.
     */

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "bnxt0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[4] = '0' + bnxt_count;
    sc->ifn.if_mtu   = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_BROADCAST;
    /* Link state unknown until HWRM_PORT_PHY_QCFG is wired */
    memmove(sc->ifn.if_hwaddr, sc->mac, 6);
    sc->ifn.if_softc = sc;
    sc->ifn.if_input = ether_input;
    sc->ifn.if_ops   = &bnxt_ifnet_ops;

    if (if_register(&sc->ifn) < 0) {
        cprintf("bnxt: failed to register ifnet\n");
        return -1;
    }

    cprintf("bnxt: attached %s BCM%04x MAC %x:%x:%x:%x:%x:%x\n",
            sc->ifn.if_xname, dev->device_id,
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
    bnxt_count++;
    return 0;
}

void
bnxt_init(void)
{
    int i;
    BOOTDBG("bnxt: initializing driver\n");
    for (i = 0; i < pci_device_count(); i++) {
        struct pci_dev *dev = pci_get_device(i);
        if (bnxt_match(dev))
            bnxt_probe(dev);
    }
}
