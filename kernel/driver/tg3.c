/*
 * Broadcom BCM5700/BCM5719/BCM5720 Gigabit Ethernet Driver for auxv6
 *
 * Covers the tg3 family of Broadcom NetXtreme I chips:
 *   BCM5700  (14E4:1644) — PCI/PCI-X, original tg3 generation
 *   BCM5719  (14E4:1657) — PCIe, quad-port
 *   BCM5720  (14E4:165F) — PCIe, quad-port, low-power variant
 *
 * Architecture (BSD tg3 / Linux tg3):
 *   64KB MMIO via BAR0.  Host-side producer/consumer ring model.
 *   A DMA'd status block carries ring consumer indices; the host polls
 *   the status block instead of decoding per-descriptor status words.
 *   TX and RX return rings are producer/consumer indexed via status block.
 *
 * Current tranche: PCI probe, BAR map, ring + status-block allocation,
 *   MAC address read, polling TX/RX skeleton with ifnet registration.
 *   Full ring control block (RCB) programming, NVRAM init, PHY bring-up,
 *   and DMA coalescing configuration are deferred to a later tranche.
 *
 * Reference: Broadcom BCM5719 Programmer's Guide
 *            Linux drivers/net/ethernet/broadcom/tg3.c
 *            NetBSD sys/dev/pci/if_bge.c (OpenBSD/NetBSD bge driver)
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

/* PCI IDs */
#define TG3_VENDOR          0x14E4   /* Broadcom */
#define TG3_DEV_BCM5700     0x1644
#define TG3_DEV_BCM5719     0x1657
#define TG3_DEV_BCM5720     0x165F

/*
 * MMIO register map — offsets from BAR0.
 * Registers are 32-bit unless noted.  Access is via memory-mapped 32-bit reads.
 *
 * Reference: tg3.h (Linux), if_bgereg.h (NetBSD/OpenBSD).
 */

/* Miscellaneous host control */
#define TG3_MISC_HOST_CTRL          0x68
#define  MISC_HOST_CTRL_MASK_PCI_INT    0x00000001
#define  MISC_HOST_CTRL_BYTE_SWAP       0x00000002
#define  MISC_HOST_CTRL_WORD_SWAP       0x00000004
#define  MISC_HOST_CTRL_USE_MEM_RD_MUL  0x00000020
#define  MISC_HOST_CTRL_TAGGED_STATUS   0x00000200

/* Miscellaneous local control */
#define TG3_MISC_LOCAL_CTRL         0x6C
#define  MISC_LOCAL_CTRL_INT_ENABLE     0x00008000

/* PCI state register (inside NIC) */
#define TG3_PCISTATE                0x70
#define  PCISTATE_CONV_PCI_MODE         0x00000001
#define  PCISTATE_BUS_IS_PCI            0x00000004
#define  PCISTATE_BUS_CLK_IS_HIGH       0x00000010

/* Memory window base (used for indirect register access on some variants) */
#define TG3_WIN_BASE                0x7C
#define TG3_WIN_DATA                0x84

/* MAC address registers (low 32 bits, high 16 bits) */
#define TG3_MAC_ADDR_0_HIGH         0x410
#define TG3_MAC_ADDR_0_LOW          0x414

/* Mode control */
#define TG3_MODE_CTRL               0x680
#define  MODE_CTRL_BYTE_SWAP_DATA       0x00000004
#define  MODE_CTRL_WORD_SWAP_DATA       0x00000010
#define  MODE_CTRL_SEND_COALS_NOW       0x00000080

/* Global control */
#define TG3_GLOBAL_CTRL             0x6800
#define  GLOBAL_CTRL_ENABLE_TX          0x00000001
#define  GLOBAL_CTRL_ENABLE_RX          0x00000002

/* TX ring host producer index (NIC mailbox) */
#define TG3_TX_RING_PROD_IDX_0      0x100    /* TX ring 0 host prod index */

