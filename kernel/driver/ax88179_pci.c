/*
 * ASIX AX88179 PCI-oriented Ethernet driver for auxv6.
 *
 * AX88179 is usually USB-attached, but for this PCI-oriented path we use a
 * conservative e1000-like MMIO/descriptor tranche so the driver is no longer
 * attach-only. This enables real ifnet TX/RX plumbing under polling.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

#define PCI_VENDOR_ASIX_PCI 0x125B
#define PCI_VENDOR_ASIX_USB 0x0B95
#define PCI_DEVICE_AX88179  0x1790
#define PCI_DEVICE_AX88179A 0x1791

#define AX88179_CTRL       0x00000
#define AX88179_STATUS     0x00008
#define AX88179_IMC        0x000D8
#define AX88179_RCTL       0x00100
#define AX88179_TCTL       0x00400
#define AX88179_TDBAL      0x03800
#define AX88179_TDBAH      0x03804
#define AX88179_TDLEN      0x03808
#define AX88179_TDH        0x03810
#define AX88179_TDT        0x03818
#define AX88179_RDBAL      0x02800
#define AX88179_RDBAH      0x02804
#define AX88179_RDLEN      0x02808
#define AX88179_RDH        0x02810
#define AX88179_RDT        0x02818
#define AX88179_MTA        0x05200
#define AX88179_RAL0       0x05400
#define AX88179_RAH0       0x05404

#define AX88179_CTRL_SLU   0x00000040
#define AX88179_CTRL_ASDE  0x00000020
#define AX88179_CTRL_RST   0x04000000

#define AX88179_STATUS_LU  0x00000002

#define AX88179_RCTL_EN       0x00000002
#define AX88179_RCTL_BAM      0x00008000
#define AX88179_RCTL_SECRC    0x04000000
#define AX88179_RCTL_BSIZE_2048 0x00000000

#define AX88179_TCTL_EN       0x00000002
#define AX88179_TCTL_PSP      0x00000008

#define AX88179_TXD_CMD_EOP   0x01
#define AX88179_TXD_CMD_IFCS  0x02
#define AX88179_TXD_CMD_RS    0x08
#define AX88179_TXD_STAT_DD   0x01

#define AX88179_RXD_STAT_DD   0x01
#define AX88179_RXD_STAT_EOP  0x02

#define AX88179_TX_RING_SIZE 64
#define AX88179_RX_RING_SIZE 64
#define AX88179_RX_BUF_SIZE  2048

#define MAX_AX88179 4

struct ax88179_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed));

struct ax88179_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct ax88179_softc {
    struct pci_dev *pci;
    struct spinlock lock;
    struct ifnet ifn;

    volatile uint32_t *regs;
    uint8_t mac[6];

    struct ax88179_tx_desc *tx_ring;
    struct mbuf *tx_mbufs[AX88179_TX_RING_SIZE];
    uint16_t tx_head;
    uint16_t tx_tail;

    struct ax88179_rx_desc *rx_ring;
    char *rx_bufs[AX88179_RX_RING_SIZE];
    uint16_t rx_tail;
};

static struct ax88179_softc ax88179_devices[MAX_AX88179];
static int ax88179_count;

static int ax88179_output(struct ifnet *ifp, struct mbuf *m);
static void ax88179_poll(struct ifnet *ifp);

static struct ifnet_ops ax88179_ifnet_ops = {
    .if_output = ax88179_output,
    .if_poll = ax88179_poll,
};

static int
ax88179_match(struct pci_dev *dev)
{
    if (dev == 0)
        return 0;

    if (dev->device_id != PCI_DEVICE_AX88179 &&
        dev->device_id != PCI_DEVICE_AX88179A)
        return 0;

    if (dev->vendor_id == PCI_VENDOR_ASIX_PCI ||
        dev->vendor_id == PCI_VENDOR_ASIX_USB)
        return 1;

    return 0;
}

static uint32_t
ax88179_read(struct ax88179_softc *sc, int reg)
{
    return sc->regs[reg / 4];
}

static void
ax88179_write(struct ax88179_softc *sc, int reg, uint32_t val)
{
    sc->regs[reg / 4] = val;
}

static void
ax88179_read_mac(struct ax88179_softc *sc)
{
    uint32_t ral = ax88179_read(sc, AX88179_RAL0);
    uint32_t rah = ax88179_read(sc, AX88179_RAH0);

    sc->mac[0] = ral & 0xFF;
    sc->mac[1] = (ral >> 8) & 0xFF;
    sc->mac[2] = (ral >> 16) & 0xFF;
    sc->mac[3] = (ral >> 24) & 0xFF;
    sc->mac[4] = rah & 0xFF;
    sc->mac[5] = (rah >> 8) & 0xFF;
}

static void
ax88179_reset(struct ax88179_softc *sc)
{
    ax88179_write(sc, AX88179_IMC, 0xFFFFFFFF);
    ax88179_write(sc, AX88179_CTRL, ax88179_read(sc, AX88179_CTRL) | AX88179_CTRL_RST);
    microdelay(10000);
    ax88179_write(sc, AX88179_IMC, 0xFFFFFFFF);
}

static int
ax88179_init_tx(struct ax88179_softc *sc)
{
    sc->tx_ring = (struct ax88179_tx_desc *)kalloc();
    if (!sc->tx_ring)
        return -1;

    memset(sc->tx_ring, 0, sizeof(struct ax88179_tx_desc) * AX88179_TX_RING_SIZE);
    sc->tx_head = 0;
    sc->tx_tail = 0;

    ax88179_write(sc, AX88179_TDBAL, V2P(sc->tx_ring));
    ax88179_write(sc, AX88179_TDBAH, 0);
    ax88179_write(sc, AX88179_TDLEN, sizeof(struct ax88179_tx_desc) * AX88179_TX_RING_SIZE);
    ax88179_write(sc, AX88179_TDH, 0);
    ax88179_write(sc, AX88179_TDT, 0);

    ax88179_write(sc, AX88179_TCTL, AX88179_TCTL_EN | AX88179_TCTL_PSP |
        (0x10 << 4) | (0x40 << 12));

    return 0;
}

static int
ax88179_init_rx(struct ax88179_softc *sc)
{
    int i;

    sc->rx_ring = (struct ax88179_rx_desc *)kalloc();
    if (!sc->rx_ring)
        return -1;

    memset(sc->rx_ring, 0, sizeof(struct ax88179_rx_desc) * AX88179_RX_RING_SIZE);
    for (i = 0; i < AX88179_RX_RING_SIZE; i++) {
        sc->rx_bufs[i] = kalloc();
        if (!sc->rx_bufs[i])
            return -1;
        sc->rx_ring[i].addr = V2P(sc->rx_bufs[i]);
    }

    sc->rx_tail = AX88179_RX_RING_SIZE - 1;

    ax88179_write(sc, AX88179_RDBAL, V2P(sc->rx_ring));
    ax88179_write(sc, AX88179_RDBAH, 0);
    ax88179_write(sc, AX88179_RDLEN, sizeof(struct ax88179_rx_desc) * AX88179_RX_RING_SIZE);
    ax88179_write(sc, AX88179_RDH, 0);
    ax88179_write(sc, AX88179_RDT, sc->rx_tail);

    for (i = 0; i < 128; i++)
        ax88179_write(sc, AX88179_MTA + i * 4, 0);

    ax88179_write(sc, AX88179_RCTL,
        AX88179_RCTL_EN | AX88179_RCTL_BAM | AX88179_RCTL_SECRC | AX88179_RCTL_BSIZE_2048);

    return 0;
}

static void
ax88179_tx_complete(struct ax88179_softc *sc)
{
    while (sc->tx_head != sc->tx_tail &&
           (sc->tx_ring[sc->tx_head].status & AX88179_TXD_STAT_DD)) {
        if (sc->tx_mbufs[sc->tx_head])
            mbuf_free(sc->tx_mbufs[sc->tx_head]);
        sc->tx_mbufs[sc->tx_head] = 0;
        sc->tx_head = (sc->tx_head + 1) % AX88179_TX_RING_SIZE;
    }
}

static void
ax88179_rx_complete(struct ax88179_softc *sc)
{
    int processed = 0;

    while (processed < 32) {
        uint16_t idx = (sc->rx_tail + 1) % AX88179_RX_RING_SIZE;
        struct ax88179_rx_desc *desc = &sc->rx_ring[idx];

        if ((desc->status & AX88179_RXD_STAT_DD) == 0)
            break;

        if ((desc->status & AX88179_RXD_STAT_EOP) && desc->errors == 0) {
            uint16_t len = desc->length;
            if (len > 0 && len <= AX88179_RX_BUF_SIZE) {
                struct mbuf *m = mbuf_alloc();
                if (m) {
                    memmove(m->data, sc->rx_bufs[idx], len);
                    m->len = len;
                    m->rcvif = &sc->ifn;
                    release(&sc->lock);
                    if_input(&sc->ifn, m);
                    acquire(&sc->lock);
                }
            }
        }

        desc->status = 0;
        desc->addr = V2P(sc->rx_bufs[idx]);
        sc->rx_tail = idx;
        ax88179_write(sc, AX88179_RDT, sc->rx_tail);
        processed++;
    }
}

static void
ax88179_poll(struct ifnet *ifp)
{
    struct ax88179_softc *sc = (struct ax88179_softc *)ifp->if_softc;

    if (!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;

    acquire(&sc->lock);
    ax88179_tx_complete(sc);
    ax88179_rx_complete(sc);
    release(&sc->lock);
}

static int
ax88179_output(struct ifnet *ifp, struct mbuf *m)
{
    struct ax88179_softc *sc = (struct ax88179_softc *)ifp->if_softc;
    uint16_t next;
    struct ax88179_tx_desc *desc;

    if (!sc || !m || m->len == 0)
        return -1;

    acquire(&sc->lock);

    if ((ifp->if_flags & IFF_RUNNING) == 0) {
        release(&sc->lock);
        return -1;
    }

    ax88179_tx_complete(sc);

    next = (sc->tx_tail + 1) % AX88179_TX_RING_SIZE;
    if (next == sc->tx_head) {
        release(&sc->lock);
        return -1;
    }

    desc = &sc->tx_ring[sc->tx_tail];
    desc->addr = V2P(m->data);
    desc->length = m->len;
    desc->cmd = AX88179_TXD_CMD_EOP | AX88179_TXD_CMD_IFCS | AX88179_TXD_CMD_RS;
    desc->status = 0;

    sc->tx_mbufs[sc->tx_tail] = m;
    sc->tx_tail = next;
    ax88179_write(sc, AX88179_TDT, sc->tx_tail);

    release(&sc->lock);
    return 0;
}

static int
ax88179_probe(struct pci_dev *dev)
{
    struct ax88179_softc *sc;
    uint32_t status;

    if (ax88179_count >= MAX_AX88179)
        return -1;

    sc = &ax88179_devices[ax88179_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "ax88179");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "ax88179");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    sc->regs = pci_map_bar(dev, 0);
    if (!sc->regs) {
        cprintf("ax88179: failed to map BAR0 at %d:%d.%d\n",
                dev->bus, dev->slot, dev->func);
        return -1;
    }

    ax88179_reset(sc);
    ax88179_read_mac(sc);

    if (ax88179_init_tx(sc) < 0 || ax88179_init_rx(sc) < 0) {
        cprintf("ax88179: failed to initialize TX/RX rings\n");
        return -1;
    }

    ax88179_write(sc, AX88179_CTRL,
        ax88179_read(sc, AX88179_CTRL) | AX88179_CTRL_SLU | AX88179_CTRL_ASDE);

    status = ax88179_read(sc, AX88179_STATUS);

    BOOTDBG("ax88179: found at %d:%d.%d vendor=%x devid=%x rev=%d irq=%d status=%x\n",
            dev->bus, dev->slot, dev->func,
            dev->vendor_id, dev->device_id, dev->revision, dev->irq_line, status);
    cprintf("ax88179: MAC %x:%x:%x:%x:%x:%x\n",
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "axp0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[3] = '0' + ax88179_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_BROADCAST;
    if (status & AX88179_STATUS_LU)
        sc->ifn.if_flags |= IFF_RUNNING;

    memmove(sc->ifn.if_hwaddr, sc->mac, sizeof(sc->ifn.if_hwaddr));
    sc->ifn.if_softc = sc;
    sc->ifn.if_input = ether_input;
    sc->ifn.if_ops = &ax88179_ifnet_ops;

    if (if_register(&sc->ifn) < 0) {
        cprintf("ax88179: failed to register ifnet\n");
        return -1;
    }

    cprintf("ax88179: attached %s (polling TX/RX)\n", sc->ifn.if_xname);
    ax88179_count++;
    return 0;
}

void
ax88179_pci_init(void)
{
    int i;

    BOOTDBG("ax88179: initializing PCI driver\n");

    for (i = 0; i < pci_device_count(); i++) {
        struct pci_dev *dev = pci_get_device(i);
        if (ax88179_match(dev))
            ax88179_probe(dev);
    }
}
