/*
 * Broadcom bnx2-family NIC driver for auxv6.
 *
 * Covers BCM5706/5708/5709 NetXtreme II 1GbE parts.
 *
 * TX/RX buffer descriptor rings set up via the context-indirect register
 * interface (5706/5708 path).  Completions via the host status block.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

#define BNX2_VENDOR_BROADCOM 0x14E4
#define BNX2_DEV_5706        0x164A
#define BNX2_DEV_5708        0x164C
#define BNX2_DEV_5709        0x1639

/* BAR0 direct registers */
#define BNX2_EMAC_MAC_MATCH0         0x00000400  /* station addr word 0 */
#define BNX2_EMAC_MAC_MATCH1         0x00000404  /* station addr word 1 */
#define BNX2_PCICFG_MISC_CONFIG      0x00006808
#define   BNX2_PCICFG_MISC_RST_REQ  0x00000001
#define   BNX2_PCICFG_MISC_RST_BSY  0x00000002
#define   BNX2_PCICFG_WIN_ENA       0x00000800
/* Host Coalescing: status block base address */
#define BNX2_HC_STATUS_ADDR_L        0x00000c00
#define BNX2_HC_STATUS_ADDR_H        0x00000c04
#define BNX2_HC_ENABLED              0x00000c08
/* Context indirect access */
#define BNX2_CTX_DATA_ADR            0x00000044
#define BNX2_CTX_DATA                0x0000004c
/* L2 send context offsets (within CID 0x10 context, base = 0x10*0x80 = 0x800) */
#define BNX2_SEND_CID                0x10
#define BNX2_L2CTX_TBDR_BHADDR_HI   0x1a4
#define BNX2_L2CTX_TBDR_BHADDR_LO   0x1a8
#define BNX2_L2CTX_HOST_BIDX         0x1b4
/* L2 recv context offsets (CID 0x30, base = 0x30*0x80 = 0x1800) */
#define BNX2_RECV_CID                0x30
#define BNX2_L2CTX_NX_BDHADDR_HI    0x08
#define BNX2_L2CTX_NX_BDHADDR_LO    0x0c
#define BNX2_L2CTX_HOST_BDIDX        0x04
/* TX buffer descriptor flags */
#define BNX2_TX_BD_FLAGS_END         0x00200000
#define BNX2_TX_BD_FLAGS_START       0x00400000
/* RX buffer descriptor flags */
#define BNX2_RX_BD_FLAGS_START       0x04
#define BNX2_RX_BD_FLAGS_END         0x02
/* L2 frame header status – written by HW at start of each RX buffer */
#define BNX2_L2FH_STATUS_RX_OK       0x2000

#define BNX2_TX_RING_SIZE    64
#define BNX2_RX_RING_SIZE    64
#define BNX2_RX_BUF_SIZE     2048

#define MAX_BNX2 4

/* TX buffer descriptor (16 bytes) */
struct bnx2_tx_bd {
    uint32_t addr_hi;
    uint32_t addr_lo;
    uint32_t nbytes_flags;  /* [15:0]=len, [31:16]=flags */
    uint32_t vlan;
} __attribute__((packed));

/* RX buffer descriptor (16 bytes) */
struct bnx2_rx_bd {
    uint32_t addr_hi;
    uint32_t addr_lo;
    uint32_t len;
    uint32_t flags;
} __attribute__((packed));

/* L2 frame header – HW prepends this to each received packet */
struct bnx2_l2fhdr {
    uint32_t status;
    uint32_t hash;
    uint16_t pkt_len;
    uint16_t vlan_tag;
    uint16_t ip_xsum;
    uint16_t tcp_xsum;
} __attribute__((packed));

/* Simplified status block (part we care about) */
struct bnx2_status_block {
    volatile uint32_t attn_bits;
    volatile uint32_t attn_bits_ack;
    volatile uint16_t status_idx;
    volatile uint16_t unused0;
    volatile uint16_t rx_cons;    /* HW RX BD consumer */
    volatile uint16_t unused1;
    volatile uint16_t tx_cons;    /* HW TX BD consumer */
    volatile uint16_t unused2;
} __attribute__((packed));