/* RX ring host producer index */
#define TG3_RX_STD_PROD_IDX         0x268    /* Standard RX ring producer */

/* Status block NIC address (used in some init sequences) */
#define TG3_STATUS_BLK_NIC_ADDR     0x0B00

/* DMA read/write config */
#define TG3_DMA_RW_CTRL             0x6DC
#define  DMA_RWCTRL_ASSERT_ALL_BE       0x00000001

/* Interrupt mailbox clear */
#define TG3_INT_MBOX_0              0x200    /* Interrupt mailbox 0 */

/* Global MSI enable */
#define TG3_MSI_MAP_0               0x50C

/* Software reset */
#define TG3_GRC_MISC_CFG            0x6804
#define  GRC_MISC_CFG_CORECLK_RESET     0x00000001
#define  GRC_MISC_CFG_KEEP_GPHY_POWER   0x00000004

/* PHY control (MDIO via memory-mapped BMCR region) */
#define TG3_PHY_CTRL                0x810
#define TG3_PHY_STATUS              0x814
#define TG3_PHY_LINKUP              0x00000004   /* status register bit 2 */

/* Ring sizes */
#define TG3_TX_RING_SIZE    512     /* Must be power of 2 */
#define TG3_RX_RING_SIZE    512
#define TG3_RX_BUF_SIZE     1536

/* TX descriptor: the tg3 "legacy" host-side TX descriptor */
struct tg3_tx_desc {
    uint32_t addr_hi;
    uint32_t addr_lo;
    uint32_t len_flags;  /* [31:16]=len, [15:0]=flags */
    uint32_t vlan_tag;
} __attribute__((packed));

#define TG3_TXD_FLAG_END        0x00000001  /* Last fragment */
#define TG3_TXD_FLAG_NO_CRC     0x00000002
#define TG3_TXD_FLAG_IP_CSUM    0x00000400
#define TG3_TXD_FLAG_TCP_CSUM   0x00000800

/* RX return descriptor */
struct tg3_rx_desc {
    uint32_t addr_hi;
    uint32_t addr_lo;
    uint32_t idx_len;    /* [31:16]=ring index, [15:0]=frame length */
    uint32_t type_flags; /* error/type bits */
    uint32_t ip_tcp_csum;
    uint32_t err_vlan;
    uint16_t rss_hash;
    uint16_t opaque;
    uint32_t reserved;
} __attribute__((packed));

#define TG3_RXD_FLAG_ERROR      0x00000400
#define TG3_RXD_FLAG_VLAN       0x00000400

/*
 * Status block: the tg3 NIC DMAs this structure to host memory on events.
 * The driver reads consumer indices from here rather than polling registers.
 */
struct tg3_status_block {
    uint32_t status_word;        /* bit 0 = updated */
    uint32_t status_tag;
    uint16_t rx_std_consumer;    /* RX standard ring consumer */
    uint16_t pad0;
    uint16_t tx_consumer;        /* TX consumer index */
    uint16_t pad1;
    uint16_t rx_ret_consumer[4]; /* RX return ring consumer indices */
    uint8_t  pad2[36];
} __attribute__((packed));

#define TG3_STATUS_UPDATED      0x00000001

/* Ring control block: programmed into NIC to describe ring location/size */
struct tg3_ring_control_block {
    uint32_t host_addr_hi;
    uint32_t host_addr_lo;
    uint32_t nic_addr;
    uint32_t max_len_flags;  /* [31:16]=max, [15:0]=flags */
} __attribute__((packed));

/* Ring control block NIC-side addresses */
#define TG3_NIC_SRAM_TX_RCB     0x00100
#define TG3_NIC_SRAM_RX_STD_RCB 0x00418
#define TG3_NIC_SRAM_RX_RET_RCB 0x00520    /* Return ring 0 RCB */

#define MAX_TG3 4

struct tg3_softc {
    struct pci_dev   *pci;
    struct spinlock   lock;
    struct ifnet      ifn;

    volatile uint32_t *regs;     /* MMIO base (BAR0) */
    uint8_t            mac[6];

