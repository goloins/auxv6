/*
 * VMware VMXnet3 Paravirtualized Ethernet Driver for auxv6
 *
 * Supports VMware's high-performance virtual network adapter.
 * Common in VMware ESXi, Workstation, and Fusion.
 *
 * Architecture:
 * - Memory-mapped I/O via BAR0 (PT) and BAR1 (VD)
 * - Multi-queue capable
 * - Supports hardware offloads (TSO, checksum)
 * - Integrates with ifnet layer via if_register()
 *
 * TODO Phase 1:
 * - [ ] PCI detection and BAR mapping
 * - [ ] Device initialization and activation
 * - [ ] Basic TX/RX with single queue
 * - [ ] MAC address configuration
 *
 * TODO Phase 2:
 * - [ ] Multi-queue support
 * - [ ] Checksum offload
 * - [ ] TSO support
 * - [ ] RSS support
 *
 * Reference: VMware VMXNET3 Driver Programming Guide
 * See also: Linux drivers/net/vmxnet3/vmxnet3_drv.c
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"
#include "mmu.h"

/* PCI Vendor/Device IDs */
#define VMXNET3_VENDOR_ID       0x15AD  /* VMware */
#define VMXNET3_DEVICE_ID       0x07B0  /* VMXnet3 */

/* BAR indices */
#define VMXNET3_BAR_PT          0       /* Passthrough */
#define VMXNET3_BAR_VD          1       /* Virtual Device */
#define VMXNET3_BAR_MSIX        2       /* MSI-X table */

/* Passthrough (PT) register offsets */
#define VMXNET3_REG_VRRS        0x000   /* VMXNET3 Revision Report Selection */
#define VMXNET3_REG_UVRS        0x008   /* UPT Version Report Selection */
#define VMXNET3_REG_DSAL        0x010   /* Driver Shared Address Low */
#define VMXNET3_REG_DSAH        0x018   /* Driver Shared Address High */
#define VMXNET3_REG_CMD         0x020   /* Command */
#define VMXNET3_REG_MACL        0x028   /* MAC Address Low */
#define VMXNET3_REG_MACH        0x030   /* MAC Address High */
#define VMXNET3_REG_ICR         0x038   /* Interrupt Cause Register */
#define VMXNET3_REG_ECR         0x040   /* Event Cause Register */

/* Virtual Device (VD) register offsets */
#define VMXNET3_REG_IMR         0x000   /* Interrupt Mask Register */
#define VMXNET3_REG_TXPROD      0x600   /* TX Ring Producer */
#define VMXNET3_REG_RXPROD      0x800   /* RX Ring 1 Producer */
#define VMXNET3_REG_RXPROD2     0xA00   /* RX Ring 2 Producer */

/* Commands */
#define VMXNET3_CMD_ENABLE      0xCAFE0000
#define VMXNET3_CMD_DISABLE     0xCAFE0001
#define VMXNET3_CMD_RESET       0xCAFE0002
#define VMXNET3_CMD_SET_RXMODE  0xCAFE0003
#define VMXNET3_CMD_SET_FILTER  0xCAFE0004
#define VMXNET3_CMD_SET_FEATURE 0xCAFE0009
#define VMXNET3_CMD_GET_STATUS  0xF00D0000
#define VMXNET3_CMD_GET_STATS   0xF00D0001
#define VMXNET3_CMD_GET_LINK    0xF00D0002
#define VMXNET3_CMD_GET_MACL    0xF00D0003
#define VMXNET3_CMD_GET_MACH    0xF00D0004
#define VMXNET3_CMD_GET_INTRCFG 0xF00D0008

/* Ring sizes */
#define VMXNET3_RING_SIZE_ALIGN 32
#define VMXNET3_TX_RING_SIZE    128
#define VMXNET3_RX_RING_SIZE    128
#define VMXNET3_RX_COMP_RING_SIZE (VMXNET3_RX_RING_SIZE * 2)
#define VMXNET3_RX_BUF_SIZE     2048

#define VMXNET3_INIT_GEN        1

#define VMXNET3_REV1_MAGIC      0xbabefee1
#define VMXNET3_DRIVER_VERSION  0x00010000

#define VMXNET3_GOS_UNKNOWN 0x00
#define VMXNET3_GOS_LINUX   0x04
#define VMXNET3_GOS_32BIT   0x01

