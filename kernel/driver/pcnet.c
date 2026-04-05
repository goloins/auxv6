/*
 * AMD PCNET (Am79C970A) Ethernet Driver for auxv6
 *
 * Supports AMD PCNET-PCI II Am79C970A (QEMU default NIC).
 * Uses 32-bit mode with software-style initialization.
 *
 * Architecture:
 * - I/O port based access (fallback from MMIO)
 * - Ring-based TX/RX with TMD/RMD descriptors
 * - Compatible with Lance/PCnet standards
 *
 * TODO Phase 1:
 * - [ ] PCI detection and port mapping
 * - [ ] Soft reset and initialization
 * - [ ] TX/RX ring setup
 * - [ ] MAC address configuration
 *
 * TODO Phase 2:
 * - [ ] Full packet TX/RX
 * - [ ] Interrupt handling
 * - [ ] Link status detection
 *
 * Reference: AMD Am79C970A PCnet-PCI II Data Sheet
 * See also: NetBSD dev/pci/if_pcn.c, Linux pcnet32.c
 */

#include "types.h"
#include "defs.h"
#include "x86.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "memlayout.h"

/* PCI Vendor/Device IDs */
#define PCNET_VENDOR_ID    0x1022  /* AMD */
#define PCNET_DEVICE_ID    0x2000  /* PCnet-PCI II */

/* I/O Port Offsets (32-bit mode) */
#define PCNET_APROM    0x00   /* EEPROM (MAC address at 0x00-0x05) */
#define PCNET_RDP      0x10   /* Register Data Port */
#define PCNET_RAP      0x14   /* Register Address Port */
#define PCNET_RESET    0x18   /* Reset (read triggers soft reset) */
#define PCNET_BDP      0x1C   /* Bus Data Port (BCR access) */

/* CSR (Control/Status Register) indices */
#define CSR0    0     /* Status/Control */
#define CSR1    1     /* IADR[15:0] - Init Block Low */
#define CSR2    2     /* IADR[31:16] - Init Block High */
#define CSR3    3     /* Interrupt Masks / Deferral Control */
#define CSR4    4     /* Test / Features */
#define CSR15   15    /* Mode */
#define CSR58   58    /* Software Style (BCR20 alias) */
#define CSR76   76    /* Receive Ring Length */
#define CSR78   78    /* Transmit Ring Length */

/* CSR0 Bit Definitions */
#define CSR0_INIT   0x0001   /* Initialize */
#define CSR0_STRT   0x0002   /* Start */
#define CSR0_STOP   0x0004   /* Stop */
#define CSR0_TDMD   0x0008   /* Transmit Demand */
#define CSR0_TXON   0x0010   /* Transmitter On */
#define CSR0_RXON   0x0020   /* Receiver On */
#define CSR0_IENA   0x0040   /* Interrupt Enable */
#define CSR0_INTR   0x0080   /* Interrupt Flag */
#define CSR0_IDON   0x0100   /* Initialization Done */
#define CSR0_TINT   0x0200   /* Transmit Interrupt */
#define CSR0_RINT   0x0400   /* Receive Interrupt */
#define CSR0_MERR   0x0800   /* Memory Error */
#define CSR0_MISS   0x1000   /* Missed Frame */
#define CSR0_CERR   0x2000   /* Collision Error */
#define CSR0_BABL   0x4000   /* Babble (TX timeout) */
#define CSR0_ERR    0x8000   /* Error Summary */

/* BCR (Bus Configuration Register) indices */
#define BCR2    2     /* Miscellaneous Configuration */
#define BCR18   18    /* Burst/Bus Control */
#define BCR20   20    /* Software Style */

/* Software style values */
#define SWSTYLE_LANCE   0     /* 16-bit Lance style */
#define SWSTYLE_ILACC   1     /* 32-bit ILACC style */
#define SWSTYLE_PCNETPCI 2    /* 32-bit PCnet-PCI style */
#define SWSTYLE_PCNETPCI_BURST 3  /* 32-bit with burst read */

/* Ring sizes (power of 2, encoded as log2) */
#define PCNET_LOG2_TX_RING  4   /* 16 TX descriptors */
#define PCNET_LOG2_RX_RING  4   /* 16 RX descriptors */
#define PCNET_TX_RING_SIZE  (1 << PCNET_LOG2_TX_RING)
#define PCNET_RX_RING_SIZE  (1 << PCNET_LOG2_RX_RING)
#define PCNET_RX_BUF_SIZE   1544