struct bnx2_softc {
    struct pci_dev *pci;
    struct spinlock lock;
    struct ifnet ifn;
    volatile uint32_t *regs;
    uint8_t mac[6];
    struct bnx2_status_block *sb;
    struct bnx2_tx_bd *tx_ring;
    struct mbuf *tx_mbufs[BNX2_TX_RING_SIZE];
    uint16_t tx_prod;
    uint16_t tx_cons;
    struct bnx2_rx_bd *rx_ring;
    char *rx_bufs[BNX2_RX_RING_SIZE];
    uint16_t rx_prod;
    uint16_t rx_cons;
};

static struct bnx2_softc bnx2_devices[MAX_BNX2];
static int bnx2_count;

static int bnx2_output(struct ifnet *ifp, struct mbuf *m);
static void bnx2_poll(struct ifnet *ifp);

static struct ifnet_ops bnx2_ifnet_ops = {
    .if_output = bnx2_output,
    .if_poll = bnx2_poll,
};

static int
bnx2_match(struct pci_dev *dev)
{
    if(!dev || dev->vendor_id != BNX2_VENDOR_BROADCOM)
        return 0;

    switch(dev->device_id){
    case BNX2_DEV_5706:
    case BNX2_DEV_5708:
    case BNX2_DEV_5709:
        return 1;
    default:
        return 0;
    }
}

static uint32_t
bnx2_read(struct bnx2_softc *sc, int reg)
{
    return sc->regs[reg / 4];
}

static void
bnx2_write(struct bnx2_softc *sc, int reg, uint32_t val)
{
    sc->regs[reg / 4] = val;
}

/* Write to context memory via indirect registers (5706/5708 path) */
static void
bnx2_ctx_wr(struct bnx2_softc *sc, uint32_t cid_addr, uint32_t off, uint32_t val)
{
    bnx2_write(sc, BNX2_CTX_DATA_ADR, cid_addr + off);
    bnx2_write(sc, BNX2_CTX_DATA, val);
}

static void
bnx2_make_local_mac(struct pci_dev *dev, uint8_t mac[6])
{
    mac[0] = 0x02;
    mac[1] = dev->vendor_id & 0xFF;
    mac[2] = dev->device_id & 0xFF;
    mac[3] = dev->bus;
    mac[4] = dev->slot;
    mac[5] = dev->func;
}

static void
bnx2_read_mac(struct bnx2_softc *sc)
{
    uint32_t lo = bnx2_read(sc, BNX2_EMAC_MAC_MATCH0);
    uint32_t hi = bnx2_read(sc, BNX2_EMAC_MAC_MATCH1);
    int i, all0 = 1, allf = 1;
    /* bnx2 EMAC_MAC_MATCH0: bits[15:8]=mac[0], bits[7:0]=mac[1] */
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
        bnx2_make_local_mac(sc->pci, sc->mac);
}

static void
bnx2_reset(struct bnx2_softc *sc)
{
    int i;
    bnx2_write(sc, BNX2_PCICFG_MISC_CONFIG,
        BNX2_PCICFG_MISC_RST_REQ | BNX2_PCICFG_WIN_ENA);
    for(i = 0; i < 1000; i++){
        if(!(bnx2_read(sc, BNX2_PCICFG_MISC_CONFIG) &
             BNX2_PCICFG_MISC_RST_BSY))
            break;
        microdelay(10);
    }
}

static int
bnx2_init_tx(struct bnx2_softc *sc)
{
    uint32_t cid = BNX2_SEND_CID * 0x80;
    sc->tx_ring = (struct bnx2_tx_bd *)kalloc();
    if(!sc->tx_ring)
        return -1;
    memset(sc->tx_ring, 0, sizeof(struct bnx2_tx_bd) * BNX2_TX_RING_SIZE);
    sc->tx_prod = 0;
    sc->tx_cons = 0;
    /* Program TX BD ring base into send context */
    bnx2_ctx_wr(sc, cid, BNX2_L2CTX_TBDR_BHADDR_HI, 0);
    bnx2_ctx_wr(sc, cid, BNX2_L2CTX_TBDR_BHADDR_LO, V2P(sc->tx_ring));
    return 0;
}

