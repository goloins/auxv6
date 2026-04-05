/*
 * Realtek RTL8125/RTL8125A/RTL8125B 2.5GbE Ethernet Driver for auxv6
 *
 * Supports:
 *   RTL8125A  (10EC:8125) — PCIe 2.5G
 *   RTL8125B/BG (10EC:8162) — PCIe 2.5G variant
 *
 * Architecture:
 *   Memory-mapped I/O via BAR2 (preferred, 64-bit) or BAR0 fallback.
 *   TX/RX descriptor rings use the same 16-byte layout as RTL8111/RTL8169.
 *   Multi-queue capable (up to 4 queues); this tranche uses a single queue.
 *
 * Current tranche: PCI probe, BAR map, single TX/RX queue, polling.
 *   IRQ-driven completions, multi-queue (RSS), and checksum offload deferred.
 *
 * Reference: Realtek RTL8125 PCIe 2.5G Family Datasheet
 * See also:  Linux drivers/net/ethernet/realtek/r8169_main.c (r8169 driver)
 *            NetBSD sys/dev/pci/if_re.c
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

/* PCI IDs */
#define RTL8125_VENDOR      0x10EC
#define RTL8125_DEV_8125    0x8125   /* RTL8125A */
#define RTL8125_DEV_8162    0x8162   /* RTL8125B/BG */

/* MMIO registers (RTL8125 superset of RTL8169) */
#define RTL8125_IDR0        0x00    /* MAC address (6 bytes) */
#define RTL8125_MAR0        0x08    /* Multicast filter (8 bytes) */
#define RTL8125_TNPDS_LO    0x20    /* TX normal priority desc base (lo) */
#define RTL8125_TNPDS_HI    0x24    /* TX normal priority desc base (hi) */
#define RTL8125_CR          0x37    /* Command register */
#define RTL8125_IMR0        0x38    /* Interrupt mask (word) */
#define RTL8125_ISR0        0x3C    /* Interrupt status (word) */
#define RTL8125_TCR         0x40    /* TX configuration */
#define RTL8125_RCR         0x44    /* RX configuration */
#define RTL8125_PHYSTATUS   0x6C    /* PHY status register */
#define RTL8125_RMS         0xDA    /* RX max packet size */
#define RTL8125_RDSAR_LO    0xE4    /* RX descriptor start address (lo) */
#define RTL8125_RDSAR_HI    0xE8    /* RX descriptor start address (hi) */
#define RTL8125_TPPOLL      0xD9    /* TX poll demand (byte) */
/* RTL8125-specific extension registers */
#define RTL8125_INT_CFG0    0x34    /* Interrupt config 0 */
#define RTL8125_INT_CFG1    0xF8    /* Interrupt config 1 */
#define RTL8125_IMR1        0x800   /* Interrupt mask 1 (extended queues) */
#define RTL8125_ISR1        0x802   /* Interrupt status 1 (extended queues) */
#define RTL8125_TPPOLL1     0x1D9   /* TX poll demand queue 1 */

/* Command register bits */
#define RTL8125_CR_RST      0x10
#define RTL8125_CR_RE       0x08
#define RTL8125_CR_TE       0x04

/* TX config bits */
#define RTL8125_TCR_IFG_STD     0x03000000
#define RTL8125_TCR_MXDMA_UNLIM 0x00000700

/* RX config bits */
#define RTL8125_RCR_APM         0x00000002
#define RTL8125_RCR_AM          0x00000004
#define RTL8125_RCR_AB          0x00000008
#define RTL8125_RCR_MXDMA_UNLIM 0x00000700

/* PHY status bits */
#define RTL8125_PHY_LINKUP  0x02    /* Bit 1 = link up */
#define RTL8125_PHY_2500M   0x10    /* 2500 Mbit */
#define RTL8125_PHY_1000M   0x08    /* 1000 Mbit */
#define RTL8125_PHY_100M    0x04    /* 100 Mbit */
#define RTL8125_PHY_10M     0x01    /* 10 Mbit */

/* ISR0 bits */
#define RTL8125_ISR_ROK     0x0001
#define RTL8125_ISR_RER     0x0002
#define RTL8125_ISR_TOK     0x0004
#define RTL8125_ISR_TER     0x0008
#define RTL8125_ISR_LINKCHG 0x0020
#define RTL8125_ISR_RXOVW   0x0010

