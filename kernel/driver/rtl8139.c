/*
 * Realtek RTL8139/RTL8139C/RTL8139C+/RTL8110S Ethernet Driver for auxv6
 *
 * Supports:
 *   RTL8139/A/B/C   (10EC:8139) — 10/100 Mbit, 4-slot TX + FIFO RX ring
 *   RTL8139C+       (10EC:8136) — 10/100 Mbit, descriptor-ring (C+ mode)
 *   RTL8110S        (10EC:8110) — older PCI GbE, C+ descriptor ring
 *
 * Architecture:
 *   BAR0 = I/O port space, BAR1 = 256-byte MMIO; driver prefers MMIO.
 *   RTL8139 "legacy" mode: 4 static TX DMA buffers (TSD0-3 + TSAD0-3),
 *     and a wrapped RX ring in host memory (RBSTART + CAPR).
 *   RTL8139C+ mode: 16-byte TX/RX descriptors, same layout as RTL8111.
 *
 * Current tranche: PCI probe, BAR map, ifnet skeleton, polling stub.
 *   C+ TX/RX poll path is functional; legacy RX drain path is a TODO.
 *
 * Reference: Realtek RTL8139C(L) Programming Guide r1.1
 *            Realtek RTL8139C+ datasheet
 * See also:  Linux drivers/net/ethernet/realtek/8139too.c
 *            NetBSD sys/dev/pci/if_re.c
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

/* PCI IDs */
#define RTL8139_VENDOR          0x10EC
#define RTL8139_DEV_8139        0x8139   /* RTL8139/A/B/C */
#define RTL8139_DEV_8139CP      0x8136   /* RTL8139C+ */
#define RTL8139_DEV_8110S       0x8110   /* RTL8110S PCI GbE */

/* MMIO register offsets */
#define RTL_IDR0        0x00    /* MAC address bytes 0-5 */
#define RTL_MAR0        0x08    /* Multicast filter (8 bytes) */
#define RTL_TSD0        0x10    /* TX status descriptor 0 */
#define RTL_TSD1        0x14    /* TX status descriptor 1 */
#define RTL_TSD2        0x18    /* TX status descriptor 2 */
#define RTL_TSD3        0x1C    /* TX status descriptor 3 */
#define RTL_TSAD0       0x20    /* TX start address 0 */
#define RTL_TSAD1       0x24    /* TX start address 1 */
#define RTL_TSAD2       0x28    /* TX start address 2 */
#define RTL_TSAD3       0x2C    /* TX start address 3 */
#define RTL_RBSTART     0x30    /* RX buffer start address */
#define RTL_CR          0x37    /* Command register */
#define RTL_CAPR        0x38    /* Current address of packet read */
#define RTL_CBR         0x3A    /* Current buffer address */
#define RTL_IMR         0x3C    /* Interrupt mask register */
#define RTL_ISR         0x3E    /* Interrupt status register */
#define RTL_TCR         0x40    /* TX configuration */
#define RTL_RCR         0x44    /* RX configuration */
#define RTL_9346CR      0x50    /* 93C46/93C56 command register */
#define RTL_MSR         0x58    /* Media status register */
/* RTL8139C+ extra registers */
#define RTL_TNPDS_LO    0x20    /* TX normal priority desc start (low) */
#define RTL_TNPDS_HI    0x24    /* TX normal priority desc start (high) */
#define RTL_CPCR        0xE0    /* C+ command register */
#define RTL_RDSAR_LO    0xE4    /* RX descriptor start (low) */
#define RTL_RDSAR_HI    0xE8    /* RX descriptor start (high) */

/* Command register bits */
#define CR_RST          0x10
#define CR_RE           0x08
#define CR_TE           0x04
#define CR_BUFE         0x01

/* TX status bits */
#define TSD_OWN         0x00002000  /* NIC owns slot (busy) */
#define TSD_TUN         0x00004000  /* TX FIFO underrun */
#define TSD_TOK         0x00008000  /* TX OK */

