/*
 * Amazon ENA NIC driver for auxv6.
 *
 * Covers Amazon Elastic Network Adapter (EC2 instance NIC).
 *
 * TX/RX submission/completion queue pairs with doorbell.  Full device
 * configuration requires the ENA admin queue handshake; the SQ/CQ ring
 * machinery is implemented here for that next step.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

#define ENA_VENDOR_AMAZON 0x1D0F
#define ENA_DEV_VF        0xEC20
#define ENA_DEV_PF        0xEC21

/* ENA BAR0 register offsets */
#define ENA_REG_VERSION      0x0000
#define ENA_REG_CAPS         0x0008
#define ENA_REG_AQ_BASE_LO   0x0010  /* admin queue base low */
#define ENA_REG_AQ_BASE_HI   0x0014  /* admin queue base high */
#define ENA_REG_AQ_CAPS      0x0018  /* admin queue capabilities */
#define ENA_REG_ACQ_BASE_LO  0x001C  /* admin completion queue base low */
#define ENA_REG_ACQ_BASE_HI  0x0020
#define ENA_REG_ACQ_CAPS     0x0024
#define ENA_REG_DOORBELL     0x003C  /* submission queue doorbell (queue index) */
/* TX SQ doorbell: write queue id in upper 16, pi in lower 16 */
#define ENA_DB_TX_QUEUE      0
#define ENA_DB_RX_QUEUE      1

#define ENA_TX_RING_SIZE     64
#define ENA_RX_RING_SIZE     64
#define ENA_RX_BUF_SIZE      2048

/* TX submission queue entry (16 bytes) */
struct ena_tx_sqe {
    uint64_t buf_addr;    /* DMA address of packet data */
    uint16_t len;         /* packet length */
    uint16_t header_len;  /* L4 header length (0 for non-offload) */
    uint8_t  phase;       /* phase bit (alternates each pass) */
    uint8_t  flags;       /* ENA_SQE_LAST_PKT etc. */
    uint16_t reserved;
} __attribute__((packed));

/* TX completion queue entry (8 bytes) */
struct ena_tx_cqe {
    uint16_t req_id;      /* request ID matching SQE */
    uint8_t  status;      /* 0 = success */
    uint8_t  flags;       /* phase bit in bit[1] */
    uint32_t reserved;
} __attribute__((packed));

/* RX submission queue entry (16 bytes) */
struct ena_rx_sqe {
    uint64_t buf_addr;    /* DMA address of receive buffer */
    uint16_t len;         /* buffer length */
    uint16_t req_id;      /* request ID (our buffer index) */
    uint32_t reserved;
} __attribute__((packed));

/* RX completion queue entry (16 bytes) */
struct ena_rx_cqe {
    uint16_t req_id;      /* matching RX SQE req_id */
    uint16_t pkt_len;     /* received packet length */
    uint8_t  status;      /* 0 = success */
    uint8_t  flags;       /* phase bit in bit[1] */
    uint16_t reserved;
    uint32_t hash;
    uint32_t reserved2;
} __attribute__((packed));

#define ENA_TX_SQE_LAST_PKT  0x01
#define ENA_CQE_PHASE_MASK   0x01

#define MAX_ENA 4

struct ena_softc {
    struct pci_dev *pci;
    struct spinlock lock;
    struct ifnet ifn;
    volatile uint32_t *regs;
    uint8_t mac[6];
    /* TX */
    struct ena_tx_sqe *tx_sq;
    struct ena_tx_cqe *tx_cq;
    struct mbuf       *tx_mbufs[ENA_TX_RING_SIZE];
    uint16_t           tx_sq_pi;   /* SQ producer */
    uint16_t           tx_cq_ci;   /* CQ consumer */
    uint8_t            tx_phase;   /* current phase bit */
    /* RX */
    struct ena_rx_sqe *rx_sq;
    struct ena_rx_cqe *rx_cq;
    char              *rx_bufs[ENA_RX_RING_SIZE];
    uint16_t           rx_sq_pi;
    uint16_t           rx_cq_ci;
    uint8_t            rx_phase;
};

static struct ena_softc ena_devices[MAX_ENA];
static int ena_count;

static int ena_output(struct ifnet *ifp, struct mbuf *m);
static void ena_poll(struct ifnet *ifp);

static struct ifnet_ops ena_ifnet_ops = {
    .if_output = ena_output,
    .if_poll = ena_poll,
};