/* TX/RX descriptor — identical layout to RTL8111/RTL8169 */
struct rtl8125_desc {
    uint32_t opts1;
    uint32_t opts2;
    uint32_t addr_lo;
    uint32_t addr_hi;
} __attribute__((packed));

#define DESC_OWN        0x80000000
#define DESC_EOR        0x40000000
#define DESC_FS         0x20000000
#define DESC_LS         0x10000000
#define DESC_LEN_MASK   0x00003FFF

#define RTL8125_TX_RING_SIZE    64
#define RTL8125_RX_RING_SIZE    64
#define RTL8125_RX_BUF_SIZE     2048

#define MAX_RTL8125 4

struct rtl8125_softc {
    struct pci_dev   *pci;
    struct spinlock   lock;
    struct ifnet      ifn;

    volatile uint8_t *regs;
    uint8_t           mac[6];

    /* TX ring */
    struct rtl8125_desc *tx_ring;
    char                *tx_bufs[RTL8125_TX_RING_SIZE];
    struct mbuf         *tx_mbufs[RTL8125_TX_RING_SIZE];
    int                  tx_head;
    int                  tx_tail;

    /* RX ring */
    struct rtl8125_desc *rx_ring;
    char                *rx_bufs[RTL8125_RX_RING_SIZE];
    int                  rx_cur;
};

static struct rtl8125_softc rtl8125_devices[MAX_RTL8125];
static int rtl8125_count;
extern int ncpu;

static int  rtl8125_output(struct ifnet *ifp, struct mbuf *m);
static void rtl8125_poll(struct ifnet *ifp);

static struct ifnet_ops rtl8125_ifnet_ops = {
    .if_output = rtl8125_output,
    .if_poll   = rtl8125_poll,
};

static int
rtl8125_match(struct pci_dev *dev)
{
    if (!dev || dev->vendor_id != RTL8125_VENDOR)
        return 0;
    return (dev->device_id == RTL8125_DEV_8125 ||
            dev->device_id == RTL8125_DEV_8162);
}

static uint8_t
rtl8125_r8(struct rtl8125_softc *sc, int r)
{
    return sc->regs[r];
}

static void
rtl8125_w8(struct rtl8125_softc *sc, int r, uint8_t v)
{
    sc->regs[r] = v;
}

static void __attribute__((unused))
rtl8125_w16(struct rtl8125_softc *sc, int r, uint16_t v)
{
    *(volatile uint16_t *)(sc->regs + r) = v;
}

static void
rtl8125_w32(struct rtl8125_softc *sc, int r, uint32_t v)
{
    *(volatile uint32_t *)(sc->regs + r) = v;
}

static uint32_t __attribute__((unused))
rtl8125_r32(struct rtl8125_softc *sc, int r)
{
    return *(volatile uint32_t *)(sc->regs + r);
}

static void
rtl8125_read_mac(struct rtl8125_softc *sc)
{
    int i;
    for (i = 0; i < 6; i++)
        sc->mac[i] = rtl8125_r8(sc, RTL8125_IDR0 + i);
}

static void
rtl8125_reset(struct rtl8125_softc *sc)
{
    int timeout;

    rtl8125_w8(sc, RTL8125_CR, RTL8125_CR_RST);
    for (timeout = 0; timeout < 1000; timeout++) {
        if (!(rtl8125_r8(sc, RTL8125_CR) & RTL8125_CR_RST))
            break;
        microdelay(1000);
    }
    if (rtl8125_r8(sc, RTL8125_CR) & RTL8125_CR_RST)
        cprintf("rtl8125: reset timeout\n");
}

