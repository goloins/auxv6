/*
 * Aquantia/Marvell Atlantic AQC107/AQC108 10G/5G Ethernet Driver for auxv6
 *
 * Covers the Atlantic family (AQtion chipset):
 *   AQC107  (1D6A:07B1) — 10GbE
 *   AQC108  (1D6A:08B1) — 5GbE
 *   AQC111  (1D6A:11B1) — 5GbE (additional variant, same driver)
 *
 * Architecture:
 *   BAR0 = 32 KB MMIO.  Multiple TX and RX rings; a separate "interrupt
 *   status" register is polled or drives MSI-X vectors.
 *   Unlike most NICs, the Atlantic has a unified "mailbox" FW interface
 *   for PHY control, and a ring model where RX and TX are mostly symmetric.
 *
 *   TX path:  host writes TX ring descriptors and advances a tail pointer
 *             into the TX ring-tail register.
 *   RX path:  host posts RX descriptors, NIC fills them and writes
 *             the completion index into a status register.
 *   No per-packet status block; completion is signaled by a ring-head
 *   register that the driver reads on each poll.
 *
 * Current tranche: PCI probe, BAR map, single TX+RX queue, polling.
 *   Multi-queue, checksum offload, flow classification, and interrupt
 *   mode deferred to a later tranche.
 *
 * Reference: Aquantia AQC107 Datasheet / AQtion SDK
 *            Linux drivers/net/ethernet/aquantia/atlantic/
 *            FreeBSD sys/dev/aq/ (aq driver)
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

/* PCI IDs */
#define ATL_VENDOR          0x1D6A   /* Aquantia Corp */
#define ATL_DEV_AQC107      0x07B1
#define ATL_DEV_AQC108      0x08B1
#define ATL_DEV_AQC111      0x11B1   /* 5G variant, same register set */

/*
 * BAR0 Register map (byte offsets, 32-bit access).
 * Sources: Linux aq_hw.h, aq_nic.h; FreeBSD if_aqreg.h
 */

/* Global/device control */
#define ATL_GLOBAL_CTRL2        0x0000   /* Global control 2 */
#define  GLOBAL_CTRL2_REGS_RST      0x00004000   /* Reset all registers */
#define ATL_MBOX_CTRL           0x0300   /* Firmware mailbox control */
#define ATL_MBOX_ADDR           0x0304   /* Firmware mailbox DMA address */

/* Interrupt status / control */
#define ATL_ISR                 0x2000   /* Interrupt status (write-clear) */
#define ATL_ICS                 0x2008   /* Interrupt cause set */
#define ATL_IMR                 0x2010   /* Interrupt mask register */
#define ATL_ITR_REG             0x2100   /* Interrupt throttle rate */

/* TX ring 0 registers */
#define ATL_TX_DMA_DESC_BASE_ADR_LSW    0x7C00   /* TX desc ring base (lo) */
#define ATL_TX_DMA_DESC_BASE_ADR_MSW    0x7C04   /* TX desc ring base (hi) */
#define ATL_TX_DMA_DESC_COUNT           0x7C08   /* TX ring size (# descs) */
#define ATL_TX_DMA_DESC_TAIL            0x7C10   /* TX ring tail pointer */
#define ATL_TX_DMA_DESC_HEAD            0x7C0C   /* TX ring head (READ ONLY) */
#define ATL_TX_CTRL                     0x7B00   /* TX control (enable bit) */
#define  ATL_TX_CTRL_EN                 0x00000001

/* RX ring 0 registers */
#define ATL_RX_DMA_DESC_BASE_ADR_LSW    0x5B00   /* RX desc ring base (lo) */
#define ATL_RX_DMA_DESC_BASE_ADR_MSW    0x5B04   /* RX desc ring base (hi) */
#define ATL_RX_DMA_DESC_COUNT           0x5B08   /* RX ring size */
#define ATL_RX_DMA_DESC_TAIL            0x5B10   /* RX ring tail (posted by host) */
#define ATL_RX_DMA_DESC_HEAD            0x5B0C   /* RX ring head (READ ONLY) */
#define ATL_RX_CTRL                     0x5700   /* RX control */
#define  ATL_RX_CTRL_EN                 0x00000001

/* MAC / PHY */
#define ATL_MAC_PHY_CTRL            0x0240   /* MAC/PHY control */
#define ATL_MACT_BASE               0x8000   /* MAC address table base */
#define ATL_MAC_ADDR0_LO            0x3900   /* MAC address low */
#define ATL_MAC_ADDR0_HI            0x3904   /* MAC address high */

/* Link state */
#define ATL_FW_LINK_STATUS          0x0058   /* FW-reported link state */
#define  ATL_FW_LINK_UP             0x00000001

