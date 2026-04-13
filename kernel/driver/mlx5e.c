/*
 * Mellanox mlx5e-family NIC driver for auxv6.
 *
 * Covers ConnectX-4/ConnectX-5 (mlx5) 25/100GbE parts.
 *
 * TX SQ / RX RQ WQE rings with CQ completion pages.  Full initialization
 * requires the mlx5 INIT_HCA firmware sequence; ring machinery is wired.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

#define MLX5_VENDOR_MELLANOX 0x15B3
#define MLX5_DEV_CX4         0x1013
#define MLX5_DEV_CX4_LX      0x1015
#define MLX5_DEV_CX5         0x1017

/*
 * mlx5 BAR0 register layout.
 * The UAR page for the PF begins at BAR0 offset 0x100000.
 * SQ send doorbell = UAR_base + 0x800 (64-bit write: upper=pi, lower=qp).
 */
#define MLX5_UAR_BASE        0x00100000
#define MLX5_UAR_SQ_DB       (MLX5_UAR_BASE + 0x800)
#define MLX5_ISEG_INIT_SEG   0x00000000   /* initialization segment */

#define MLX5_TX_RING_SIZE    64
#define MLX5_RX_RING_SIZE    64
#define MLX5_CQ_SIZE         64
#define MLX5_WQE_SIZE        64
#define MLX5_CQE_SIZE        32   /* mlx5 CQE is 32 bytes */
#define MLX5_RX_BUF_SIZE     2048

/* SQ WQE ctrl segment done bit (byte 0, bit 0 of owner byte) */
#define MLX5_WQE_CTRL_OWN    0x01
/* CQE validity – alternates each ring cycle */
#define MLX5_CQE_OWN_MASK    0x01

#define MAX_MLX5E 4

/* TX WQE (64 bytes): simplified ctrl + eth segments */
struct mlx5_tx_wqe {
    uint8_t  ctrl[16];    /* control segment */
    uint8_t  eth[16];     /* ethernet inline segment */
    uint64_t ds_addr;     /* data segment: DMA address */
    uint32_t ds_bcount;   /* data segment: byte count */
    uint32_t ds_lkey;     /* data segment: local key (0 for simplified) */
    uint8_t  pad[8];
} __attribute__((packed));

/* RX WQE (16 bytes): host posts buffer */
struct mlx5_rx_wqe {
    uint64_t addr;
    uint32_t byte_count;
    uint32_t lkey;
} __attribute__((packed));

/* CQE (32 bytes): hardware completion */
struct mlx5_cqe {
    uint8_t  pkt_info;
    uint8_t  reserved[17];
    uint16_t wqe_counter;   /* WQE index that completed */
    uint32_t byte_cnt;      /* packet byte count */
    uint8_t  op_own;        /* bits[3:0]=opcode, bit[0]=owner */
    uint8_t  reserved2[7];
} __attribute__((packed));

struct mlx5e_softc {
    struct pci_dev *pci;
    struct spinlock lock;
    struct ifnet ifn;
    volatile uint32_t *regs;
    uint8_t mac[6];
    /* TX SQ */
    struct mlx5_tx_wqe *sq;
    struct mbuf        *tx_mbufs[MLX5_TX_RING_SIZE];
    uint16_t            sq_head;
    uint16_t            sq_tail;
    /* RX RQ */
    struct mlx5_rx_wqe *rq;
    char               *rx_bufs[MLX5_RX_RING_SIZE];
    uint16_t            rq_tail;
    /* CQ */
    struct mlx5_cqe    *cq;
    uint8_t             cq_ci;   /* CQ consumer index */
    uint8_t             cq_owner;
};

static struct mlx5e_softc mlx5e_devices[MAX_MLX5E];
static int mlx5e_count;

static int mlx5e_output(struct ifnet *ifp, struct mbuf *m);
static void mlx5e_poll(struct ifnet *ifp);