/* RX config bits */
#define RCR_APM         0x00000002  /* Accept physical match */
#define RCR_AM          0x00000004  /* Accept multicast */
#define RCR_AB          0x00000008  /* Accept broadcast */
#define RCR_WRAP        0x00000080  /* Wrap ring */
#define RCR_MXDMA_UNLIM 0x00000700
#define RCR_RXFTH_NONE  0x0000E000

/* TX config bits */
#define TCR_MXDMA_UNLIM 0x00000700
#define TCR_IFG_STD     0x03000000

/* ISR bits */
#define ISR_ROK         0x0001
#define ISR_RER         0x0002
#define ISR_TOK         0x0004
#define ISR_TER         0x0008
#define ISR_RXOVW       0x0010

/* 93C46 command register — config unlock/lock */
#define CR9346_WE       0xC0    /* Write enable */
#define CR9346_NORMAL   0x00    /* Normal mode */

/* C+ command register bits */
#define CPCR_RX_CSUM    0x0020
#define CPCR_MULRW      0x0008

/* MSR bits */
#define MSR_LINKB       0x04    /* Link bad flag (0 = link OK) */

/* C+ descriptor */
struct rtl8139_desc {
    uint32_t opts1;
    uint32_t opts2;
    uint32_t addr_lo;
    uint32_t addr_hi;
} __attribute__((packed));

#define DESC_OWN        0x80000000
#define DESC_EOR        0x40000000
#define DESC_FS         0x20000000
#define DESC_LS         0x10000000
#define DESC_LEN_MASK   0x00001FFF

/* Ring / buffer sizes */
#define RTL8139_TX_SLOTS        4
#define RTL8139CP_TX_RING_SIZE  64
#define RTL8139CP_RX_RING_SIZE  64
#define RTL8139CP_RX_BUF_SIZE   2048

#define MAX_RTL8139 4

struct rtl8139_softc {
    struct pci_dev   *pci;
    struct spinlock   lock;
    struct ifnet      ifn;

    volatile uint8_t *regs;
    uint8_t           mac[6];
    int               is_cplus;     /* non-zero → C+ descriptor mode */

    /* --- legacy (non-C+) TX --- */
    char             *tx_bufs[RTL8139_TX_SLOTS];
    uint32_t          tx_bufs_phys[RTL8139_TX_SLOTS];
    int               tx_slot;

    /* --- C+ descriptor rings --- */
    struct rtl8139_desc *tx_ring;
    char                *tx_cpbufs[RTL8139CP_TX_RING_SIZE];
    struct mbuf         *tx_mbufs[RTL8139CP_TX_RING_SIZE];
    int                  tx_head;
    int                  tx_tail;

    struct rtl8139_desc *rx_ring;
    char                *rx_bufs[RTL8139CP_RX_RING_SIZE];
    int                  rx_cur;
};

static struct rtl8139_softc rtl8139_devices[MAX_RTL8139];
static int rtl8139_count;
extern int ncpu;

static int  rtl8139_output(struct ifnet *ifp, struct mbuf *m);
static void rtl8139_poll(struct ifnet *ifp);

static struct ifnet_ops rtl8139_ifnet_ops = {
    .if_output = rtl8139_output,
    .if_poll   = rtl8139_poll,
};

static int
rtl8139_match(struct pci_dev *dev)
{
    if (!dev || dev->vendor_id != RTL8139_VENDOR)
        return 0;
    switch (dev->device_id) {
    case RTL8139_DEV_8139:
    case RTL8139_DEV_8139CP:
    case RTL8139_DEV_8110S:
        return 1;
    default:
        return 0;
    }
}

static uint8_t
rtl_r8(struct rtl8139_softc *sc, int r)
{
    return sc->regs[r];
}

static void
rtl_w8(struct rtl8139_softc *sc, int r, uint8_t v)
{
    sc->regs[r] = v;
}

static void __attribute__((unused))
rtl_w16(struct rtl8139_softc *sc, int r, uint16_t v)
{
    *(volatile uint16_t *)(sc->regs + r) = v;
}

static void
rtl_w32(struct rtl8139_softc *sc, int r, uint32_t v)
{
    *(volatile uint32_t *)(sc->regs + r) = v;
}

static uint32_t __attribute__((unused))
rtl_r32(struct rtl8139_softc *sc, int r)
{
    return *(volatile uint32_t *)(sc->regs + r);
}