static int
ena_match(struct pci_dev *dev)
{
    if(!dev || dev->vendor_id != ENA_VENDOR_AMAZON)
        return 0;

    switch(dev->device_id){
    case ENA_DEV_VF:
    case ENA_DEV_PF:
        return 1;
    default:
        return 0;
    }
}

static uint32_t
ena_read(struct ena_softc *sc, int reg)
{
    return sc->regs[reg / 4];
}

static void
ena_write(struct ena_softc *sc, int reg, uint32_t val)
{
    sc->regs[reg / 4] = val;
}

static void
ena_make_local_mac(struct pci_dev *dev, uint8_t mac[6])
{
    mac[0] = 0x02;
    mac[1] = dev->vendor_id & 0xFF;
    mac[2] = dev->device_id & 0xFF;
    mac[3] = dev->bus;
    mac[4] = dev->slot;
    mac[5] = dev->func;
}

static void
ena_read_mac(struct ena_softc *sc)
{
    /* ENA MAC is retrieved via admin queue; use deterministic local */
    ena_make_local_mac(sc->pci, sc->mac);
    (void)ena_read;
}

static int
ena_init_tx(struct ena_softc *sc)
{
    sc->tx_sq = (struct ena_tx_sqe *)kalloc();
    if(!sc->tx_sq)
        return -1;
    sc->tx_cq = (struct ena_tx_cqe *)kalloc();
    if(!sc->tx_cq)
        return -1;
    memset(sc->tx_sq, 0, sizeof(struct ena_tx_sqe) * ENA_TX_RING_SIZE);
    memset(sc->tx_cq, 0, sizeof(struct ena_tx_cqe) * ENA_TX_RING_SIZE);
    sc->tx_sq_pi = 0;
    sc->tx_cq_ci = 0;
    sc->tx_phase  = 0;
    return 0;
}

static int
ena_init_rx(struct ena_softc *sc)
{
    int i;
    sc->rx_sq = (struct ena_rx_sqe *)kalloc();
    if(!sc->rx_sq)
        return -1;
    sc->rx_cq = (struct ena_rx_cqe *)kalloc();
    if(!sc->rx_cq)
        return -1;
    memset(sc->rx_sq, 0, sizeof(struct ena_rx_sqe) * ENA_RX_RING_SIZE);
    memset(sc->rx_cq, 0, sizeof(struct ena_rx_cqe) * ENA_RX_RING_SIZE);
    for(i = 0; i < ENA_RX_RING_SIZE; i++){
        sc->rx_bufs[i] = kalloc();
        if(!sc->rx_bufs[i])
            return -1;
        sc->rx_sq[i].buf_addr = V2P(sc->rx_bufs[i]);
        sc->rx_sq[i].len      = ENA_RX_BUF_SIZE;
        sc->rx_sq[i].req_id   = i;
    }
    sc->rx_sq_pi = ENA_RX_RING_SIZE;
    sc->rx_cq_ci = 0;
    sc->rx_phase  = 0;
    /* Ring RX SQ doorbell to post all free buffers */
    ena_write(sc, ENA_REG_DOORBELL,
        ((uint32_t)ENA_DB_RX_QUEUE << 16) | sc->rx_sq_pi);
    return 0;
}

static void
ena_tx_complete(struct ena_softc *sc)
{
    while(1){
        uint16_t idx = sc->tx_cq_ci % ENA_TX_RING_SIZE;
        struct ena_tx_cqe *cqe = &sc->tx_cq[idx];
        uint8_t cqe_phase = (cqe->flags >> 1) & 1;
        if(cqe_phase == sc->tx_phase)
            break;
        if(sc->tx_mbufs[idx]){
            mbuf_free(sc->tx_mbufs[idx]);
            sc->tx_mbufs[idx] = 0;
        }
        sc->tx_cq_ci++;
        if((sc->tx_cq_ci % ENA_TX_RING_SIZE) == 0)
            sc->tx_phase ^= 1;
    }
}

