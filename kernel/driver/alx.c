/*
 * Qualcomm Atheros alx-family NIC driver for auxv6.
 *
 * Covers AR8131/AR8151/AR8161/AR8171 Gigabit parts.
 *
 * Full descriptor-ring TX/RX: TPD transmit ring, RFD receive-free ring,
 * and RRD receive-return ring, with polling completions via ifnet.if_poll.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

#define ALX_VENDOR_ATTANSIC 0x1969
#define ALX_DEV_AR8131      0x1063
#define ALX_DEV_AR8151      0x1062
#define ALX_DEV_AR8161      0x1091
#define ALX_DEV_AR8171      0x10A1

/* Master control */
#define ALX_MASTER           0x1400
#define ALX_MASTER_RESET     0x00000001
/* MAC address */
#define ALX_STAD0            0x1488
#define ALX_STAD1            0x148C
/* Interrupt mask */
#define ALX_IMR              0x1514
/* TX ring */
#define ALX_TPD_PRI0_ADDR_LO 0x1544
#define ALX_TPD_RING_SZ      0x1580
/* RX free descriptor ring */
#define ALX_RFD_ADDR_LO      0x1540
#define ALX_RFD_RING_SZ      0x1582
/* RX return descriptor ring */
#define ALX_RRD_ADDR_LO      0x1560
#define ALX_RRD_RING_SZ      0x1584
/* RX buffer size */
#define ALX_RFD_BUF_SZ       0x1578
/* Producer/consumer index registers (16-bit at these byte offsets) */
#define ALX_RFD_PIDX         0x15B0
#define ALX_TPD_PRI0_PIDX    0x15B4
#define ALX_TPD_PRI0_CIDX    0x15C0
#define ALX_RRD_CIDX         0x15BC
/* Queue enable */
#define ALX_TX_Q0            0x15A0
#define ALX_RX_Q0            0x15A4

#define ALX_TX_RING_SIZE     64
#define ALX_RX_RING_SIZE     64
#define ALX_RX_BUF_SIZE      2048

#define ALX_RRD_UPDATED      0x80000000U
#define ALX_RRD_PKT_LEN(w3)  (((w3) >> 16) & 0x3FFF)
#define ALX_TPD_EOP          0x40000000U

#define MAX_ALX 4

/* Transmit Packet Descriptor (16 bytes) */
struct alx_tpd {
    uint16_t    len;
    uint16_t    vlan;
    uint32_t    word1;
    uint32_t    addr_lo;
    uint32_t    addr_hi;
} __attribute__((packed));

/* Receive Free Descriptor (8 bytes) */
struct alx_rfd {
    uint32_t    addr_lo;
    uint32_t    addr_hi;
} __attribute__((packed));

/* Receive Return Descriptor (16 bytes) */
struct alx_rrd {
    uint32_t    word0;
    uint32_t    rss_hash;
    uint32_t    word2;
    uint32_t    word3;
} __attribute__((packed));

struct alx_softc {
    struct pci_dev *pci;
    struct spinlock lock;
    struct ifnet ifn;
    volatile uint32_t *regs;
    uint8_t mac[6];
    struct alx_tpd  *tx_ring;
    struct mbuf     *tx_mbufs[ALX_TX_RING_SIZE];
    uint16_t         tx_pidx;
    uint16_t         tx_cidx;
    struct alx_rfd  *rfd_ring;
    char            *rx_bufs[ALX_RX_RING_SIZE];
    uint16_t         rfd_pidx;
    struct alx_rrd  *rrd_ring;
    uint16_t         rrd_cidx;
};

static struct alx_softc alx_devices[MAX_ALX];
static int alx_count;

static int alx_output(struct ifnet *ifp, struct mbuf *m);
static void alx_poll(struct ifnet *ifp);

static struct ifnet_ops alx_ifnet_ops = {
    .if_output = alx_output,
    .if_poll = alx_poll,
};

static int
alx_match(struct pci_dev *dev)
{
    if(!dev || dev->vendor_id != ALX_VENDOR_ATTANSIC)
        return 0;

    switch(dev->device_id){
    case ALX_DEV_AR8131:
    case ALX_DEV_AR8151:
    case ALX_DEV_AR8161:
    case ALX_DEV_AR8171:
        return 1;
    default:
        return 0;
    }
}

static uint32_t
alx_read32(struct alx_softc *sc, int reg)
{
    return sc->regs[reg / 4];
}

static void
alx_write32(struct alx_softc *sc, int reg, uint32_t val)
{
    sc->regs[reg / 4] = val;
}

static uint16_t
alx_read16(struct alx_softc *sc, int reg)
{
    volatile uint8_t *p = (volatile uint8_t *)sc->regs;
    return *((volatile uint16_t *)(p + reg));
}

