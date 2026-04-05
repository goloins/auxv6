/*
 * Marvell Yukon / SysKonnect SK-NET 88E8001 Gigabit Ethernet Driver
 * for auxv6  (skge-class driver)
 *
 * Covers:
 *   Marvell 88E8001 (11AB:4320) — Yukon EC PCI GbE (SK-9821/SK-9822)
 *
 * The same register set (with minor deviations) applies to the broader
 * Marvell Yukon "legacy" family (88E8035, 88E8036, 88E8038, 88E8050,
 * 88E8052, 88E8055, 88E8061, 88E8062) — additional PCI IDs can be added
 * to the match table when tested.
 *
 * Architecture (sk98lin / skge / sky2):
 *   BAR0 = 128 KB MMIO.  Two independent MAC/PHY units on-chip (we use unit 0).
 *   Control memory (CSMBUS) accessed at SRAM_BASE (0x0800) via indirect
 *     pointer registers for queue and descriptor management.
 *   TX and RX descriptor arrays are linked lists; completion is detected
 *     by polling the RX/TX "done" flag in the descriptor's status word.
 *   Descriptor format: 8 bytes control + 8 bytes opaque (software use).
 *
 * Current tranche: PCI probe, BAR map, descriptor ring allocation,
 *   MAC read, link check, polling TX/RX skeleton, ifnet registration.
 *   GMAC PHY bring-up sequence, GMAC_CTRL full init, and advanced
 *   features (VLAN, TCP offload, flow control) deferred.
 *
 * Reference: SysKonnect Yukon Programmer's Reference (publicly available)
 *            Linux drivers/net/ethernet/marvell/skge.c
 *            NetBSD sys/dev/pci/if_sk.c  (the original BSD port)
 *            OpenBSD sys/dev/pci/if_sk.c
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

/* PCI IDs */
#define SKGE_VENDOR         0x11AB   /* Marvell Technology Group */
#define SKGE_DEV_88E8001    0x4320   /* Yukon-EC, SK-9821/SK-9822 */
/* Additional Yukon legacy variants (same register set): */
#define SKGE_DEV_88E8003    0x4340
#define SKGE_DEV_88E8010    0x4350
#define SKGE_DEV_88E8022    0x4360

/*
 * BAR0 register map.
 * Most registers are 32-bit R/W; some 8- or 16-bit accesses are noted.
 * Reference: skge.h (Linux), if_skreg.h (NetBSD/OpenBSD).
 */

/* Chip identification */
#define SK_CHIP_ID          0x0100   /* Chip ID register (byte) */
#define SK_ID_YUKON         0xB6     /* Yukon chip ID */

/* Control/Status */
#define SK_CSR              0x0000   /* Control/Status Register */
#define  SK_CSR_SW_RESET    0x00010000

/* Interrupt */
#define SK_ISRC             0x000C   /* IRQ Source Register (clr on read) */
#define SK_IMSK             0x0010   /* IRQ Mask Register */
#define  SK_IMSK_NONE       0x00000000   /* Mask all */

/* GPIO (used for PHY reset on some variants) */
#define SK_GPIO             0x0200
#define  SK_GPIO_CLR_LED1   0x00000008

/* Port (MAC) registers for port 0 (each port spaced 0x80 apart) */
#define SK_PORT_STRIDE      0x0080
/* GMAC registers within each port offset */
#define SK_GMAC_CTRL        0x0F00   /* GMAC control */
#define  GMAC_CTRL_EN_TX    0x00000001
#define  GMAC_CTRL_EN_RX    0x00000002
#define  GMAC_CTRL_RST      0x00000004

/* PHY address/data for GMII */
#define SK_GMII_CTRL        0x0F08   /* GMII control register */
#define SK_GMII_DATA        0x0F0C
#define  SK_GMII_REQ        0x00000001
#define  SK_GMII_WRITE      0x00000002