#define VMXNET3_MAX_TX_QUEUES 8
#define VMXNET3_MAX_RX_QUEUES 16
#define VMXNET3_MAX_INTRS (VMXNET3_MAX_TX_QUEUES + VMXNET3_MAX_RX_QUEUES + 1)

#define VMXNET3_ICTRL_DISABLE_ALL 0x01

#define VMXNET3_RXMODE_UCAST    0x01
#define VMXNET3_RXMODE_MCAST    0x02
#define VMXNET3_RXMODE_BCAST    0x04

#define VMXNET3_TX_LEN_M   0x00003fff
#define VMXNET3_TX_LEN_S   0
#define VMXNET3_TX_GEN_M   0x00000001U
#define VMXNET3_TX_GEN_S   14

#define VMXNET3_TX_EOP     0x00001000
#define VMXNET3_TX_COMPREQ 0x00002000

#define VMXNET3_TXC_EOPIDX_M 0x00000fff
#define VMXNET3_TXC_EOPIDX_S 0
#define VMXNET3_TXC_GEN_M  0x00000001U
#define VMXNET3_TXC_GEN_S  31

#define VMXNET3_RX_LEN_M   0x00003fff
#define VMXNET3_RX_LEN_S   0
#define VMXNET3_RX_BTYPE_S 14
#define VMXNET3_RX_GEN_M   0x00000001U
#define VMXNET3_RX_GEN_S   31

#define VMXNET3_RXC_IDX_M  0x00000fff
#define VMXNET3_RXC_IDX_S  0
#define VMXNET3_RXC_EOP    0x00004000
#define VMXNET3_RXC_SOP    0x00008000
#define VMXNET3_RXC_QID_M  0x000003ff
#define VMXNET3_RXC_QID_S  16
#define VMXNET3_RXC_LEN_M  0x00003fff
#define VMXNET3_RXC_LEN_S  0
#define VMXNET3_RXC_ERROR  0x00004000
#define VMXNET3_RXC_GEN_M  0x00000001U
#define VMXNET3_RXC_GEN_S  31

#define VMXNET3_BTYPE_HEAD 0
#define VMXNET3_BTYPE_BODY 1

#define VMX_TX_GEN  (VMXNET3_TX_GEN_M << VMXNET3_TX_GEN_S)
#define VMX_TXC_GEN (VMXNET3_TXC_GEN_M << VMXNET3_TXC_GEN_S)
#define VMX_RX_GEN  (VMXNET3_RX_GEN_M << VMXNET3_RX_GEN_S)
#define VMX_RXC_GEN (VMXNET3_RXC_GEN_M << VMXNET3_RXC_GEN_S)

struct UPT1_TxStats {
    uint64_t TSO_packets;
    uint64_t TSO_bytes;
    uint64_t ucast_packets;
    uint64_t ucast_bytes;
    uint64_t mcast_packets;
    uint64_t mcast_bytes;
    uint64_t bcast_packets;
    uint64_t bcast_bytes;
    uint64_t error;
    uint64_t discard;
} __attribute__((packed));

struct UPT1_RxStats {
    uint64_t LRO_packets;
    uint64_t LRO_bytes;
    uint64_t ucast_packets;
    uint64_t ucast_bytes;
    uint64_t mcast_packets;
    uint64_t mcast_bytes;
    uint64_t bcast_packets;
    uint64_t bcast_bytes;
    uint64_t nobuffer;
    uint64_t error;
} __attribute__((packed));

struct vmxnet3_txdesc {
    uint64_t addr;
    uint32_t word2;
    uint32_t word3;
} __attribute__((packed));

struct vmxnet3_txcompdesc {
    uint32_t word0;
    uint32_t word1;
    uint32_t word2;
    uint32_t word3;
} __attribute__((packed));

struct vmxnet3_rxdesc {
    uint64_t addr;
    uint32_t word2;
    uint32_t word3;
} __attribute__((packed));

struct vmxnet3_rxcompdesc {
    uint32_t word0;
    uint32_t word1;
    uint32_t word2;
    uint32_t word3;
} __attribute__((packed));