static int
rtl8125_init_tx(struct rtl8125_softc *sc)
{
    int i;

    sc->tx_ring = (struct rtl8125_desc *)kalloc();
    if (!sc->tx_ring)
        return -1;
    memset(sc->tx_ring, 0,
           sizeof(struct rtl8125_desc) * RTL8125_TX_RING_SIZE);

    for (i = 0; i < RTL8125_TX_RING_SIZE; i++) {
        sc->tx_bufs[i] = kalloc();
        if (!sc->tx_bufs[i])
            return -1;
        sc->tx_ring[i].addr_lo = V2P(sc->tx_bufs[i]);
        sc->tx_ring[i].addr_hi = 0;
    }
    sc->tx_ring[RTL8125_TX_RING_SIZE - 1].opts1 = DESC_EOR;
    sc->tx_head = 0;
    sc->tx_tail = 0;

    rtl8125_w32(sc, RTL8125_TNPDS_LO, V2P(sc->tx_ring));
    rtl8125_w32(sc, RTL8125_TNPDS_HI, 0);
    rtl8125_w32(sc, RTL8125_TCR,
        RTL8125_TCR_IFG_STD | RTL8125_TCR_MXDMA_UNLIM);
    return 0;
}

static int
rtl8125_init_rx(struct rtl8125_softc *sc)
{
    int i;

    sc->rx_ring = (struct rtl8125_desc *)kalloc();
    if (!sc->rx_ring)
        return -1;
    memset(sc->rx_ring, 0,
           sizeof(struct rtl8125_desc) * RTL8125_RX_RING_SIZE);

    for (i = 0; i < RTL8125_RX_RING_SIZE; i++) {
        sc->rx_bufs[i] = kalloc();
        if (!sc->rx_bufs[i])
            return -1;
        sc->rx_ring[i].addr_lo = V2P(sc->rx_bufs[i]);
        sc->rx_ring[i].addr_hi = 0;
        sc->rx_ring[i].opts1   = DESC_OWN | RTL8125_RX_BUF_SIZE;
    }
    sc->rx_ring[RTL8125_RX_RING_SIZE - 1].opts1 |= DESC_EOR;
    sc->rx_cur = 0;

    rtl8125_w32(sc, RTL8125_RDSAR_LO, V2P(sc->rx_ring));
    rtl8125_w32(sc, RTL8125_RDSAR_HI, 0);
    *(volatile uint16_t *)(sc->regs + RTL8125_RMS) = RTL8125_RX_BUF_SIZE;
    rtl8125_w32(sc, RTL8125_RCR,
        RTL8125_RCR_APM | RTL8125_RCR_AB | RTL8125_RCR_AM |
        RTL8125_RCR_MXDMA_UNLIM);
    return 0;
}

static void
rtl8125_tx_complete(struct rtl8125_softc *sc)
{
    struct rtl8125_desc *d;
    while (sc->tx_head != sc->tx_tail) {
        d = &sc->tx_ring[sc->tx_head];
        if (d->opts1 & DESC_OWN)
            break;
        if (sc->tx_mbufs[sc->tx_head]) {
            mbuf_free(sc->tx_mbufs[sc->tx_head]);
            sc->tx_mbufs[sc->tx_head] = 0;
        }
        sc->tx_head = (sc->tx_head + 1) % RTL8125_TX_RING_SIZE;
    }
}

static void
rtl8125_rx_complete(struct rtl8125_softc *sc)
{
    struct rtl8125_desc *d;
    struct mbuf *m;
    int processed = 0;

    while (processed < 32) {
        d = &sc->rx_ring[sc->rx_cur];
        if (d->opts1 & DESC_OWN)
            break;

        if ((d->opts1 & (DESC_FS | DESC_LS)) == (DESC_FS | DESC_LS)) {
            uint16_t len = (uint16_t)(d->opts1 & DESC_LEN_MASK);
            if (len > 0 && len <= RTL8125_RX_BUF_SIZE) {
                m = mbuf_alloc();
                if (m) {
                    memmove(m->data, sc->rx_bufs[sc->rx_cur], len);
                    m->len  = len;
                    m->rcvif = &sc->ifn;
                    release(&sc->lock);
                    if_input(&sc->ifn, m);
                    acquire(&sc->lock);
                }
            }
        }

        d->opts1 = DESC_OWN | RTL8125_RX_BUF_SIZE;
        if (sc->rx_cur == RTL8125_RX_RING_SIZE - 1)
            d->opts1 |= DESC_EOR;
        sc->rx_cur = (sc->rx_cur + 1) % RTL8125_RX_RING_SIZE;
        processed++;
    }
}