static void
alx_write16(struct alx_softc *sc, int reg, uint16_t val)
{
    volatile uint8_t *p = (volatile uint8_t *)sc->regs;
    *((volatile uint16_t *)(p + reg)) = val;
}

static void
alx_make_local_mac(struct pci_dev *dev, uint8_t mac[6])
{
    mac[0] = 0x02;
    mac[1] = dev->vendor_id & 0xFF;
    mac[2] = dev->device_id & 0xFF;
    mac[3] = dev->bus;
    mac[4] = dev->slot;
    mac[5] = dev->func;
}

static void
alx_read_mac(struct alx_softc *sc)
{
    uint32_t lo = alx_read32(sc, ALX_STAD0);
    uint32_t hi = alx_read32(sc, ALX_STAD1);
    int i, all0 = 1, allf = 1;

    sc->mac[0] = lo & 0xFF;
    sc->mac[1] = (lo >> 8) & 0xFF;
    sc->mac[2] = (lo >> 16) & 0xFF;
    sc->mac[3] = (lo >> 24) & 0xFF;
    sc->mac[4] = hi & 0xFF;
    sc->mac[5] = (hi >> 8) & 0xFF;

    for(i = 0; i < 6; i++){
        if(sc->mac[i] != 0x00) all0 = 0;
        if(sc->mac[i] != 0xFF) allf = 0;
    }
    if(all0 || allf || (sc->mac[0] & 1))
        alx_make_local_mac(sc->pci, sc->mac);
}

static void
alx_reset(struct alx_softc *sc)
{
    alx_write32(sc, ALX_IMR, 0);
    alx_write32(sc, ALX_MASTER,
        alx_read32(sc, ALX_MASTER) | ALX_MASTER_RESET);
    microdelay(50000);
    alx_write32(sc, ALX_IMR, 0);
}

static int
alx_init_tx(struct alx_softc *sc)
{
    sc->tx_ring = (struct alx_tpd *)kalloc();
    if(!sc->tx_ring)
        return -1;
    memset(sc->tx_ring, 0, sizeof(struct alx_tpd) * ALX_TX_RING_SIZE);
    sc->tx_pidx = 0;
    sc->tx_cidx = 0;
    alx_write32(sc, ALX_TPD_PRI0_ADDR_LO, V2P(sc->tx_ring));
    alx_write16(sc, ALX_TPD_RING_SZ, ALX_TX_RING_SIZE);
    alx_write32(sc, ALX_TX_Q0, 1);
    return 0;
}

static int
alx_init_rx(struct alx_softc *sc)
{
    int i;
    sc->rfd_ring = (struct alx_rfd *)kalloc();
    if(!sc->rfd_ring)
        return -1;
    sc->rrd_ring = (struct alx_rrd *)kalloc();
    if(!sc->rrd_ring)
        return -1;
    memset(sc->rfd_ring, 0, sizeof(struct alx_rfd) * ALX_RX_RING_SIZE);
    memset(sc->rrd_ring, 0, sizeof(struct alx_rrd) * ALX_RX_RING_SIZE);
    for(i = 0; i < ALX_RX_RING_SIZE; i++){
        sc->rx_bufs[i] = kalloc();
        if(!sc->rx_bufs[i])
            return -1;
        sc->rfd_ring[i].addr_lo = V2P(sc->rx_bufs[i]);
        sc->rfd_ring[i].addr_hi = 0;
    }
    sc->rfd_pidx = ALX_RX_RING_SIZE;
    sc->rrd_cidx = 0;
    alx_write32(sc, ALX_RFD_ADDR_LO, V2P(sc->rfd_ring));
    alx_write32(sc, ALX_RRD_ADDR_LO, V2P(sc->rrd_ring));
    alx_write16(sc, ALX_RFD_RING_SZ, ALX_RX_RING_SIZE);
    alx_write16(sc, ALX_RRD_RING_SZ, ALX_RX_RING_SIZE);
    alx_write32(sc, ALX_RFD_BUF_SZ, ALX_RX_BUF_SIZE);
    alx_write16(sc, ALX_RFD_PIDX, (uint16_t)(ALX_RX_RING_SIZE & 0xFFFF));
    alx_write32(sc, ALX_RX_Q0, 1);
    return 0;
}

static void
alx_tx_complete(struct alx_softc *sc)
{
    uint16_t cidx = alx_read16(sc, ALX_TPD_PRI0_CIDX);
    while(sc->tx_cidx != cidx){
        uint16_t idx = sc->tx_cidx % ALX_TX_RING_SIZE;
        if(sc->tx_mbufs[idx]){
            mbuf_free(sc->tx_mbufs[idx]);
            sc->tx_mbufs[idx] = 0;
        }
        sc->tx_cidx = (sc->tx_cidx + 1) % ALX_TX_RING_SIZE;
    }
}