static void
rtl8139_read_mac(struct rtl8139_softc *sc)
{
    int i;
    for (i = 0; i < 6; i++)
        sc->mac[i] = rtl_r8(sc, RTL_IDR0 + i);
}

static void
rtl8139_reset(struct rtl8139_softc *sc)
{
    int timeout;

    rtl_w8(sc, RTL_9346CR, CR9346_WE);
    rtl_w32(sc, RTL_IMR, 0);
    rtl_w32(sc, (int)RTL_ISR, 0xFFFF);  /* clear pending */

    rtl_w8(sc, RTL_CR, CR_RST);
    for (timeout = 0; timeout < 1000; timeout++) {
        if (!(rtl_r8(sc, RTL_CR) & CR_RST))
            break;
        microdelay(1000);
    }
    if (rtl_r8(sc, RTL_CR) & CR_RST)
        cprintf("rtl8139: reset timeout\n");

    rtl_w8(sc, RTL_9346CR, CR9346_NORMAL);
}

static int
rtl8139_init_tx_legacy(struct rtl8139_softc *sc)
{
    int i;
    for (i = 0; i < RTL8139_TX_SLOTS; i++) {
        sc->tx_bufs[i] = kalloc();
        if (!sc->tx_bufs[i])
            return -1;
        sc->tx_bufs_phys[i] = V2P(sc->tx_bufs[i]);
        rtl_w32(sc, RTL_TSAD0 + i * 4, sc->tx_bufs_phys[i]);
    }
    sc->tx_slot = 0;
    rtl_w32(sc, RTL_TCR, TCR_MXDMA_UNLIM | TCR_IFG_STD);
    return 0;
}

static int
rtl8139_init_rx_legacy(struct rtl8139_softc *sc)
{
    /*
     * TODO: allocate a full 64 KB ring via multi-page or DMA allocator.
     * For now we use a single kalloc() page as a minimal stand-in; real
     * packet reception is not yet wired on the legacy (non-C+) path.
     */
    char *ring = kalloc();
    if (!ring)
        return -1;
    rtl_w32(sc, RTL_RBSTART, V2P(ring));
    rtl_w32(sc, RTL_RCR,
        RCR_AB | RCR_AM | RCR_APM | RCR_MXDMA_UNLIM |
        RCR_RXFTH_NONE | RCR_WRAP);
    return 0;
}

static int
rtl8139_init_cplus(struct rtl8139_softc *sc)
{
    int i;

    sc->tx_ring = (struct rtl8139_desc *)kalloc();
    if (!sc->tx_ring)
        return -1;
    memset(sc->tx_ring, 0,
           sizeof(struct rtl8139_desc) * RTL8139CP_TX_RING_SIZE);

    for (i = 0; i < RTL8139CP_TX_RING_SIZE; i++) {
        sc->tx_cpbufs[i] = kalloc();
        if (!sc->tx_cpbufs[i])
            return -1;
        sc->tx_ring[i].addr_lo = V2P(sc->tx_cpbufs[i]);
        sc->tx_ring[i].addr_hi = 0;
    }
    sc->tx_ring[RTL8139CP_TX_RING_SIZE - 1].opts1 = DESC_EOR;
    sc->tx_head = 0;
    sc->tx_tail = 0;

    sc->rx_ring = (struct rtl8139_desc *)kalloc();
    if (!sc->rx_ring)
        return -1;
    memset(sc->rx_ring, 0,
           sizeof(struct rtl8139_desc) * RTL8139CP_RX_RING_SIZE);

    for (i = 0; i < RTL8139CP_RX_RING_SIZE; i++) {
        sc->rx_bufs[i] = kalloc();
        if (!sc->rx_bufs[i])
            return -1;
        sc->rx_ring[i].addr_lo = V2P(sc->rx_bufs[i]);
        sc->rx_ring[i].addr_hi = 0;
        sc->rx_ring[i].opts1   = DESC_OWN | RTL8139CP_RX_BUF_SIZE;
    }
    sc->rx_ring[RTL8139CP_RX_RING_SIZE - 1].opts1 |= DESC_EOR;
    sc->rx_cur = 0;

    rtl_w32(sc, RTL_TNPDS_LO, V2P(sc->tx_ring));
    rtl_w32(sc, RTL_TNPDS_HI, 0);
    rtl_w32(sc, RTL_RDSAR_LO, V2P(sc->rx_ring));
    rtl_w32(sc, RTL_RDSAR_HI, 0);

    /* Enable C+ mode */
    *(volatile uint16_t *)(sc->regs + RTL_CPCR) = CPCR_RX_CSUM | CPCR_MULRW;
    rtl_w32(sc, RTL_TCR, TCR_MXDMA_UNLIM | TCR_IFG_STD);
    rtl_w32(sc, RTL_RCR,
        RCR_AB | RCR_AM | RCR_APM | RCR_MXDMA_UNLIM | RCR_RXFTH_NONE);
    return 0;
}