static void
rtl8125_poll(struct ifnet *ifp)
{
    struct rtl8125_softc *sc = (struct rtl8125_softc *)ifp->if_softc;
    if (!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;
    acquire(&sc->lock);
    rtl8125_tx_complete(sc);
    rtl8125_rx_complete(sc);
    release(&sc->lock);
}

static int
rtl8125_output(struct ifnet *ifp, struct mbuf *m)
{
    struct rtl8125_softc *sc = (struct rtl8125_softc *)ifp->if_softc;
    struct rtl8125_desc *d;
    int idx;

    if (!sc || !m || m->len == 0)
        return -1;

    acquire(&sc->lock);

    if ((ifp->if_flags & IFF_RUNNING) == 0) {
        release(&sc->lock);
        return -1;
    }

    rtl8125_tx_complete(sc);

    idx = sc->tx_tail;
    d   = &sc->tx_ring[idx];
    if (d->opts1 & DESC_OWN) {
        release(&sc->lock);
        return -1;
    }

    memmove(sc->tx_bufs[idx], m->data, m->len);
    sc->tx_mbufs[idx] = m;
    d->opts1 = DESC_OWN | DESC_FS | DESC_LS | (m->len & DESC_LEN_MASK);
    if (idx == RTL8125_TX_RING_SIZE - 1)
        d->opts1 |= DESC_EOR;
    sc->tx_tail = (idx + 1) % RTL8125_TX_RING_SIZE;

    /* Kick TX polling on queue 0 */
    rtl8125_w8(sc, RTL8125_TPPOLL, 0x01);

    release(&sc->lock);
    return 0;
}

static int
rtl8125_probe(struct pci_dev *dev)
{
    struct rtl8125_softc *sc;
    uint8_t phystatus;

    if (rtl8125_count >= MAX_RTL8125)
        return -1;

    sc = &rtl8125_devices[rtl8125_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "rtl8125");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "rtl8125");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    /* BAR2 = 64-bit MMIO (preferred); fall back to BAR0 */
    sc->regs = pci_map_bar(dev, 2);
    if (!sc->regs)
        sc->regs = pci_map_bar(dev, 0);
    if (!sc->regs) {
        cprintf("rtl8125: failed to map registers at %d:%d.%d\n",
                dev->bus, dev->slot, dev->func);
        return -1;
    }

    BOOTDBG("rtl8125: found %04x at %d:%d.%d irq=%d regs=%p\n",
            dev->device_id, dev->bus, dev->slot, dev->func,
            dev->irq_line, sc->regs);

    rtl8125_reset(sc);
    rtl8125_read_mac(sc);

    if (rtl8125_init_tx(sc) < 0 || rtl8125_init_rx(sc) < 0) {
        cprintf("rtl8125: failed to initialize rings\n");
        return -1;
    }

    /* Mask all interrupts (polling mode) */
    rtl8125_w32(sc, RTL8125_IMR0, 0);
    rtl8125_w32(sc, RTL8125_IMR1, 0);

    /* Enable TX + RX */
    rtl8125_w8(sc, RTL8125_CR, RTL8125_CR_TE | RTL8125_CR_RE);

    phystatus = rtl8125_r8(sc, RTL8125_PHYSTATUS);

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "rge0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[3] = '0' + rtl8125_count;
    sc->ifn.if_mtu   = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_BROADCAST;
    if (phystatus & RTL8125_PHY_LINKUP)
        sc->ifn.if_flags |= IFF_RUNNING;
    memmove(sc->ifn.if_hwaddr, sc->mac, 6);
    sc->ifn.if_softc = sc;
    sc->ifn.if_input = ether_input;
    sc->ifn.if_ops   = &rtl8125_ifnet_ops;

    if (if_register(&sc->ifn) < 0) {
        cprintf("rtl8125: failed to register ifnet\n");
        return -1;
    }

    cprintf("rtl8125: attached %s 2.5G MAC %x:%x:%x:%x:%x:%x\n",
            sc->ifn.if_xname,
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
    rtl8125_count++;
    return 0;
}

void
rtl8125_init(void)
{
    int i;
    BOOTDBG("rtl8125: initializing driver\n");
    for (i = 0; i < pci_device_count(); i++) {
        struct pci_dev *dev = pci_get_device(i);
        if (rtl8125_match(dev))
            rtl8125_probe(dev);
    }
}
