/*
 * Intel I226-V (igc-class) Ethernet Driver for auxv6.
 *
 * This tranche mirrors a conservative Unix bring-up strategy used by
 * BSD/Linux drivers: descriptor rings with polling completions and a
 * minimal link-status model.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

#define I226_CTRL       0x00000
#define I226_STATUS     0x00008
#define I226_IMC        0x000D8
#define I226_RCTL       0x00100
#define I226_TCTL       0x00400
#define I226_TDBAL      0x03800
#define I226_TDBAH      0x03804
#define I226_TDLEN      0x03808
#define I226_TDH        0x03810
#define I226_TDT        0x03818
#define I226_RDBAL      0x02800
#define I226_RDBAH      0x02804
#define I226_RDLEN      0x02808
#define I226_RDH        0x02810
#define I226_RDT        0x02818
#define I226_MTA        0x05200
#define I226_RAL0       0x05400
#define I226_RAH0       0x05404

#define I226_CTRL_SLU   0x00000040
#define I226_CTRL_ASDE  0x00000020
#define I226_CTRL_RST   0x04000000

#define I226_STATUS_LU  0x00000002

#define I226_RCTL_EN       0x00000002
#define I226_RCTL_BAM      0x00008000
#define I226_RCTL_SECRC    0x04000000
#define I226_RCTL_BSIZE_2048 0x00000000

#define I226_TCTL_EN       0x00000002
#define I226_TCTL_PSP      0x00000008

#define I226_TXD_CMD_EOP   0x01
#define I226_TXD_CMD_IFCS  0x02
#define I226_TXD_CMD_RS    0x08
#define I226_TXD_STAT_DD   0x01

#define I226_RXD_STAT_DD   0x01
#define I226_RXD_STAT_EOP  0x02

#define PCI_DEVICE_I226_LM 0x125B
#define PCI_DEVICE_I226_V  0x125C
#define PCI_DEVICE_I226_IT 0x125D

#define I226_TX_RING_SIZE 64
#define I226_RX_RING_SIZE 64
#define I226_RX_BUF_SIZE  2048

#define MAX_I226 4

struct i226_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed));

struct i226_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct i226_softc {
    struct pci_dev *pci;
    struct spinlock lock;
    struct ifnet ifn;

    volatile uint32_t *regs;
    uint8_t mac[6];

    struct i226_tx_desc *tx_ring;
    struct mbuf *tx_mbufs[I226_TX_RING_SIZE];
    uint16_t tx_head;
    uint16_t tx_tail;

    struct i226_rx_desc *rx_ring;
    char *rx_bufs[I226_RX_RING_SIZE];
    uint16_t rx_tail;
};

static struct i226_softc i226_devices[MAX_I226];
static int i226_count;

static int i226_output(struct ifnet *ifp, struct mbuf *m);
static void i226_poll(struct ifnet *ifp);

static struct ifnet_ops i226_ifnet_ops = {
    .if_output = i226_output,
    .if_poll = i226_poll,
};

static int
i226_match(struct pci_dev *dev)
{
    if (dev == 0)
        return 0;
    if (dev->vendor_id != PCI_VENDOR_INTEL)
        return 0;

    switch (dev->device_id) {
    case PCI_DEVICE_I226_LM:
    case PCI_DEVICE_I226_V:
    case PCI_DEVICE_I226_IT:
        return 1;
    default:
        return 0;
    }
}

static uint32_t
i226_read(struct i226_softc *sc, int reg)
{
    return sc->regs[reg / 4];
}

static void
i226_write(struct i226_softc *sc, int reg, uint32_t val)
{
    sc->regs[reg / 4] = val;
}

static void
i226_read_mac(struct i226_softc *sc)
{
    uint32_t ral = i226_read(sc, I226_RAL0);
    uint32_t rah = i226_read(sc, I226_RAH0);

    sc->mac[0] = ral & 0xFF;
    sc->mac[1] = (ral >> 8) & 0xFF;
    sc->mac[2] = (ral >> 16) & 0xFF;
    sc->mac[3] = (ral >> 24) & 0xFF;
    sc->mac[4] = rah & 0xFF;
    sc->mac[5] = (rah >> 8) & 0xFF;
}

static void
i226_reset(struct i226_softc *sc)
{
    i226_write(sc, I226_IMC, 0xFFFFFFFF);
    i226_write(sc, I226_CTRL, i226_read(sc, I226_CTRL) | I226_CTRL_RST);
    microdelay(10000);
    i226_write(sc, I226_IMC, 0xFFFFFFFF);
}

static int
i226_init_tx(struct i226_softc *sc)
{
    sc->tx_ring = (struct i226_tx_desc *)kalloc();
    if (!sc->tx_ring)
        return -1;

    memset(sc->tx_ring, 0, sizeof(struct i226_tx_desc) * I226_TX_RING_SIZE);
    sc->tx_head = 0;
    sc->tx_tail = 0;

    i226_write(sc, I226_TDBAL, V2P(sc->tx_ring));
    i226_write(sc, I226_TDBAH, 0);
    i226_write(sc, I226_TDLEN, sizeof(struct i226_tx_desc) * I226_TX_RING_SIZE);
    i226_write(sc, I226_TDH, 0);
    i226_write(sc, I226_TDT, 0);

    i226_write(sc, I226_TCTL, I226_TCTL_EN | I226_TCTL_PSP |
        (0x10 << 4) | (0x40 << 12));

    return 0;
}

static int
i226_init_rx(struct i226_softc *sc)
{
    int i;

    sc->rx_ring = (struct i226_rx_desc *)kalloc();
    if (!sc->rx_ring)
        return -1;

    memset(sc->rx_ring, 0, sizeof(struct i226_rx_desc) * I226_RX_RING_SIZE);
    for (i = 0; i < I226_RX_RING_SIZE; i++) {
        sc->rx_bufs[i] = kalloc();
        if (!sc->rx_bufs[i])
            return -1;
        sc->rx_ring[i].addr = V2P(sc->rx_bufs[i]);
    }

    sc->rx_tail = I226_RX_RING_SIZE - 1;

    i226_write(sc, I226_RDBAL, V2P(sc->rx_ring));
    i226_write(sc, I226_RDBAH, 0);
    i226_write(sc, I226_RDLEN, sizeof(struct i226_rx_desc) * I226_RX_RING_SIZE);
    i226_write(sc, I226_RDH, 0);
    i226_write(sc, I226_RDT, sc->rx_tail);

    for (i = 0; i < 128; i++)
        i226_write(sc, I226_MTA + i * 4, 0);

    i226_write(sc, I226_RCTL,
        I226_RCTL_EN | I226_RCTL_BAM | I226_RCTL_SECRC | I226_RCTL_BSIZE_2048);

    return 0;
}

static void
i226_tx_complete(struct i226_softc *sc)
{
    while (sc->tx_head != sc->tx_tail &&
           (sc->tx_ring[sc->tx_head].status & I226_TXD_STAT_DD)) {
        if (sc->tx_mbufs[sc->tx_head])
            mbuf_free(sc->tx_mbufs[sc->tx_head]);
        sc->tx_mbufs[sc->tx_head] = 0;
        sc->tx_head = (sc->tx_head + 1) % I226_TX_RING_SIZE;
    }
}

static void
i226_rx_complete(struct i226_softc *sc)
{
    int processed = 0;

    while (processed < 32) {
        uint16_t idx = (sc->rx_tail + 1) % I226_RX_RING_SIZE;
        struct i226_rx_desc *desc = &sc->rx_ring[idx];

        if ((desc->status & I226_RXD_STAT_DD) == 0)
            break;

        if ((desc->status & I226_RXD_STAT_EOP) && desc->errors == 0) {
            uint16_t len = desc->length;
            if (len > 0 && len <= I226_RX_BUF_SIZE) {
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
        i226_write(sc, I226_RDT, sc->rx_tail);
        processed++;
    }
}

static void
i226_poll(struct ifnet *ifp)
{
    struct i226_softc *sc = (struct i226_softc *)ifp->if_softc;

    if (!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;

    acquire(&sc->lock);
    i226_tx_complete(sc);
    i226_rx_complete(sc);
    release(&sc->lock);
}

static int
i226_output(struct ifnet *ifp, struct mbuf *m)
{
    struct i226_softc *sc = (struct i226_softc *)ifp->if_softc;
    uint16_t next;
    struct i226_tx_desc *desc;

    if (!sc || !m || m->len == 0)
        return -1;

    acquire(&sc->lock);

    if ((ifp->if_flags & IFF_RUNNING) == 0) {
        release(&sc->lock);
        return -1;
    }

    i226_tx_complete(sc);

    next = (sc->tx_tail + 1) % I226_TX_RING_SIZE;
    if (next == sc->tx_head) {
        release(&sc->lock);
        return -1;
    }

    desc = &sc->tx_ring[sc->tx_tail];
    desc->addr = V2P(m->data);
    desc->length = m->len;
    desc->cmd = I226_TXD_CMD_EOP | I226_TXD_CMD_IFCS | I226_TXD_CMD_RS;
    desc->status = 0;

    sc->tx_mbufs[sc->tx_tail] = m;
    sc->tx_tail = next;
    i226_write(sc, I226_TDT, sc->tx_tail);

    release(&sc->lock);
    return 0;
}

static int
i226_probe(struct pci_dev *dev)
{
    struct i226_softc *sc;
    uint32_t status;

    if (i226_count >= MAX_I226)
        return -1;

    sc = &i226_devices[i226_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "i226");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "i226");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    sc->regs = pci_map_bar(dev, 0);
    if (!sc->regs) {
        cprintf("i226: failed to map BAR0 at %d:%d.%d\n",
                dev->bus, dev->slot, dev->func);
        return -1;
    }

    i226_reset(sc);
    i226_read_mac(sc);

    if (i226_init_tx(sc) < 0 || i226_init_rx(sc) < 0) {
        cprintf("i226: failed to initialize TX/RX rings\n");
        return -1;
    }

    i226_write(sc, I226_CTRL, i226_read(sc, I226_CTRL) | I226_CTRL_SLU | I226_CTRL_ASDE);

    status = i226_read(sc, I226_STATUS);

    BOOTDBG("i226: found at %d:%d.%d devid=%x rev=%d irq=%d status=%x\n",
            dev->bus, dev->slot, dev->func,
            dev->device_id, dev->revision, dev->irq_line, status);
    cprintf("i226: MAC %x:%x:%x:%x:%x:%x\n",
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "igc0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[3] = '0' + i226_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_BROADCAST;
    if (status & I226_STATUS_LU)
        sc->ifn.if_flags |= IFF_RUNNING;

    memmove(sc->ifn.if_hwaddr, sc->mac, sizeof(sc->ifn.if_hwaddr));
    sc->ifn.if_softc = sc;
    sc->ifn.if_input = ether_input;
    sc->ifn.if_ops = &i226_ifnet_ops;

    if (if_register(&sc->ifn) < 0) {
        cprintf("i226: failed to register ifnet\n");
        return -1;
    }

    cprintf("i226: attached %s (polling TX/RX)\n", sc->ifn.if_xname);
    i226_count++;
    return 0;
}

void
i226_init(void)
{
    int i;

    BOOTDBG("i226: initializing driver\n");

    for (i = 0; i < pci_device_count(); i++) {
        struct pci_dev *dev = pci_get_device(i);
        if (i226_match(dev))
            i226_probe(dev);
    }
}
