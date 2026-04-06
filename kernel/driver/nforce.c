/*
 * NVIDIA nForce MCP79 Ethernet Driver (10de:0ab0) for auxv6
 *
 * Tranche 1 scope:
 * - PCI match/probe for MCP79 (10de:0ab0)
 * - MMIO BAR mapping and basic MAC/link bring-up
 * - Polling TX/RX descriptor rings (32-bit descriptor format)
 * - ifnet integration (if_output + if_poll)
 *
 * Design notes:
 * - Register layout and bring-up sequencing are inspired by the
 *   nForce/forcedeth family used in Linux/OpenBSD drivers.
 * - This implementation intentionally keeps to a conservative polling
 *   datapath and defers MSI/MSI-X, checksum offload, and PHY tuning.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

#define NFORCE_VENDOR_NVIDIA 0x10DE
#define NFORCE_DEV_MCP79     0x0AB0

/* MMIO register offsets */
#define NFE_TX_CTL           0x084
#define NFE_RX_CTL           0x094
#define NFE_RX_STATUS        0x098
#define NFE_SETUP_R1         0x0A0
#define NFE_SETUP_R2         0x0A4
#define NFE_MACADDR_HI       0x0A8
#define NFE_MACADDR_LO       0x0AC
#define NFE_TX_RING_ADDR_LO  0x100
#define NFE_RX_RING_ADDR_LO  0x104
#define NFE_RING_SIZE        0x108
#define NFE_LINKSPEED        0x110
#define NFE_SETUP_R5         0x130
#define NFE_SETUP_R3         0x13C
#define NFE_SETUP_R7         0x140
#define NFE_RXTX_CTL         0x144

/* Control bits/magic */
#define NFE_TX_START         0x01
#define NFE_RX_START         0x01

#define NFE_RXTX_KICKTX      0x0001
#define NFE_RXTX_BIT2        0x0004
#define NFE_RXTX_RESET       0x0010
#define NFE_RXTX_V3MAGIC     0x2200

#define NFE_R1_MAGIC         0x16070f
#define NFE_R2_MAGIC         0x16
#define NFE_R4_MAGIC         0x08

/* Ring size register field shifts */
#define NFE_RING_TX_SHIFT    0
#define NFE_RING_RX_SHIFT    16

/* Descriptor flags */
#define NFE_DESC_OWN         0x8000
#define NFE_RX_VALID         0x0001
#define NFE_TX_LASTFRAG      0x0001

#define NFE_TX_RING_SIZE     64
#define NFE_RX_RING_SIZE     64
#define NFE_RX_BUF_SIZE      2048

#define MAX_NFORCE           2

struct nfe_desc32 {
    uint32_t physaddr;
    uint16_t length;
    uint16_t flags;
} __attribute__((packed));

struct nforce_softc {
    struct pci_dev *pci;
    struct spinlock lock;
    struct ifnet ifn;

    volatile uint8_t *regs;
    uint8_t mac[6];

    struct nfe_desc32 *tx_ring;
    char *tx_bufs[NFE_TX_RING_SIZE];
    struct mbuf *tx_mbufs[NFE_TX_RING_SIZE];
    int tx_prod;
    int tx_cons;

    struct nfe_desc32 *rx_ring;
    char *rx_bufs[NFE_RX_RING_SIZE];
    int rx_cons;
};

static struct nforce_softc nforce_devices[MAX_NFORCE];
static int nforce_count;

static int nforce_output(struct ifnet *ifp, struct mbuf *m);
static void nforce_poll(struct ifnet *ifp);

static struct ifnet_ops nforce_ifnet_ops = {
    .if_output = nforce_output,
    .if_poll = nforce_poll,
};

static int
nforce_match(struct pci_dev *dev)
{
    return dev && dev->vendor_id == NFORCE_VENDOR_NVIDIA &&
           dev->device_id == NFORCE_DEV_MCP79;
}

static uint32_t
nfe_r32(struct nforce_softc *sc, int reg)
{
    return *(volatile uint32_t *)(sc->regs + reg);
}

static void
nfe_w32(struct nforce_softc *sc, int reg, uint32_t val)
{
    *(volatile uint32_t *)(sc->regs + reg) = val;
}

static void
nforce_make_local_mac(struct pci_dev *dev, uint8_t mac[6])
{
    mac[0] = 0x02;
    mac[1] = dev->vendor_id & 0xFF;
    mac[2] = dev->device_id & 0xFF;
    mac[3] = dev->bus;
    mac[4] = dev->slot;
    mac[5] = dev->func;
}

