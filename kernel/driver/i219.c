/*
 * Intel I219-V (e1000e-class) Ethernet Driver for auxv6.
 *
 * This implementation follows a conservative BSD/Linux-style bring-up
 * tranche: PCI probe + BAR map, descriptor-ring TX/RX, and polling
 * completions through ifnet.if_poll.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

#define I219_CTRL       0x00000
#define I219_STATUS     0x00008
#define I219_IMC        0x000D8
#define I219_RCTL       0x00100
#define I219_TCTL       0x00400
#define I219_TDBAL      0x03800
#define I219_TDBAH      0x03804
#define I219_TDLEN      0x03808
#define I219_TDH        0x03810
#define I219_TDT        0x03818
#define I219_RDBAL      0x02800
#define I219_RDBAH      0x02804
#define I219_RDLEN      0x02808
#define I219_RDH        0x02810
#define I219_RDT        0x02818
#define I219_MTA        0x05200
#define I219_RAL0       0x05400
#define I219_RAH0       0x05404

#define I219_CTRL_SLU   0x00000040
#define I219_CTRL_ASDE  0x00000020
#define I219_CTRL_RST   0x04000000

#define I219_STATUS_LU  0x00000002

#define I219_RCTL_EN       0x00000002
#define I219_RCTL_BAM      0x00008000
#define I219_RCTL_SECRC    0x04000000
#define I219_RCTL_BSIZE_2048 0x00000000

#define I219_TCTL_EN       0x00000002
#define I219_TCTL_PSP      0x00000008

#define I219_TXD_CMD_EOP   0x01
#define I219_TXD_CMD_IFCS  0x02
#define I219_TXD_CMD_RS    0x08
#define I219_TXD_STAT_DD   0x01

#define I219_RXD_STAT_DD   0x01
#define I219_RXD_STAT_EOP  0x02

#define PCI_DEVICE_I219_LM_1 0x15B7
#define PCI_DEVICE_I219_V_1  0x15B8
#define PCI_DEVICE_I219_LM_2 0x15D7
#define PCI_DEVICE_I219_V_2  0x15D8
#define PCI_DEVICE_I219_LM_3 0x0D4E
#define PCI_DEVICE_I219_V_3  0x0D4F

#define I219_TX_RING_SIZE 64
#define I219_RX_RING_SIZE 64
#define I219_RX_BUF_SIZE  2048

#define MAX_I219 4

struct i219_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed));

struct i219_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct i219_softc {
    struct pci_dev *pci;
    struct spinlock lock;
    struct ifnet ifn;

    volatile uint32_t *regs;
    uint8_t mac[6];

    struct i219_tx_desc *tx_ring;
    struct mbuf *tx_mbufs[I219_TX_RING_SIZE];
    uint16_t tx_head;
    uint16_t tx_tail;

    struct i219_rx_desc *rx_ring;
    char *rx_bufs[I219_RX_RING_SIZE];
    uint16_t rx_tail;
};

static struct i219_softc i219_devices[MAX_I219];
static int i219_count;

static int i219_output(struct ifnet *ifp, struct mbuf *m);
static void i219_poll(struct ifnet *ifp);

static struct ifnet_ops i219_ifnet_ops = {
    .if_output = i219_output,
    .if_poll = i219_poll,
};

static int
i219_match(struct pci_dev *dev)
{
    if (dev == 0)
        return 0;
    if (dev->vendor_id != PCI_VENDOR_INTEL)
        return 0;

    switch (dev->device_id) {
    case PCI_DEVICE_I219_LM_1:
    case PCI_DEVICE_I219_V_1:
    case PCI_DEVICE_I219_LM_2:
    case PCI_DEVICE_I219_V_2:
    case PCI_DEVICE_I219_LM_3:
    case PCI_DEVICE_I219_V_3:
        return 1;
    default:
        return 0;
    }
}

static uint32_t
i219_read(struct i219_softc *sc, int reg)
{
    return sc->regs[reg / 4];
}

static void
i219_write(struct i219_softc *sc, int reg, uint32_t val)
{
    sc->regs[reg / 4] = val;
}

static void
i219_read_mac(struct i219_softc *sc)
{
    uint32_t ral = i219_read(sc, I219_RAL0);
    uint32_t rah = i219_read(sc, I219_RAH0);

    sc->mac[0] = ral & 0xFF;
    sc->mac[1] = (ral >> 8) & 0xFF;
    sc->mac[2] = (ral >> 16) & 0xFF;
    sc->mac[3] = (ral >> 24) & 0xFF;
    sc->mac[4] = rah & 0xFF;
    sc->mac[5] = (rah >> 8) & 0xFF;
}

static void
i219_reset(struct i219_softc *sc)
{
    i219_write(sc, I219_IMC, 0xFFFFFFFF);
    i219_write(sc, I219_CTRL, i219_read(sc, I219_CTRL) | I219_CTRL_RST);
    microdelay(10000);
    i219_write(sc, I219_IMC, 0xFFFFFFFF);
}

static int
i219_init_tx(struct i219_softc *sc)
{
    sc->tx_ring = (struct i219_tx_desc *)kalloc();
    if (!sc->tx_ring)
        return -1;

    memset(sc->tx_ring, 0, sizeof(struct i219_tx_desc) * I219_TX_RING_SIZE);
    sc->tx_head = 0;
    sc->tx_tail = 0;

    i219_write(sc, I219_TDBAL, V2P(sc->tx_ring));
    i219_write(sc, I219_TDBAH, 0);
    i219_write(sc, I219_TDLEN, sizeof(struct i219_tx_desc) * I219_TX_RING_SIZE);
    i219_write(sc, I219_TDH, 0);
    i219_write(sc, I219_TDT, 0);

    i219_write(sc, I219_TCTL, I219_TCTL_EN | I219_TCTL_PSP |
        (0x10 << 4) | (0x40 << 12));

    return 0;
}

static int
i219_init_rx(struct i219_softc *sc)
{
    int i;

    sc->rx_ring = (struct i219_rx_desc *)kalloc();
    if (!sc->rx_ring)
        return -1;

    memset(sc->rx_ring, 0, sizeof(struct i219_rx_desc) * I219_RX_RING_SIZE);
    for (i = 0; i < I219_RX_RING_SIZE; i++) {
        sc->rx_bufs[i] = kalloc();
        if (!sc->rx_bufs[i])
            return -1;
        sc->rx_ring[i].addr = V2P(sc->rx_bufs[i]);
    }

    sc->rx_tail = I219_RX_RING_SIZE - 1;

    i219_write(sc, I219_RDBAL, V2P(sc->rx_ring));
    i219_write(sc, I219_RDBAH, 0);
    i219_write(sc, I219_RDLEN, sizeof(struct i219_rx_desc) * I219_RX_RING_SIZE);
    i219_write(sc, I219_RDH, 0);
    i219_write(sc, I219_RDT, sc->rx_tail);

    for (i = 0; i < 128; i++)
        i219_write(sc, I219_MTA + i * 4, 0);

    i219_write(sc, I219_RCTL,
        I219_RCTL_EN | I219_RCTL_BAM | I219_RCTL_SECRC | I219_RCTL_BSIZE_2048);

    return 0;
}

static void
i219_tx_complete(struct i219_softc *sc)
{
    while (sc->tx_head != sc->tx_tail &&
           (sc->tx_ring[sc->tx_head].status & I219_TXD_STAT_DD)) {
        if (sc->tx_mbufs[sc->tx_head])
            mbuf_free(sc->tx_mbufs[sc->tx_head]);
        sc->tx_mbufs[sc->tx_head] = 0;
        sc->tx_head = (sc->tx_head + 1) % I219_TX_RING_SIZE;
    }
}

static void
i219_rx_complete(struct i219_softc *sc)
{
    int processed = 0;

    while (processed < 32) {
        uint16_t idx = (sc->rx_tail + 1) % I219_RX_RING_SIZE;
        struct i219_rx_desc *desc = &sc->rx_ring[idx];

        if ((desc->status & I219_RXD_STAT_DD) == 0)
            break;

        if ((desc->status & I219_RXD_STAT_EOP) && desc->errors == 0) {
            uint16_t len = desc->length;
            if (len > 0 && len <= I219_RX_BUF_SIZE) {
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
        i219_write(sc, I219_RDT, sc->rx_tail);
        processed++;
    }
}

static void
i219_poll(struct ifnet *ifp)
{
    struct i219_softc *sc = (struct i219_softc *)ifp->if_softc;

    if (!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;

    acquire(&sc->lock);
    i219_tx_complete(sc);
    i219_rx_complete(sc);
    release(&sc->lock);
}

static int
i219_output(struct ifnet *ifp, struct mbuf *m)
{
    struct i219_softc *sc = (struct i219_softc *)ifp->if_softc;
    uint16_t next;
    struct i219_tx_desc *desc;

    if (!sc || !m || m->len == 0)
        return -1;

    acquire(&sc->lock);

    if ((ifp->if_flags & IFF_RUNNING) == 0) {
        release(&sc->lock);
        return -1;
    }

    i219_tx_complete(sc);

    next = (sc->tx_tail + 1) % I219_TX_RING_SIZE;
    if (next == sc->tx_head) {
        release(&sc->lock);
        return -1;
    }

    desc = &sc->tx_ring[sc->tx_tail];
    desc->addr = V2P(m->data);
    desc->length = m->len;
    desc->cmd = I219_TXD_CMD_EOP | I219_TXD_CMD_IFCS | I219_TXD_CMD_RS;
    desc->status = 0;

    sc->tx_mbufs[sc->tx_tail] = m;
    sc->tx_tail = next;
    i219_write(sc, I219_TDT, sc->tx_tail);

    release(&sc->lock);
    return 0;
}

static int
i219_probe(struct pci_dev *dev)
{
    struct i219_softc *sc;
    uint32_t status;

    if (i219_count >= MAX_I219)
        return -1;

    sc = &i219_devices[i219_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "i219");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "i219");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    sc->regs = pci_map_bar(dev, 0);
    if (!sc->regs) {
        cprintf("i219: failed to map BAR0 at %d:%d.%d\n",
                dev->bus, dev->slot, dev->func);
        return -1;
    }

    i219_reset(sc);
    i219_read_mac(sc);

    if (i219_init_tx(sc) < 0 || i219_init_rx(sc) < 0) {
        cprintf("i219: failed to initialize TX/RX rings\n");
        return -1;
    }

    i219_write(sc, I219_CTRL, i219_read(sc, I219_CTRL) | I219_CTRL_SLU | I219_CTRL_ASDE);

    status = i219_read(sc, I219_STATUS);

    BOOTDBG("i219: found at %d:%d.%d devid=%x rev=%d irq=%d status=%x\n",
            dev->bus, dev->slot, dev->func,
            dev->device_id, dev->revision, dev->irq_line, status);
    cprintf("i219: MAC %x:%x:%x:%x:%x:%x\n",
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "wm0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[2] = '0' + i219_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_BROADCAST;
    if (status & I219_STATUS_LU)
        sc->ifn.if_flags |= IFF_RUNNING;

    memmove(sc->ifn.if_hwaddr, sc->mac, sizeof(sc->ifn.if_hwaddr));
    sc->ifn.if_softc = sc;
    sc->ifn.if_input = ether_input;
    sc->ifn.if_ops = &i219_ifnet_ops;

    if (if_register(&sc->ifn) < 0) {
        cprintf("i219: failed to register ifnet\n");
        return -1;
    }

    cprintf("i219: attached %s (polling TX/RX)\n", sc->ifn.if_xname);
    i219_count++;
    return 0;
}

void
i219_init(void)
{
    int i;

    BOOTDBG("i219: initializing driver\n");

    for (i = 0; i < pci_device_count(); i++) {
        struct pci_dev *dev = pci_get_device(i);
        if (i219_match(dev))
            i219_probe(dev);
    }
}
