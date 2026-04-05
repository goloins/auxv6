/*
 * Intel ice-family NIC driver for auxv6.
 *
 * Covers Intel E810 25/100GbE parts.
 *
 * Full TX/RX descriptor ring machinery using ice queue registers.
 * Ring base configuration requires admin-queue on real hardware.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

#define ICE_VENDOR_INTEL 0x8086
#define ICE_DEV_E810_1   0x1591
#define ICE_DEV_E810_2   0x1592
#define ICE_DEV_E810_3   0x1593

/* ice LAN TX/RX queue tail registers */
#define ICE_QTX_COMM_DBELL(q)   (0x002C0000 + (q) * 4)
#define ICE_QRX_TAIL(q)         (0x00400000 + (q) * 4)
/* Interrupt */
#define ICE_PFINT_OICR_ENA      0x0016CA00

/* TX descriptor bits (reuse i40e layout) */
#define ICE_TX_DESC_CMD_EOP    0x0001
#define ICE_TX_DESC_CMD_ICRC   0x0004
#define ICE_TX_DESC_CMD_RS     0x0008
#define ICE_TX_DESC_DTYPE_DONE 0xF
/* RX descriptor status */
#define ICE_RX_DESC_STATUS_DD  0x0001
#define ICE_RX_DESC_STATUS_EOP 0x0002

#define ICE_TX_RING_SIZE  64
#define ICE_RX_RING_SIZE  64
#define ICE_RX_BUF_SIZE   2048

#define MAX_ICE 4

/* TX data descriptor (16 bytes) */
struct ice_tx_desc {
    uint64_t buf_addr;
    uint64_t cmd_type_offset_bsz;
} __attribute__((packed));

/* RX descriptor read format (16 bytes) */
struct ice_rx_desc {
    uint64_t pkt_addr;
    uint64_t hdr_addr;
} __attribute__((packed));

/* RX write-back format */
struct ice_rx_wb {
    uint64_t filter_status;
    uint64_t qword1;   /* bit[0]=DD; bits[38:25]=pkt_len */
} __attribute__((packed));

struct ice_softc {
    struct pci_dev *pci;
    struct spinlock lock;
    struct ifnet ifn;
    volatile uint32_t *regs;
    uint8_t mac[6];
    struct ice_tx_desc *tx_ring;
    struct mbuf        *tx_mbufs[ICE_TX_RING_SIZE];
    uint16_t            tx_head;
    uint16_t            tx_tail;
    struct ice_rx_desc *rx_ring;
    char               *rx_bufs[ICE_RX_RING_SIZE];
    uint16_t            rx_tail;
};

static struct ice_softc ice_devices[MAX_ICE];
static int ice_count;

static int ice_output(struct ifnet *ifp, struct mbuf *m);
static void ice_poll(struct ifnet *ifp);

static struct ifnet_ops ice_ifnet_ops = {
    .if_output = ice_output,
    .if_poll = ice_poll,
};

static int
ice_match(struct pci_dev *dev)
{
    if(!dev || dev->vendor_id != ICE_VENDOR_INTEL)
        return 0;

    switch(dev->device_id){
    case ICE_DEV_E810_1:
    case ICE_DEV_E810_2:
    case ICE_DEV_E810_3:
        return 1;
    default:
        return 0;
    }
}

static uint32_t
ice_read(struct ice_softc *sc, int reg)
{
    return sc->regs[reg / 4];
}

static void
ice_write(struct ice_softc *sc, int reg, uint32_t val)
{
    sc->regs[reg / 4] = val;
}

static void
ice_make_local_mac(struct pci_dev *dev, uint8_t mac[6])
{
    mac[0] = 0x02;
    mac[1] = dev->vendor_id & 0xFF;
    mac[2] = dev->device_id & 0xFF;
    mac[3] = dev->bus;
    mac[4] = dev->slot;
    mac[5] = dev->func;
}

static void
ice_read_mac(struct ice_softc *sc)
{
    /* ice MAC is programmed via admin queue; use deterministic local */
    ice_make_local_mac(sc->pci, sc->mac);
    (void)ice_read;
}

static int
ice_init_tx(struct ice_softc *sc)
{
    sc->tx_ring = (struct ice_tx_desc *)kalloc();
    if(!sc->tx_ring)
        return -1;
    memset(sc->tx_ring, 0, sizeof(struct ice_tx_desc) * ICE_TX_RING_SIZE);
    sc->tx_head = 0;
    sc->tx_tail = 0;
    ice_write(sc, ICE_QTX_COMM_DBELL(0), 0);
    return 0;
}

static int
ice_init_rx(struct ice_softc *sc)
{
    int i;
    sc->rx_ring = (struct ice_rx_desc *)kalloc();
    if(!sc->rx_ring)
        return -1;
    memset(sc->rx_ring, 0, sizeof(struct ice_rx_desc) * ICE_RX_RING_SIZE);
    for(i = 0; i < ICE_RX_RING_SIZE; i++){
        sc->rx_bufs[i] = kalloc();
        if(!sc->rx_bufs[i])
            return -1;
        sc->rx_ring[i].pkt_addr = V2P(sc->rx_bufs[i]);
        sc->rx_ring[i].hdr_addr = 0;
    }
    sc->rx_tail = ICE_RX_RING_SIZE - 1;
    ice_write(sc, ICE_QRX_TAIL(0), sc->rx_tail);
    return 0;
}