/* DMA control */
#define ATL_DMA_STREAM_CTRL         0x7000   /* DMA stream control */
#define  ATL_DMA_STREAM_TX_EN       0x00000001
#define  ATL_DMA_STREAM_RX_EN       0x00010000

/*
 * TX descriptor (16 bytes).
 * The Atlantic TX descriptor uses a write-back layout:
 *   - addr: physical address of packet buffer
 *   - len:  lower 16 bits = packet length
 *   - flags: control bits (EOP, SOP, etc.)
 *   - stat: written back by hardware (unchanged in WRB mode)
 */
struct atl_tx_desc {
    uint64_t addr;
    uint32_t len_type;   /* [31:19]=rsvd [18:16]=type [15:0]=len */
    uint32_t flags_mss;  /* [31:0] flags + MSS for TSO */
} __attribute__((packed));

#define ATL_TXD_TYPE_DATA   0x1    /* Data packet */
#define ATL_TXD_FLAG_EOP    (1 << 21)   /* End of packet */
#define ATL_TXD_FLAG_FCS    (1 << 22)   /* Insert Ethernet FCS */
#define ATL_TXD_FLAG_IPV4   (1 << 23)   /* IPv4 checksum offload */

/*
 * RX descriptor (16 bytes).
 * Host writes address in first quadword; NIC fills status fields
 * in the write-back area after reception.
 */
struct atl_rx_desc {
    uint64_t addr;      /* Physical buffer address (host → NIC) */
    uint64_t status;    /* Status word (NIC → host write-back) */
} __attribute__((packed));

/* RX status word fields */
#define ATL_RXD_STAT_DD     (1ULL << 0)    /* Descriptor done (packet received) */
#define ATL_RXD_STAT_EOP    (1ULL << 1)    /* End of packet */
#define ATL_RXD_LEN_MASK    0xFFFFULL       /* Packet length in bits [31:16] */
#define ATL_RXD_LEN_SHIFT   16

#define ATL_TX_RING_SIZE    512
#define ATL_RX_RING_SIZE    512
#define ATL_RX_BUF_SIZE     2048

#define MAX_ATL 4

struct atl_softc {
    struct pci_dev   *pci;
    struct spinlock   lock;
    struct ifnet      ifn;

    volatile uint8_t *regs;   /* BAR0 */
    uint8_t           mac[6];

    /* TX ring */
    struct atl_tx_desc  *tx_ring;
    char                *tx_bufs[ATL_TX_RING_SIZE];
    struct mbuf         *tx_mbufs[ATL_TX_RING_SIZE];
    uint32_t             tx_tail;   /* software tail (posted to hardware) */
    uint32_t             tx_head;   /* software-tracked completion head */

    /* RX ring */
    struct atl_rx_desc  *rx_ring;
    char                *rx_bufs[ATL_RX_RING_SIZE];
    uint32_t             rx_tail;   /* free descriptor tail */
    uint32_t             rx_head;   /* last processed completion */
};

static struct atl_softc atl_devices[MAX_ATL];
static int atl_count;
extern int ncpu;

static int  atl_output(struct ifnet *ifp, struct mbuf *m);
static void atl_poll(struct ifnet *ifp);

static struct ifnet_ops atl_ifnet_ops = {
    .if_output = atl_output,
    .if_poll   = atl_poll,
};

static int
atl_match(struct pci_dev *dev)
{
    if (!dev || dev->vendor_id != ATL_VENDOR)
        return 0;
    switch (dev->device_id) {
    case ATL_DEV_AQC107:
    case ATL_DEV_AQC108:
    case ATL_DEV_AQC111:
        return 1;
    default:
        return 0;
    }
}

static uint32_t
atl_read(struct atl_softc *sc, uint32_t reg)
{
    return *(volatile uint32_t *)(sc->regs + reg);
}

static void
atl_write(struct atl_softc *sc, uint32_t reg, uint32_t val)
{
    *(volatile uint32_t *)(sc->regs + reg) = val;
}

static void
atl_read_mac(struct atl_softc *sc)
{
    uint32_t lo = atl_read(sc, ATL_MAC_ADDR0_LO);
    uint32_t hi = atl_read(sc, ATL_MAC_ADDR0_HI);

    sc->mac[0] = (lo >>  0) & 0xFF;
    sc->mac[1] = (lo >>  8) & 0xFF;
    sc->mac[2] = (lo >> 16) & 0xFF;
    sc->mac[3] = (lo >> 24) & 0xFF;
    sc->mac[4] = (hi >>  0) & 0xFF;
    sc->mac[5] = (hi >>  8) & 0xFF;
}