static void
alx_rx_complete(struct alx_softc *sc)
{
    int processed = 0;
    while(processed < 32){
        uint16_t idx = sc->rrd_cidx % ALX_RX_RING_SIZE;
        struct alx_rrd *rrd = &sc->rrd_ring[idx];
        uint16_t len, rfd_idx;
        if((rrd->word3 & ALX_RRD_UPDATED) == 0)
            break;
        len = (uint16_t)ALX_RRD_PKT_LEN(rrd->word3);
        rfd_idx = rrd->word0 & 0x7FF;
        if(len > 0 && len <= ALX_RX_BUF_SIZE && rfd_idx < ALX_RX_RING_SIZE){
            struct mbuf *m = mbuf_alloc();
            if(m){
                memmove(m->data, sc->rx_bufs[rfd_idx], len);
                m->len = len;
                m->rcvif = &sc->ifn;
                release(&sc->lock);
                if_input(&sc->ifn, m);
                acquire(&sc->lock);
            }
        }
        rrd->word3 = 0;
        sc->rfd_ring[rfd_idx].addr_lo = V2P(sc->rx_bufs[rfd_idx]);
        sc->rfd_ring[rfd_idx].addr_hi = 0;
        sc->rrd_cidx = (sc->rrd_cidx + 1) % ALX_RX_RING_SIZE;
        sc->rfd_pidx  = (sc->rfd_pidx  + 1) % ALX_RX_RING_SIZE;
        alx_write16(sc, ALX_RFD_PIDX, sc->rfd_pidx);
        alx_write16(sc, ALX_RRD_CIDX, sc->rrd_cidx);
        processed++;
    }
}

static int
alx_output(struct ifnet *ifp, struct mbuf *m)
{
    struct alx_softc *sc = (struct alx_softc *)ifp->if_softc;
    uint16_t next;
    struct alx_tpd *desc;

    if(!sc || !m || m->len == 0)
        return -1;
    acquire(&sc->lock);
    if((ifp->if_flags & IFF_RUNNING) == 0){
        release(&sc->lock);
        return -1;
    }
    alx_tx_complete(sc);
    next = (sc->tx_pidx + 1) % ALX_TX_RING_SIZE;
    if(next == sc->tx_cidx){
        release(&sc->lock);
        return -1;
    }
    desc = &sc->tx_ring[sc->tx_pidx];
    desc->len     = (uint16_t)m->len;
    desc->vlan    = 0;
    desc->word1   = ALX_TPD_EOP;
    desc->addr_lo = V2P(m->data);
    desc->addr_hi = 0;
    sc->tx_mbufs[sc->tx_pidx] = m;
    sc->tx_pidx = next;
    alx_write16(sc, ALX_TPD_PRI0_PIDX, sc->tx_pidx);
    release(&sc->lock);
    return 0;
}

static void
alx_poll(struct ifnet *ifp)
{
    struct alx_softc *sc = (struct alx_softc *)ifp->if_softc;
    if(!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;
    acquire(&sc->lock);
    alx_tx_complete(sc);
    alx_rx_complete(sc);
    release(&sc->lock);
}

static void
alx_probe(struct pci_dev *dev)
{
    struct alx_softc *sc;

    if(alx_count >= MAX_ALX)
        return;

    sc = &alx_devices[alx_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "alx");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "alx");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    sc->regs = (volatile uint32_t *)pci_map_bar(dev, 0);
    if(!sc->regs){
        cprintf("alx: failed to map BAR0\n");
        return;
    }
    alx_reset(sc);
    alx_read_mac(sc);
    if(alx_init_tx(sc) < 0 || alx_init_rx(sc) < 0){
        cprintf("alx: failed to init rings\n");
        return;
    }

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "alx0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[3] = '0' + alx_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_RUNNING | IFF_BROADCAST;
    memmove(sc->ifn.if_hwaddr, sc->mac, 6);
    sc->ifn.if_softc = sc;
    sc->ifn.if_ops = &alx_ifnet_ops;
    sc->ifn.if_input = ether_input;

    if(if_register(&sc->ifn) < 0){
        cprintf("alx: if_register failed for %x\n", dev->device_id);
        return;
    }

    cprintf("alx: attached %s dev=%x irq=%d mac=%x:%x:%x:%x:%x:%x\n",
            sc->ifn.if_xname, dev->device_id, dev->irq_line,
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
    alx_count++;
}

void
alx_init(void)
{
    int i;

    BOOTDBG("alx: probing supported PCI IDs\n");
    for(i = 0; i < pci_device_count(); i++){
        struct pci_dev *dev = pci_get_device(i);
        if(dev && alx_match(dev))
            alx_probe(dev);
    }
}