    /* TX producer ring */
    struct tg3_tx_desc   *tx_ring;
    char                 *tx_bufs[TG3_TX_RING_SIZE];
    struct mbuf          *tx_mbufs[TG3_TX_RING_SIZE];
    uint32_t              tx_prod;   /* host producer index */
    uint32_t              tx_cons;   /* host-tracked consumer copy */

    /* RX standard producer ring (posted to NIC) */
    struct tg3_rx_desc   *rx_ring;
    char                 *rx_bufs[TG3_RX_RING_SIZE];
    uint32_t              rx_prod;   /* standard ring producer */

    /* RX return ring (NIC writes completions here) */
    struct tg3_rx_desc   *rx_ret_ring;
    uint32_t              rx_ret_cons;

    /* DMA-backed status block */
    struct tg3_status_block *status_blk;
};

static struct tg3_softc tg3_devices[MAX_TG3];
static int tg3_count;
extern int ncpu;

static int  tg3_output(struct ifnet *ifp, struct mbuf *m);
static void tg3_poll(struct ifnet *ifp);

static struct ifnet_ops tg3_ifnet_ops = {
    .if_output = tg3_output,
    .if_poll   = tg3_poll,
};

static int
tg3_match(struct pci_dev *dev)
{
    if (!dev || dev->vendor_id != TG3_VENDOR)
        return 0;
    switch (dev->device_id) {
    case TG3_DEV_BCM5700:
    case TG3_DEV_BCM5719:
    case TG3_DEV_BCM5720:
        return 1;
    default:
        return 0;
    }
}

static uint32_t
tg3_read(struct tg3_softc *sc, uint32_t reg)
{
    return sc->regs[reg / 4];
}

static void
tg3_write(struct tg3_softc *sc, uint32_t reg, uint32_t val)
{
    sc->regs[reg / 4] = val;
}

/*
 * Write to a NIC-side SRAM register via the memory window.
 * Used to program ring control blocks into the NIC's own SRAM.
 */
static void
tg3_write_indirect(struct tg3_softc *sc, uint32_t nic_addr, uint32_t val)
{
    tg3_write(sc, TG3_WIN_BASE, nic_addr);
    tg3_write(sc, TG3_WIN_DATA, val);
}

static void
tg3_read_mac(struct tg3_softc *sc)
{
    uint32_t hi = tg3_read(sc, TG3_MAC_ADDR_0_HIGH);
    uint32_t lo = tg3_read(sc, TG3_MAC_ADDR_0_LOW);

    sc->mac[0] = (hi >> 8) & 0xFF;
    sc->mac[1] =  hi        & 0xFF;
    sc->mac[2] = (lo >> 24) & 0xFF;
    sc->mac[3] = (lo >> 16) & 0xFF;
    sc->mac[4] = (lo >>  8) & 0xFF;
    sc->mac[5] =  lo         & 0xFF;
}

/*
 * Software reset.  Follows the sequence from the BCM5719 programmer's guide:
 *   1. Assert GPHY power-down to avoid GPHY PLL issues.
 *   2. Assert core-clock reset, then wait for completion.
 *   3. Re-establish MMIO access before touching other registers.
 */
static void
tg3_reset(struct tg3_softc *sc)
{
    uint32_t val;
    int i;

    /* Disable interrupts */
    tg3_write(sc, TG3_MISC_LOCAL_CTRL,
        tg3_read(sc, TG3_MISC_LOCAL_CTRL) & ~MISC_LOCAL_CTRL_INT_ENABLE);

    /* Assert reset */
    val = tg3_read(sc, TG3_GRC_MISC_CFG);
    tg3_write(sc, TG3_GRC_MISC_CFG,
        val | GRC_MISC_CFG_CORECLK_RESET | GRC_MISC_CFG_KEEP_GPHY_POWER);

    microdelay(100);

    /* De-assert reset */
    tg3_write(sc, TG3_GRC_MISC_CFG, val & ~GRC_MISC_CFG_CORECLK_RESET);

    /* Wait for MMIO to be accessible again (up to 10ms) */
    for (i = 0; i < 100; i++) {
        microdelay(100);
        if (tg3_read(sc, TG3_MISC_HOST_CTRL) != 0xFFFFFFFF)
            break;
    }
}