/* MAC address registers (port 0) */
#define SK_MAC_ADDR0_LO     0x0F18   /* MAC address low 32 bits */
#define SK_MAC_ADDR0_HI     0x0F1C   /* MAC address high 16 bits */

/* Status word / link status */
#define SK_PHY_STATUS       0x0F80   /* PHY link/speed status */
#define  SK_PHY_LINK_UP     0x0001   /* Link established */
#define  SK_PHY_SPEED_1000  0x0004   /* 1000 Mbit */
#define  SK_PHY_SPEED_100   0x0002   /* 100 Mbit */

/*
 * Descriptor queue offsets (SRAM / control memory).
 * Each queue is described by a 4-register block (PUT, GET, ADDR, LEN).
 * For simplicity we use the MMIO "legacy" address registers directly.
 */
#define SK_Q_TX0_PUT        0x0680   /* TX queue 0 put pointer */
#define SK_Q_TX0_GET        0x0684   /* TX queue 0 get pointer (RO) */
#define SK_Q_TX0_ADDR_LO    0x0688   /* TX queue 0 descriptor base (lo) */
#define SK_Q_TX0_ADDR_HI    0x068C   /* TX queue 0 descriptor base (hi) */
#define SK_Q_TX0_COUNT      0x0690   /* TX ring descriptor count */
#define SK_Q_TX0_CTRL       0x0694   /* TX queue control */
#define  SK_Q_TX_EN         0x00000001

#define SK_Q_RX0_PUT        0x0700   /* RX queue 0 put pointer */
#define SK_Q_RX0_GET        0x0704   /* RX queue 0 get pointer (RO) */
#define SK_Q_RX0_ADDR_LO    0x0708   /* RX queue 0 descriptor base (lo) */
#define SK_Q_RX0_ADDR_HI    0x070C   /* RX queue 0 descriptor base (hi) */
#define SK_Q_RX0_COUNT      0x0710   /* RX ring descriptor count */
#define SK_Q_RX0_CTRL       0x0714   /* RX queue control */
#define  SK_Q_RX_EN         0x00000001

/*
 * Yukon descriptor (LE format used by skge, 8+4 byte layout):
 *   Word 0: physical buffer address (low 32 bits)
 *   Word 1: op_ctrl — opcode in upper byte, control/flags in lower 24 bits
 *   Word 2: status  — written by NIC for completions
 *   Word 3: packet length / frame size
 */
struct skge_desc {
    uint32_t addr_lo;
    uint32_t addr_hi;
    uint32_t op_ctrl;   /* [31:24]=opcode [23:16]=flags [15:0]=len */
    uint32_t status;    /* written by NIC */
} __attribute__((packed));

/* Opcodes for op_ctrl */
#define SKGE_OP_TX_DATA     0x21   /* Transmit data segment */
#define SKGE_OP_RX_STD     0x10   /* Standard receive buffer */

/* Control flags */
#define SKGE_CTRL_EOF       0x01   /* End of frame */
#define SKGE_CTRL_SOF       0x02   /* Start of frame */

/* Status word bits (written by NIC) */
#define SKGE_STAT_OWN       0x80000000  /* Owned by NIC */
#define SKGE_STAT_OK        0x00010000  /* Receive OK */

#define SKGE_TX_RING_SIZE   64
#define SKGE_RX_RING_SIZE   64
#define SKGE_RX_BUF_SIZE    2048

#define MAX_SKGE 4

struct skge_softc {
    struct pci_dev   *pci;
    struct spinlock   lock;
    struct ifnet      ifn;

    volatile uint8_t *regs;   /* BAR0 */
    uint8_t           mac[6];

    /* TX ring */
    struct skge_desc  *tx_ring;
    char              *tx_bufs[SKGE_TX_RING_SIZE];
    struct mbuf       *tx_mbufs[SKGE_TX_RING_SIZE];
    int                tx_prod;
    int                tx_cons;

    /* RX ring */
    struct skge_desc  *rx_ring;
    char              *rx_bufs[SKGE_RX_RING_SIZE];
    int                rx_prod;
    int                rx_cons;
};