static void
rtl8139_tx_complete_cp(struct rtl8139_softc *sc)
{
    struct rtl8139_desc *d;
    while (sc->tx_head != sc->tx_tail) {
        d = &sc->tx_ring[sc->tx_head];
        if (d->opts1 & DESC_OWN)
            break;
        if (sc->tx_mbufs[sc->tx_head]) {
            mbuf_free(sc->tx_mbufs[sc->tx_head]);
            sc->tx_mbufs[sc->tx_head] = 0;
        }
        sc->tx_head = (sc->tx_head + 1) % RTL8139CP_TX_RING_SIZE;
    }
}

static void
rtl8139_rx_complete_cp(struct rtl8139_softc *sc)
{
    struct rtl8139_desc *d;
    struct mbuf *m;
    int processed = 0;

    while (processed < 32) {
        d = &sc->rx_ring[sc->rx_cur];
        if (d->opts1 & DESC_OWN)
            break;

        if ((d->opts1 & (DESC_FS | DESC_LS)) == (DESC_FS | DESC_LS)) {
            uint16_t len = (uint16_t)(d->opts1 & DESC_LEN_MASK);
            if (len > 0 && len <= RTL8139CP_RX_BUF_SIZE) {
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

        d->opts1 = DESC_OWN | RTL8139CP_RX_BUF_SIZE;
        if (sc->rx_cur == RTL8139CP_RX_RING_SIZE - 1)
            d->opts1 |= DESC_EOR;
        sc->rx_cur = (sc->rx_cur + 1) % RTL8139CP_RX_RING_SIZE;
        processed++;
    }
}

static void
rtl8139_poll(struct ifnet *ifp)
{
    struct rtl8139_softc *sc = (struct rtl8139_softc *)ifp->if_softc;

    if (!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;

    acquire(&sc->lock);
    if (sc->is_cplus) {
        rtl8139_tx_complete_cp(sc);
        rtl8139_rx_complete_cp(sc);
    }
    /* TODO: legacy mode RX drain: walk CAPR ring, process per-packet header */
    release(&sc->lock);
}

static int
rtl8139_output(struct ifnet *ifp, struct mbuf *m)
{
    struct rtl8139_softc *sc = (struct rtl8139_softc *)ifp->if_softc;
    int slot;
    uint32_t tsd;

    if (!sc || !m || m->len == 0)
        return -1;

    acquire(&sc->lock);

    if (sc->is_cplus) {
        struct rtl8139_desc *d;
        int idx = sc->tx_tail;

        rtl8139_tx_complete_cp(sc);

        d = &sc->tx_ring[idx];
        if (d->opts1 & DESC_OWN) {
            release(&sc->lock);
            return -1;
        }

        memmove(sc->tx_cpbufs[idx], m->data, m->len);
        sc->tx_mbufs[idx] = m;
        d->opts1 = DESC_OWN | DESC_FS | DESC_LS | (m->len & DESC_LEN_MASK);
        if (idx == RTL8139CP_TX_RING_SIZE - 1)
            d->opts1 |= DESC_EOR;
        sc->tx_tail = (idx + 1) % RTL8139CP_TX_RING_SIZE;

        /* Poll demand: re-assert TE to notify chip */
        rtl_w8(sc, RTL_CR, CR_TE | CR_RE);
    } else {
        /* Legacy 4-slot TX: round-robin across TSD0-3 */
        slot = sc->tx_slot;
        tsd  = *(volatile uint32_t *)(sc->regs + RTL_TSD0 + slot * 4);
        if (!(tsd & TSD_OWN)) {
            uint32_t len = (m->len < 4096) ? (uint32_t)m->len : 4095;
            memmove(sc->tx_bufs[slot], m->data, len);
            /* Writing TSD with length and clearing OWN hands to NIC */
            *(volatile uint32_t *)(sc->regs + RTL_TSD0 + slot * 4) =
                len & 0x1FFF;
            sc->tx_slot = (slot + 1) % RTL8139_TX_SLOTS;
            mbuf_free(m);
        } else {
            /* All slots busy; drop */
            release(&sc->lock);
            mbuf_free(m);
            return -1;
        }
    }

    release(&sc->lock);
    return 0;
}

static int
rtl8139_probe(struct pci_dev *dev)
{
    struct rtl8139_softc *sc;
    uint8_t msr;

    if (rtl8139_count >= MAX_RTL8139)
        return -1;

    sc = &rtl8139_devices[rtl8139_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "rtl8139");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "rtl8139");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    /* Prefer BAR1 (MMIO) over BAR0 (I/O port) */
    sc->regs = pci_map_bar(dev, 1);
    if (!sc->regs)
        sc->regs = pci_map_bar(dev, 0);
    if (!sc->regs) {
        cprintf("rtl8139: failed to map registers at %d:%d.%d\n",
                dev->bus, dev->slot, dev->func);
        return -1;
    }

    BOOTDBG("rtl8139: found %04x at %d:%d.%d irq=%d regs=%p\n",
            dev->device_id, dev->bus, dev->slot, dev->func,
            dev->irq_line, sc->regs);

    rtl8139_reset(sc);
    rtl8139_read_mac(sc);

    sc->is_cplus = (dev->device_id == RTL8139_DEV_8139CP ||
                    dev->device_id == RTL8139_DEV_8110S);

    if (sc->is_cplus) {
        if (rtl8139_init_cplus(sc) < 0) {
            cprintf("rtl8139: failed to init C+ rings\n");
            return -1;
        }
    } else {
        if (rtl8139_init_tx_legacy(sc) < 0 ||
            rtl8139_init_rx_legacy(sc) < 0) {
            cprintf("rtl8139: failed to init legacy TX/RX\n");
            return -1;
        }
    }

    /* Enable TX+RX; mask all interrupts (polling mode) */
    rtl_w8(sc, RTL_CR, CR_TE | CR_RE);
    rtl_w32(sc, RTL_IMR, 0);

    /* Check link via MSR */
    msr = rtl_r8(sc, RTL_MSR);

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    sc->ifn.if_xname[0] = 'r';
    sc->ifn.if_xname[1] = 'l';
    sc->ifn.if_xname[2] = '0' + rtl8139_count;
    sc->ifn.if_xname[3] = '\0';
    sc->ifn.if_mtu      = 1500;
    sc->ifn.if_flags    = IFF_UP | IFF_BROADCAST;
    if (!(msr & MSR_LINKB))
        sc->ifn.if_flags |= IFF_RUNNING;
    memmove(sc->ifn.if_hwaddr, sc->mac, 6);
    sc->ifn.if_softc = sc;
    sc->ifn.if_input = ether_input;
    sc->ifn.if_ops   = &rtl8139_ifnet_ops;

    if (if_register(&sc->ifn) < 0) {
        cprintf("rtl8139: failed to register ifnet\n");
        return -1;
    }

    cprintf("rtl8139: attached %s %s MAC %x:%x:%x:%x:%x:%x\n",
            sc->ifn.if_xname,
            sc->is_cplus ? "(C+)" : "(legacy)",
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
    rtl8139_count++;
    return 0;
}

void
rtl8139_init(void)
{
    int i;
    BOOTDBG("rtl8139: initializing driver\n");
    for (i = 0; i < pci_device_count(); i++) {
        struct pci_dev *dev = pci_get_device(i);
        if (rtl8139_match(dev))
            rtl8139_probe(dev);
    }
}
