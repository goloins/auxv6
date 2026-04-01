/*
 * Realtek RTL8111/RTL8168 Gigabit Ethernet Driver for auxv6
 *
 * Supports RTL8111/RTL8168 family chips, common in real hardware.
 *
 * Architecture:
 * - Memory-mapped I/O via BAR2 (or BAR1/0 fallback)
 * - Ring-based TX/RX queues with descriptor format
 * - Integrates with ifnet layer via if_register()
 *
 * Reference: RTL8111/RTL8168 Datasheet
 * See also: Linux drivers/net/ethernet/realtek/r8169.c
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

#define RTL8111_DEVICE_8161 0x8161
#define RTL8111_DEVICE_8168 0x8168
#define RTL8111_DEVICE_8169 0x8169

/* Register offsets (memory-mapped) */
#define RTL_IDR0        0x00    /* MAC address (6 bytes) */
#define RTL_MAR0        0x08    /* Multicast filter (8 bytes) */
#define RTL_TSD0        0x10    /* TX status 0 */
#define RTL_TSAD0       0x20    /* TX start address 0 */
#define RTL_RBSTART     0x30    /* RX buffer start */
#define RTL_CR          0x37    /* Command register */
#define RTL_CAPR        0x38    /* Current address of packet read */
#define RTL_CBR         0x3A    /* Current buffer address */
#define RTL_IMR         0x3C    /* Interrupt mask */
#define RTL_ISR         0x3E    /* Interrupt status */
#define RTL_TCR         0x40    /* TX config */
#define RTL_RCR         0x44    /* RX config */
#define RTL_MPC         0x4C    /* Missed packet counter */
#define RTL_9346CR      0x50    /* 93C46 command register */
#define RTL_CONFIG0     0x51    /* Configuration 0 */
#define RTL_CONFIG1     0x52    /* Configuration 1 */
#define RTL_MSR         0x58    /* Media status */
#define RTL_PHY_AR      0x60    /* PHY access register */
#define RTL_PHY_DR      0x64    /* PHY data register */
#define RTL_RMS         0xDA    /* RX max size */
#define RTL_MTPS        0xEC    /* Max TX packet size */

/* For RTL8169/8111 style descriptors */
#define RTL_TNPDS_LO    0x20    /* TX normal priority descriptor start (low) */
#define RTL_TNPDS_HI    0x24    /* TX normal priority descriptor start (high) */
#define RTL_RDSAR_LO    0xE4    /* RX descriptor start (low) */
#define RTL_RDSAR_HI    0xE8    /* RX descriptor start (high) */

/* Command register bits */
#define CR_RST          0x10    /* Reset */
#define CR_RE           0x08    /* Receiver enable */
#define CR_TE           0x04    /* Transmitter enable */
#define CR_BUFE         0x01    /* Buffer empty (RX) */

/* Interrupt bits */
#define ISR_ROK         0x0001  /* RX OK */
#define ISR_RER         0x0002  /* RX error */
#define ISR_TOK         0x0004  /* TX OK */
#define ISR_TER         0x0008  /* TX error */
#define ISR_RX_OVW      0x0010  /* RX buffer overflow */
#define ISR_PUN         0x0020  /* Packet underrun */
#define ISR_FOVW        0x0040  /* RX FIFO overflow */
#define ISR_LENCHG      0x2000  /* Cable length change */
#define ISR_TIMEOUT     0x4000  /* Timeout */
#define ISR_SERR        0x8000  /* System error */

/* RX config bits */
#define RCR_AAP         0x01    /* Accept all packets (promiscuous) */
#define RCR_APM         0x02    /* Accept physical match */
#define RCR_AM          0x04    /* Accept multicast */
#define RCR_AB          0x08    /* Accept broadcast */
#define RCR_AR          0x10    /* Accept runt */
#define RCR_AER         0x20    /* Accept error */
#define RCR_WRAP        0x80    /* Wrap (1=wrap at end, 0=no wrap) */