static void
ice_tx_complete(struct ice_softc *sc)
{
    while(sc->tx_head != sc->tx_tail){
        struct ice_tx_desc *desc = &sc->tx_ring[sc->tx_head];
        if((uint8_t)(desc->cmd_type_offset_bsz & 0xF) != ICE_TX_DESC_DTYPE_DONE)
            break;
        if(sc->tx_mbufs[sc->tx_head]){
            mbuf_free(sc->tx_mbufs[sc->tx_head]);
            sc->tx_mbufs[sc->tx_head] = 0;
        }
        sc->tx_head = (sc->tx_head + 1) % ICE_TX_RING_SIZE;
    }
}

static void
ice_rx_complete(struct ice_softc *sc)
{
    int processed = 0;
    while(processed < 32){
        uint16_t idx = (sc->rx_tail + 1) % ICE_RX_RING_SIZE;
        struct ice_rx_wb *wb = (struct ice_rx_wb *)&sc->rx_ring[idx];
        uint64_t qword1 = wb->qword1;
        uint16_t len;
        if((qword1 & ICE_RX_DESC_STATUS_DD) == 0)
            break;
        len = (uint16_t)((qword1 >> 38) & 0x3FFF);
        if((qword1 & ICE_RX_DESC_STATUS_EOP) && len > 0 &&
           len <= ICE_RX_BUF_SIZE){
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
        sc->rx_ring[idx].pkt_addr = V2P(sc->rx_bufs[idx]);
        sc->rx_ring[idx].hdr_addr = 0;
        sc->rx_tail = idx;
        ice_write(sc, ICE_QRX_TAIL(0), sc->rx_tail);
        processed++;
    }
}

static int
ice_output(struct ifnet *ifp, struct mbuf *m)
{
    struct ice_softc *sc = (struct ice_softc *)ifp->if_softc;
    uint16_t next;
    struct ice_tx_desc *desc;
    uint64_t cmd;

    if(!sc || !m || m->len == 0)
        return -1;
    acquire(&sc->lock);
    if((ifp->if_flags & IFF_RUNNING) == 0){
        release(&sc->lock);
        return -1;
    }
    ice_tx_complete(sc);
    next = (sc->tx_tail + 1) % ICE_TX_RING_SIZE;
    if(next == sc->tx_head){
        release(&sc->lock);
        return -1;
    }
    desc = &sc->tx_ring[sc->tx_tail];
    desc->buf_addr = V2P(m->data);
    cmd = ((uint64_t)m->len << 34) |
          ((uint64_t)(ICE_TX_DESC_CMD_EOP | ICE_TX_DESC_CMD_ICRC |
                      ICE_TX_DESC_CMD_RS) << 4);
    desc->cmd_type_offset_bsz = cmd;
    sc->tx_mbufs[sc->tx_tail] = m;
    sc->tx_tail = next;
    ice_write(sc, ICE_QTX_COMM_DBELL(0), sc->tx_tail);
    release(&sc->lock);
    return 0;
}

static void
ice_poll(struct ifnet *ifp)
{
    struct ice_softc *sc = (struct ice_softc *)ifp->if_softc;
    if(!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;
    acquire(&sc->lock);
    ice_tx_complete(sc);
    ice_rx_complete(sc);
    release(&sc->lock);
}

static void
ice_probe(struct pci_dev *dev)
{
    struct ice_softc *sc;

    if(ice_count >= MAX_ICE)
        return;

    sc = &ice_devices[ice_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "ice");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "ice");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    sc->regs = (volatile uint32_t *)pci_map_bar(dev, 0);
    if(!sc->regs){
        cprintf("ice: failed to map BAR0\n");
        return;
    }
    ice_read_mac(sc);
    if(ice_init_tx(sc) < 0 || ice_init_rx(sc) < 0){
        cprintf("ice: failed to init rings\n");
        return;
    }

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "ice0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[3] = '0' + ice_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_RUNNING | IFF_BROADCAST;
    memmove(sc->ifn.if_hwaddr, sc->mac, 6);
    sc->ifn.if_softc = sc;
    sc->ifn.if_ops = &ice_ifnet_ops;
    sc->ifn.if_input = ether_input;

    if(if_register(&sc->ifn) < 0){
        cprintf("ice: if_register failed for %x\n", dev->device_id);
        return;
    }

    cprintf("ice: attached %s dev=%x irq=%d mac=%x:%x:%x:%x:%x:%x\n",
            sc->ifn.if_xname, dev->device_id, dev->irq_line,
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
    ice_count++;
}

void
ice_init(void)
{
    int i;

    BOOTDBG("ice: probing supported PCI IDs\n");
    for(i = 0; i < pci_device_count(); i++){
        struct pci_dev *dev = pci_get_device(i);
        if(dev && ice_match(dev))
            ice_probe(dev);
    }
}