static int
tg3_alloc_rings(struct tg3_softc *sc)
{
    int i;

    /* TX producer ring */
    sc->tx_ring = (struct tg3_tx_desc *)kalloc();
    if (!sc->tx_ring) return -1;
    memset(sc->tx_ring, 0, sizeof(struct tg3_tx_desc) * TG3_TX_RING_SIZE);

    for (i = 0; i < TG3_TX_RING_SIZE; i++) {
        sc->tx_bufs[i] = kalloc();
        if (!sc->tx_bufs[i]) return -1;
    }
    sc->tx_prod = 0;
    sc->tx_cons = 0;

    /* RX standard producer ring */
    sc->rx_ring = (struct tg3_rx_desc *)kalloc();
    if (!sc->rx_ring) return -1;
    memset(sc->rx_ring, 0, sizeof(struct tg3_rx_desc) * TG3_RX_RING_SIZE);

    for (i = 0; i < TG3_RX_RING_SIZE; i++) {
        sc->rx_bufs[i] = kalloc();
        if (!sc->rx_bufs[i]) return -1;
        sc->rx_ring[i].addr_lo = V2P(sc->rx_bufs[i]);
        sc->rx_ring[i].addr_hi = 0;
    }
    sc->rx_prod = TG3_RX_RING_SIZE - 1;

    /* RX return ring (written by NIC, sized = RX_RING_SIZE) */
    sc->rx_ret_ring = (struct tg3_rx_desc *)kalloc();
    if (!sc->rx_ret_ring) return -1;
    memset(sc->rx_ret_ring, 0,
           sizeof(struct tg3_rx_desc) * TG3_RX_RING_SIZE);
    sc->rx_ret_cons = 0;

    /* Status block (NIC DMA target) */
    sc->status_blk = (struct tg3_status_block *)kalloc();
    if (!sc->status_blk) return -1;
    memset(sc->status_blk, 0, sizeof(*sc->status_blk));

    return 0;
}

/*
 * Program ring control blocks into NIC SRAM so the hardware knows
 * where each ring lives in host memory.
 */
static void
tg3_init_rings(struct tg3_softc *sc)
{
    uint32_t phys;

    /* TX ring RCB */
    phys = V2P(sc->tx_ring);
    tg3_write_indirect(sc, TG3_NIC_SRAM_TX_RCB + 0x00, 0);          /* hi */
    tg3_write_indirect(sc, TG3_NIC_SRAM_TX_RCB + 0x04, phys);        /* lo */
    tg3_write_indirect(sc, TG3_NIC_SRAM_TX_RCB + 0x08, 0);           /* nic */
    tg3_write_indirect(sc, TG3_NIC_SRAM_TX_RCB + 0x0C,
                       TG3_TX_RING_SIZE << 16);                        /* max/flags */

    /* Standard RX ring RCB */
    phys = V2P(sc->rx_ring);
    tg3_write_indirect(sc, TG3_NIC_SRAM_RX_STD_RCB + 0x00, 0);
    tg3_write_indirect(sc, TG3_NIC_SRAM_RX_STD_RCB + 0x04, phys);
    tg3_write_indirect(sc, TG3_NIC_SRAM_RX_STD_RCB + 0x08, 0);
    tg3_write_indirect(sc, TG3_NIC_SRAM_RX_STD_RCB + 0x0C,
                       TG3_RX_RING_SIZE << 16);

    /* RX return ring 0 RCB */
    phys = V2P(sc->rx_ret_ring);
    tg3_write_indirect(sc, TG3_NIC_SRAM_RX_RET_RCB + 0x00, 0);
    tg3_write_indirect(sc, TG3_NIC_SRAM_RX_RET_RCB + 0x04, phys);
    tg3_write_indirect(sc, TG3_NIC_SRAM_RX_RET_RCB + 0x08, 0);
    tg3_write_indirect(sc, TG3_NIC_SRAM_RX_RET_RCB + 0x0C,
                       TG3_RX_RING_SIZE << 16);

    /* Status block host address */
    phys = V2P(sc->status_blk);
    tg3_write(sc, 0x388, 0);     /* status block host addr hi */
    tg3_write(sc, 0x38C, phys);  /* status block host addr lo */
    tg3_write(sc, 0x390, TG3_STATUS_BLK_NIC_ADDR);

    /* Post RX ring producer index */
    tg3_write(sc, TG3_RX_STD_PROD_IDX, sc->rx_prod);
}

