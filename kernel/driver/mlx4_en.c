/*
 * Mellanox mlx4_en-family NIC driver for auxv6.
 *
 * Covers ConnectX-3 (mlx4) 10/40GbE parts.
 *
 * TX/RX WQE rings with CQ completion.  Full hardware bring-up requires
 * the mlx4 INIT_HCA firmware command sequence; the ring machinery is
 * in place here for that next step.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

#define MLX4_VENDOR_MELLANOX 0x15B3
#define MLX4_DEV_CX3_1       0x1003
#define MLX4_DEV_CX3_2       0x1007
#define MLX4_DEV_CX3_PRO     0x1011

/*
 * mlx4 register layout (BAR0).
 * The UAR (User Access Region) base for the PF is at BAR0 + 0x100000.
 * TX send doorbell = UAR_base + 0x14.
 */
#define MLX4_HCR_BASE        0x00080000  /* host command register */
#define MLX4_UAR_BASE        0x00100000  /* UAR page for PF */
#define MLX4_UAR_SEND_DB     (MLX4_UAR_BASE + 0x14)  /* send doorbell */
#define MLX4_CLR_INT         0x00000008  /* clear interrupt */
/* Simplified SQ/RQ/CQ indices without full HCA init */
#define MLX4_TX_QPN          1   /* send queue pair number (simplified) */
#define MLX4_RX_QPN          2   /* recv queue pair number (simplified) */

/* WQE ring dimensions */
#define MLX4_TX_RING_SIZE    64   /* TX WQEs */
#define MLX4_RX_RING_SIZE    64   /* RX WQEs */
#define MLX4_CQ_SIZE         64   /* CQ entries */
#define MLX4_WQE_SIZE        64   /* bytes per TX/RX WQE */
#define MLX4_CQE_SIZE        16   /* bytes per CQ entry */
#define MLX4_RX_BUF_SIZE     2048

/* CQE ownership bit */
#define MLX4_CQE_OWNER_HW    0x80
/* TX WQE ctrl seg – simplified: byte[48] = owner/doorbell */
#define MLX4_WQE_CTRL_OWN    0x80

#define MAX_MLX4_EN 4

/*
 * TX WQE: 64 bytes = ctrl_seg (16B) + eth_seg (16B) + data_seg (32B).
 * We use a simplified layout: first 8 bytes are DMA addr + length.
 */
struct mlx4_tx_wqe {
    uint64_t addr;         /* data buffer physical address */
    uint32_t byte_count;   /* payload length */
    uint32_t ctrl;         /* ownership / flags */
    uint8_t  pad[48];
} __attribute__((packed));

/*
 * RX WQE: host posts buffer to hardware (64 bytes).
 * We only use the first 16 bytes (address + byte_count).
 */
struct mlx4_rx_wqe {
    uint64_t addr;
    uint32_t byte_count;
    uint32_t flags;
    uint8_t  pad[48];
} __attribute__((packed));

/* CQ entry (16 bytes) */
struct mlx4_cqe {
    uint32_t byte_cnt;     /* packet byte count */
    uint32_t reserved[2];
    uint8_t  owner_sr_opcode; /* bit[7]=owner; lower bits=opcode */
    uint8_t  reserved2[3];
} __attribute__((packed));

struct mlx4_en_softc {
    struct pci_dev *pci;
    struct spinlock lock;
    struct ifnet ifn;
    volatile uint32_t *regs;
    uint8_t mac[6];
    /* TX */
    struct mlx4_tx_wqe *tx_ring;
    struct mbuf        *tx_mbufs[MLX4_TX_RING_SIZE];
    uint16_t            tx_head;
    uint16_t            tx_tail;
    /* RX */
    struct mlx4_rx_wqe *rx_ring;
    char               *rx_bufs[MLX4_RX_RING_SIZE];
    uint16_t            rx_tail;
    /* CQ */
    struct mlx4_cqe    *cq;
    uint8_t             cq_owner; /* expected owner bit value */
};

static struct mlx4_en_softc mlx4_en_devices[MAX_MLX4_EN];
static int mlx4_en_count;

static int mlx4_en_output(struct ifnet *ifp, struct mbuf *m);
static void mlx4_en_poll(struct ifnet *ifp);