/* TX config bits */
#define TCR_CLRABT      0x01    /* Clear abort */
#define TCR_MXDMA_256   0x400   /* Max DMA burst 256 */
#define TCR_MXDMA_512   0x500   /* Max DMA burst 512 */
#define TCR_MXDMA_1024  0x600   /* Max DMA burst 1024 */
#define TCR_MXDMA_UNLIM 0x700   /* Max DMA unlimited */
#define TCR_IFG_STD     0x03000000  /* Interframe gap standard */

/* TX/RX descriptor for 8169-style interface */
struct rtl_desc {
    uint32_t opts1;     /* Control word 1 */
    uint32_t opts2;     /* Control word 2 */
    uint32_t addr_lo;   /* Buffer address low */
    uint32_t addr_hi;   /* Buffer address high */
} __attribute__((packed));

/* Descriptor opts1 bits */
#define DESC_OWN        0x80000000  /* Owned by hardware */
#define DESC_EOR        0x40000000  /* End of ring */
#define DESC_FS         0x20000000  /* First segment */
#define DESC_LS         0x10000000  /* Last segment */
#define DESC_LEN_MASK   0x00003FFF  /* Packet length mask */

/* Ring sizes */
#define RTL_TX_RING_SIZE    64
#define RTL_RX_RING_SIZE    64
#define RTL_RX_BUF_SIZE     2048

/* Per-device state */
struct rtl8111_softc {
    struct pci_dev    *pci;
    struct spinlock    lock;
    struct ifnet       ifn;
    
    volatile uint8_t  *regs;
    uint8_t            mac[6];
    
    /* TX ring */
    struct rtl_desc   *tx_ring;
    char              *tx_bufs[RTL_TX_RING_SIZE];
    struct mbuf       *tx_mbufs[RTL_TX_RING_SIZE];
    int                tx_head;
    int                tx_tail;
    
    /* RX ring */
    struct rtl_desc   *rx_ring;
    char              *rx_bufs[RTL_RX_RING_SIZE];
    int                rx_cur;
};

static int rtl8111_output(struct ifnet *ifp, struct mbuf *m);

static struct ifnet_ops rtl8111_ifnet_ops = {
    .if_output = rtl8111_output,
};

/* Global array */
#define MAX_RTL8111 4
static struct rtl8111_softc rtl8111_devices[MAX_RTL8111];
static int rtl8111_count = 0;
extern int ncpu;

static int
rtl8111_match(struct pci_dev *dev)
{
    if (dev == 0)
        return 0;
    if (dev->vendor_id != PCI_VENDOR_REALTEK)
        return 0;
    if (dev->device_id == RTL8111_DEVICE_8161 ||
        dev->device_id == RTL8111_DEVICE_8168 ||
        dev->device_id == RTL8111_DEVICE_8169)
        return 1;
    return 0;
}

/* Read 8-bit register */
static uint8_t
rtl_read8(struct rtl8111_softc *sc, int reg)
{
    return sc->regs[reg];
}

/* Write 8-bit register */
static void
rtl_write8(struct rtl8111_softc *sc, int reg, uint8_t val)
{
    sc->regs[reg] = val;
}

/* Read 16-bit register */
static uint16_t
rtl_read16(struct rtl8111_softc *sc, int reg)
{
    return *(volatile uint16_t *)(sc->regs + reg);
}

/* Write 16-bit register */
static void
rtl_write16(struct rtl8111_softc *sc, int reg, uint16_t val)
{
    *(volatile uint16_t *)(sc->regs + reg) = val;
}

/* Read 32-bit register */
static uint32_t __attribute__((unused))
rtl_read32(struct rtl8111_softc *sc, int reg)
{
    return *(volatile uint32_t *)(sc->regs + reg);
}

