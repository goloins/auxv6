/*
 * Intel igb-family NIC driver for auxv6.
 *
 * Covers common 1GbE parts from 2007-2014 era:
 * 82575/82576/82580, I350, I210, I211.
 *
 * Full descriptor-ring TX/RX.  Register layout is identical to the e1000
 * class (same as I219).
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

#define IGB_VENDOR_INTEL 0x8086
#define IGB_DEV_82575    0x10A7
#define IGB_DEV_82576    0x10C9
#define IGB_DEV_82580    0x150E
#define IGB_DEV_I350     0x1521
#define IGB_DEV_I210     0x1533
#define IGB_DEV_I211     0x1539

/* e1000-class MMIO register offsets */
#define IGB_CTRL    0x00000
#define IGB_STATUS  0x00008
#define IGB_IMC     0x000D8
#define IGB_RCTL    0x00100
#define IGB_TCTL    0x00400
#define IGB_TDBAL   0x03800
#define IGB_TDBAH   0x03804
#define IGB_TDLEN   0x03808
#define IGB_TDH     0x03810
#define IGB_TDT     0x03818
#define IGB_RDBAL   0x02800
#define IGB_RDBAH   0x02804
#define IGB_RDLEN   0x02808
#define IGB_RDH     0x02810
#define IGB_RDT     0x02818
#define IGB_MTA     0x05200
#define IGB_RAL0    0x05400
#define IGB_RAH0    0x05404

#define IGB_CTRL_SLU    0x00000040
#define IGB_CTRL_ASDE   0x00000020
#define IGB_CTRL_RST    0x04000000
#define IGB_STATUS_LU   0x00000002
#define IGB_RCTL_EN     0x00000002
#define IGB_RCTL_BAM    0x00008000
#define IGB_RCTL_SECRC  0x04000000
#define IGB_TCTL_EN     0x00000002
#define IGB_TCTL_PSP    0x00000008
#define IGB_TXD_CMD_EOP  0x01
#define IGB_TXD_CMD_IFCS 0x02
#define IGB_TXD_CMD_RS   0x08
#define IGB_TXD_STAT_DD  0x01
#define IGB_RXD_STAT_DD  0x01
#define IGB_RXD_STAT_EOP 0x02

#define IGB_TX_RING_SIZE 64
#define IGB_RX_RING_SIZE 64
#define IGB_RX_BUF_SIZE  2048

#define MAX_IGB 4

struct igb_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed));

struct igb_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

struct igb_softc {
    struct pci_dev *pci;
    struct spinlock lock;
    struct ifnet ifn;
    volatile uint32_t *regs;
    uint8_t mac[6];
    struct igb_tx_desc *tx_ring;
    struct mbuf *tx_mbufs[IGB_TX_RING_SIZE];
    uint16_t tx_head;
    uint16_t tx_tail;
    struct igb_rx_desc *rx_ring;
    char *rx_bufs[IGB_RX_RING_SIZE];
    uint16_t rx_tail;
};

static struct igb_softc igb_devices[MAX_IGB];
static int igb_count;

static int igb_output(struct ifnet *ifp, struct mbuf *m);
static void igb_poll(struct ifnet *ifp);

static struct ifnet_ops igb_ifnet_ops = {
    .if_output = igb_output,
    .if_poll = igb_poll,
};

static int
igb_match(struct pci_dev *dev)
{
    if(!dev || dev->vendor_id != IGB_VENDOR_INTEL)
        return 0;

    switch(dev->device_id){
    case IGB_DEV_82575:
    case IGB_DEV_82576:
    case IGB_DEV_82580:
    case IGB_DEV_I350:
    case IGB_DEV_I210:
    case IGB_DEV_I211:
        return 1;
    default:
        return 0;
    }
}

static uint32_t
igb_read(struct igb_softc *sc, int reg)
{
    return sc->regs[reg / 4];
}

static void
igb_write(struct igb_softc *sc, int reg, uint32_t val)
{
    sc->regs[reg / 4] = val;
}

