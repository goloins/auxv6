/*
 * VIA Technologies VT6103 Rhine III Ethernet Driver for auxv6
 *
 * Covers:
 *   VIA VT6103  (1106:3106) — Rhine III 10/100 Mbit PCI NIC
 *
 * The Rhine family shares a common architecture across variants:
 *   VT86C100A (0x3043) — Rhine I
 *   VT6102    (0x3065) — Rhine II
 *   VT6103    (0x3106) — Rhine III (this driver's primary target)
 * Additional PCI IDs from this family can be added to the match table.
 *
 * Architecture (via-rhine):
 *   BAR0 = 256-byte I/O port space.
 *   BAR1 = 256-byte MMIO (preferred; same register map).
 *   TX ring: 16 descriptors in a circular list, each 16 bytes.
 *   RX ring: 16 descriptors in a circular list, each 16 bytes.
 *   Descriptor status word uses OWN bit (bit 31): set = owned by NIC.
 *   Completion is polled by checking OWN == 0 on the consumer index.
 *
 * Current tranche: PCI probe, BAR map, descriptor ring allocation,
 *   MAC read, link check, polling TX/RX skeleton, ifnet registration.
 *   Power management, VLAN tag stripping, and MII media negotiation
 *   are deferred to a later tranche.
 *
 * Reference: VIA VT6103 datasheet
 *            Linux drivers/net/ethernet/via/via-rhine.c
 *            NetBSD sys/dev/ic/viaRhine.c
 *            OpenBSD sys/dev/pci/if_vr.c (same chip family)
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

/* PCI IDs */
#define VR_VENDOR           0x1106   /* VIA Technologies */
#define VR_DEV_VT86C100A    0x3043   /* Rhine I */
#define VR_DEV_VT6102       0x3065   /* Rhine II */
#define VR_DEV_VT6103       0x3106   /* Rhine III */

/*
 * MMIO register map (BAR1, byte offsets).
 * Identical layout to I/O port map (BAR0).
 * Reference: if_vrreg.h (NetBSD/OpenBSD), via-rhine.c (Linux)
 */

/* MAC address (6 bytes, r/w) */
#define VR_PAR0             0x00    /* Physical address 0 */
#define VR_PAR1             0x01
#define VR_PAR2             0x02
#define VR_PAR3             0x03
#define VR_PAR4             0x04
#define VR_PAR5             0x05

/* Receive configuration */
#define VR_RXCFG            0x06    /* RX configuration */
#define  VR_RXCFG_RX_BROAD  0x04   /* Accept broadcast */
#define  VR_RXCFG_RX_MULTI  0x02   /* Accept multicast */
#define  VR_RXCFG_RX_UNI    0x01   /* Accept unicast (physical match) */
#define  VR_RXCFG_RX_ALL    0x08   /* Promiscuous mode */

/* Interrupt status / mask (byte-wide) */
#define VR_ISR              0x0C    /* Interrupt status register (word) */
#define VR_IMR              0x0E    /* Interrupt mask register (word) */
#define  VR_ISR_RX_OK       0x0001  /* RX packet received OK */
#define  VR_ISR_RX_ERR      0x0002  /* RX error */
#define  VR_ISR_TX_OK       0x0004  /* TX packet sent OK */
#define  VR_ISR_TX_ERR      0x0008  /* TX error */
#define  VR_ISR_LINK_CHG    0x0200  /* Link status change */

/* Command register (byte) */
#define VR_CR               0x09    /* CR0 command register */
#define  VR_CR_TX_EN        0x10    /* Enable TX */
#define  VR_CR_RX_EN        0x40    /* Enable RX */
#define  VR_CR_TX_DEMAND    0x20    /* TX demand (poll) */
#define  VR_CR_RX_DEMAND    0x80    /* RX demand */

/* TX configuration (byte) */
#define VR_TXCFG            0x07    /* TX configuration */
#define  VR_TXCFG_IFG_STD  0xE0    /* Interframe gap standard */

/* RX descriptor base address */
#define VR_RXDESC_LO        0x18    /* RX descriptor ring base (lo) */
#define VR_RXDESC_HI        0x1C    /* RX descriptor ring base (hi) */

/* TX descriptor base address */
#define VR_TXDESC_LO        0x14    /* TX descriptor ring base (lo) */
#define VR_TXDESC_HI        0x1A    /* TX descriptor ring base (hi) */