static struct ifnet_ops mlx5e_ifnet_ops = {
    .if_output = mlx5e_output,
    .if_poll = mlx5e_poll,
};

static int
mlx5e_match(struct pci_dev *dev)
{
    if(!dev || dev->vendor_id != MLX5_VENDOR_MELLANOX)
        return 0;

    switch(dev->device_id){
    case MLX5_DEV_CX4:
    case MLX5_DEV_CX4_LX:
    case MLX5_DEV_CX5:
        return 1;
    default:
        return 0;
    }
}

static void
mlx5e_make_local_mac(struct pci_dev *dev, uint8_t mac[6])
{
    mac[0] = 0x02;
    mac[1] = dev->vendor_id & 0xFF;
    mac[2] = dev->device_id & 0xFF;
    mac[3] = dev->bus;
    mac[4] = dev->slot;
    mac[5] = dev->func;
}

static void
mlx5e_read_mac(struct mlx5e_softc *sc)
{
    /* mlx5 MAC is set via INIT_HCA firmware; use deterministic local */
    mlx5e_make_local_mac(sc->pci, sc->mac);
}

static void
mlx5e_sq_ring_db(struct mlx5e_softc *sc, uint16_t pi)
{
    volatile uint32_t *db =
        (volatile uint32_t *)((volatile uint8_t *)sc->regs +
                              MLX5_UAR_SQ_DB);
    db[0] = (uint32_t)pi;
}

static int
mlx5e_init_tx(struct mlx5e_softc *sc)
{
    sc->sq = (struct mlx5_tx_wqe *)kalloc();
    if(!sc->sq)
        return -1;
    memset(sc->sq, 0, sizeof(struct mlx5_tx_wqe) * MLX5_TX_RING_SIZE);
    sc->sq_head = 0;
    sc->sq_tail = 0;
    return 0;
}

static int
mlx5e_init_rx(struct mlx5e_softc *sc)
{
    int i;
    sc->rq = (struct mlx5_rx_wqe *)kalloc();
    if(!sc->rq)
        return -1;
    sc->cq = (struct mlx5_cqe *)kalloc();
    if(!sc->cq)
        return -1;
    memset(sc->rq, 0, sizeof(struct mlx5_rx_wqe) * MLX5_RX_RING_SIZE);
    memset(sc->cq, 0, sizeof(struct mlx5_cqe) * MLX5_CQ_SIZE);
    for(i = 0; i < MLX5_RX_RING_SIZE; i++){
        sc->rx_bufs[i] = kalloc();
        if(!sc->rx_bufs[i])
            return -1;
        sc->rq[i].addr       = V2P(sc->rx_bufs[i]);
        sc->rq[i].byte_count = MLX5_RX_BUF_SIZE;
        sc->rq[i].lkey       = 0;
    }
    sc->rq_tail  = 0;
    sc->cq_ci    = 0;
    sc->cq_owner = 0;
    return 0;
}

static void
mlx5e_tx_complete(struct mlx5e_softc *sc)
{
    while(sc->sq_head != sc->sq_tail){
        uint16_t cq_idx = sc->sq_head % MLX5_CQ_SIZE;
        struct mlx5_cqe *cqe = &sc->cq[cq_idx];
        if((cqe->op_own & MLX5_CQE_OWN_MASK) == sc->cq_owner)
            break;
        if(sc->tx_mbufs[sc->sq_head]){
            mbuf_free(sc->tx_mbufs[sc->sq_head]);
            sc->tx_mbufs[sc->sq_head] = 0;
        }
        sc->sq_head = (sc->sq_head + 1) % MLX5_TX_RING_SIZE;
        if(sc->sq_head == 0)
            sc->cq_owner ^= 1;
    }
}