static void
igb_make_local_mac(struct pci_dev *dev, uint8_t mac[6])
{
    mac[0] = 0x02;
    mac[1] = dev->vendor_id & 0xFF;
    mac[2] = dev->device_id & 0xFF;
    mac[3] = dev->bus;
    mac[4] = dev->slot;
    mac[5] = dev->func;
}

static void
igb_read_mac(struct igb_softc *sc)
{
    uint32_t ral = igb_read(sc, IGB_RAL0);
    uint32_t rah = igb_read(sc, IGB_RAH0);
    int i, all0 = 1, allf = 1;

    sc->mac[0] = ral & 0xFF;
    sc->mac[1] = (ral >> 8) & 0xFF;
    sc->mac[2] = (ral >> 16) & 0xFF;
    sc->mac[3] = (ral >> 24) & 0xFF;
    sc->mac[4] = rah & 0xFF;
    sc->mac[5] = (rah >> 8) & 0xFF;

    for(i = 0; i < 6; i++){
        if(sc->mac[i] != 0x00) all0 = 0;
        if(sc->mac[i] != 0xFF) allf = 0;
    }
    if(all0 || allf || (sc->mac[0] & 1))
        igb_make_local_mac(sc->pci, sc->mac);
}

static void
igb_reset(struct igb_softc *sc)
{
    igb_write(sc, IGB_IMC, 0xFFFFFFFF);
    igb_write(sc, IGB_CTRL, igb_read(sc, IGB_CTRL) | IGB_CTRL_RST);
    microdelay(10000);
    igb_write(sc, IGB_IMC, 0xFFFFFFFF);
}

static int
igb_init_tx(struct igb_softc *sc)
{
    sc->tx_ring = (struct igb_tx_desc *)kalloc();
    if(!sc->tx_ring)
        return -1;
    memset(sc->tx_ring, 0, sizeof(struct igb_tx_desc) * IGB_TX_RING_SIZE);
    sc->tx_head = 0;
    sc->tx_tail = 0;
    igb_write(sc, IGB_TDBAL, V2P(sc->tx_ring));
    igb_write(sc, IGB_TDBAH, 0);
    igb_write(sc, IGB_TDLEN, sizeof(struct igb_tx_desc) * IGB_TX_RING_SIZE);
    igb_write(sc, IGB_TDH, 0);
    igb_write(sc, IGB_TDT, 0);
    igb_write(sc, IGB_TCTL, IGB_TCTL_EN | IGB_TCTL_PSP | (0x10 << 4) | (0x40 << 12));
    return 0;
}

static int
igb_init_rx(struct igb_softc *sc)
{
    int i;
    sc->rx_ring = (struct igb_rx_desc *)kalloc();
    if(!sc->rx_ring)
        return -1;
    memset(sc->rx_ring, 0, sizeof(struct igb_rx_desc) * IGB_RX_RING_SIZE);
    for(i = 0; i < IGB_RX_RING_SIZE; i++){
        sc->rx_bufs[i] = kalloc();
        if(!sc->rx_bufs[i])
            return -1;
        sc->rx_ring[i].addr = V2P(sc->rx_bufs[i]);
    }
    sc->rx_tail = IGB_RX_RING_SIZE - 1;
    igb_write(sc, IGB_RDBAL, V2P(sc->rx_ring));
    igb_write(sc, IGB_RDBAH, 0);
    igb_write(sc, IGB_RDLEN, sizeof(struct igb_rx_desc) * IGB_RX_RING_SIZE);
    igb_write(sc, IGB_RDH, 0);
    igb_write(sc, IGB_RDT, sc->rx_tail);
    for(i = 0; i < 128; i++)
        igb_write(sc, IGB_MTA + i * 4, 0);
    igb_write(sc, IGB_RCTL, IGB_RCTL_EN | IGB_RCTL_BAM | IGB_RCTL_SECRC);
    return 0;
}

static void
igb_tx_complete(struct igb_softc *sc)
{
    while(sc->tx_head != sc->tx_tail &&
          (sc->tx_ring[sc->tx_head].status & IGB_TXD_STAT_DD)){
        if(sc->tx_mbufs[sc->tx_head])
            mbuf_free(sc->tx_mbufs[sc->tx_head]);
        sc->tx_mbufs[sc->tx_head] = 0;
        sc->tx_head = (sc->tx_head + 1) % IGB_TX_RING_SIZE;
    }
}