static void
tg3_tx_complete(struct tg3_softc *sc)
{
    uint32_t cons;

    /* Consumer comes from status block (DMA'd by NIC) */
    cons = sc->status_blk->tx_consumer & (TG3_TX_RING_SIZE - 1);

    while (sc->tx_cons != cons) {
        if (sc->tx_mbufs[sc->tx_cons]) {
            mbuf_free(sc->tx_mbufs[sc->tx_cons]);
            sc->tx_mbufs[sc->tx_cons] = 0;
        }
        sc->tx_cons = (sc->tx_cons + 1) & (TG3_TX_RING_SIZE - 1);
    }
}

static void
tg3_rx_complete(struct tg3_softc *sc)
{
    uint32_t cons;
    struct tg3_rx_desc *d;
    struct mbuf *m;
    int processed = 0;

    cons = sc->status_blk->rx_ret_consumer[0] & (TG3_RX_RING_SIZE - 1);

    while (sc->rx_ret_cons != cons && processed < 32) {
        d = &sc->rx_ret_ring[sc->rx_ret_cons];

        if (!(d->type_flags & TG3_RXD_FLAG_ERROR)) {
            uint16_t len = (uint16_t)(d->idx_len & 0xFFFF);
            uint16_t src = (uint16_t)((d->idx_len >> 16) & (TG3_RX_RING_SIZE - 1));

            if (len > 0 && len <= TG3_RX_BUF_SIZE) {
                m = mbuf_alloc();
                if (m) {
                    memmove(m->data, sc->rx_bufs[src], len);
                    m->len   = len;
                    m->rcvif = &sc->ifn;
                    release(&sc->lock);
                    if_input(&sc->ifn, m);
                    acquire(&sc->lock);
                }
            }
        }

        sc->rx_ret_cons = (sc->rx_ret_cons + 1) & (TG3_RX_RING_SIZE - 1);
        processed++;
    }

    /* Acknowledge consumed return slots; advance standard RX producer */
    if (processed > 0) {
        sc->rx_prod = (sc->rx_prod + processed) & (TG3_RX_RING_SIZE - 1);
        tg3_write(sc, TG3_RX_STD_PROD_IDX, sc->rx_prod);
        /* Clear interrupt mailbox (acknowledge event) */
        tg3_write(sc, TG3_INT_MBOX_0, sc->status_blk->status_tag << 24);
    }
}

static void
tg3_poll(struct ifnet *ifp)
{
    struct tg3_softc *sc = (struct tg3_softc *)ifp->if_softc;
    if (!sc || (ifp->if_flags & IFF_RUNNING) == 0)
        return;
    if (!(sc->status_blk->status_word & TG3_STATUS_UPDATED))
        return;
    sc->status_blk->status_word &= ~TG3_STATUS_UPDATED;

    acquire(&sc->lock);
    tg3_tx_complete(sc);
    tg3_rx_complete(sc);
    release(&sc->lock);
}

