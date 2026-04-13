/*
 * Intel ixgbe-family NIC driver for auxv6.
 *
 * Covers common 10GbE parts: 82598, 82599, X540, X550.
 *
 * Full descriptor-ring TX/RX using legacy 16-byte descriptors.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

#define IXGBE_VENDOR_INTEL 0x8086
#define IXGBE_DEV_82598    0x10F8
#define IXGBE_DEV_82599    0x10FB
#define IXGBE_DEV_X540     0x1528
#define IXGBE_DEV_X550     0x1563

/* BAR0 MMIO register offsets */
#define IXGBE_CTRL          0x00000
#define IXGBE_STATUS        0x00008
#define IXGBE_EIMC          0x00888
#define IXGBE_FCTRL         0x05080
#define IXGBE_HLREG0        0x04240
#define IXGBE_RXCTRL        0x03000
#define IXGBE_LINKS         0x042A4
#define IXGBE_RAL0          0x0A200
#define IXGBE_RAH0          0x0A204
/* RX queue 0 */
#define IXGBE_RDBAL0        0x01000
#define IXGBE_RDBAH0        0x01004
#define IXGBE_RDLEN0        0x01008
#define IXGBE_RDH0          0x01010
#define IXGBE_RDT0          0x01018
#define IXGBE_RXDCTL0       0x01028
/* TX queue 0 */
#define IXGBE_TDBAL0        0x06000
#define IXGBE_TDBAH0        0x06004
#define IXGBE_TDLEN0        0x06008
#define IXGBE_TDH0          0x06010
#define IXGBE_TDT0          0x06018
#define IXGBE_TXDCTL0       0x06028

#define IXGBE_CTRL_RST       0x04000008
#define IXGBE_LINKS_UP       0x40000000
#define IXGBE_FCTRL_UPE      0x00000200
#define IXGBE_FCTRL_MPE      0x00000100
#define IXGBE_HLREG0_TXCRCEN 0x00000001
#define IXGBE_HLREG0_RXCRCSTRIP 0x00000002
#define IXGBE_RXDCTL_ENABLE  0x02000000
#define IXGBE_TXDCTL_ENABLE  0x02000000
#define IXGBE_RXCTRL_RXEN    0x00000001
#define IXGBE_TXD_CMD_EOP    0x01
#define IXGBE_TXD_CMD_IFCS   0x02
#define IXGBE_TXD_CMD_RS     0x08
#define IXGBE_TXD_STAT_DD    0x01
#define IXGBE_RXD_STAT_DD    0x01
#define IXGBE_RXD_STAT_EOP   0x02

#define IXGBE_TX_RING_SIZE   64
#define IXGBE_RX_RING_SIZE   64
#define IXGBE_RX_BUF_SIZE    2048

#define MAX_IXGBE 4

/* Legacy TX descriptor (16 bytes) */
struct ixgbe_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t vlan;
} __attribute__((packed));

/* Legacy RX descriptor (16 bytes) */
struct ixgbe_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t csum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t vlan;
} __attribute__((packed));

struct ixgbe_softc {
    struct pci_dev *pci;
    struct spinlock lock;
    struct ifnet ifn;
    volatile uint32_t *regs;
    uint8_t mac[6];
    struct ixgbe_tx_desc *tx_ring;
    struct mbuf *tx_mbufs[IXGBE_TX_RING_SIZE];
    uint16_t tx_head;
    uint16_t tx_tail;
    struct ixgbe_rx_desc *rx_ring;
    char *rx_bufs[IXGBE_RX_RING_SIZE];
    uint16_t rx_tail;
};

static struct ixgbe_softc ixgbe_devices[MAX_IXGBE];
static int ixgbe_count;

static int ixgbe_output(struct ifnet *ifp, struct mbuf *m);
static void ixgbe_poll(struct ifnet *ifp);

static struct ifnet_ops ixgbe_ifnet_ops = {
    .if_output = ixgbe_output,
    .if_poll = ixgbe_poll,
};

static int
ixgbe_match(struct pci_dev *dev)
{
    if(!dev || dev->vendor_id != IXGBE_VENDOR_INTEL)
        return 0;

    switch(dev->device_id){
    case IXGBE_DEV_82598:
    case IXGBE_DEV_82599:
    case IXGBE_DEV_X540:
    case IXGBE_DEV_X550:
        return 1;
    default:
        return 0;
    }
}

static uint32_t
ixgbe_read(struct ixgbe_softc *sc, int reg)
{
    return sc->regs[reg / 4];
}