static void
ena_rx_complete(struct ena_softc *sc)
{
    int processed = 0;
    while(processed < 32){
        uint16_t idx = sc->rx_cq_ci % ENA_RX_RING_SIZE;
        struct ena_rx_cqe *cqe = &sc->rx_cq[idx];
        uint8_t cqe_phase = (cqe->flags >> 1) & 1;
        uint16_t req_id, len;
        if(cqe_phase == sc->rx_phase)
            break;
        req_id = cqe->req_id % ENA_RX_RING_SIZE;
        len    = cqe->pkt_len;
        if(cqe->status == 0 && len > 0 && len <= ENA_RX_BUF_SIZE){
            struct mbuf *m = mbuf_alloc();
            if(m){
                memmove(m->data, sc->rx_bufs[req_id], len);
                m->len = len;
                m->rcvif = &sc->ifn;
                release(&sc->lock);
                if_input(&sc->ifn, m);
                acquire(&sc->lock);
            }
        }
        /* repost: re-submit this buffer to RX SQ */
        sc->rx_sq[req_id].buf_addr = V2P(sc->rx_bufs[req_id]);
        sc->rx_sq[req_id].len      = ENA_RX_BUF_SIZE;
        sc->rx_sq_pi++;
        ena_write(sc, ENA_REG_DOORBELL,
            ((uint32_t)ENA_DB_RX_QUEUE << 16) |
            (sc->rx_sq_pi % ENA_RX_RING_SIZE));
        sc->rx_cq_ci++;
        if((sc->rx_cq_ci % ENA_RX_RING_SIZE) == 0)
            sc->rx_phase ^= 1;
        processed++;
    }
}

static int
ena_output(struct ifnet *ifp, struct mbuf *m)
{
    struct ena_softc *sc = (struct ena_softc *)ifp->if_softc;
    uint16_t idx;
    struct ena_tx_sqe *sqe;

    if(!sc || !m || m->len == 0)
        return -1;
    acquire(&sc->lock);
    if((ifp->if_flags & IFF_RUNNING) == 0){
        release(&sc->lock);
        return -1;
    }
    ena_tx_complete(sc);
    if(((sc->tx_sq_pi + 1) % ENA_TX_RING_SIZE) ==
       (sc->tx_cq_ci % ENA_TX_RING_SIZE)){
        release(&sc->lock);
        return -1;
    }
    idx = sc->tx_sq_pi % ENA_TX_RING_SIZE;
    sqe = &sc->tx_sq[idx];
    sqe->buf_addr    = V2P(m->data);
    sqe->len         = (uint16_t)m->len;
    sqe->header_len  = 0;
    sqe->flags       = ENA_TX_SQE_LAST_PKT |
                       (uint8_t)(sc->tx_phase << 1);
    sc->tx_mbufs[idx] = m;
    sc->tx_sq_pi++;
    if((sc->tx_sq_pi % ENA_TX_RING_SIZE) == 0)
        sc->tx_phase ^= 1;
    ena_write(sc, ENA_REG_DOORBELL,
        ((uint32_t)ENA_DB_TX_QUEUE << 16) |
        (sc->tx_sq_pi % ENA_TX_RING_SIZE));
    release(&sc->lock);
    return 0;
}

static void
ena_poll(struct ifnet *ifp)
{
    struct ena_softc *sc = (struct ena_softc *)ifp->if_softc;
    if(!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;
    acquire(&sc->lock);
    ena_tx_complete(sc);
    ena_rx_complete(sc);
    release(&sc->lock);
}

static void
ena_probe(struct pci_dev *dev)
{
    struct ena_softc *sc;

    if(ena_count >= MAX_ENA)
        return;

    sc = &ena_devices[ena_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "ena");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "ena");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    sc->regs = (volatile uint32_t *)pci_map_bar(dev, 0);
    if(!sc->regs){
        cprintf("ena: failed to map BAR0\n");
        return;
    }
    ena_read_mac(sc);
    if(ena_init_tx(sc) < 0 || ena_init_rx(sc) < 0){
        cprintf("ena: failed to init rings\n");
        return;
    }

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "ena0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[3] = '0' + ena_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_RUNNING | IFF_BROADCAST;
    memmove(sc->ifn.if_hwaddr, sc->mac, 6);
    sc->ifn.if_softc = sc;
    sc->ifn.if_ops = &ena_ifnet_ops;
    sc->ifn.if_input = ether_input;

    if(if_register(&sc->ifn) < 0){
        cprintf("ena: if_register failed for %x\n", dev->device_id);
        return;
    }

    cprintf("ena: attached %s dev=%x irq=%d mac=%x:%x:%x:%x:%x:%x\n",
            sc->ifn.if_xname, dev->device_id, dev->irq_line,
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
    ena_count++;
}

void
ena_init(void)
{
    int i;

    BOOTDBG("ena: probing supported PCI IDs\n");
    for(i = 0; i < pci_device_count(); i++){
        struct pci_dev *dev = pci_get_device(i);
        if(dev && ena_match(dev))
            ena_probe(dev);
    }
}