/* MII / PHY access */
#define VR_MIICMD           0x6C    /* MII command */
#define  VR_MIICMD_READ     0x40
#define  VR_MIICMD_WRITE    0x80
#define VR_MIIADDR          0x6D    /* PHY register address */
#define VR_MIIDATA          0x70    /* PHY data */

/* Link status register */
#define VR_MIISTATUS        0x6E    /* MII/link status byte */
#define  VR_MIISTATUS_LINK  0x04    /* Link up (Bit 2) */

/* Software reset via CR1 (byte at 0x0A) */
#define VR_CR1              0x0A
#define  VR_CR1_RESET       0x80    /* Software reset */

/*
 * Rhine descriptor (16 bytes).
 * This matches the "legacy" Rhine/Rhine-II/Rhine-III descriptor format.
 * Fields:
 *   status   — hardware-written status; bit 31 = OWN (1=NIC, 0=host)
 *   control  — [31:16]=length control [15:0]=frame length
 *   addr     — physical buffer address
 *   next     — physical address of next descriptor (linked list)
 */
struct vr_desc {
    volatile uint32_t status;   /* Status; written by NIC on completion */
    uint32_t          control;  /* Buffer length and control */
    uint32_t          addr;     /* Physical buffer address */
    uint32_t          next;     /* Physical address of next descriptor */
} __attribute__((packed));

/* Status word bits */
#define VR_STAT_OWN         0x80000000  /* Owned by NIC */
#define VR_STAT_RX_OK       0x00008000  /* RX frame received OK */
#define VR_STAT_TX_OK       0x00008000  /* TX frame transmitted OK  */
#define VR_STAT_FRAME_LEN   0x07FF0000  /* Frame length mask (bits 26:16) */
#define VR_STAT_FRAME_SHIFT 16

/* Control bits for TX */
#define VR_CTRL_TX_CHAIN    0x00000600  /* Chain: STP + EDP */
#define VR_CTRL_TX_STP      0x00000200  /* Start of packet */
#define VR_CTRL_TX_EDP      0x00000400  /* End of packet */
#define VR_CTRL_TX_IC       0x00800000  /* Interrupt on completion */
#define VR_MAXLEN_MASK      0x000007FF  /* Buffer length masks (11 bits) */

/* Control bits for RX */
#define VR_CTRL_RX_LEN_MASK 0x000007FF

#define VR_TX_RING_SIZE     16
#define VR_RX_RING_SIZE     16
#define VR_RX_BUF_SIZE      1536    /* Max Ethernet frame + CRC */

#define MAX_VR 4

struct vr_softc {
    struct pci_dev   *pci;
    struct spinlock   lock;
    struct ifnet      ifn;

    volatile uint8_t *regs;   /* BAR1 MMIO */
    uint8_t           mac[6];

    /* TX ring (physically contiguous for "next" pointer chain) */
    struct vr_desc   *tx_ring;
    char             *tx_bufs[VR_TX_RING_SIZE];
    struct mbuf      *tx_mbufs[VR_TX_RING_SIZE];
    int               tx_prod;
    int               tx_cons;

    /* RX ring */
    struct vr_desc   *rx_ring;
    char             *rx_bufs[VR_RX_RING_SIZE];
    int               rx_cons;
};

static struct vr_softc vr_devices[MAX_VR];
static int vr_count;
extern int ncpu;

static int  vr_output(struct ifnet *ifp, struct mbuf *m);
static void vr_poll(struct ifnet *ifp);

static struct ifnet_ops vr_ifnet_ops = {
    .if_output = vr_output,
    .if_poll   = vr_poll,
};

static int
vr_match(struct pci_dev *dev)
{
    if (!dev || dev->vendor_id != VR_VENDOR)
        return 0;
    switch (dev->device_id) {
    case VR_DEV_VT86C100A:
    case VR_DEV_VT6102:
    case VR_DEV_VT6103:
        return 1;
    default:
        return 0;
    }
}

static uint8_t
vr_read8(struct vr_softc *sc, int reg)
{
    return sc->regs[reg];
}

static void
vr_write8(struct vr_softc *sc, int reg, uint8_t val)
{
    sc->regs[reg] = val;
}

static void __attribute__((unused))
vr_write16(struct vr_softc *sc, int reg, uint16_t val)
{
    *(volatile uint16_t *)(sc->regs + reg) = val;
}

static void
vr_write32(struct vr_softc *sc, int reg, uint32_t val)
{
    *(volatile uint32_t *)(sc->regs + reg) = val;
}

static void
vr_read_mac(struct vr_softc *sc)
{
    int i;
    for (i = 0; i < 6; i++)
        sc->mac[i] = vr_read8(sc, VR_PAR0 + i);
}