static struct skge_softc skge_devices[MAX_SKGE];
static int skge_count;
extern int ncpu;

static int  skge_output(struct ifnet *ifp, struct mbuf *m);
static void skge_poll(struct ifnet *ifp);

static struct ifnet_ops skge_ifnet_ops = {
    .if_output = skge_output,
    .if_poll   = skge_poll,
};

static int
skge_match(struct pci_dev *dev)
{
    if (!dev || dev->vendor_id != SKGE_VENDOR)
        return 0;
    switch (dev->device_id) {
    case SKGE_DEV_88E8001:
    case SKGE_DEV_88E8003:
    case SKGE_DEV_88E8010:
    case SKGE_DEV_88E8022:
        return 1;
    default:
        return 0;
    }
}

static uint8_t
skge_read8(struct skge_softc *sc, uint32_t reg)
{
    return *(volatile uint8_t *)(sc->regs + reg);
}

static uint32_t
skge_read32(struct skge_softc *sc, uint32_t reg)
{
    return *(volatile uint32_t *)(sc->regs + reg);
}

static void
skge_write32(struct skge_softc *sc, uint32_t reg, uint32_t val)
{
    *(volatile uint32_t *)(sc->regs + reg) = val;
}

static void __attribute__((unused))
skge_write8(struct skge_softc *sc, uint32_t reg, uint8_t val)
{
    *(volatile uint8_t *)(sc->regs + reg) = val;
}

static void
skge_read_mac(struct skge_softc *sc)
{
    uint32_t lo = skge_read32(sc, SK_MAC_ADDR0_LO);
    uint32_t hi = skge_read32(sc, SK_MAC_ADDR0_HI);

    /*
     * Yukon stores MAC in big-endian byte order; the lo register
     * holds bytes 0-3 (most significant first).
     */
    sc->mac[0] = (lo >> 24) & 0xFF;
    sc->mac[1] = (lo >> 16) & 0xFF;
    sc->mac[2] = (lo >>  8) & 0xFF;
    sc->mac[3] =  lo         & 0xFF;
    sc->mac[4] = (hi >>  8) & 0xFF;
    sc->mac[5] =  hi         & 0xFF;
}

static void
skge_reset(struct skge_softc *sc)
{
    int i;

    /* Mask all IRQs before reset */
    skge_write32(sc, SK_IMSK, SK_IMSK_NONE);

    /* Software reset */
    skge_write32(sc, SK_CSR,
        skge_read32(sc, SK_CSR) | SK_CSR_SW_RESET);
    microdelay(10000);
    skge_write32(sc, SK_CSR,
        skge_read32(sc, SK_CSR) & ~SK_CSR_SW_RESET);

    /* Wait for chip to settle */
    for (i = 0; i < 100; i++) {
        microdelay(1000);
        if (skge_read8(sc, SK_CHIP_ID) != 0xFF)
            break;
    }
}

static int
skge_alloc_rings(struct skge_softc *sc)
{
    int i;

    sc->tx_ring = (struct skge_desc *)kalloc();
    if (!sc->tx_ring) return -1;
    memset(sc->tx_ring, 0, sizeof(struct skge_desc) * SKGE_TX_RING_SIZE);

    for (i = 0; i < SKGE_TX_RING_SIZE; i++) {
        sc->tx_bufs[i] = kalloc();
        if (!sc->tx_bufs[i]) return -1;
    }
    sc->tx_prod = 0;
    sc->tx_cons = 0;

    sc->rx_ring = (struct skge_desc *)kalloc();
    if (!sc->rx_ring) return -1;
    memset(sc->rx_ring, 0, sizeof(struct skge_desc) * SKGE_RX_RING_SIZE);

    for (i = 0; i < SKGE_RX_RING_SIZE; i++) {
        sc->rx_bufs[i] = kalloc();
        if (!sc->rx_bufs[i]) return -1;

        sc->rx_ring[i].addr_lo = V2P(sc->rx_bufs[i]);
        sc->rx_ring[i].addr_hi = 0;
        sc->rx_ring[i].op_ctrl = (SKGE_OP_RX_STD << 24) |
                                  SKGE_CTRL_SOF | SKGE_CTRL_EOF |
                                  (SKGE_RX_BUF_SIZE & 0xFFFF);
        sc->rx_ring[i].status  = SKGE_STAT_OWN;
    }
    sc->rx_prod = SKGE_RX_RING_SIZE - 1;
    sc->rx_cons = 0;

    return 0;
}