/* Initialization Block (32-bit software style 2) */
struct pcnet_init_block {
    uint16_t mode;       /* CSR15 value */
    uint8_t  rlen;       /* RLEN[3:0] | reserved */
    uint8_t  tlen;       /* TLEN[3:0] | reserved */
    uint8_t  padr[6];    /* Physical address (MAC) */
    uint16_t reserved;
    uint8_t  ladrf[8];   /* Logical address filter */
    uint32_t rdra;       /* RX descriptor ring address */
    uint32_t tdra;       /* TX descriptor ring address */
} __attribute__((packed));

/* Receive Message Descriptor (RMD) - 32-bit style */
struct pcnet_rmd {
    uint32_t rbadr;      /* Buffer address */
    int16_t  bcnt;       /* Buffer byte count (negative, 2's complement!) */
    uint16_t rmd1;       /* Status bits / reserved */
    uint32_t mcnt;       /* Message byte count / zeros */
    uint32_t user;       /* User (unused) */
} __attribute__((packed));

#define RMD1_OWN   0x8000   /* Owned by controller */
#define RMD1_ERR   0x4000   /* Error summary */
#define RMD1_FRAM  0x2000   /* Framing error */
#define RMD1_OFLO  0x1000   /* Overflow */
#define RMD1_CRC   0x0800   /* CRC error */
#define RMD1_BUFF  0x0400   /* Buffer error */
#define RMD1_STP   0x0200   /* Start of packet */
#define RMD1_ENP   0x0100   /* End of packet */

/* Transmit Message Descriptor (TMD) - 32-bit style */
struct pcnet_tmd {
    uint32_t tbadr;      /* Buffer address */
    int16_t  bcnt;       /* Buffer byte count (negative, 2's complement!) */
    uint16_t tmd1;       /* Status bits */
    uint32_t tmd2;       /* Misc (TDR, TRC) */
    uint32_t user;       /* User (unused) */
} __attribute__((packed));

#define TMD1_OWN   0x8000   /* Owned by controller */
#define TMD1_ERR   0x4000   /* Error summary */
#define TMD1_ADD_FCS 0x2000 /* Append FCS (CRC) */
#define TMD1_MORE  0x1000   /* More than one retry */
#define TMD1_ONE   0x0800   /* One retry */
#define TMD1_DEF   0x0400   /* Deferred */
#define TMD1_STP   0x0200   /* Start of packet */
#define TMD1_ENP   0x0100   /* End of packet */

/* Per-device state */
struct pcnet_softc {
    struct pci_dev   *pci;
    struct spinlock   lock;
    struct ifnet      ifn;             /* ifnet structure */
    
    uint16_t iobase;      /* I/O port base */
    uint8_t  mac[6];
    
    /* Init block (must be 16-byte aligned) */
    struct pcnet_init_block *init_block;
    
    /* TX ring */
    struct pcnet_tmd *tx_ring;
    char *tx_bufs[PCNET_TX_RING_SIZE];
    struct mbuf *tx_mbufs[PCNET_TX_RING_SIZE];
    int tx_head, tx_tail;
    
    /* RX ring */
    struct pcnet_rmd *rx_ring;
    char *rx_bufs[PCNET_RX_RING_SIZE];
    int rx_head;
};

static int pcnet_output(struct ifnet *ifp, struct mbuf *m);

static struct ifnet_ops pcnet_ifnet_ops = {
    .if_output = pcnet_output,
};

/* Global array of PCNET devices */
#define MAX_PCNET 4
static struct pcnet_softc pcnet_devices[MAX_PCNET];
static int pcnet_count = 0;
extern int ncpu;

/*
 * Read CSR register
 */
static uint32_t
pcnet_csr_read(struct pcnet_softc *sc, int reg)
{
    outl(sc->iobase + PCNET_RAP, reg);
    return inl(sc->iobase + PCNET_RDP);
}

/*
 * Write CSR register
 */
static void
pcnet_csr_write(struct pcnet_softc *sc, int reg, uint32_t val)
{
    outl(sc->iobase + PCNET_RAP, reg);
    outl(sc->iobase + PCNET_RDP, val);
}