static struct ifnet_ops mlx4_en_ifnet_ops = {
    .if_output = mlx4_en_output,
    .if_poll = mlx4_en_poll,
};

static int
mlx4_en_match(struct pci_dev *dev)
{
    if(!dev || dev->vendor_id != MLX4_VENDOR_MELLANOX)
        return 0;

    switch(dev->device_id){
    case MLX4_DEV_CX3_1:
    case MLX4_DEV_CX3_2:
    case MLX4_DEV_CX3_PRO:
        return 1;
    default:
        return 0;
    }
}

static void
mlx4_en_make_local_mac(struct pci_dev *dev, uint8_t mac[6])
{
    mac[0] = 0x02;
    mac[1] = dev->vendor_id & 0xFF;
    mac[2] = dev->device_id & 0xFF;
    mac[3] = dev->bus;
    mac[4] = dev->slot;
    mac[5] = dev->func;
}

static void
mlx4_en_read_mac(struct mlx4_en_softc *sc)
{
    /* mlx4 MAC is set by firmware via INIT_HCA; use deterministic local */
    mlx4_en_make_local_mac(sc->pci, sc->mac);
}

static void
mlx4_en_ring_tx_doorbell(struct mlx4_en_softc *sc, uint16_t idx)
{
    /* doorbell write: WQE index in bits[15:0], QPN in bits[31:16] */
    volatile uint32_t *db =
        (volatile uint32_t *)((volatile uint8_t *)sc->regs +
                              MLX4_UAR_SEND_DB);
    *db = ((uint32_t)MLX4_TX_QPN << 16) | (idx & 0xFFFF);
}

static int
mlx4_en_init_tx(struct mlx4_en_softc *sc)
{
    sc->tx_ring = (struct mlx4_tx_wqe *)kalloc();
    if(!sc->tx_ring)
        return -1;
    memset(sc->tx_ring, 0, sizeof(struct mlx4_tx_wqe) * MLX4_TX_RING_SIZE);
    sc->tx_head = 0;
    sc->tx_tail = 0;
    return 0;
}

static int
mlx4_en_init_rx(struct mlx4_en_softc *sc)
{
    int i;
    sc->rx_ring = (struct mlx4_rx_wqe *)kalloc();
    if(!sc->rx_ring)
        return -1;
    sc->cq = (struct mlx4_cqe *)kalloc();
    if(!sc->cq)
        return -1;
    memset(sc->rx_ring, 0, sizeof(struct mlx4_rx_wqe) * MLX4_RX_RING_SIZE);
    memset(sc->cq, 0, sizeof(struct mlx4_cqe) * MLX4_CQ_SIZE);
    for(i = 0; i < MLX4_RX_RING_SIZE; i++){
        sc->rx_bufs[i] = kalloc();
        if(!sc->rx_bufs[i])
            return -1;
        sc->rx_ring[i].addr = V2P(sc->rx_bufs[i]);
        sc->rx_ring[i].byte_count = MLX4_RX_BUF_SIZE;
    }
    sc->rx_tail = 0;
    sc->cq_owner = 0;   /* initial owner = software (CQE valid when top bit = 0) */
    return 0;
}

static void
mlx4_en_tx_complete(struct mlx4_en_softc *sc)
{
    /* Check CQ for TX completions (opcode 0x0A = send completion) */
    while(sc->tx_head != sc->tx_tail){
        uint16_t cq_idx = sc->tx_head % MLX4_CQ_SIZE;
        struct mlx4_cqe *cqe = &sc->cq[cq_idx];
        uint8_t owner = (cqe->owner_sr_opcode & MLX4_CQE_OWNER_HW) ? 1 : 0;
        if(owner == sc->cq_owner)
            break;  /* hardware hasn't posted this CQE yet */
        if(sc->tx_mbufs[sc->tx_head % MLX4_TX_RING_SIZE]){
            mbuf_free(sc->tx_mbufs[sc->tx_head % MLX4_TX_RING_SIZE]);
            sc->tx_mbufs[sc->tx_head % MLX4_TX_RING_SIZE] = 0;
        }
        sc->tx_head = (sc->tx_head + 1) % MLX4_TX_RING_SIZE;
        if(sc->tx_head == 0)
            sc->cq_owner ^= 1; /* flip expected owner on ring wrap */
    }
}