struct vmxnet3_driver_shared {
    uint32_t magic;
    uint32_t pad1;
    uint32_t version;
    uint32_t guest;
    uint32_t vmxnet3_revision;
    uint32_t upt_version;
    uint64_t upt_features;
    uint64_t driver_data;
    uint64_t queue_shared;
    uint32_t driver_data_len;
    uint32_t queue_shared_len;
    uint32_t mtu;
    uint16_t nrxsg_max;
    uint8_t ntxqueue;
    uint8_t nrxqueue;
    uint32_t reserved1[4];
    uint8_t automask;
    uint8_t nintr;
    uint8_t evintr;
    uint8_t modlevel[VMXNET3_MAX_INTRS];
    uint32_t ictrl;
    uint32_t reserved2[2];
    uint32_t rxmode;
    uint16_t mcast_tablelen;
    uint16_t pad2;
    uint64_t mcast_table;
    uint32_t vlan_filter[4096 / 32];
    struct {
        uint32_t version;
        uint32_t len;
        uint64_t paddr;
    } rss, pm, plugin;
    uint32_t event;
    uint32_t reserved3[5];
} __attribute__((packed));

struct vmxnet3_txq_shared {
    uint32_t npending;
    uint32_t intr_threshold;
    uint64_t reserved1;
    uint64_t cmd_ring;
    uint64_t data_ring;
    uint64_t comp_ring;
    uint64_t driver_data;
    uint64_t reserved2;
    uint32_t cmd_ring_len;
    uint32_t data_ring_len;
    uint32_t comp_ring_len;
    uint32_t driver_data_len;
    uint8_t intr_idx;
    uint8_t pad1[7];
    uint8_t stopped;
    uint8_t pad2[3];
    uint32_t error;
    struct UPT1_TxStats stats;
    uint8_t pad3[88];
} __attribute__((packed));

struct vmxnet3_rxq_shared {
    uint8_t update_rxhead;
    uint8_t pad1[7];
    uint64_t reserved1;
    uint64_t cmd_ring[2];
    uint64_t comp_ring;
    uint64_t driver_data;
    uint64_t reserved2;
    uint32_t cmd_ring_len[2];
    uint32_t comp_ring_len;
    uint32_t driver_data_len;
    uint8_t intr_idx;
    uint8_t pad2[7];
    uint8_t stopped;
    uint8_t pad3[3];
    uint32_t error;
    struct UPT1_RxStats stats;
    uint8_t pad4[88];
} __attribute__((packed));

struct vmxnet3_txring {
    struct vmxnet3_txdesc *desc;
    struct mbuf *mbufs[VMXNET3_TX_RING_SIZE];
    uint32_t prod;
    uint32_t cons;
    uint32_t gen;
};

struct vmxnet3_rxring {
    struct vmxnet3_rxdesc *desc;
    struct mbuf *mbufs[VMXNET3_RX_RING_SIZE];
    uint32_t fill;
    uint32_t gen;
    uint8_t rid;
    uint32_t rxh_reg;
};

struct vmxnet3_comp_ring {
    union {
        struct vmxnet3_txcompdesc *txcd;
        struct vmxnet3_rxcompdesc *rxcd;
    } u;
    uint32_t next;
    uint32_t gen;
};

/* Per-device state */
struct vmxnet3_softc {
    struct pci_dev    *pci;
    struct spinlock    lock;
    struct ifnet       ifn;
    
    volatile uint32_t *pt_regs;     /* Passthrough registers */
    volatile uint32_t *vd_regs;     /* Virtual device registers */
    
    uint8_t            mac[6];

    struct vmxnet3_driver_shared *ds;
    struct vmxnet3_txq_shared *txs;
    struct vmxnet3_rxq_shared *rxs;

    struct vmxnet3_txring tx_ring;
    struct vmxnet3_comp_ring tx_comp;

    struct vmxnet3_rxring rx_ring[2];
    struct vmxnet3_comp_ring rx_comp;
};

static int vmxnet3_output(struct ifnet *ifp, struct mbuf *m);
static void vmxnet3_poll(struct ifnet *ifp);

static struct ifnet_ops vmxnet3_ifnet_ops = {
    .if_output = vmxnet3_output,
    .if_poll = vmxnet3_poll,
};

/* Global array */
#define MAX_VMXNET3 4
static struct vmxnet3_softc vmxnet3_devices[MAX_VMXNET3];
static int vmxnet3_count = 0;
extern int ncpu;

static int
vmxnet3_match(struct pci_dev *dev)
{
    if (dev == 0)
        return 0;
    return (dev->vendor_id == VMXNET3_VENDOR_ID &&
            dev->device_id == VMXNET3_DEVICE_ID);
}

