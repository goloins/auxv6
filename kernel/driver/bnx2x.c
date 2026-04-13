/*
 * Broadcom bnx2x-family NIC driver for auxv6.
 *
 * Covers BCM57710/57711/57712 NetXtreme II 10GbE parts.
 *
 * TX/RX buffer descriptor rings; status-block based completions.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

#define BNX2X_VENDOR_BROADCOM 0x14E4
#define BNX2X_DEV_57710       0x164E
#define BNX2X_DEV_57711       0x164F
#define BNX2X_DEV_57712       0x1662

/* EMAC MAC address registers */
#define BNX2X_NIG_INGRESS_BMAC0_MEM  0x00014000  /* BAR0 NIG base */
/* Simplified: read MAC from NIG STAT registers or fall back to local */
/* Port 0 EMAC0 MAC address: lower/upper 32-bit words */
#define BNX2X_EMAC0_MAC_0            0x00008300
#define BNX2X_EMAC0_MAC_1            0x00008304
/* Misc / reset */
#define BNX2X_MISC_RESET_STEP        0x00008000
/* Host Coalescing status block */
#define BNX2X_HC_REG_CONFIG_0        0x00108000
#define BNX2X_HC_STATUS_ADDR_L_0     0x00108030
#define BNX2X_HC_STATUS_ADDR_H_0     0x00108034
/* Fast-path TX queue registers (simplified, function 0 queue 0) */
#define BNX2X_TSDM_INT_MASK_0        0x000422c0
/* TX/RX descriptor sizes */
#define BNX2X_TX_RING_SIZE    64
#define BNX2X_RX_RING_SIZE    64
#define BNX2X_RX_BUF_SIZE     2048

#define MAX_BNX2X 4

/* TX buffer descriptor (16 bytes, same layout as bnx2) */
struct bnx2x_tx_bd {
    uint32_t addr_hi;
    uint32_t addr_lo;
    uint32_t nbytes_flags;
    uint32_t vlan;
} __attribute__((packed));

/* RX buffer descriptor (16 bytes) */
struct bnx2x_rx_bd {
    uint32_t addr_hi;
    uint32_t addr_lo;
    uint32_t len;
    uint32_t flags;
} __attribute__((packed));

/* Receive completion (16 bytes) – hardware fills this per received packet */
struct bnx2x_rx_cqe {
    uint32_t type_error_flags;  /* bit 0 = packet ready */
    uint32_t rss_hash;
    uint16_t pkt_len;
    uint16_t vlan_tag;
    uint32_t bd_index;          /* which RX BD holds the data */
} __attribute__((packed));

/* Simplified status block */
struct bnx2x_sb {
    volatile uint32_t attn;
    volatile uint32_t attn_ack;
    volatile uint16_t index;
    volatile uint16_t unused;
    volatile uint16_t rx_cons;
    volatile uint16_t unused2;
    volatile uint16_t tx_cons;
    volatile uint16_t unused3;
} __attribute__((packed));

struct bnx2x_softc {
    struct pci_dev *pci;
    struct spinlock lock;
    struct ifnet ifn;
    volatile uint32_t *regs;
    uint8_t mac[6];
    struct bnx2x_sb     *sb;
    struct bnx2x_tx_bd  *tx_ring;
    struct mbuf         *tx_mbufs[BNX2X_TX_RING_SIZE];
    uint16_t             tx_prod;
    uint16_t             tx_cons;
    struct bnx2x_rx_bd  *rx_ring;
    struct bnx2x_rx_cqe *rx_cqe;
    char                *rx_bufs[BNX2X_RX_RING_SIZE];
    uint16_t             rx_prod;
    uint16_t             rx_cons;
};

static struct bnx2x_softc bnx2x_devices[MAX_BNX2X];
static int bnx2x_count;

static int bnx2x_output(struct ifnet *ifp, struct mbuf *m);
static void bnx2x_poll(struct ifnet *ifp);

static struct ifnet_ops bnx2x_ifnet_ops = {
    .if_output = bnx2x_output,
    .if_poll = bnx2x_poll,
};

static int
bnx2x_match(struct pci_dev *dev)
{
    if(!dev || dev->vendor_id != BNX2X_VENDOR_BROADCOM)
        return 0;

    switch(dev->device_id){
    case BNX2X_DEV_57710:
    case BNX2X_DEV_57711:
    case BNX2X_DEV_57712:
        return 1;
    default:
        return 0;
    }
}