static void
atl_reset(struct atl_softc *sc)
{
    uint32_t val;
    int i;

    /* Disable RX/TX then issue global register reset */
    atl_write(sc, ATL_TX_CTRL, 0);
    atl_write(sc, ATL_RX_CTRL, 0);
    microdelay(10000);

    val = atl_read(sc, ATL_GLOBAL_CTRL2);
    atl_write(sc, ATL_GLOBAL_CTRL2, val | GLOBAL_CTRL2_REGS_RST);

    /* Wait for reset bit to de-assert (firmware clears it) */
    for (i = 0; i < 100; i++) {
        microdelay(1000);
        if (!(atl_read(sc, ATL_GLOBAL_CTRL2) & GLOBAL_CTRL2_REGS_RST))
            break;
    }
    if (atl_read(sc, ATL_GLOBAL_CTRL2) & GLOBAL_CTRL2_REGS_RST)
        cprintf("atl: reset timeout\n");
}

static int
atl_alloc_rings(struct atl_softc *sc)
{
    int i;

    sc->tx_ring = (struct atl_tx_desc *)kalloc();
    if (!sc->tx_ring) return -1;
    memset(sc->tx_ring, 0, sizeof(struct atl_tx_desc) * ATL_TX_RING_SIZE);

    for (i = 0; i < ATL_TX_RING_SIZE; i++) {
        sc->tx_bufs[i] = kalloc();
        if (!sc->tx_bufs[i]) return -1;
    }
    sc->tx_tail = 0;
    sc->tx_head = 0;

    sc->rx_ring = (struct atl_rx_desc *)kalloc();
    if (!sc->rx_ring) return -1;
    memset(sc->rx_ring, 0, sizeof(struct atl_rx_desc) * ATL_RX_RING_SIZE);

    for (i = 0; i < ATL_RX_RING_SIZE; i++) {
        sc->rx_bufs[i] = kalloc();
        if (!sc->rx_bufs[i]) return -1;
        sc->rx_ring[i].addr   = V2P(sc->rx_bufs[i]);
        sc->rx_ring[i].status = 0;
    }
    sc->rx_tail = ATL_RX_RING_SIZE - 1;
    sc->rx_head = 0;

    return 0;
}

static void
atl_init_rings(struct atl_softc *sc)
{
    uint32_t phys;

    /* TX ring 0 */
    phys = V2P(sc->tx_ring);
    atl_write(sc, ATL_TX_DMA_DESC_BASE_ADR_LSW, phys);
    atl_write(sc, ATL_TX_DMA_DESC_BASE_ADR_MSW, 0);
    atl_write(sc, ATL_TX_DMA_DESC_COUNT, ATL_TX_RING_SIZE);
    atl_write(sc, ATL_TX_DMA_DESC_TAIL, 0);

    /* RX ring 0 */
    phys = V2P(sc->rx_ring);
    atl_write(sc, ATL_RX_DMA_DESC_BASE_ADR_LSW, phys);
    atl_write(sc, ATL_RX_DMA_DESC_BASE_ADR_MSW, 0);
    atl_write(sc, ATL_RX_DMA_DESC_COUNT, ATL_RX_RING_SIZE);
    atl_write(sc, ATL_RX_DMA_DESC_TAIL, sc->rx_tail);

    /* Enable TX and RX DMA */
    atl_write(sc, ATL_DMA_STREAM_CTRL,
        ATL_DMA_STREAM_TX_EN | ATL_DMA_STREAM_RX_EN);
    atl_write(sc, ATL_TX_CTRL, ATL_TX_CTRL_EN);
    atl_write(sc, ATL_RX_CTRL, ATL_RX_CTRL_EN);
}

static void
atl_tx_complete(struct atl_softc *sc)
{
    uint32_t head;

    head = atl_read(sc, ATL_TX_DMA_DESC_HEAD) & (ATL_TX_RING_SIZE - 1);

    while (sc->tx_head != head) {
        if (sc->tx_mbufs[sc->tx_head]) {
            mbuf_free(sc->tx_mbufs[sc->tx_head]);
            sc->tx_mbufs[sc->tx_head] = 0;
        }
        sc->tx_head = (sc->tx_head + 1) & (ATL_TX_RING_SIZE - 1);
    }
}