static void
skge_init_rings(struct skge_softc *sc)
{
    /* TX queue 0 */
    skge_write32(sc, SK_Q_TX0_ADDR_LO, V2P(sc->tx_ring));
    skge_write32(sc, SK_Q_TX0_ADDR_HI, 0);
    skge_write32(sc, SK_Q_TX0_COUNT,   SKGE_TX_RING_SIZE);
    skge_write32(sc, SK_Q_TX0_PUT,     0);
    skge_write32(sc, SK_Q_TX0_CTRL,    SK_Q_TX_EN);

    /* RX queue 0 */
    skge_write32(sc, SK_Q_RX0_ADDR_LO, V2P(sc->rx_ring));
    skge_write32(sc, SK_Q_RX0_ADDR_HI, 0);
    skge_write32(sc, SK_Q_RX0_COUNT,   SKGE_RX_RING_SIZE);
    skge_write32(sc, SK_Q_RX0_PUT,     sc->rx_prod);
    skge_write32(sc, SK_Q_RX0_CTRL,    SK_Q_RX_EN);

    /* Enable GMAC TX+RX on port 0 */
    skge_write32(sc, SK_GMAC_CTRL,
        GMAC_CTRL_EN_TX | GMAC_CTRL_EN_RX);
}

static void
skge_tx_complete(struct skge_softc *sc)
{
    struct skge_desc *d;

    while (sc->tx_cons != sc->tx_prod) {
        d = &sc->tx_ring[sc->tx_cons];
        /* Descriptor no longer owned by NIC = TX complete */
        if (d->status & SKGE_STAT_OWN)
            break;
        if (sc->tx_mbufs[sc->tx_cons]) {
            mbuf_free(sc->tx_mbufs[sc->tx_cons]);
            sc->tx_mbufs[sc->tx_cons] = 0;
        }
        sc->tx_cons = (sc->tx_cons + 1) % SKGE_TX_RING_SIZE;
    }
}

static void
skge_rx_complete(struct skge_softc *sc)
{
    struct skge_desc *d;
    struct mbuf *m;
    int processed = 0;

    while (processed < 32) {
        d = &sc->rx_ring[sc->rx_cons];
        /* Descriptor still owned by NIC = no packet yet */
        if (d->status & SKGE_STAT_OWN)
            break;
        if (d->status & SKGE_STAT_OK) {
            uint16_t len = (uint16_t)(d->op_ctrl & 0xFFFF);
            if (len > 0 && len <= SKGE_RX_BUF_SIZE) {
                m = mbuf_alloc();
                if (m) {
                    memmove(m->data, sc->rx_bufs[sc->rx_cons], len);
                    m->len   = len;
                    m->rcvif = &sc->ifn;
                    release(&sc->lock);
                    if_input(&sc->ifn, m);
                    acquire(&sc->lock);
                }
            }
        }

        /* Recycle descriptor */
        d->op_ctrl = (SKGE_OP_RX_STD << 24) |
                     SKGE_CTRL_SOF | SKGE_CTRL_EOF |
                     (SKGE_RX_BUF_SIZE & 0xFFFF);
        d->status  = SKGE_STAT_OWN;

        sc->rx_cons = (sc->rx_cons + 1) % SKGE_RX_RING_SIZE;
        sc->rx_prod = (sc->rx_prod + 1) % SKGE_RX_RING_SIZE;
        skge_write32(sc, SK_Q_RX0_PUT, sc->rx_prod);
        processed++;
    }
}