static uint32_t
bnx2x_read(struct bnx2x_softc *sc, int reg)
{
    return sc->regs[reg / 4];
}

static void
bnx2x_write(struct bnx2x_softc *sc, int reg, uint32_t val)
{
    sc->regs[reg / 4] = val;
}

static void
bnx2x_make_local_mac(struct pci_dev *dev, uint8_t mac[6])
{
    mac[0] = 0x02;
    mac[1] = dev->vendor_id & 0xFF;
    mac[2] = dev->device_id & 0xFF;
    mac[3] = dev->bus;
    mac[4] = dev->slot;
    mac[5] = dev->func;
}

static void
bnx2x_read_mac(struct bnx2x_softc *sc)
{
    /* bnx2x EMAC0 MAC registers share the bnx2 big-endian layout */
    uint32_t lo = bnx2x_read(sc, BNX2X_EMAC0_MAC_0);
    uint32_t hi = bnx2x_read(sc, BNX2X_EMAC0_MAC_1);
    int i, all0 = 1, allf = 1;
    sc->mac[0] = (lo >> 8) & 0xFF;
    sc->mac[1] = lo & 0xFF;
    sc->mac[2] = (hi >> 24) & 0xFF;
    sc->mac[3] = (hi >> 16) & 0xFF;
    sc->mac[4] = (hi >> 8) & 0xFF;
    sc->mac[5] = hi & 0xFF;
    for(i = 0; i < 6; i++){
        if(sc->mac[i] != 0x00) all0 = 0;
        if(sc->mac[i] != 0xFF) allf = 0;
    }
    if(all0 || allf || (sc->mac[0] & 1))
        bnx2x_make_local_mac(sc->pci, sc->mac);
}

static void
bnx2x_reset(struct bnx2x_softc *sc)
{
    /* Mask all interrupts; chip reset done via MISC block */
    bnx2x_write(sc, BNX2X_TSDM_INT_MASK_0, 0xFFFFFFFF);
    microdelay(10000);
}

static int
bnx2x_init_tx(struct bnx2x_softc *sc)
{
    sc->tx_ring = (struct bnx2x_tx_bd *)kalloc();
    if(!sc->tx_ring)
        return -1;
    memset(sc->tx_ring, 0, sizeof(struct bnx2x_tx_bd) * BNX2X_TX_RING_SIZE);
    sc->tx_prod = 0;
    sc->tx_cons = 0;
    return 0;
}

static int
bnx2x_init_rx(struct bnx2x_softc *sc)
{
    int i;
    sc->rx_ring = (struct bnx2x_rx_bd *)kalloc();
    if(!sc->rx_ring)
        return -1;
    sc->rx_cqe = (struct bnx2x_rx_cqe *)kalloc();
    if(!sc->rx_cqe)
        return -1;
    memset(sc->rx_ring, 0, sizeof(struct bnx2x_rx_bd) * BNX2X_RX_RING_SIZE);
    memset(sc->rx_cqe, 0, sizeof(struct bnx2x_rx_cqe) * BNX2X_RX_RING_SIZE);
    for(i = 0; i < BNX2X_RX_RING_SIZE; i++){
        sc->rx_bufs[i] = kalloc();
        if(!sc->rx_bufs[i])
            return -1;
        sc->rx_ring[i].addr_hi = 0;
        sc->rx_ring[i].addr_lo = V2P(sc->rx_bufs[i]);
        sc->rx_ring[i].len = BNX2X_RX_BUF_SIZE;
    }
    sc->rx_prod = BNX2X_RX_RING_SIZE;
    sc->rx_cons = 0;
    return 0;
}

static void
bnx2x_tx_complete(struct bnx2x_softc *sc)
{
    uint16_t hw_cons = sc->sb->tx_cons;
    while(sc->tx_cons != hw_cons){
        uint16_t idx = sc->tx_cons % BNX2X_TX_RING_SIZE;
        if(sc->tx_mbufs[idx]){
            mbuf_free(sc->tx_mbufs[idx]);
            sc->tx_mbufs[idx] = 0;
        }
        sc->tx_cons = (sc->tx_cons + 1) % BNX2X_TX_RING_SIZE;
    }
}