static void
vr_reset(struct vr_softc *sc)
{
    int timeout;

    /* Disable interrupts first */
    *(volatile uint16_t *)(sc->regs + VR_IMR) = 0;

    /* Software reset via CR1[7] */
    vr_write8(sc, VR_CR1, VR_CR1_RESET);

    for (timeout = 0; timeout < 1000; timeout++) {
        if (!(vr_read8(sc, VR_CR1) & VR_CR1_RESET))
            break;
        microdelay(1000);
    }
    if (vr_read8(sc, VR_CR1) & VR_CR1_RESET)
        cprintf("vr: reset timeout\n");
}

static int
vr_alloc_rings(struct vr_softc *sc)
{
    int i;

    /* TX ring */
    sc->tx_ring = (struct vr_desc *)kalloc();
    if (!sc->tx_ring) return -1;
    memset(sc->tx_ring, 0, sizeof(struct vr_desc) * VR_TX_RING_SIZE);

    for (i = 0; i < VR_TX_RING_SIZE; i++) {
        sc->tx_bufs[i] = kalloc();
        if (!sc->tx_bufs[i]) return -1;
        /* Chain descriptors: each "next" points to the next physical desc */
        sc->tx_ring[i].next =
            V2P(sc->tx_ring) + ((i + 1) % VR_TX_RING_SIZE) *
            sizeof(struct vr_desc);
    }
    sc->tx_prod = 0;
    sc->tx_cons = 0;

    /* RX ring */
    sc->rx_ring = (struct vr_desc *)kalloc();
    if (!sc->rx_ring) return -1;
    memset(sc->rx_ring, 0, sizeof(struct vr_desc) * VR_RX_RING_SIZE);

    for (i = 0; i < VR_RX_RING_SIZE; i++) {
        sc->rx_bufs[i] = kalloc();
        if (!sc->rx_bufs[i]) return -1;

        sc->rx_ring[i].addr    = V2P(sc->rx_bufs[i]);
        sc->rx_ring[i].control = VR_RX_BUF_SIZE & VR_CTRL_RX_LEN_MASK;
        sc->rx_ring[i].status  = VR_STAT_OWN;
        sc->rx_ring[i].next    =
            V2P(sc->rx_ring) + ((i + 1) % VR_RX_RING_SIZE) *
            sizeof(struct vr_desc);
    }
    sc->rx_cons = 0;

    return 0;
}

static void
vr_init_rings(struct vr_softc *sc)
{
    /* Program descriptor ring base addresses */
    vr_write32(sc, VR_RXDESC_LO, V2P(sc->rx_ring));
    vr_write32(sc, VR_TXDESC_LO, V2P(sc->tx_ring));

    /* RX config: accept broadcast, multicast, unicast */
    vr_write8(sc, VR_RXCFG,
        VR_RXCFG_RX_BROAD | VR_RXCFG_RX_MULTI | VR_RXCFG_RX_UNI);

    /* TX config: standard interframe gap */
    vr_write8(sc, VR_TXCFG, VR_TXCFG_IFG_STD);

    /* Enable TX and RX */
    vr_write8(sc, VR_CR,
        VR_CR_TX_EN | VR_CR_RX_EN | VR_CR_RX_DEMAND);
}

static void
vr_tx_complete(struct vr_softc *sc)
{
    struct vr_desc *d;

    while (sc->tx_cons != sc->tx_prod) {
        d = &sc->tx_ring[sc->tx_cons];
        if (d->status & VR_STAT_OWN)
            break;  /* NIC still owns it */
        if (sc->tx_mbufs[sc->tx_cons]) {
            mbuf_free(sc->tx_mbufs[sc->tx_cons]);
            sc->tx_mbufs[sc->tx_cons] = 0;
        }
        sc->tx_cons = (sc->tx_cons + 1) % VR_TX_RING_SIZE;
    }
}