static void
skge_poll(struct ifnet *ifp)
{
    struct skge_softc *sc = (struct skge_softc *)ifp->if_softc;
    if (!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;
    acquire(&sc->lock);
    skge_tx_complete(sc);
    skge_rx_complete(sc);
    release(&sc->lock);
}

static int
skge_output(struct ifnet *ifp, struct mbuf *m)
{
    struct skge_softc *sc = (struct skge_softc *)ifp->if_softc;
    struct skge_desc *d;
    int idx, next;

    if (!sc || !m || m->len == 0)
        return -1;

    acquire(&sc->lock);

    if ((ifp->if_flags & IFF_RUNNING) == 0) {
        release(&sc->lock);
        return -1;
    }

    skge_tx_complete(sc);

    idx  = sc->tx_prod;
    next = (idx + 1) % SKGE_TX_RING_SIZE;
    if (next == sc->tx_cons) {
        release(&sc->lock);
        return -1;
    }

    memmove(sc->tx_bufs[idx], m->data, m->len);
    sc->tx_mbufs[idx] = m;

    d = &sc->tx_ring[idx];
    d->addr_lo = V2P(sc->tx_bufs[idx]);
    d->addr_hi = 0;
    d->op_ctrl = ((uint32_t)SKGE_OP_TX_DATA << 24) |
                 ((uint32_t)(SKGE_CTRL_SOF | SKGE_CTRL_EOF) << 16) |
                 (m->len & 0xFFFF);
    d->status  = SKGE_STAT_OWN;   /* Hand to NIC */

    sc->tx_prod = next;
    skge_write32(sc, SK_Q_TX0_PUT, sc->tx_prod);

    release(&sc->lock);
    return 0;
}

static int
skge_probe(struct pci_dev *dev)
{
    struct skge_softc *sc;
    uint32_t phystatus;

    if (skge_count >= MAX_SKGE)
        return -1;

    sc = &skge_devices[skge_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "skge");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "skge");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    /* BAR0 = 128 KB MMIO */
    sc->regs = pci_map_bar(dev, 0);
    if (!sc->regs) {
        cprintf("skge: failed to map BAR0 at %d:%d.%d\n",
                dev->bus, dev->slot, dev->func);
        return -1;
    }

    BOOTDBG("skge: found %04x at %d:%d.%d irq=%d regs=%p\n",
            dev->device_id, dev->bus, dev->slot, dev->func,
            dev->irq_line, sc->regs);

    skge_reset(sc);
    skge_read_mac(sc);

    if (skge_alloc_rings(sc) < 0) {
        cprintf("skge: ring allocation failed\n");
        return -1;
    }

    /* Mask all interrupts (polling mode) */
    skge_write32(sc, SK_IMSK, SK_IMSK_NONE);

    skge_init_rings(sc);

    phystatus = skge_read32(sc, SK_PHY_STATUS);

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "sk0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[2] = '0' + skge_count;
    sc->ifn.if_mtu   = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_BROADCAST;
    if (phystatus & SK_PHY_LINK_UP)
        sc->ifn.if_flags |= IFF_RUNNING;
    memmove(sc->ifn.if_hwaddr, sc->mac, 6);
    sc->ifn.if_softc = sc;
    sc->ifn.if_input = ether_input;
    sc->ifn.if_ops   = &skge_ifnet_ops;

    if (if_register(&sc->ifn) < 0) {
        cprintf("skge: failed to register ifnet\n");
        return -1;
    }

    cprintf("skge: attached %s 88E%04x MAC %x:%x:%x:%x:%x:%x\n",
            sc->ifn.if_xname, dev->device_id,
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
    skge_count++;
    return 0;
}

void
skge_init(void)
{
    int i;
    BOOTDBG("skge: initializing driver\n");
    for (i = 0; i < pci_device_count(); i++) {
        struct pci_dev *dev = pci_get_device(i);
        if (skge_match(dev))
            skge_probe(dev);
    }
}