static int
tg3_output(struct ifnet *ifp, struct mbuf *m)
{
    struct tg3_softc *sc = (struct tg3_softc *)ifp->if_softc;
    struct tg3_tx_desc *d;
    uint32_t idx, next;

    if (!sc || !m || m->len == 0)
        return -1;

    acquire(&sc->lock);

    if ((ifp->if_flags & IFF_RUNNING) == 0) {
        release(&sc->lock);
        return -1;
    }

    tg3_tx_complete(sc);

    idx  = sc->tx_prod;
    next = (idx + 1) & (TG3_TX_RING_SIZE - 1);
    if (next == sc->tx_cons) {
        /* Ring full */
        release(&sc->lock);
        return -1;
    }

    memmove(sc->tx_bufs[idx], m->data, m->len);
    sc->tx_mbufs[idx] = m;

    d = &sc->tx_ring[idx];
    d->addr_hi   = 0;
    d->addr_lo   = V2P(sc->tx_bufs[idx]);
    d->len_flags = ((uint32_t)m->len << 16) | TG3_TXD_FLAG_END;
    d->vlan_tag  = 0;

    sc->tx_prod = next;

    /* Notify NIC of new TX descriptor */
    tg3_write(sc, TG3_TX_RING_PROD_IDX_0, sc->tx_prod);

    release(&sc->lock);
    return 0;
}

static int
tg3_probe(struct pci_dev *dev)
{
    struct tg3_softc *sc;
    uint32_t phystatus;

    if (tg3_count >= MAX_TG3)
        return -1;

    sc = &tg3_devices[tg3_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "tg3");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "tg3");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    /* BAR0 = 64 KB MMIO */
    sc->regs = pci_map_bar(dev, 0);
    if (!sc->regs) {
        cprintf("tg3: failed to map BAR0 at %d:%d.%d\n",
                dev->bus, dev->slot, dev->func);
        return -1;
    }

    BOOTDBG("tg3: found %04x at %d:%d.%d irq=%d regs=%p\n",
            dev->device_id, dev->bus, dev->slot, dev->func,
            dev->irq_line, sc->regs);

    tg3_reset(sc);
    tg3_read_mac(sc);

    if (tg3_alloc_rings(sc) < 0) {
        cprintf("tg3: ring allocation failed\n");
        return -1;
    }

    tg3_init_rings(sc);

    /* Enable byte-swap and memory-mapped access */
    tg3_write(sc, TG3_MISC_HOST_CTRL,
        MISC_HOST_CTRL_MASK_PCI_INT |
        MISC_HOST_CTRL_BYTE_SWAP    |
        MISC_HOST_CTRL_WORD_SWAP    |
        MISC_HOST_CTRL_USE_MEM_RD_MUL);

    /* Enable TX + RX global */
    tg3_write(sc, TG3_GLOBAL_CTRL,
        GLOBAL_CTRL_ENABLE_TX | GLOBAL_CTRL_ENABLE_RX);

    /* Check PHY link status */
    phystatus = tg3_read(sc, TG3_PHY_STATUS);

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "bge0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[3] = '0' + tg3_count;
    sc->ifn.if_mtu   = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_BROADCAST;
    if (phystatus & TG3_PHY_LINKUP)
        sc->ifn.if_flags |= IFF_RUNNING;
    memmove(sc->ifn.if_hwaddr, sc->mac, 6);
    sc->ifn.if_softc = sc;
    sc->ifn.if_input = ether_input;
    sc->ifn.if_ops   = &tg3_ifnet_ops;

    if (if_register(&sc->ifn) < 0) {
        cprintf("tg3: failed to register ifnet\n");
        return -1;
    }

    cprintf("tg3: attached %s BCM%04x MAC %x:%x:%x:%x:%x:%x\n",
            sc->ifn.if_xname, dev->device_id,
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
    tg3_count++;
    return 0;
}

void
tg3_init(void)
{
    int i;
    BOOTDBG("tg3: initializing driver\n");
    for (i = 0; i < pci_device_count(); i++) {
        struct pci_dev *dev = pci_get_device(i);
        if (tg3_match(dev))
            tg3_probe(dev);
    }
}