/* Write 32-bit register */
static void
rtl_write32(struct rtl8111_softc *sc, int reg, uint32_t val)
{
    *(volatile uint32_t *)(sc->regs + reg) = val;
}

static void
rtl8111_read_mac(struct rtl8111_softc *sc)
{
    int i;
    
    for (i = 0; i < 6; i++) {
        sc->mac[i] = rtl_read8(sc, RTL_IDR0 + i);
    }
    
    cprintf("rtl8111: MAC %x:%x:%x:%x:%x:%x\n",
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
}

static void
rtl8111_reset(struct rtl8111_softc *sc)
{
    int timeout;
    
    /* Disable interrupts */
    rtl_write16(sc, RTL_IMR, 0);
    
    /* Issue reset */
    rtl_write8(sc, RTL_CR, CR_RST);
    
    /* Wait for reset to complete */
    for (timeout = 0; timeout < 1000; timeout++) {
        if ((rtl_read8(sc, RTL_CR) & CR_RST) == 0)
            break;
        microdelay(1000);
    }
    
    if (rtl_read8(sc, RTL_CR) & CR_RST)
        cprintf("rtl8111: reset timeout\n");
}

static int
rtl8111_init_tx(struct rtl8111_softc *sc)
{
    int i;
    
    /* Allocate TX descriptor ring */
    sc->tx_ring = (struct rtl_desc *)kalloc();
    if (!sc->tx_ring)
        return -1;
    memset(sc->tx_ring, 0, sizeof(struct rtl_desc) * RTL_TX_RING_SIZE);
    
    /* Allocate TX buffers */
    for (i = 0; i < RTL_TX_RING_SIZE; i++) {
        sc->tx_bufs[i] = kalloc();
        if (!sc->tx_bufs[i])
            return -1;
        sc->tx_ring[i].addr_lo = V2P(sc->tx_bufs[i]);
        sc->tx_ring[i].addr_hi = 0;
    }
    
    /* Mark end of ring */
    sc->tx_ring[RTL_TX_RING_SIZE - 1].opts1 = DESC_EOR;
    
    sc->tx_head = 0;
    sc->tx_tail = 0;
    
    /* Set TX descriptor start address */
    rtl_write32(sc, RTL_TNPDS_LO, V2P(sc->tx_ring));
    rtl_write32(sc, RTL_TNPDS_HI, 0);
    
    return 0;
}

static int
rtl8111_init_rx(struct rtl8111_softc *sc)
{
    int i;
    
    /* Allocate RX descriptor ring */
    sc->rx_ring = (struct rtl_desc *)kalloc();
    if (!sc->rx_ring)
        return -1;
    memset(sc->rx_ring, 0, sizeof(struct rtl_desc) * RTL_RX_RING_SIZE);
    
    /* Allocate RX buffers and set up descriptors */
    for (i = 0; i < RTL_RX_RING_SIZE; i++) {
        sc->rx_bufs[i] = kalloc();
        if (!sc->rx_bufs[i])
            return -1;
        
        sc->rx_ring[i].addr_lo = V2P(sc->rx_bufs[i]);
        sc->rx_ring[i].addr_hi = 0;
        sc->rx_ring[i].opts1 = DESC_OWN | RTL_RX_BUF_SIZE;
    }
    
    /* Mark end of ring */
    sc->rx_ring[RTL_RX_RING_SIZE - 1].opts1 |= DESC_EOR;
    
    sc->rx_cur = 0;
    
    /* Set RX descriptor start address */
    rtl_write32(sc, RTL_RDSAR_LO, V2P(sc->rx_ring));
    rtl_write32(sc, RTL_RDSAR_HI, 0);
    
    /* Set RX config - accept broadcast, multicast, and our MAC */
    rtl_write32(sc, RTL_RCR, RCR_AB | RCR_AM | RCR_APM | 0x0E);
    
    /* Set max RX size */
    rtl_write16(sc, RTL_RMS, RTL_RX_BUF_SIZE);
    
    return 0;
}

static int
rtl8111_output(struct ifnet *ifp, struct mbuf *m)
{
    struct rtl8111_softc *sc = (struct rtl8111_softc *)ifp->if_softc;
    struct rtl_desc *desc;
    int idx;
    
    if (!m || m->len == 0)
        return -1;
    
    acquire(&sc->lock);
    
    idx = sc->tx_tail;
    desc = &sc->tx_ring[idx];
    
    /* Check if descriptor is available */
    if (desc->opts1 & DESC_OWN) {
        release(&sc->lock);
        return -1;
    }
    
    /* Copy data to TX buffer */
    memmove(sc->tx_bufs[idx], m->data, m->len);
    sc->tx_mbufs[idx] = m;
    
    /* Set up descriptor */
    desc->opts1 = DESC_OWN | DESC_FS | DESC_LS | (m->len & DESC_LEN_MASK);
    if (idx == RTL_TX_RING_SIZE - 1)
        desc->opts1 |= DESC_EOR;
    desc->opts2 = 0;
    
    /* Advance tail */
    sc->tx_tail = (idx + 1) % RTL_TX_RING_SIZE;
    
    /* Trigger TX poll */
    rtl_write8(sc, RTL_CR, rtl_read8(sc, RTL_CR) | CR_TE);
    
    release(&sc->lock);
    
    return 0;
}

static void
rtl8111_tx_complete(struct rtl8111_softc *sc)
{
    struct rtl_desc *desc;
    
    while (sc->tx_head != sc->tx_tail) {
        desc = &sc->tx_ring[sc->tx_head];
        
        if (desc->opts1 & DESC_OWN)
            break;
        
        /* Free mbuf */
        if (sc->tx_mbufs[sc->tx_head])
            mbuf_free(sc->tx_mbufs[sc->tx_head]);
        sc->tx_mbufs[sc->tx_head] = 0;
        
        sc->tx_head = (sc->tx_head + 1) % RTL_TX_RING_SIZE;
    }
}

static void
rtl8111_rx_complete(struct rtl8111_softc *sc)
{
    struct rtl_desc *desc;
    struct mbuf *m;
    uint32_t opts1;
    uint16_t len;
    int processed = 0;
    
    while (processed < 32) {
        desc = &sc->rx_ring[sc->rx_cur];
        opts1 = desc->opts1;
        
        /* Check if hardware owns this descriptor */
        if (opts1 & DESC_OWN)
            break;
        
        /* Check if this is a valid complete packet */
        if ((opts1 & (DESC_FS | DESC_LS)) == (DESC_FS | DESC_LS)) {
            len = opts1 & DESC_LEN_MASK;
            
            if (len > 0 && len <= RTL_RX_BUF_SIZE) {
                m = mbuf_alloc();
                if (m) {
                    memmove(m->data, sc->rx_bufs[sc->rx_cur], len);
                    m->len = len;
                    m->rcvif = &sc->ifn;
                    
                    /* Release lock, deliver packet, reacquire */
                    release(&sc->lock);
                    if_input(&sc->ifn, m);
                    acquire(&sc->lock);
                }
            }
        }
        
        /* Reset descriptor for reuse */
        desc->opts1 = DESC_OWN | RTL_RX_BUF_SIZE;
        if (sc->rx_cur == RTL_RX_RING_SIZE - 1)
            desc->opts1 |= DESC_EOR;
        
        sc->rx_cur = (sc->rx_cur + 1) % RTL_RX_RING_SIZE;
        processed++;
    }
}

static void
rtl8111_irq_handler(int irq, void *arg)
{
    struct rtl8111_softc *sc = (struct rtl8111_softc *)arg;
    uint16_t isr;
    
    (void)irq;
    if (!sc)
        return;
    
    isr = rtl_read16(sc, RTL_ISR);
    if (isr == 0)
        return;
    
    /* Clear interrupts */
    rtl_write16(sc, RTL_ISR, isr);
    
    acquire(&sc->lock);
    
    /* TX completion */
    if (isr & (ISR_TOK | ISR_TER)) {
        rtl8111_tx_complete(sc);
    }
    
    /* RX completion */
    if (isr & (ISR_ROK | ISR_RER | ISR_RX_OVW)) {
        rtl8111_rx_complete(sc);
    }
    
    release(&sc->lock);
}

static int
rtl8111_probe(struct pci_dev *dev)
{
    struct rtl8111_softc *sc;
    void *regs;
    
    if (rtl8111_count >= MAX_RTL8111)
        return -1;
    
    sc = &rtl8111_devices[rtl8111_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "rtl8111");
    sc->pci = dev;
    
    /* Enable memory and bus master */
    pci_enable_mem(dev);
    pci_set_master(dev);
    
    /* Map BAR (try BAR2 first, then 1, then 0) */
    regs = pci_map_bar(dev, 2);
    if (regs == 0)
        regs = pci_map_bar(dev, 1);
    if (regs == 0)
        regs = pci_map_bar(dev, 0);
    if (regs == 0) {
        cprintf("rtl8111: failed to map registers\n");
        return -1;
    }
    
    sc->regs = (volatile uint8_t *)regs;
    
    BOOTDBG("rtl8111: found %x at %d:%d.%d irq=%d regs=%p\n",
            dev->device_id, dev->bus, dev->slot, dev->func,
            dev->irq_line, regs);
    
    /* Reset hardware */
    rtl8111_reset(sc);
    
    /* Read MAC address */
    rtl8111_read_mac(sc);
    
    /* Initialize TX/RX rings */
    if (rtl8111_init_tx(sc) < 0 || rtl8111_init_rx(sc) < 0) {
        cprintf("rtl8111: failed to initialize rings\n");
        return -1;
    }
    
    /* Set TX config */
    rtl_write32(sc, RTL_TCR, TCR_IFG_STD | TCR_MXDMA_UNLIM);
    
    /* Register IRQ handler */
    if (irq_register(dev->irq_line, rtl8111_irq_handler, sc, "rtl8111") < 0) {
        cprintf("rtl8111: failed to register IRQ %d\n", dev->irq_line);
        return -1;
    }
    
    /* Enable interrupts */
    rtl_write16(sc, RTL_IMR, ISR_ROK | ISR_RER | ISR_TOK | ISR_TER | ISR_RX_OVW);
    
    /* Enable TX and RX */
    rtl_write8(sc, RTL_CR, CR_TE | CR_RE);
    
    /* Enable PCI interrupt */
    pci_enable_irq(dev, ncpu - 1);
    
    /* Set up ifnet structure */
    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "rtl0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[3] = '0' + rtl8111_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_BROADCAST | IFF_RUNNING;
    memmove(sc->ifn.if_hwaddr, sc->mac, sizeof(sc->ifn.if_hwaddr));
    sc->ifn.if_softc = sc;
    sc->ifn.if_input = ether_input;
    sc->ifn.if_ops = &rtl8111_ifnet_ops;
    
    /* Register with network stack */
    if (if_register(&sc->ifn) < 0) {
        cprintf("rtl8111: failed to register ifnet\n");
        irq_unregister(dev->irq_line, "rtl8111");
        return -1;
    }
    
    rtl8111_count++;
    cprintf("rtl8111: attached %s irq=%d\n", sc->ifn.if_xname, dev->irq_line);
    
    return 0;
}

void
rtl8111_init(void)
{
    int i;
    struct pci_dev *dev;

    BOOTDBG("rtl8111: initializing driver\n");
    for (i = 0; i < pci_device_count(); i++) {
        dev = pci_get_device(i);
        if (rtl8111_match(dev))
            rtl8111_probe(dev);
    }
}