/*
 * Read BCR register
 */
static uint32_t __attribute__((unused))
pcnet_bcr_read(struct pcnet_softc *sc, int reg)
{
    outl(sc->iobase + PCNET_RAP, reg);
    return inl(sc->iobase + PCNET_BDP);
}

/*
 * Write BCR register
 */
static void
pcnet_bcr_write(struct pcnet_softc *sc, int reg, uint32_t val)
{
    outl(sc->iobase + PCNET_RAP, reg);
    outl(sc->iobase + PCNET_BDP, val);
}

/*
 * Read MAC address from EEPROM
 */
static void
pcnet_read_mac(struct pcnet_softc *sc)
{
    for (int i = 0; i < 6; i++) {
        sc->mac[i] = inb(sc->iobase + PCNET_APROM + i);
    }
    
    cprintf("pcnet: MAC %x:%x:%x:%x:%x:%x\n",
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
}

/*
 * Reset the PCNET controller
 */
static void
pcnet_reset(struct pcnet_softc *sc)
{
    /* Reading the RESET register triggers a soft reset */
    inl(sc->iobase + PCNET_RESET);
    
    /* Wait for reset to complete */
    microdelay(10000);
    
    /* Set the software style to 32-bit PCnet-PCI */
    pcnet_bcr_write(sc, BCR20, SWSTYLE_PCNETPCI);
}

/*
 * Initialize TX ring
 */
static int
pcnet_init_tx(struct pcnet_softc *sc)
{
    /* Allocate TX descriptors */
    sc->tx_ring = (struct pcnet_tmd *)kalloc();
    if (!sc->tx_ring)
        return -1;
    memset(sc->tx_ring, 0, sizeof(struct pcnet_tmd) * PCNET_TX_RING_SIZE);
    
    /* Allocate TX buffers */
    for (int i = 0; i < PCNET_TX_RING_SIZE; i++) {
        sc->tx_bufs[i] = kalloc();
        if (!sc->tx_bufs[i])
            return -1;
        sc->tx_ring[i].tbadr = V2P(sc->tx_bufs[i]);
    }
    
    sc->tx_head = 0;
    sc->tx_tail = 0;
    
    return 0;
}

/*
 * Initialize RX ring
 */
static int
pcnet_init_rx(struct pcnet_softc *sc)
{
    /* Allocate RX descriptors */
    sc->rx_ring = (struct pcnet_rmd *)kalloc();
    if (!sc->rx_ring)
        return -1;
    memset(sc->rx_ring, 0, sizeof(struct pcnet_rmd) * PCNET_RX_RING_SIZE);
    
    /* Allocate RX buffers and set up descriptors */
    for (int i = 0; i < PCNET_RX_RING_SIZE; i++) {
        sc->rx_bufs[i] = kalloc();
        if (!sc->rx_bufs[i])
            return -1;
        
        sc->rx_ring[i].rbadr = V2P(sc->rx_bufs[i]);
        sc->rx_ring[i].bcnt = -PCNET_RX_BUF_SIZE;  /* 2's complement */
        sc->rx_ring[i].rmd1 = RMD1_OWN;  /* Give to controller */
    }
    
    sc->rx_head = 0;
    
    return 0;
}

/*
 * Set up initialization block
 */
static void
pcnet_setup_init_block(struct pcnet_softc *sc)
{
    struct pcnet_init_block *ib = sc->init_block;
    
    memset(ib, 0, sizeof(*ib));
    
    /* Mode: not promiscuous, accept broadcast */
    ib->mode = 0;
    
    /* Ring lengths (encoded as log2 in bits 7:4) */
    ib->rlen = (PCNET_LOG2_RX_RING << 4);
    ib->tlen = (PCNET_LOG2_TX_RING << 4);
    
    /* MAC address */
    for (int i = 0; i < 6; i++)
        ib->padr[i] = sc->mac[i];
    
    /* Logical address filter (accept all multicast for now) */
    for (int i = 0; i < 8; i++)
        ib->ladrf[i] = 0xFF;
    
    /* Ring addresses */
    ib->rdra = V2P(sc->rx_ring);
    ib->tdra = V2P(sc->tx_ring);
}

/*
 * Start the controller
 */
static int
pcnet_start(struct pcnet_softc *sc)
{
    /* Set init block address */
    uint32_t ib_phys = V2P(sc->init_block);
    pcnet_csr_write(sc, CSR1, ib_phys & 0xFFFF);
    pcnet_csr_write(sc, CSR2, (ib_phys >> 16) & 0xFFFF);
    
    /* Trigger initialization */
    pcnet_csr_write(sc, CSR0, CSR0_INIT);
    
    /* Wait for init done */
    for (int i = 0; i < 1000; i++) {
        if (pcnet_csr_read(sc, CSR0) & CSR0_IDON)
            break;
        microdelay(1000);
    }
    
    if (!(pcnet_csr_read(sc, CSR0) & CSR0_IDON)) {
        cprintf("pcnet: init timeout\n");
        return -1;
    }
    
    /* Clear IDON and start */
    pcnet_csr_write(sc, CSR0, CSR0_IDON | CSR0_STRT | CSR0_IENA);
    
    cprintf("pcnet: started, CSR0=0x%x\n", pcnet_csr_read(sc, CSR0));
    
    return 0;
}

/*
 * ifnet output function
 */
static int
pcnet_output(struct ifnet *ifp, struct mbuf *m)
{
    struct pcnet_softc *sc = (struct pcnet_softc *)ifp->if_softc;
    
    if (!m || m->len == 0)
        return -1;
    
    acquire(&sc->lock);
    
    /* Check for space in TX ring */
    if (sc->tx_ring[sc->tx_tail].tmd1 & TMD1_OWN) {
        release(&sc->lock);
        return -1;  /* No free descriptors */
    }
    
    /* Copy data to TX buffer */
    memmove(sc->tx_bufs[sc->tx_tail], m->data, m->len);
    
    /* Set up descriptor */
    sc->tx_ring[sc->tx_tail].bcnt = -m->len;
    sc->tx_ring[sc->tx_tail].tmd2 = 0;
    sc->tx_ring[sc->tx_tail].tmd1 = TMD1_OWN | TMD1_STP | TMD1_ENP | TMD1_ADD_FCS;
    sc->tx_mbufs[sc->tx_tail] = m;
    
    /* Advance tail */
    sc->tx_tail = (sc->tx_tail + 1) % PCNET_TX_RING_SIZE;
    
    /* Trigger transmit */
    pcnet_csr_write(sc, CSR0, CSR0_TDMD | CSR0_IENA);
    
    release(&sc->lock);
    
    return 0;
}

/*
 * Process completed TX descriptors
 */
static void
pcnet_tx_complete(struct pcnet_softc *sc)
{
    while (sc->tx_head != sc->tx_tail &&
           !(sc->tx_ring[sc->tx_head].tmd1 & TMD1_OWN)) {
        /* Free TX mbuf */
        if (sc->tx_mbufs[sc->tx_head])
            mbuf_free(sc->tx_mbufs[sc->tx_head]);
        sc->tx_mbufs[sc->tx_head] = 0;
        sc->tx_head = (sc->tx_head + 1) % PCNET_TX_RING_SIZE;
    }
}

/*
 * Process received packets
 */
static void
pcnet_rx_complete(struct pcnet_softc *sc)
{
    struct mbuf *m;
    int processed = 0;
    
    while (processed < 32 && !(sc->rx_ring[sc->rx_head].rmd1 & RMD1_OWN)) {
        /* Received a packet */
        uint16_t status = sc->rx_ring[sc->rx_head].rmd1;
        uint32_t len = sc->rx_ring[sc->rx_head].mcnt & 0xFFF;
        
        if ((status & (RMD1_STP | RMD1_ENP)) == (RMD1_STP | RMD1_ENP) &&
            !(status & RMD1_ERR) && len > 0 && len <= PCNET_RX_BUF_SIZE) {
            /* Valid complete packet */
            m = mbuf_alloc();
            if (m) {
                memmove(m->data, sc->rx_bufs[sc->rx_head], len);
                m->len = len;
                m->rcvif = &sc->ifn;
                
                /* Hand off to network stack outside lock */
                release(&sc->lock);
                if_input(&sc->ifn, m);
                acquire(&sc->lock);
            }
        }
        
        /* Return descriptor to controller */
        sc->rx_ring[sc->rx_head].rmd1 = RMD1_OWN;
        sc->rx_ring[sc->rx_head].bcnt = -PCNET_RX_BUF_SIZE;
        
        sc->rx_head = (sc->rx_head + 1) % PCNET_RX_RING_SIZE;
        processed++;
    }
}

/*
 * IRQ handler for dynamic registration
 */
static void
pcnet_irq_handler(int irq, void *arg)
{
    struct pcnet_softc *sc = (struct pcnet_softc *)arg;
    uint32_t csr0;
    
    (void)irq;
    if (!sc)
        return;
    
    csr0 = pcnet_csr_read(sc, CSR0);
    if (!(csr0 & CSR0_INTR))
        return;
    
    /* Clear interrupt flags (write 1 to clear) */
    pcnet_csr_write(sc, CSR0, csr0 & (CSR0_RINT | CSR0_TINT | CSR0_IDON |
                                      CSR0_MERR | CSR0_MISS | CSR0_IENA));
    
    acquire(&sc->lock);
    
    /* TX interrupt */
    if (csr0 & CSR0_TINT) {
        pcnet_tx_complete(sc);
    }
    
    /* RX interrupt */
    if (csr0 & CSR0_RINT) {
        pcnet_rx_complete(sc);
    }
    
    release(&sc->lock);
}

/*
 * PCI probe function
 */
int
pcnet_probe(struct pci_dev *pci)
{
    struct pcnet_softc *sc;
    
    if (pcnet_count >= MAX_PCNET)
        return -1;
    
    sc = &pcnet_devices[pcnet_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "pcnet");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "pcnet");
    sc->pci = pci;
    
    /* Get I/O base from BAR0 */
    sc->iobase = pci_bar_base(pci, 0) & 0xFFFF;
    if (sc->iobase == 0) {
        cprintf("pcnet: no I/O base\n");
        return -1;
    }
    
    /* Enable I/O and bus master */
    pci_enable_io(pci);
    pci_set_master(pci);
    
    cprintf("pcnet: found device at %d:%d.%d io=0x%x\n",
            pci->bus, pci->slot, pci->func, sc->iobase);
    
    /* Reset controller */
    pcnet_reset(sc);
    
    /* Read MAC address */
    pcnet_read_mac(sc);
    
    /* Allocate init block (16-byte aligned) */
    sc->init_block = (struct pcnet_init_block *)kalloc();
    if (!sc->init_block)
        return -1;
    
    /* Initialize rings */
    if (pcnet_init_tx(sc) < 0 || pcnet_init_rx(sc) < 0) {
        cprintf("pcnet: failed to initialize rings\n");
        return -1;
    }
    
    /* Set up init block */
    pcnet_setup_init_block(sc);
    
    /* Start controller */
    if (pcnet_start(sc) < 0)
        return -1;
    
    /* Register IRQ handler */
    if (irq_register(pci->irq_line, pcnet_irq_handler, sc, "pcnet") < 0) {
        cprintf("pcnet: failed to register IRQ %d\n", pci->irq_line);
        return -1;
    }
    
    /* Enable interrupt */
    pci_enable_irq(pci, ncpu - 1);
    
    /* Set up ifnet structure */
    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "pcn0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[3] = '0' + pcnet_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_BROADCAST | IFF_RUNNING;
    memmove(sc->ifn.if_hwaddr, sc->mac, sizeof(sc->ifn.if_hwaddr));
    sc->ifn.if_softc = sc;
    sc->ifn.if_input = ether_input;
    sc->ifn.if_ops = &pcnet_ifnet_ops;
    
    /* Register with network stack */
    if (if_register(&sc->ifn) < 0) {
        cprintf("pcnet: failed to register ifnet\n");
        irq_unregister(pci->irq_line, "pcnet");
        return -1;
    }
    
    pcnet_count++;
    cprintf("pcnet: attached %s irq=%d\n", sc->ifn.if_xname, pci->irq_line);
    
    return 0;
}

/*
 * Module init
 */
void
pcnet_init(void)
{
    BOOTDBG("pcnet: initializing driver\n");
    
    /* Search for PCNET PCI devices */
    for (int i = 0; i < pci_device_count(); i++) {
        struct pci_dev *dev = pci_get_device(i);
        if (!dev)
            continue;
        
        if (dev->vendor_id == PCNET_VENDOR_ID &&
            dev->device_id == PCNET_DEVICE_ID) {
            pcnet_probe(dev);
        }
    }
}