static void
vr_rx_complete(struct vr_softc *sc)
{
    struct vr_desc *d;
    struct mbuf *m;
    uint32_t stat;
    uint16_t len;
    int processed = 0;

    while (processed < 32) {
        d    = &sc->rx_ring[sc->rx_cons];
        stat = d->status;

        if (stat & VR_STAT_OWN)
            break;  /* NIC still owns it */

        if (stat & VR_STAT_RX_OK) {
            len = (uint16_t)((stat & VR_STAT_FRAME_LEN) >> VR_STAT_FRAME_SHIFT);
            /* Subtract 4-byte CRC the Rhine appends */
            if (len > 4) len -= 4;

            if (len > 0 && len <= VR_RX_BUF_SIZE) {
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
        d->control = VR_RX_BUF_SIZE & VR_CTRL_RX_LEN_MASK;
        d->status  = VR_STAT_OWN;

        sc->rx_cons = (sc->rx_cons + 1) % VR_RX_RING_SIZE;
        processed++;
    }
}

static void
vr_poll(struct ifnet *ifp)
{
    struct vr_softc *sc = (struct vr_softc *)ifp->if_softc;
    if (!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;
    acquire(&sc->lock);
    vr_tx_complete(sc);
    vr_rx_complete(sc);
    release(&sc->lock);
}

static int
vr_output(struct ifnet *ifp, struct mbuf *m)
{
    struct vr_softc *sc = (struct vr_softc *)ifp->if_softc;
    struct vr_desc *d;
    int idx, next;

    if (!sc || !m || m->len == 0)
        return -1;

    acquire(&sc->lock);

    if ((ifp->if_flags & IFF_RUNNING) == 0) {
        release(&sc->lock);
        return -1;
    }

    vr_tx_complete(sc);

    idx  = sc->tx_prod;
    next = (idx + 1) % VR_TX_RING_SIZE;
    if (next == sc->tx_cons) {
        release(&sc->lock);
        return -1;
    }

    memmove(sc->tx_bufs[idx], m->data, m->len);
    sc->tx_mbufs[idx] = m;

    d = &sc->tx_ring[idx];
    d->addr    = V2P(sc->tx_bufs[idx]);
    d->control = VR_CTRL_TX_STP | VR_CTRL_TX_EDP | VR_CTRL_TX_IC |
                 (m->len & VR_MAXLEN_MASK);
    d->status  = VR_STAT_OWN;  /* Hand to NIC */

    sc->tx_prod = next;

    /* TX demand: wake up the transmitter */
    vr_write8(sc, VR_CR, vr_read8(sc, VR_CR) | VR_CR_TX_DEMAND);

    release(&sc->lock);
    return 0;
}

static int
vr_probe(struct pci_dev *dev)
{
    struct vr_softc *sc;
    uint8_t miistatus;

    if (vr_count >= MAX_VR)
        return -1;

    sc = &vr_devices[vr_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "vr");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "vr");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    /* Prefer BAR1 (MMIO) over BAR0 (I/O port) */
    sc->regs = pci_map_bar(dev, 1);
    if (!sc->regs)
        sc->regs = pci_map_bar(dev, 0);
    if (!sc->regs) {
        cprintf("vr: failed to map registers at %d:%d.%d\n",
                dev->bus, dev->slot, dev->func);
        return -1;
    }

    BOOTDBG("vr: found %04x at %d:%d.%d irq=%d regs=%p\n",
            dev->device_id, dev->bus, dev->slot, dev->func,
            dev->irq_line, sc->regs);

    vr_reset(sc);
    vr_read_mac(sc);

    if (vr_alloc_rings(sc) < 0) {
        cprintf("vr: ring allocation failed\n");
        return -1;
    }

    /* Mask all interrupts (polling mode) */
    *(volatile uint16_t *)(sc->regs + VR_IMR) = 0;

    vr_init_rings(sc);

    /* Check link via MII status */
    miistatus = vr_read8(sc, VR_MIISTATUS);

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "vr0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[2] = '0' + vr_count;
    sc->ifn.if_mtu   = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_BROADCAST;
    if (miistatus & VR_MIISTATUS_LINK)
        sc->ifn.if_flags |= IFF_RUNNING;
    memmove(sc->ifn.if_hwaddr, sc->mac, 6);
    sc->ifn.if_softc = sc;
    sc->ifn.if_input = ether_input;
    sc->ifn.if_ops   = &vr_ifnet_ops;

    if (if_register(&sc->ifn) < 0) {
        cprintf("vr: failed to register ifnet\n");
        return -1;
    }

    cprintf("vr: attached %s VT%04x MAC %x:%x:%x:%x:%x:%x\n",
            sc->ifn.if_xname, dev->device_id,
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
    vr_count++;
    return 0;
}

void
via_rhine_init(void)
{
    int i;
    BOOTDBG("vr: initializing VIA Rhine driver\n");
    for (i = 0; i < pci_device_count(); i++) {
        struct pci_dev *dev = pci_get_device(i);
        if (vr_match(dev))
            vr_probe(dev);
    }
}
