/*
 * Intel i40e-family NIC driver for auxv6.
 *
 * Covers X710/XL710/XXV710 10/25GbE parts.
 *
 * Full TX/RX descriptor ring machinery.  Ring base addresses require the
 * admin-queue queue-allocate sequence for real hardware bring-up; the
 * QTX_TAIL/QRX_TAIL registers and ring indices are wired here.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

#define I40E_VENDOR_INTEL 0x8086
#define I40E_DEV_X710     0x1572
#define I40E_DEV_XL710    0x1583
#define I40E_DEV_XXV710   0x158B

/* PF LAN queue tail registers (accessible without admin queue) */
#define I40E_QTX_TAIL(q)   (0x00E000 + (q) * 4)
#define I40E_QRX_TAIL(q)   (0x02000  + (q) * 4)
/* Global: interrupt masking */
#define I40E_PFINT_LNKLST0 0x00038000
#define I40E_PFINT_ICR0_ENA 0x00038800
/* Link status – simplified (not real HW bit) */
#define I40E_GLNVM_ULD     0x000B6008

/* TX descriptor command/type bits */
#define I40E_TX_DESC_CMD_EOP   0x0001
#define I40E_TX_DESC_CMD_ICRC  0x0004
#define I40E_TX_DESC_CMD_RS    0x0008
/* TX descriptor done type */
#define I40E_TX_DESC_DTYPE_DONE 0xF
/* RX descriptor status */
#define I40E_RX_DESC_STATUS_DD  0x0001
#define I40E_RX_DESC_STATUS_EOP 0x0002
/* RX packet length field in write-back qword1 */
#define I40E_RX_DESC_LEN_MASK  0x3FFF
#define I40E_RX_DESC_LEN_SHIFT 38

#define I40E_TX_RING_SIZE  64
#define I40E_RX_RING_SIZE  64
#define I40E_RX_BUF_SIZE   2048

#define MAX_I40E 4

/* TX data descriptor (16 bytes) */
struct i40e_tx_desc {
    uint64_t buf_addr;
    uint64_t cmd_type_offset_bsz; /* bits[3:0]=DTYPE; set to DONE when consumed */
} __attribute__((packed));

/* RX descriptor read format (16 bytes) */
struct i40e_rx_desc {
    uint64_t pkt_addr;   /* host gives buffer address */
    uint64_t hdr_addr;   /* header split addr (set 0) */
} __attribute__((packed));

/* RX descriptor write-back format (hardware fills) */
struct i40e_rx_wb {
    uint64_t filter_status;
    uint64_t qword1;   /* bit[0]=DD, bits[38:25]=pkt_len */
} __attribute__((packed));

struct i40e_softc {
    struct pci_dev *pci;
    struct spinlock lock;
    struct ifnet ifn;
    volatile uint32_t *regs;
    uint8_t mac[6];
    struct i40e_tx_desc *tx_ring;
    struct mbuf         *tx_mbufs[I40E_TX_RING_SIZE];
    uint16_t             tx_head;
    uint16_t             tx_tail;
    struct i40e_rx_desc *rx_ring;
    char                *rx_bufs[I40E_RX_RING_SIZE];
    uint16_t             rx_tail;
};

static struct i40e_softc i40e_devices[MAX_I40E];
static int i40e_count;

static int i40e_output(struct ifnet *ifp, struct mbuf *m);
static void i40e_poll(struct ifnet *ifp);

static struct ifnet_ops i40e_ifnet_ops = {
    .if_output = i40e_output,
    .if_poll = i40e_poll,
};

static int
i40e_match(struct pci_dev *dev)
{
    if(!dev || dev->vendor_id != I40E_VENDOR_INTEL)
        return 0;

    switch(dev->device_id){
    case I40E_DEV_X710:
    case I40E_DEV_XL710:
    case I40E_DEV_XXV710:
        return 1;
    default:
        return 0;
    }
}

static uint32_t
i40e_read(struct i40e_softc *sc, int reg)
{
    return sc->regs[reg / 4];
}

static void
i40e_write(struct i40e_softc *sc, int reg, uint32_t val)
{
    sc->regs[reg / 4] = val;
}

static void
i40e_make_local_mac(struct pci_dev *dev, uint8_t mac[6])
{
    mac[0] = 0x02;
    mac[1] = dev->vendor_id & 0xFF;
    mac[2] = dev->device_id & 0xFF;
    mac[3] = dev->bus;
    mac[4] = dev->slot;
    mac[5] = dev->func;
}

static void
i40e_read_mac(struct i40e_softc *sc)
{
    /* i40e MAC is set by firmware via admin queue; use deterministic local */
    i40e_make_local_mac(sc->pci, sc->mac);
    (void)i40e_read; /* suppress unused warning if no other caller */
}

static int
i40e_init_tx(struct i40e_softc *sc)
{
    sc->tx_ring = (struct i40e_tx_desc *)kalloc();
    if(!sc->tx_ring)
        return -1;
    memset(sc->tx_ring, 0, sizeof(struct i40e_tx_desc) * I40E_TX_RING_SIZE);
    sc->tx_head = 0;
    sc->tx_tail = 0;
    /* Ring base addr requires admin-queue queue-allocate on real HW.
     * QTX_TAIL is wired so completions via poll will work once HW is init. */
    i40e_write(sc, I40E_QTX_TAIL(0), 0);
    return 0;
}