static void
ixgbe_write(struct ixgbe_softc *sc, int reg, uint32_t val)
{
    sc->regs[reg / 4] = val;
}

static void
ixgbe_make_local_mac(struct pci_dev *dev, uint8_t mac[6])
{
    mac[0] = 0x02;
    mac[1] = dev->vendor_id & 0xFF;
    mac[2] = dev->device_id & 0xFF;
    mac[3] = dev->bus;
    mac[4] = dev->slot;
    mac[5] = dev->func;
}

static void
ixgbe_read_mac(struct ixgbe_softc *sc)
{
    uint32_t lo = ixgbe_read(sc, IXGBE_RAL0);
    uint32_t hi = ixgbe_read(sc, IXGBE_RAH0);
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
        ixgbe_make_local_mac(sc->pci, sc->mac);
}

static void
ixgbe_reset(struct ixgbe_softc *sc)
{
    ixgbe_write(sc, IXGBE_EIMC, 0x7FFFFFFF);
    ixgbe_write(sc, IXGBE_CTRL,
        ixgbe_read(sc, IXGBE_CTRL) | IXGBE_CTRL_RST);
    microdelay(10000);
    ixgbe_write(sc, IXGBE_EIMC, 0x7FFFFFFF);
}

static int
ixgbe_init_tx(struct ixgbe_softc *sc)
{
    sc->tx_ring = (struct ixgbe_tx_desc *)kalloc();
    if(!sc->tx_ring)
        return -1;
    memset(sc->tx_ring, 0,
        sizeof(struct ixgbe_tx_desc) * IXGBE_TX_RING_SIZE);
    sc->tx_head = 0;
    sc->tx_tail = 0;
    ixgbe_write(sc, IXGBE_TDBAL0, V2P(sc->tx_ring));
    ixgbe_write(sc, IXGBE_TDBAH0, 0);
    ixgbe_write(sc, IXGBE_TDLEN0,
        sizeof(struct ixgbe_tx_desc) * IXGBE_TX_RING_SIZE);
    ixgbe_write(sc, IXGBE_TDH0, 0);
    ixgbe_write(sc, IXGBE_TDT0, 0);
    ixgbe_write(sc, IXGBE_TXDCTL0, IXGBE_TXDCTL_ENABLE);
    return 0;
}

static int
ixgbe_init_rx(struct ixgbe_softc *sc)
{
    int i;
    sc->rx_ring = (struct ixgbe_rx_desc *)kalloc();
    if(!sc->rx_ring)
        return -1;
    memset(sc->rx_ring, 0,
        sizeof(struct ixgbe_rx_desc) * IXGBE_RX_RING_SIZE);
    for(i = 0; i < IXGBE_RX_RING_SIZE; i++){
        sc->rx_bufs[i] = kalloc();
        if(!sc->rx_bufs[i])
            return -1;
        sc->rx_ring[i].addr = V2P(sc->rx_bufs[i]);
    }
    sc->rx_tail = IXGBE_RX_RING_SIZE - 1;
    ixgbe_write(sc, IXGBE_RDBAL0, V2P(sc->rx_ring));
    ixgbe_write(sc, IXGBE_RDBAH0, 0);
    ixgbe_write(sc, IXGBE_RDLEN0,
        sizeof(struct ixgbe_rx_desc) * IXGBE_RX_RING_SIZE);
    ixgbe_write(sc, IXGBE_RDH0, 0);
    ixgbe_write(sc, IXGBE_RDT0, sc->rx_tail);
    ixgbe_write(sc, IXGBE_RXDCTL0, IXGBE_RXDCTL_ENABLE);
    ixgbe_write(sc, IXGBE_FCTRL, IXGBE_FCTRL_UPE | IXGBE_FCTRL_MPE);
    ixgbe_write(sc, IXGBE_HLREG0,
        IXGBE_HLREG0_TXCRCEN | IXGBE_HLREG0_RXCRCSTRIP);
    ixgbe_write(sc, IXGBE_RXCTRL, IXGBE_RXCTRL_RXEN);
    return 0;
}

static void
ixgbe_tx_complete(struct ixgbe_softc *sc)
{
    while(sc->tx_head != sc->tx_tail &&
          (sc->tx_ring[sc->tx_head].status & IXGBE_TXD_STAT_DD)){
        if(sc->tx_mbufs[sc->tx_head])
            mbuf_free(sc->tx_mbufs[sc->tx_head]);
        sc->tx_mbufs[sc->tx_head] = 0;
        sc->tx_head = (sc->tx_head + 1) % IXGBE_TX_RING_SIZE;
    }
}