static void
nforce_read_mac(struct nforce_softc *sc)
{
    uint32_t hi, lo;
    int i, all0 = 1, allf = 1;

    /* MCP79 follows the corrected ordering seen in newer nforce parts. */
    hi = nfe_r32(sc, NFE_MACADDR_HI);
    lo = nfe_r32(sc, NFE_MACADDR_LO);

    sc->mac[0] = hi & 0xFF;
    sc->mac[1] = (hi >> 8) & 0xFF;
    sc->mac[2] = (hi >> 16) & 0xFF;
    sc->mac[3] = (hi >> 24) & 0xFF;
    sc->mac[4] = lo & 0xFF;
    sc->mac[5] = (lo >> 8) & 0xFF;

    for (i = 0; i < 6; i++) {
        if (sc->mac[i] != 0x00)
            all0 = 0;
        if (sc->mac[i] != 0xFF)
            allf = 0;
    }
    if (all0 || allf || (sc->mac[0] & 1))
        nforce_make_local_mac(sc->pci, sc->mac);
}

static void
nforce_reset(struct nforce_softc *sc)
{
    nfe_w32(sc, NFE_RXTX_CTL, NFE_RXTX_RESET | NFE_RXTX_BIT2 | NFE_RXTX_V3MAGIC);
    microdelay(10);
    nfe_w32(sc, NFE_RXTX_CTL, NFE_RXTX_BIT2 | NFE_RXTX_V3MAGIC);
}

static int
nforce_alloc_rings(struct nforce_softc *sc)
{
    int i;

    sc->tx_ring = (struct nfe_desc32 *)kalloc();
    if (!sc->tx_ring)
        return -1;
    memset(sc->tx_ring, 0, sizeof(struct nfe_desc32) * NFE_TX_RING_SIZE);

    for (i = 0; i < NFE_TX_RING_SIZE; i++) {
        sc->tx_bufs[i] = kalloc();
        if (!sc->tx_bufs[i])
            return -1;
    }

    sc->rx_ring = (struct nfe_desc32 *)kalloc();
    if (!sc->rx_ring)
        return -1;
    memset(sc->rx_ring, 0, sizeof(struct nfe_desc32) * NFE_RX_RING_SIZE);

    for (i = 0; i < NFE_RX_RING_SIZE; i++) {
        sc->rx_bufs[i] = kalloc();
        if (!sc->rx_bufs[i])
            return -1;
        sc->rx_ring[i].physaddr = V2P(sc->rx_bufs[i]);
        sc->rx_ring[i].length = NFE_RX_BUF_SIZE;
        sc->rx_ring[i].flags = NFE_DESC_OWN;
    }

    sc->tx_prod = 0;
    sc->tx_cons = 0;
    sc->rx_cons = 0;
    return 0;
}

static void
nforce_hw_init(struct nforce_softc *sc)
{
    uint32_t ringsz;

    /* Stop paths before reprogramming rings. */
    nfe_w32(sc, NFE_TX_CTL, 0);
    nfe_w32(sc, NFE_RX_CTL, 0);

    nfe_w32(sc, NFE_SETUP_R1, NFE_R1_MAGIC);
    nfe_w32(sc, NFE_SETUP_R2, NFE_R2_MAGIC);
    nfe_w32(sc, NFE_SETUP_R7, NFE_R4_MAGIC);

    nfe_w32(sc, NFE_TX_RING_ADDR_LO, V2P(sc->tx_ring));
    nfe_w32(sc, NFE_RX_RING_ADDR_LO, V2P(sc->rx_ring));

    ringsz = ((NFE_RX_RING_SIZE - 1) << NFE_RING_RX_SHIFT) |
             ((NFE_TX_RING_SIZE - 1) << NFE_RING_TX_SHIFT);
    nfe_w32(sc, NFE_RING_SIZE, ringsz);

    /* Conservative defaults for tranche 1 bring-up. */
    nfe_w32(sc, NFE_SETUP_R5, 0);
    nfe_w32(sc, NFE_SETUP_R3, 0);
    nfe_w32(sc, NFE_LINKSPEED, 0);
    nfe_w32(sc, NFE_RX_STATUS, 0xF);

    nfe_w32(sc, NFE_RXTX_CTL, NFE_RXTX_BIT2 | NFE_RXTX_V3MAGIC);

    nfe_w32(sc, NFE_RX_CTL, NFE_RX_START);
    nfe_w32(sc, NFE_TX_CTL, NFE_TX_START);
    nfe_w32(sc, NFE_RXTX_CTL, NFE_RXTX_BIT2 | NFE_RXTX_V3MAGIC | NFE_RXTX_KICKTX);
}

static void
nforce_tx_complete(struct nforce_softc *sc)
{
    struct nfe_desc32 *d;

    while (sc->tx_cons != sc->tx_prod) {
        d = &sc->tx_ring[sc->tx_cons];
        if (d->flags & NFE_DESC_OWN)
            break;
        if (sc->tx_mbufs[sc->tx_cons]) {
            mbuf_free(sc->tx_mbufs[sc->tx_cons]);
            sc->tx_mbufs[sc->tx_cons] = 0;
        }
        sc->tx_cons = (sc->tx_cons + 1) % NFE_TX_RING_SIZE;
    }
}