static void
atl_rx_complete(struct atl_softc *sc)
{
    struct atl_rx_desc *d;
    struct mbuf *m;
    int processed = 0;

    while (processed < 32) {
        d = &sc->rx_ring[sc->rx_head];
        if (!(d->status & ATL_RXD_STAT_DD))
            break;

        if (d->status & ATL_RXD_STAT_EOP) {
            uint16_t len = (uint16_t)((d->status >> ATL_RXD_LEN_SHIFT) &
                                       ATL_RXD_LEN_MASK);
            if (len > 0 && len <= ATL_RX_BUF_SIZE) {
                m = mbuf_alloc();
                if (m) {
                    memmove(m->data, sc->rx_bufs[sc->rx_head], len);
                    m->len   = len;
                    m->rcvif = &sc->ifn;
                    release(&sc->lock);
                    if_input(&sc->ifn, m);
                    acquire(&sc->lock);
                }
            }
        }

        /* Recycle descriptor */
        d->status = 0;
        d->addr   = V2P(sc->rx_bufs[sc->rx_head]);

        sc->rx_head = (sc->rx_head + 1) & (ATL_RX_RING_SIZE - 1);
        sc->rx_tail = (sc->rx_tail + 1) & (ATL_RX_RING_SIZE - 1);
        atl_write(sc, ATL_RX_DMA_DESC_TAIL, sc->rx_tail);
        processed++;
    }
}

static void
atl_poll(struct ifnet *ifp)
{
    struct atl_softc *sc = (struct atl_softc *)ifp->if_softc;
    if (!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;
    acquire(&sc->lock);
    atl_tx_complete(sc);
    atl_rx_complete(sc);
    release(&sc->lock);
}

static int
atl_output(struct ifnet *ifp, struct mbuf *m)
{
    struct atl_softc *sc = (struct atl_softc *)ifp->if_softc;
    struct atl_tx_desc *d;
    uint32_t idx, next;

    if (!sc || !m || m->len == 0)
        return -1;

    acquire(&sc->lock);

    if ((ifp->if_flags & IFF_RUNNING) == 0) {
        release(&sc->lock);
        return -1;
    }

    atl_tx_complete(sc);

    idx  = sc->tx_tail;
    next = (idx + 1) & (ATL_TX_RING_SIZE - 1);
    if (next == sc->tx_head) {
        release(&sc->lock);
        return -1;
    }

    memmove(sc->tx_bufs[idx], m->data, m->len);
    sc->tx_mbufs[idx] = m;

    d = &sc->tx_ring[idx];
    d->addr      = V2P(sc->tx_bufs[idx]);
    d->len_type  = ((uint32_t)m->len & 0xFFFF) | (ATL_TXD_TYPE_DATA << 16);
    d->flags_mss = ATL_TXD_FLAG_EOP | ATL_TXD_FLAG_FCS;

    sc->tx_tail = next;
    atl_write(sc, ATL_TX_DMA_DESC_TAIL, sc->tx_tail);

    release(&sc->lock);
    return 0;
}

static int
atl_probe(struct pci_dev *dev)
{
    struct atl_softc *sc;
    uint32_t link;

    if (atl_count >= MAX_ATL)
        return -1;

    sc = &atl_devices[atl_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "atl");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "atl");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    /* BAR0 = 32 KB MMIO register space */
    sc->regs = pci_map_bar(dev, 0);
    if (!sc->regs) {
        cprintf("atl: failed to map BAR0 at %d:%d.%d\n",
                dev->bus, dev->slot, dev->func);
        return -1;
    }

    BOOTDBG("atl: found %04x at %d:%d.%d irq=%d regs=%p\n",
            dev->device_id, dev->bus, dev->slot, dev->func,
            dev->irq_line, sc->regs);

    atl_reset(sc);
    atl_read_mac(sc);

    if (atl_alloc_rings(sc) < 0) {
        cprintf("atl: ring allocation failed\n");
        return -1;
    }

    /* Disable all interrupts (polling) */
    atl_write(sc, ATL_IMR, 0);

    atl_init_rings(sc);

    link = atl_read(sc, ATL_FW_LINK_STATUS);

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "aq0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[2] = '0' + atl_count;
    sc->ifn.if_mtu   = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_BROADCAST;
    if (link & ATL_FW_LINK_UP)
        sc->ifn.if_flags |= IFF_RUNNING;
    memmove(sc->ifn.if_hwaddr, sc->mac, 6);
    sc->ifn.if_softc = sc;
    sc->ifn.if_input = ether_input;
    sc->ifn.if_ops   = &atl_ifnet_ops;

    if (if_register(&sc->ifn) < 0) {
        cprintf("atl: failed to register ifnet\n");
        return -1;
    }

    cprintf("atl: attached %s AQC%04x %s MAC %x:%x:%x:%x:%x:%x\n",
            sc->ifn.if_xname,
            dev->device_id,
            (dev->device_id == ATL_DEV_AQC107) ? "10G" : "5G",
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
    atl_count++;
    return 0;
}

void
atlantic_init(void)
{
    int i;
    BOOTDBG("atl: initializing Atlantic driver\n");
    for (i = 0; i < pci_device_count(); i++) {
        struct pci_dev *dev = pci_get_device(i);
        if (atl_match(dev))
            atl_probe(dev);
    }
}