static int
bnx2_init_rx(struct bnx2_softc *sc)
{
    uint32_t cid = BNX2_RECV_CID * 0x80;
    int i;
    sc->rx_ring = (struct bnx2_rx_bd *)kalloc();
    if(!sc->rx_ring)
        return -1;
    memset(sc->rx_ring, 0, sizeof(struct bnx2_rx_bd) * BNX2_RX_RING_SIZE);
    for(i = 0; i < BNX2_RX_RING_SIZE; i++){
        /* each RX buf is large enough for l2fhdr + full frame */
        sc->rx_bufs[i] = kalloc();
        if(!sc->rx_bufs[i])
            return -1;
        sc->rx_ring[i].addr_hi = 0;
        sc->rx_ring[i].addr_lo = V2P(sc->rx_bufs[i]);
        sc->rx_ring[i].len = BNX2_RX_BUF_SIZE;
        sc->rx_ring[i].flags = BNX2_RX_BD_FLAGS_START | BNX2_RX_BD_FLAGS_END;
    }
    sc->rx_prod = BNX2_RX_RING_SIZE;
    sc->rx_cons = 0;
    /* Program RX BD ring base and producer into recv context */
    bnx2_ctx_wr(sc, cid, BNX2_L2CTX_NX_BDHADDR_HI, 0);
    bnx2_ctx_wr(sc, cid, BNX2_L2CTX_NX_BDHADDR_LO, V2P(sc->rx_ring));
    bnx2_ctx_wr(sc, cid, BNX2_L2CTX_HOST_BDIDX, sc->rx_prod);
    return 0;
}

static void
bnx2_tx_complete(struct bnx2_softc *sc)
{
    uint16_t hw_cons = sc->sb->tx_cons;
    while(sc->tx_cons != hw_cons){
        uint16_t idx = sc->tx_cons % BNX2_TX_RING_SIZE;
        if(sc->tx_mbufs[idx]){
            mbuf_free(sc->tx_mbufs[idx]);
            sc->tx_mbufs[idx] = 0;
        }
        sc->tx_cons = (sc->tx_cons + 1) % BNX2_TX_RING_SIZE;
    }
}

static void
bnx2_rx_complete(struct bnx2_softc *sc)
{
    int processed = 0;
    while(processed < 32){
        uint16_t hw_cons = sc->sb->rx_cons;
        uint16_t idx;
        struct bnx2_rx_bd *bd;
        struct bnx2_l2fhdr *hdr;
        if(sc->rx_cons == hw_cons)
            break;
        idx = sc->rx_cons % BNX2_RX_RING_SIZE;
        bd  = &sc->rx_ring[idx];
        hdr = (struct bnx2_l2fhdr *)sc->rx_bufs[idx];
        if(hdr->status & BNX2_L2FH_STATUS_RX_OK){
            uint16_t len = hdr->pkt_len;
            char *data   = sc->rx_bufs[idx] + sizeof(struct bnx2_l2fhdr);
            if(len > 0 && len <= BNX2_RX_BUF_SIZE){
                struct mbuf *m = mbuf_alloc();
                if(m){
                    memmove(m->data, data, len);
                    m->len = len;
                    m->rcvif = &sc->ifn;
                    release(&sc->lock);
                    if_input(&sc->ifn, m);
                    acquire(&sc->lock);
                }
            }
        }
        /* repost buffer */
        hdr->status = 0;
        bd->addr_lo = V2P(sc->rx_bufs[idx]);
        sc->rx_cons = (sc->rx_cons + 1) % BNX2_RX_RING_SIZE;
        sc->rx_prod = (sc->rx_prod + 1) % BNX2_RX_RING_SIZE;
        bnx2_ctx_wr(sc, BNX2_RECV_CID * 0x80,
            BNX2_L2CTX_HOST_BDIDX, sc->rx_prod);
        processed++;
    }
}