static void
nforce_rx_complete(struct nforce_softc *sc)
{
    struct nfe_desc32 *d;
    struct mbuf *m;
    uint16_t flags;
    uint16_t len;
    int processed = 0;

    while (processed < 32) {
        d = &sc->rx_ring[sc->rx_cons];
        flags = d->flags;

        if (flags & NFE_DESC_OWN)
            break;

        len = d->length & 0x3FFF;
        if ((flags & NFE_RX_VALID) && len > 0 && len <= NFE_RX_BUF_SIZE) {
            m = mbuf_alloc();
            if (m) {
                memmove(m->data, sc->rx_bufs[sc->rx_cons], len);
                m->len = len;
                m->rcvif = &sc->ifn;
                release(&sc->lock);
                if_input(&sc->ifn, m);
                acquire(&sc->lock);
            }
        }

        d->length = NFE_RX_BUF_SIZE;
        d->flags = NFE_DESC_OWN;
        sc->rx_cons = (sc->rx_cons + 1) % NFE_RX_RING_SIZE;
        processed++;
    }
}

static void
nforce_poll(struct ifnet *ifp)
{
    struct nforce_softc *sc = (struct nforce_softc *)ifp->if_softc;
    if (!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;

    acquire(&sc->lock);
    nforce_tx_complete(sc);
    nforce_rx_complete(sc);
    release(&sc->lock);
}

static int
nforce_output(struct ifnet *ifp, struct mbuf *m)
{
    struct nforce_softc *sc = (struct nforce_softc *)ifp->if_softc;
    struct nfe_desc32 *d;
    int idx, next;

    if (!sc || !m || m->len == 0)
        return -1;

    acquire(&sc->lock);

    if ((ifp->if_flags & IFF_RUNNING) == 0) {
        release(&sc->lock);
        return -1;
    }

    nforce_tx_complete(sc);

    idx = sc->tx_prod;
    next = (idx + 1) % NFE_TX_RING_SIZE;
    if (next == sc->tx_cons) {
        release(&sc->lock);
        return -1;
    }

    if (m->len > NFE_RX_BUF_SIZE) {
        release(&sc->lock);
        return -1;
    }

    memmove(sc->tx_bufs[idx], m->data, m->len);
    sc->tx_mbufs[idx] = m;

    d = &sc->tx_ring[idx];
    d->physaddr = V2P(sc->tx_bufs[idx]);
    d->length = m->len;
    d->flags = NFE_DESC_OWN | NFE_TX_LASTFRAG;

    sc->tx_prod = next;

    nfe_w32(sc, NFE_RXTX_CTL, NFE_RXTX_BIT2 | NFE_RXTX_V3MAGIC | NFE_RXTX_KICKTX);

    release(&sc->lock);
    return 0;
}

static int
nforce_probe(struct pci_dev *dev)
{
    struct nforce_softc *sc;

    if (nforce_count >= MAX_NFORCE)
        return -1;

    sc = &nforce_devices[nforce_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "nforce");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "nforce");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    sc->regs = pci_map_bar(dev, 0);
    if (!sc->regs) {
        cprintf("nforce: failed to map BAR0 at %d:%d.%d\n",
                dev->bus, dev->slot, dev->func);
        return -1;
    }

    BOOTDBG("nforce: found %04x at %d:%d.%d irq=%d regs=%p\n",
            dev->device_id, dev->bus, dev->slot, dev->func,
            dev->irq_line, sc->regs);

    nforce_reset(sc);
    nforce_read_mac(sc);

    if (nforce_alloc_rings(sc) < 0) {
        cprintf("nforce: ring allocation failed\n");
        return -1;
    }

    nforce_hw_init(sc);

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "nfe0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[3] = '0' + nforce_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_BROADCAST | IFF_RUNNING;
    sc->ifn.if_link_state = LINK_STATE_UP;
    memmove(sc->ifn.if_hwaddr, sc->mac, 6);
    sc->ifn.if_softc = sc;
    sc->ifn.if_input = ether_input;
    sc->ifn.if_ops = &nforce_ifnet_ops;

    if (if_register(&sc->ifn) < 0) {
        cprintf("nforce: failed to register ifnet\n");
        return -1;
    }

    cprintf("nforce: attached %s MCP79 MAC %x:%x:%x:%x:%x:%x\n",
            sc->ifn.if_xname,
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);

    nforce_count++;
    return 0;
}

void
nforce_init(void)
{
    int i;

    BOOTDBG("nforce: initializing NVIDIA nForce driver\n");
    for (i = 0; i < pci_device_count(); i++) {
        struct pci_dev *dev = pci_get_device(i);
        if (nforce_match(dev))
            nforce_probe(dev);
    }
}