static int
i40e_init_rx(struct i40e_softc *sc)
{
    int i;
    sc->rx_ring = (struct i40e_rx_desc *)kalloc();
    if(!sc->rx_ring)
        return -1;
    memset(sc->rx_ring, 0, sizeof(struct i40e_rx_desc) * I40E_RX_RING_SIZE);
    for(i = 0; i < I40E_RX_RING_SIZE; i++){
        sc->rx_bufs[i] = kalloc();
        if(!sc->rx_bufs[i])
            return -1;
        sc->rx_ring[i].pkt_addr = V2P(sc->rx_bufs[i]);
        sc->rx_ring[i].hdr_addr = 0;
    }
    sc->rx_tail = I40E_RX_RING_SIZE - 1;
    i40e_write(sc, I40E_QRX_TAIL(0), sc->rx_tail);
    return 0;
}

static void
i40e_tx_complete(struct i40e_softc *sc)
{
    while(sc->tx_head != sc->tx_tail){
        struct i40e_tx_desc *desc = &sc->tx_ring[sc->tx_head];
        uint8_t dtype = (uint8_t)(desc->cmd_type_offset_bsz & 0xF);
        if(dtype != I40E_TX_DESC_DTYPE_DONE)
            break;
        if(sc->tx_mbufs[sc->tx_head]){
            mbuf_free(sc->tx_mbufs[sc->tx_head]);
            sc->tx_mbufs[sc->tx_head] = 0;
        }
        sc->tx_head = (sc->tx_head + 1) % I40E_TX_RING_SIZE;
    }
}

static void
i40e_rx_complete(struct i40e_softc *sc)
{
    int processed = 0;
    while(processed < 32){
        uint16_t idx = (sc->rx_tail + 1) % I40E_RX_RING_SIZE;
        struct i40e_rx_wb *wb = (struct i40e_rx_wb *)&sc->rx_ring[idx];
        uint64_t qword1 = wb->qword1;
        uint16_t len;
        if((qword1 & I40E_RX_DESC_STATUS_DD) == 0)
            break;
        len = (uint16_t)((qword1 >> 38) & I40E_RX_DESC_LEN_MASK);
        if((qword1 & I40E_RX_DESC_STATUS_EOP) && len > 0 &&
           len <= I40E_RX_BUF_SIZE){
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
        /* repost: clear wb and restore read-format pkt_addr */
        sc->rx_ring[idx].pkt_addr = V2P(sc->rx_bufs[idx]);
        sc->rx_ring[idx].hdr_addr = 0;
        sc->rx_tail = idx;
        i40e_write(sc, I40E_QRX_TAIL(0), sc->rx_tail);
        processed++;
    }
}

static int
i40e_output(struct ifnet *ifp, struct mbuf *m)
{
    struct i40e_softc *sc = (struct i40e_softc *)ifp->if_softc;
    uint16_t next;
    struct i40e_tx_desc *desc;
    uint64_t cmd;

    if(!sc || !m || m->len == 0)
        return -1;
    acquire(&sc->lock);
    if((ifp->if_flags & IFF_RUNNING) == 0){
        release(&sc->lock);
        return -1;
    }
    i40e_tx_complete(sc);
    next = (sc->tx_tail + 1) % I40E_TX_RING_SIZE;
    if(next == sc->tx_head){
        release(&sc->lock);
        return -1;
    }
    desc = &sc->tx_ring[sc->tx_tail];
    desc->buf_addr = V2P(m->data);
    cmd = ((uint64_t)m->len << 34) |
          ((uint64_t)(I40E_TX_DESC_CMD_EOP | I40E_TX_DESC_CMD_ICRC |
                      I40E_TX_DESC_CMD_RS) << 4) | 0; /* DTYPE=data=0 */
    desc->cmd_type_offset_bsz = cmd;
    sc->tx_mbufs[sc->tx_tail] = m;
    sc->tx_tail = next;
    i40e_write(sc, I40E_QTX_TAIL(0), sc->tx_tail);
    release(&sc->lock);
    return 0;
}

static void
i40e_poll(struct ifnet *ifp)
{
    struct i40e_softc *sc = (struct i40e_softc *)ifp->if_softc;
    if(!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;
    acquire(&sc->lock);
    i40e_tx_complete(sc);
    i40e_rx_complete(sc);
    release(&sc->lock);
}

static void
i40e_probe(struct pci_dev *dev)
{
    struct i40e_softc *sc;

    if(i40e_count >= MAX_I40E)
        return;

    sc = &i40e_devices[i40e_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "i40e");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "i40e");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    sc->regs = (volatile uint32_t *)pci_map_bar(dev, 0);
    if(!sc->regs){
        cprintf("i40e: failed to map BAR0\n");
        return;
    }
    i40e_read_mac(sc);
    if(i40e_init_tx(sc) < 0 || i40e_init_rx(sc) < 0){
        cprintf("i40e: failed to init rings\n");
        return;
    }

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "i40e0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[4] = '0' + i40e_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_RUNNING | IFF_BROADCAST;
    memmove(sc->ifn.if_hwaddr, sc->mac, 6);
    sc->ifn.if_softc = sc;
    sc->ifn.if_ops = &i40e_ifnet_ops;
    sc->ifn.if_input = ether_input;

    if(if_register(&sc->ifn) < 0){
        cprintf("i40e: if_register failed for %x\n", dev->device_id);
        return;
    }

    cprintf("i40e: attached %s dev=%x irq=%d mac=%x:%x:%x:%x:%x:%x\n",
            sc->ifn.if_xname, dev->device_id, dev->irq_line,
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
    i40e_count++;
}

void
i40e_init(void)
{
    int i;

    BOOTDBG("i40e: probing supported PCI IDs\n");
    for(i = 0; i < pci_device_count(); i++){
        struct pci_dev *dev = pci_get_device(i);
        if(dev && i40e_match(dev))
            i40e_probe(dev);
    }
}