static void
bnx2x_rx_complete(struct bnx2x_softc *sc)
{
    int processed = 0;
    while(processed < 32){
        uint16_t idx = sc->rx_cons % BNX2X_RX_RING_SIZE;
        struct bnx2x_rx_cqe *cqe = &sc->rx_cqe[idx];
        uint16_t len;
        if((cqe->type_error_flags & 1) == 0)
            break;
        len = cqe->pkt_len;
        if(len > 0 && len <= BNX2X_RX_BUF_SIZE){
            uint32_t bd_idx = cqe->bd_index % BNX2X_RX_RING_SIZE;
            struct mbuf *m = mbuf_alloc();
            if(m){
                memmove(m->data, sc->rx_bufs[bd_idx], len);
                m->len = len;
                m->rcvif = &sc->ifn;
                release(&sc->lock);
                if_input(&sc->ifn, m);
                acquire(&sc->lock);
            }
        }
        cqe->type_error_flags = 0;
        sc->rx_cons = (sc->rx_cons + 1) % BNX2X_RX_RING_SIZE;
        sc->rx_prod = (sc->rx_prod + 1) % BNX2X_RX_RING_SIZE;
        processed++;
    }
}

static int
bnx2x_output(struct ifnet *ifp, struct mbuf *m)
{
    struct bnx2x_softc *sc = (struct bnx2x_softc *)ifp->if_softc;
    uint16_t next;
    struct bnx2x_tx_bd *bd;

    if(!sc || !m || m->len == 0)
        return -1;
    acquire(&sc->lock);
    if((ifp->if_flags & IFF_RUNNING) == 0){
        release(&sc->lock);
        return -1;
    }
    bnx2x_tx_complete(sc);
    next = (sc->tx_prod + 1) % BNX2X_TX_RING_SIZE;
    if(next == sc->tx_cons){
        release(&sc->lock);
        return -1;
    }
    bd = &sc->tx_ring[sc->tx_prod];
    bd->addr_hi      = 0;
    bd->addr_lo      = V2P(m->data);
    bd->nbytes_flags = (uint32_t)m->len & 0xFFFF;
    bd->vlan         = 0;
    sc->tx_mbufs[sc->tx_prod] = m;
    sc->tx_prod = next;
    release(&sc->lock);
    return 0;
}

static void
bnx2x_poll(struct ifnet *ifp)
{
    struct bnx2x_softc *sc = (struct bnx2x_softc *)ifp->if_softc;
    if(!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;
    acquire(&sc->lock);
    bnx2x_tx_complete(sc);
    bnx2x_rx_complete(sc);
    release(&sc->lock);
}

static void
bnx2x_probe(struct pci_dev *dev)
{
    struct bnx2x_softc *sc;

    if(bnx2x_count >= MAX_BNX2X)
        return;

    sc = &bnx2x_devices[bnx2x_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "bnx2x");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "bnx2x");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    sc->regs = (volatile uint32_t *)pci_map_bar(dev, 0);
    if(!sc->regs){
        cprintf("bnx2x: failed to map BAR0\n");
        return;
    }
    bnx2x_reset(sc);
    bnx2x_read_mac(sc);
    sc->sb = (struct bnx2x_sb *)kalloc();
    if(!sc->sb){
        cprintf("bnx2x: failed to alloc status block\n");
        return;
    }
    memset(sc->sb, 0, sizeof(*sc->sb));
    bnx2x_write(sc, BNX2X_HC_STATUS_ADDR_L_0, V2P(sc->sb));
    bnx2x_write(sc, BNX2X_HC_STATUS_ADDR_H_0, 0);
    if(bnx2x_init_tx(sc) < 0 || bnx2x_init_rx(sc) < 0){
        cprintf("bnx2x: failed to init rings\n");
        return;
    }

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "bnx2x0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[5] = '0' + bnx2x_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_RUNNING | IFF_BROADCAST;
    memmove(sc->ifn.if_hwaddr, sc->mac, 6);
    sc->ifn.if_softc = sc;
    sc->ifn.if_ops = &bnx2x_ifnet_ops;
    sc->ifn.if_input = ether_input;

    if(if_register(&sc->ifn) < 0){
        cprintf("bnx2x: if_register failed for %x\n", dev->device_id);
        return;
    }

    cprintf("bnx2x: attached %s dev=%x irq=%d mac=%x:%x:%x:%x:%x:%x\n",
            sc->ifn.if_xname, dev->device_id, dev->irq_line,
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
    bnx2x_count++;
}

void
bnx2x_init(void)
{
    int i;

    BOOTDBG("bnx2x: probing supported PCI IDs\n");
    for(i = 0; i < pci_device_count(); i++){
        struct pci_dev *dev = pci_get_device(i);
        if(dev && bnx2x_match(dev))
            bnx2x_probe(dev);
    }
}