static void
igb_rx_complete(struct igb_softc *sc)
{
    int processed = 0;
    while(processed < 32){
        uint16_t idx = (sc->rx_tail + 1) % IGB_RX_RING_SIZE;
        struct igb_rx_desc *desc = &sc->rx_ring[idx];
        if((desc->status & IGB_RXD_STAT_DD) == 0)
            break;
        if((desc->status & IGB_RXD_STAT_EOP) && desc->errors == 0){
            uint16_t len = desc->length;
            if(len > 0 && len <= IGB_RX_BUF_SIZE){
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
        igb_write(sc, IGB_RDT, sc->rx_tail);
        processed++;
    }
}

static int
igb_output(struct ifnet *ifp, struct mbuf *m)
{
    struct igb_softc *sc = (struct igb_softc *)ifp->if_softc;
    uint16_t next;
    struct igb_tx_desc *desc;

    if(!sc || !m || m->len == 0)
        return -1;

    acquire(&sc->lock);
    if((ifp->if_flags & IFF_RUNNING) == 0){
        release(&sc->lock);
        return -1;
    }
    igb_tx_complete(sc);
    next = (sc->tx_tail + 1) % IGB_TX_RING_SIZE;
    if(next == sc->tx_head){
        release(&sc->lock);
        return -1;
    }
    desc = &sc->tx_ring[sc->tx_tail];
    desc->addr   = V2P(m->data);
    desc->length = m->len;
    desc->cmd    = IGB_TXD_CMD_EOP | IGB_TXD_CMD_IFCS | IGB_TXD_CMD_RS;
    desc->status = 0;
    sc->tx_mbufs[sc->tx_tail] = m;
    sc->tx_tail = next;
    igb_write(sc, IGB_TDT, sc->tx_tail);
    release(&sc->lock);
    return 0;
}

static void
igb_poll(struct ifnet *ifp)
{
    struct igb_softc *sc = (struct igb_softc *)ifp->if_softc;
    if(!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;
    acquire(&sc->lock);
    igb_tx_complete(sc);
    igb_rx_complete(sc);
    release(&sc->lock);
}

static void
igb_probe(struct pci_dev *dev)
{
    struct igb_softc *sc;
    uint32_t status;

    if(igb_count >= MAX_IGB)
        return;

    sc = &igb_devices[igb_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "igb");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "igb");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    sc->regs = (volatile uint32_t *)pci_map_bar(dev, 0);
    if(!sc->regs){
        cprintf("igb: failed to map BAR0\n");
        return;
    }
    igb_reset(sc);
    igb_read_mac(sc);
    if(igb_init_tx(sc) < 0 || igb_init_rx(sc) < 0){
        cprintf("igb: failed to init rings\n");
        return;
    }
    igb_write(sc, IGB_CTRL, igb_read(sc, IGB_CTRL) | IGB_CTRL_SLU | IGB_CTRL_ASDE);
    status = igb_read(sc, IGB_STATUS);

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "igb0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[3] = '0' + igb_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_BROADCAST;
    if(status & IGB_STATUS_LU)
        sc->ifn.if_flags |= IFF_RUNNING;
    memmove(sc->ifn.if_hwaddr, sc->mac, 6);
    sc->ifn.if_softc = sc;
    sc->ifn.if_ops = &igb_ifnet_ops;
    sc->ifn.if_input = ether_input;

    if(if_register(&sc->ifn) < 0){
        cprintf("igb: if_register failed for %x\n", dev->device_id);
        return;
    }

    cprintf("igb: attached %s dev=%x irq=%d mac=%x:%x:%x:%x:%x:%x\n",
            sc->ifn.if_xname, dev->device_id, dev->irq_line,
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
    igb_count++;
}

void
igb_init(void)
{
    int i;

    BOOTDBG("igb: probing supported PCI IDs\n");
    for(i = 0; i < pci_device_count(); i++){
        struct pci_dev *dev = pci_get_device(i);
        if(dev && igb_match(dev))
            igb_probe(dev);
    }
}