static void
ixgbe_rx_complete(struct ixgbe_softc *sc)
{
    int processed = 0;
    while(processed < 32){
        uint16_t idx = (sc->rx_tail + 1) % IXGBE_RX_RING_SIZE;
        struct ixgbe_rx_desc *desc = &sc->rx_ring[idx];
        if((desc->status & IXGBE_RXD_STAT_DD) == 0)
            break;
        if((desc->status & IXGBE_RXD_STAT_EOP) && desc->errors == 0){
            uint16_t len = desc->length;
            if(len > 0 && len <= IXGBE_RX_BUF_SIZE){
                struct mbuf *m = mbuf_alloc();
                if(m){
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
        ixgbe_write(sc, IXGBE_RDT0, sc->rx_tail);
        processed++;
    }
}

static int
ixgbe_output(struct ifnet *ifp, struct mbuf *m)
{
    struct ixgbe_softc *sc = (struct ixgbe_softc *)ifp->if_softc;
    uint16_t next;
    struct ixgbe_tx_desc *desc;

    if(!sc || !m || m->len == 0)
        return -1;
    acquire(&sc->lock);
    if((ifp->if_flags & IFF_RUNNING) == 0){
        release(&sc->lock);
        return -1;
    }
    ixgbe_tx_complete(sc);
    next = (sc->tx_tail + 1) % IXGBE_TX_RING_SIZE;
    if(next == sc->tx_head){
        release(&sc->lock);
        return -1;
    }
    desc = &sc->tx_ring[sc->tx_tail];
    desc->addr   = V2P(m->data);
    desc->length = m->len;
    desc->cmd    = IXGBE_TXD_CMD_EOP | IXGBE_TXD_CMD_IFCS | IXGBE_TXD_CMD_RS;
    desc->status = 0;
    sc->tx_mbufs[sc->tx_tail] = m;
    sc->tx_tail = next;
    ixgbe_write(sc, IXGBE_TDT0, sc->tx_tail);
    release(&sc->lock);
    return 0;
}

static void
ixgbe_poll(struct ifnet *ifp)
{
    struct ixgbe_softc *sc = (struct ixgbe_softc *)ifp->if_softc;
    if(!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;
    acquire(&sc->lock);
    ixgbe_tx_complete(sc);
    ixgbe_rx_complete(sc);
    release(&sc->lock);
}

static void
ixgbe_probe(struct pci_dev *dev)
{
    struct ixgbe_softc *sc;
    uint32_t links;

    if(ixgbe_count >= MAX_IXGBE)
        return;

    sc = &ixgbe_devices[ixgbe_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "ixgbe");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "ixgbe");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    sc->regs = (volatile uint32_t *)pci_map_bar(dev, 0);
    if(!sc->regs){
        cprintf("ixgbe: failed to map BAR0\n");
        return;
    }
    ixgbe_reset(sc);
    ixgbe_read_mac(sc);
    if(ixgbe_init_tx(sc) < 0 || ixgbe_init_rx(sc) < 0){
        cprintf("ixgbe: failed to init rings\n");
        return;
    }
    links = ixgbe_read(sc, IXGBE_LINKS);

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "ixgbe0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[5] = '0' + ixgbe_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_BROADCAST;
    if(links & IXGBE_LINKS_UP)
        sc->ifn.if_flags |= IFF_RUNNING;
    memmove(sc->ifn.if_hwaddr, sc->mac, 6);
    sc->ifn.if_softc = sc;
    sc->ifn.if_ops = &ixgbe_ifnet_ops;
    sc->ifn.if_input = ether_input;

    if(if_register(&sc->ifn) < 0){
        cprintf("ixgbe: if_register failed for %x\n", dev->device_id);
        return;
    }

    cprintf("ixgbe: attached %s dev=%x irq=%d mac=%x:%x:%x:%x:%x:%x\n",
            sc->ifn.if_xname, dev->device_id, dev->irq_line,
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
    ixgbe_count++;
}

void
ixgbe_init(void)
{
    int i;

    BOOTDBG("ixgbe: probing supported PCI IDs\n");
    for(i = 0; i < pci_device_count(); i++){
        struct pci_dev *dev = pci_get_device(i);
        if(dev && ixgbe_match(dev))
            ixgbe_probe(dev);
    }
}