/* Read PT register */
static uint32_t
vmxnet3_pt_read(struct vmxnet3_softc *sc, int reg)
{
    return sc->pt_regs[reg / 4];
}

/* Write PT register */
static void
vmxnet3_pt_write(struct vmxnet3_softc *sc, int reg, uint32_t val)
{
    sc->pt_regs[reg / 4] = val;
}

static void
vmxnet3_read_mac(struct vmxnet3_softc *sc)
{
    uint32_t macl, mach;
    
    /* Get permanent MAC address via command */
    vmxnet3_pt_write(sc, VMXNET3_REG_CMD, VMXNET3_CMD_GET_MACL);
    macl = vmxnet3_pt_read(sc, VMXNET3_REG_CMD);
    
    vmxnet3_pt_write(sc, VMXNET3_REG_CMD, VMXNET3_CMD_GET_MACH);
    mach = vmxnet3_pt_read(sc, VMXNET3_REG_CMD);
    
    sc->mac[0] = (macl >> 0) & 0xFF;
    sc->mac[1] = (macl >> 8) & 0xFF;
    sc->mac[2] = (macl >> 16) & 0xFF;
    sc->mac[3] = (macl >> 24) & 0xFF;
    sc->mac[4] = (mach >> 0) & 0xFF;
    sc->mac[5] = (mach >> 8) & 0xFF;
    
    cprintf("vmxnet3: MAC %x:%x:%x:%x:%x:%x\n",
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
}

static inline void
vmxnet3_barrier(void)
{
    __sync_synchronize();
}

static int
vmxnet3_tx_avail(struct vmxnet3_txring *ring)
{
    int avail = (int)ring->cons - (int)ring->prod - 1;
    if (avail < 0)
        avail += VMXNET3_TX_RING_SIZE;
    return avail;
}

static void
vmxnet3_tx_complete(struct vmxnet3_softc *sc)
{
    struct vmxnet3_comp_ring *comp = &sc->tx_comp;
    struct vmxnet3_txring *ring = &sc->tx_ring;

    for (;;) {
        struct vmxnet3_txcompdesc *txcd = &comp->u.txcd[comp->next];
        if ((txcd->word3 & VMX_TXC_GEN) != comp->gen)
            break;

        uint32_t eop = (txcd->word0 >> VMXNET3_TXC_EOPIDX_S) & VMXNET3_TXC_EOPIDX_M;
        uint32_t idx = ring->cons;

        for (;;) {
            if (ring->mbufs[idx]) {
                mbuf_free(ring->mbufs[idx]);
                ring->mbufs[idx] = 0;
            }
            if (idx == eop)
                break;
            idx = (idx + 1) % VMXNET3_TX_RING_SIZE;
        }

        ring->cons = (eop + 1) % VMXNET3_TX_RING_SIZE;

        comp->next++;
        if (comp->next == VMXNET3_TX_RING_SIZE) {
            comp->next = 0;
            comp->gen ^= VMX_TXC_GEN;
        }
    }
}

static int
vmxnet3_rx_fill_one(struct vmxnet3_softc *sc, struct vmxnet3_rxring *ring)
{
    struct vmxnet3_rxdesc *rxd;
    struct mbuf *m;
    uint32_t word2;
    uint32_t idx = ring->fill;

    m = mbuf_alloc();
    if (!m)
        return -1;

    ring->mbufs[idx] = m;
    rxd = &ring->desc[idx];
    rxd->addr = V2P(m->data);

    word2 = (VMXNET3_RX_BUF_SIZE & VMXNET3_RX_LEN_M) << VMXNET3_RX_LEN_S;
    word2 |= (ring->rid == 0 ? VMXNET3_BTYPE_HEAD : VMXNET3_BTYPE_BODY) << VMXNET3_RX_BTYPE_S;
    word2 |= (ring->gen & 1U) << VMXNET3_RX_GEN_S;
    rxd->word2 = word2;
    rxd->word3 = 0;

    ring->fill++;
    if (ring->fill == VMXNET3_RX_RING_SIZE) {
        ring->fill = 0;
        ring->gen ^= 1;
    }

    if (sc->rxs && sc->rxs->update_rxhead)
        vmxnet3_pt_write(sc, ring->rxh_reg, ring->fill);

    return 0;
}

static int
vmxnet3_rx_fill_ring(struct vmxnet3_softc *sc, struct vmxnet3_rxring *ring)
{
    for (int i = 0; i < VMXNET3_RX_RING_SIZE; i++) {
        if (vmxnet3_rx_fill_one(sc, ring) < 0)
            return -1;
    }
    return 0;
}

static void
vmxnet3_rx_complete(struct vmxnet3_softc *sc)
{
    struct vmxnet3_comp_ring *comp = &sc->rx_comp;

    for (;;) {
        struct vmxnet3_rxcompdesc *rxcd = &comp->u.rxcd[comp->next];
        if ((rxcd->word3 & VMX_RXC_GEN) != comp->gen)
            break;

        uint32_t idx = (rxcd->word0 >> VMXNET3_RXC_IDX_S) & VMXNET3_RXC_IDX_M;
        uint32_t qid = (rxcd->word0 >> VMXNET3_RXC_QID_S) & VMXNET3_RXC_QID_M;
        uint32_t rid = (qid < 1) ? 0 : 1;

        if (rid > 1) {
            comp->next++;
            if (comp->next == VMXNET3_RX_COMP_RING_SIZE) {
                comp->next = 0;
                comp->gen ^= VMX_RXC_GEN;
            }
            continue;
        }

        struct vmxnet3_rxring *ring = &sc->rx_ring[rid];
        struct mbuf *m = ring->mbufs[idx];
        ring->mbufs[idx] = 0;

        if (m) {
            uint32_t len = (rxcd->word2 >> VMXNET3_RXC_LEN_S) & VMXNET3_RXC_LEN_M;
            if ((rxcd->word2 & VMXNET3_RXC_ERROR) ||
                (rxcd->word0 & (VMXNET3_RXC_EOP | VMXNET3_RXC_SOP)) !=
                (VMXNET3_RXC_EOP | VMXNET3_RXC_SOP) ||
                len == 0 || len > VMXNET3_RX_BUF_SIZE) {
                mbuf_free(m);
            } else {
                m->len = len;
                m->rcvif = &sc->ifn;
                if_input(&sc->ifn, m);
            }
        }

        ring->fill = idx;
        vmxnet3_rx_fill_one(sc, ring);

        comp->next++;
        if (comp->next == VMXNET3_RX_COMP_RING_SIZE) {
            comp->next = 0;
            comp->gen ^= VMX_RXC_GEN;
        }
    }
}

static void
vmxnet3_poll(struct ifnet *ifp)
{
    struct vmxnet3_softc *sc = (struct vmxnet3_softc *)ifp->if_softc;

    if (!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;

    acquire(&sc->lock);
    vmxnet3_tx_complete(sc);
    vmxnet3_rx_complete(sc);
    release(&sc->lock);
}

static int
vmxnet3_output(struct ifnet *ifp, struct mbuf *m)
{
    struct vmxnet3_softc *sc = (struct vmxnet3_softc *)ifp->if_softc;
    struct vmxnet3_txring *ring;
    struct vmxnet3_txdesc *txd;
    uint32_t idx;
    uint32_t word2;

    if (!sc || !m || m->len == 0)
        return -1;

    acquire(&sc->lock);

    if ((ifp->if_flags & IFF_RUNNING) == 0) {
        release(&sc->lock);
        return -1;
    }

    vmxnet3_tx_complete(sc);

    ring = &sc->tx_ring;
    if (vmxnet3_tx_avail(ring) <= 0) {
        release(&sc->lock);
        return -1;
    }

    idx = ring->prod;
    txd = &ring->desc[idx];
    txd->addr = V2P(m->data);

    word2 = (m->len & VMXNET3_TX_LEN_M) << VMXNET3_TX_LEN_S;
    word2 |= (ring->gen & 1U) << VMXNET3_TX_GEN_S;
    txd->word2 = word2;
    txd->word3 = VMXNET3_TX_EOP | VMXNET3_TX_COMPREQ;

    ring->mbufs[idx] = m;
    ring->prod++;
    if (ring->prod == VMXNET3_TX_RING_SIZE) {
        ring->prod = 0;
        ring->gen ^= 1;
    }

    vmxnet3_barrier();
    vmxnet3_pt_write(sc, VMXNET3_REG_TXPROD, ring->prod);

    release(&sc->lock);

    return 0;
}

static void *
vmxnet3_alloc_ring(uint size)
{
    void *mem;

    if (size > PGSIZE)
        return 0;

    mem = kalloc();
    if (!mem)
        return 0;

    memset(mem, 0, size);
    return mem;
}

static int
vmxnet3_setup(struct vmxnet3_softc *sc)
{
    uint size;
    void *qs;

    sc->ds = (struct vmxnet3_driver_shared *)kalloc();
    if (!sc->ds)
        return -1;
    memset(sc->ds, 0, sizeof(*sc->ds));

    size = sizeof(struct vmxnet3_txq_shared) + sizeof(struct vmxnet3_rxq_shared);
    if (size > PGSIZE)
        return -1;

    qs = kalloc();
    if (!qs)
        return -1;
    memset(qs, 0, size);
    sc->txs = (struct vmxnet3_txq_shared *)qs;
    sc->rxs = (struct vmxnet3_rxq_shared *)((char *)qs + sizeof(*sc->txs));

    size = VMXNET3_TX_RING_SIZE * sizeof(struct vmxnet3_txdesc);
    sc->tx_ring.desc = (struct vmxnet3_txdesc *)vmxnet3_alloc_ring(size);
    if (!sc->tx_ring.desc)
        return -1;

    size = VMXNET3_TX_RING_SIZE * sizeof(struct vmxnet3_txcompdesc);
    sc->tx_comp.u.txcd = (struct vmxnet3_txcompdesc *)vmxnet3_alloc_ring(size);
    if (!sc->tx_comp.u.txcd)
        return -1;

    size = VMXNET3_RX_RING_SIZE * sizeof(struct vmxnet3_rxdesc);
    sc->rx_ring[0].desc = (struct vmxnet3_rxdesc *)vmxnet3_alloc_ring(size);
    sc->rx_ring[1].desc = (struct vmxnet3_rxdesc *)vmxnet3_alloc_ring(size);
    if (!sc->rx_ring[0].desc || !sc->rx_ring[1].desc)
        return -1;

    size = VMXNET3_RX_COMP_RING_SIZE * sizeof(struct vmxnet3_rxcompdesc);
    sc->rx_comp.u.rxcd = (struct vmxnet3_rxcompdesc *)vmxnet3_alloc_ring(size);
    if (!sc->rx_comp.u.rxcd)
        return -1;

    sc->tx_ring.prod = 0;
    sc->tx_ring.cons = 0;
    sc->tx_ring.gen = VMXNET3_INIT_GEN;
    sc->tx_comp.next = 0;
    sc->tx_comp.gen = VMX_TXC_GEN;

    sc->rx_ring[0].rid = 0;
    sc->rx_ring[0].fill = 0;
    sc->rx_ring[0].gen = VMXNET3_INIT_GEN;
    sc->rx_ring[0].rxh_reg = VMXNET3_REG_RXPROD;

    sc->rx_ring[1].rid = 1;
    sc->rx_ring[1].fill = 0;
    sc->rx_ring[1].gen = VMXNET3_INIT_GEN;
    sc->rx_ring[1].rxh_reg = VMXNET3_REG_RXPROD2;

    sc->rx_comp.next = 0;
    sc->rx_comp.gen = VMX_RXC_GEN;

    if (vmxnet3_rx_fill_ring(sc, &sc->rx_ring[0]) < 0)
        return -1;
    if (vmxnet3_rx_fill_ring(sc, &sc->rx_ring[1]) < 0)
        return -1;

    sc->txs->cmd_ring = V2P(sc->tx_ring.desc);
    sc->txs->data_ring = 0;
    sc->txs->comp_ring = V2P(sc->tx_comp.u.txcd);
    sc->txs->cmd_ring_len = VMXNET3_TX_RING_SIZE;
    sc->txs->data_ring_len = 0;
    sc->txs->comp_ring_len = VMXNET3_TX_RING_SIZE;
    sc->txs->driver_data = ~0ULL;
    sc->txs->driver_data_len = 0;
    sc->txs->intr_idx = 0;

    sc->rxs->update_rxhead = 1;
    sc->rxs->cmd_ring[0] = V2P(sc->rx_ring[0].desc);
    sc->rxs->cmd_ring[1] = V2P(sc->rx_ring[1].desc);
    sc->rxs->cmd_ring_len[0] = VMXNET3_RX_RING_SIZE;
    sc->rxs->cmd_ring_len[1] = VMXNET3_RX_RING_SIZE;
    sc->rxs->comp_ring = V2P(sc->rx_comp.u.rxcd);
    sc->rxs->comp_ring_len = VMXNET3_RX_COMP_RING_SIZE;
    sc->rxs->driver_data = ~0ULL;
    sc->rxs->driver_data_len = 0;
    sc->rxs->intr_idx = 0;

    sc->ds->magic = VMXNET3_REV1_MAGIC;
    sc->ds->version = VMXNET3_DRIVER_VERSION;
    sc->ds->guest = VMXNET3_GOS_LINUX | VMXNET3_GOS_32BIT;
    sc->ds->vmxnet3_revision = 1;
    sc->ds->upt_version = 1;
    sc->ds->driver_data = ~0ULL;
    sc->ds->queue_shared = V2P(qs);
    sc->ds->queue_shared_len = sizeof(struct vmxnet3_txq_shared) + sizeof(struct vmxnet3_rxq_shared);
    sc->ds->mtu = sc->ifn.if_mtu;
    sc->ds->nrxsg_max = 1;
    sc->ds->ntxqueue = 1;
    sc->ds->nrxqueue = 1;
    sc->ds->automask = 1;
    sc->ds->nintr = 1;
    sc->ds->evintr = 0;
    sc->ds->modlevel[0] = 0;
    sc->ds->ictrl = VMXNET3_ICTRL_DISABLE_ALL;
    sc->ds->rxmode = VMXNET3_RXMODE_UCAST | VMXNET3_RXMODE_MCAST | VMXNET3_RXMODE_BCAST;

    vmxnet3_pt_write(sc, VMXNET3_REG_DSAL, V2P(sc->ds));
    vmxnet3_pt_write(sc, VMXNET3_REG_DSAH, 0);

    vmxnet3_pt_write(sc, VMXNET3_REG_CMD, VMXNET3_CMD_ENABLE);
    vmxnet3_pt_write(sc, VMXNET3_REG_CMD, VMXNET3_CMD_SET_RXMODE);

    return 0;
}

static int
vmxnet3_probe(struct pci_dev *dev)
{
    struct vmxnet3_softc *sc;
    
    if (vmxnet3_count >= MAX_VMXNET3)
        return -1;
    
    sc = &vmxnet3_devices[vmxnet3_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "vmxnet3");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "vmxnet3");
    sc->pci = dev;
    
    /* Enable memory and bus master */
    pci_enable_mem(dev);
    pci_set_master(dev);
    
    /* Map BAR0 (PT) and BAR1 (VD) */
    sc->pt_regs = pci_map_bar(dev, VMXNET3_BAR_PT);
    sc->vd_regs = pci_map_bar(dev, VMXNET3_BAR_VD);
    
    if (!sc->pt_regs || !sc->vd_regs) {
        cprintf("vmxnet3: failed to map registers\n");
        return -1;
    }
    
    BOOTDBG("vmxnet3: found at %d:%d.%d irq=%d pt=%p vd=%p\n",
            dev->bus, dev->slot, dev->func, dev->irq_line,
            sc->pt_regs, sc->vd_regs);
    
    /* Read MAC address */
    vmxnet3_read_mac(sc);
    
    /* Set up ifnet structure */
    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "vmx0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[3] = '0' + vmxnet3_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_BROADCAST;
    memmove(sc->ifn.if_hwaddr, sc->mac, sizeof(sc->ifn.if_hwaddr));
    sc->ifn.if_softc = sc;
    sc->ifn.if_input = ether_input;
    sc->ifn.if_ops = &vmxnet3_ifnet_ops;

    if (vmxnet3_setup(sc) < 0) {
        cprintf("vmxnet3: failed to initialize rings\n");
        return -1;
    }

    vmxnet3_pt_write(sc, VMXNET3_REG_CMD, VMXNET3_CMD_GET_LINK);
    if (vmxnet3_pt_read(sc, VMXNET3_REG_CMD))
        sc->ifn.if_flags |= IFF_RUNNING;

    if (if_register(&sc->ifn) < 0) {
        cprintf("vmxnet3: failed to register ifnet\n");
        return -1;
    }
    
    vmxnet3_count++;
    cprintf("vmxnet3: attached %s (polling)\n", sc->ifn.if_xname);
    
    return 0;
}

void
vmxnet3_init(void)
{
    int i;
    struct pci_dev *dev;
    
    BOOTDBG("vmxnet3: initializing driver\n");
    
    for (i = 0; i < pci_device_count(); i++) {
        dev = pci_get_device(i);
        if (vmxnet3_match(dev))
            vmxnet3_probe(dev);
    }
}