static void
mlx5e_rx_complete(struct mlx5e_softc *sc)
{
    int processed = 0;
    while(processed < 32){
        uint16_t cq_idx = (MLX5_CQ_SIZE / 2 + sc->rq_tail) % MLX5_CQ_SIZE;
        struct mlx5_cqe *cqe = &sc->cq[cq_idx];
        uint16_t idx = sc->rq_tail % MLX5_RX_RING_SIZE;
        uint32_t len;
        if(cqe->byte_cnt == 0)
            break;
        len = cqe->byte_cnt;
        if(len > 0 && len <= MLX5_RX_BUF_SIZE){
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
        cqe->byte_cnt = 0;
        sc->rq[idx].addr       = V2P(sc->rx_bufs[idx]);
        sc->rq[idx].byte_count = MLX5_RX_BUF_SIZE;
        sc->rq_tail = (sc->rq_tail + 1) % MLX5_RX_RING_SIZE;
        processed++;
    }
}

static int
mlx5e_output(struct ifnet *ifp, struct mbuf *m)
{
    struct mlx5e_softc *sc = (struct mlx5e_softc *)ifp->if_softc;
    uint16_t next;
    struct mlx5_tx_wqe *wqe;

    if(!sc || !m || m->len == 0)
        return -1;
    acquire(&sc->lock);
    if((ifp->if_flags & IFF_RUNNING) == 0){
        release(&sc->lock);
        return -1;
    }
    mlx5e_tx_complete(sc);
    next = (sc->sq_tail + 1) % MLX5_TX_RING_SIZE;
    if(next == sc->sq_head){
        release(&sc->lock);
        return -1;
    }
    wqe = &sc->sq[sc->sq_tail];
    memset(wqe, 0, sizeof(*wqe));
    wqe->ds_addr   = V2P(m->data);
    wqe->ds_bcount = m->len;
    wqe->ctrl[0]   = MLX5_WQE_CTRL_OWN;
    sc->tx_mbufs[sc->sq_tail] = m;
    sc->sq_tail = next;
    mlx5e_sq_ring_db(sc, sc->sq_tail);
    release(&sc->lock);
    return 0;
}

static void
mlx5e_poll(struct ifnet *ifp)
{
    struct mlx5e_softc *sc = (struct mlx5e_softc *)ifp->if_softc;
    if(!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;
    acquire(&sc->lock);
    mlx5e_tx_complete(sc);
    mlx5e_rx_complete(sc);
    release(&sc->lock);
}

static void
mlx5e_probe(struct pci_dev *dev)
{
    struct mlx5e_softc *sc;

    if(mlx5e_count >= MAX_MLX5E)
        return;

    sc = &mlx5e_devices[mlx5e_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "mlx5e");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "mlx5e");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    sc->regs = (volatile uint32_t *)pci_map_bar(dev, 0);
    if(!sc->regs){
        cprintf("mlx5e: failed to map BAR0\n");
        return;
    }
    mlx5e_read_mac(sc);
    if(mlx5e_init_tx(sc) < 0 || mlx5e_init_rx(sc) < 0){
        cprintf("mlx5e: failed to init rings\n");
        return;
    }

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "mlx5e0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[5] = '0' + mlx5e_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_RUNNING | IFF_BROADCAST;
    memmove(sc->ifn.if_hwaddr, sc->mac, 6);
    sc->ifn.if_softc = sc;
    sc->ifn.if_ops = &mlx5e_ifnet_ops;
    sc->ifn.if_input = ether_input;

    if(if_register(&sc->ifn) < 0){
        cprintf("mlx5e: if_register failed for %x\n", dev->device_id);
        return;
    }

    cprintf("mlx5e: attached %s dev=%x irq=%d mac=%x:%x:%x:%x:%x:%x\n",
            sc->ifn.if_xname, dev->device_id, dev->irq_line,
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
    mlx5e_count++;
}

void
mlx5e_init(void)
{
    int i;

    BOOTDBG("mlx5e: probing supported PCI IDs\n");
    for(i = 0; i < pci_device_count(); i++){
        struct pci_dev *dev = pci_get_device(i);
        if(dev && mlx5e_match(dev))
            mlx5e_probe(dev);
    }
}