static void
mlx4_en_rx_complete(struct mlx4_en_softc *sc)
{
    int processed = 0;
    while(processed < 32){
        uint16_t idx = sc->rx_tail % MLX4_RX_RING_SIZE;
        struct mlx4_cqe *cqe = &sc->cq[(MLX4_CQ_SIZE / 2 + idx) % MLX4_CQ_SIZE];
        uint16_t len;
        /* Check if HW has posted an RX CQE in the second half of the CQ */
        if(cqe->owner_sr_opcode == 0)
            break;
        len = (uint16_t)(cqe->byte_cnt & 0xFFFF);
        if(len > 0 && len <= MLX4_RX_BUF_SIZE){
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
        cqe->owner_sr_opcode = 0;
        sc->rx_ring[idx].addr = V2P(sc->rx_bufs[idx]);
        sc->rx_ring[idx].byte_count = MLX4_RX_BUF_SIZE;
        sc->rx_tail = (sc->rx_tail + 1) % MLX4_RX_RING_SIZE;
        processed++;
    }
}

static int
mlx4_en_output(struct ifnet *ifp, struct mbuf *m)
{
    struct mlx4_en_softc *sc = (struct mlx4_en_softc *)ifp->if_softc;
    uint16_t next;
    struct mlx4_tx_wqe *wqe;

    if(!sc || !m || m->len == 0)
        return -1;
    acquire(&sc->lock);
    if((ifp->if_flags & IFF_RUNNING) == 0){
        release(&sc->lock);
        return -1;
    }
    mlx4_en_tx_complete(sc);
    next = (sc->tx_tail + 1) % MLX4_TX_RING_SIZE;
    if(next == sc->tx_head){
        release(&sc->lock);
        return -1;
    }
    wqe = &sc->tx_ring[sc->tx_tail];
    wqe->addr       = V2P(m->data);
    wqe->byte_count = m->len;
    wqe->ctrl       = MLX4_WQE_CTRL_OWN;
    sc->tx_mbufs[sc->tx_tail] = m;
    sc->tx_tail = next;
    mlx4_en_ring_tx_doorbell(sc, sc->tx_tail);
    release(&sc->lock);
    return 0;
}

static void
mlx4_en_poll(struct ifnet *ifp)
{
    struct mlx4_en_softc *sc = (struct mlx4_en_softc *)ifp->if_softc;
    if(!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;
    acquire(&sc->lock);
    mlx4_en_tx_complete(sc);
    mlx4_en_rx_complete(sc);
    release(&sc->lock);
}

static void
mlx4_en_probe(struct pci_dev *dev)
{
    struct mlx4_en_softc *sc;

    if(mlx4_en_count >= MAX_MLX4_EN)
        return;

    sc = &mlx4_en_devices[mlx4_en_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "mlx4_en");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "mlx4_en");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    sc->regs = (volatile uint32_t *)pci_map_bar(dev, 0);
    if(!sc->regs){
        cprintf("mlx4_en: failed to map BAR0\n");
        return;
    }
    mlx4_en_read_mac(sc);
    if(mlx4_en_init_tx(sc) < 0 || mlx4_en_init_rx(sc) < 0){
        cprintf("mlx4_en: failed to init rings\n");
        return;
    }

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "mlx4e0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[5] = '0' + mlx4_en_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_RUNNING | IFF_BROADCAST;
    memmove(sc->ifn.if_hwaddr, sc->mac, 6);
    sc->ifn.if_softc = sc;
    sc->ifn.if_ops = &mlx4_en_ifnet_ops;
    sc->ifn.if_input = ether_input;

    if(if_register(&sc->ifn) < 0){
        cprintf("mlx4_en: if_register failed for %x\n", dev->device_id);
        return;
    }

    cprintf("mlx4_en: attached %s dev=%x irq=%d mac=%x:%x:%x:%x:%x:%x\n",
            sc->ifn.if_xname, dev->device_id, dev->irq_line,
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
    mlx4_en_count++;
}

void
mlx4_en_init(void)
{
    int i;

    BOOTDBG("mlx4_en: probing supported PCI IDs\n");
    for(i = 0; i < pci_device_count(); i++){
        struct pci_dev *dev = pci_get_device(i);
        if(dev && mlx4_en_match(dev))
            mlx4_en_probe(dev);
    }
}