static int
bnx2_output(struct ifnet *ifp, struct mbuf *m)
{
    struct bnx2_softc *sc = (struct bnx2_softc *)ifp->if_softc;
    uint16_t next;
    struct bnx2_tx_bd *bd;

    if(!sc || !m || m->len == 0)
        return -1;
    acquire(&sc->lock);
    if((ifp->if_flags & IFF_RUNNING) == 0){
        release(&sc->lock);
        return -1;
    }
    bnx2_tx_complete(sc);
    next = (sc->tx_prod + 1) % BNX2_TX_RING_SIZE;
    if(next == sc->tx_cons){
        release(&sc->lock);
        return -1;
    }
    bd = &sc->tx_ring[sc->tx_prod];
    bd->addr_hi     = 0;
    bd->addr_lo     = V2P(m->data);
    bd->nbytes_flags = ((uint32_t)m->len & 0xFFFF) |
        BNX2_TX_BD_FLAGS_END | BNX2_TX_BD_FLAGS_START;
    bd->vlan        = 0;
    sc->tx_mbufs[sc->tx_prod] = m;
    sc->tx_prod = next;
    /* Advance TX BD index in send context */
    bnx2_ctx_wr(sc, BNX2_SEND_CID * 0x80,
        BNX2_L2CTX_HOST_BIDX, sc->tx_prod);
    release(&sc->lock);
    return 0;
}

static void
bnx2_poll(struct ifnet *ifp)
{
    struct bnx2_softc *sc = (struct bnx2_softc *)ifp->if_softc;
    if(!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;
    acquire(&sc->lock);
    bnx2_tx_complete(sc);
    bnx2_rx_complete(sc);
    release(&sc->lock);
}

static void
bnx2_probe(struct pci_dev *dev)
{
    struct bnx2_softc *sc;

    if(bnx2_count >= MAX_BNX2)
        return;

    sc = &bnx2_devices[bnx2_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "bnx2");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "bnx2");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    sc->regs = (volatile uint32_t *)pci_map_bar(dev, 0);
    if(!sc->regs){
        cprintf("bnx2: failed to map BAR0\n");
        return;
    }
    bnx2_reset(sc);
    bnx2_read_mac(sc);
    /* Status block */
    sc->sb = (struct bnx2_status_block *)kalloc();
    if(!sc->sb){
        cprintf("bnx2: failed to alloc status block\n");
        return;
    }
    memset(sc->sb, 0, sizeof(*sc->sb));
    bnx2_write(sc, BNX2_HC_STATUS_ADDR_L, V2P(sc->sb));
    bnx2_write(sc, BNX2_HC_STATUS_ADDR_H, 0);
    bnx2_write(sc, BNX2_HC_ENABLED, 1);
    if(bnx2_init_tx(sc) < 0 || bnx2_init_rx(sc) < 0){
        cprintf("bnx2: failed to init rings\n");
        return;
    }

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "bnx20", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[4] = '0' + bnx2_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_RUNNING | IFF_BROADCAST;
    memmove(sc->ifn.if_hwaddr, sc->mac, 6);
    sc->ifn.if_softc = sc;
    sc->ifn.if_ops = &bnx2_ifnet_ops;
    sc->ifn.if_input = ether_input;

    if(if_register(&sc->ifn) < 0){
        cprintf("bnx2: if_register failed for %x\n", dev->device_id);
        return;
    }

    cprintf("bnx2: attached %s dev=%x irq=%d mac=%x:%x:%x:%x:%x:%x\n",
            sc->ifn.if_xname, dev->device_id, dev->irq_line,
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
    bnx2_count++;
}

void
bnx2_init(void)
{
    int i;

    BOOTDBG("bnx2: probing supported PCI IDs\n");
    for(i = 0; i < pci_device_count(); i++){
        struct pci_dev *dev = pci_get_device(i);
        if(dev && bnx2_match(dev))
            bnx2_probe(dev);
    }
}
