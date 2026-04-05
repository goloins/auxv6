/*
 * Intel E1000 Gigabit Ethernet Driver for auxv6
 *
 * Supports Intel 82540EM and similar E1000 family chips.
 * Common in QEMU/VirtualBox emulation.
 *
 * Architecture:
 * - Memory-mapped I/O via BAR0
 * - Ring-based TX/RX queues
 * - Integrates with ifnet layer via if_register()
 *
 * TODO Phase 1:
 * - [ ] PCI probe and BAR mapping
 * - [ ] Basic register access
 * - [ ] TX/RX ring initialization
 * - [ ] MAC address reading
 * - [ ] Link status detection
 *
 * TODO Phase 2:
 * - [ ] Full TX/RX implementation
 * - [ ] Interrupt handling
 * - [ ] Checksum offload
 * - [ ] PHY management
 *
 * Reference: Intel 82540EM Developer's Manual
 * See also: NetBSD dev/pci/if_wm.c, Linux e1000/e1000_main.c
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"
#include "stdint.h" 
#include "memlayout.h"

/* E1000 Register Offsets */
#define E1000_CTRL      0x00000  /* Device Control */
#define E1000_STATUS    0x00008  /* Device Status */
#define E1000_EECD      0x00010  /* EEPROM/Flash Control */
#define E1000_EERD      0x00014  /* EEPROM Read */
#define E1000_CTRL_EXT  0x00018  /* Extended Device Control */
#define E1000_MDIC      0x00020  /* MDI Control */
#define E1000_FCAL      0x00028  /* Flow Control Address Low */
#define E1000_FCAH      0x0002C  /* Flow Control Address High */
#define E1000_FCT       0x00030  /* Flow Control Type */
#define E1000_ICR       0x000C0  /* Interrupt Cause Read */
#define E1000_ICS       0x000C8  /* Interrupt Cause Set */
#define E1000_IMS       0x000D0  /* Interrupt Mask Set */
#define E1000_IMC       0x000D8  /* Interrupt Mask Clear */
#define E1000_RCTL      0x00100  /* Receive Control */
#define E1000_RDBAL     0x02800  /* RX Descriptor Base Low */
#define E1000_RDBAH     0x02804  /* RX Descriptor Base High */
#define E1000_RDLEN     0x02808  /* RX Descriptor Length */
#define E1000_RDH       0x02810  /* RX Descriptor Head */
#define E1000_RDT       0x02818  /* RX Descriptor Tail */
#define E1000_TCTL      0x00400  /* Transmit Control */
#define E1000_TDBAL     0x03800  /* TX Descriptor Base Low */
#define E1000_TDBAH     0x03804  /* TX Descriptor Base High */
#define E1000_TDLEN     0x03808  /* TX Descriptor Length */
#define E1000_TDH       0x03810  /* TX Descriptor Head */
#define E1000_TDT       0x03818  /* TX Descriptor Tail */
#define E1000_MTA       0x05200  /* Multicast Table Array (0x5200-0x53FC) */
#define E1000_RAL       0x05400  /* Receive Address Low */
#define E1000_RAH       0x05404  /* Receive Address High */

/* Control Register bits */
#define E1000_CTRL_FD       0x00000001  /* Full Duplex */
#define E1000_CTRL_LRST     0x00000008  /* Link Reset */
#define E1000_CTRL_ASDE     0x00000020  /* Auto-Speed Detection Enable */
#define E1000_CTRL_SLU      0x00000040  /* Set Link Up */
#define E1000_CTRL_ILOS     0x00000080  /* Invert Loss-of-Signal */
#define E1000_CTRL_RST      0x04000000  /* Device Reset */
#define E1000_CTRL_VME      0x40000000  /* VLAN Mode Enable */
#define E1000_CTRL_PHY_RST  0x80000000  /* PHY Reset */

/* Status Register bits */
#define E1000_STATUS_FD     0x00000001  /* Full Duplex */
#define E1000_STATUS_LU     0x00000002  /* Link Up */
#define E1000_STATUS_SPEED  0x000000C0  /* Speed setting mask */
#define E1000_STATUS_SPEED_10   0x00000000
#define E1000_STATUS_SPEED_100  0x00000040
#define E1000_STATUS_SPEED_1000 0x00000080

/* Receive Control bits */
#define E1000_RCTL_EN       0x00000002  /* Receiver Enable */
#define E1000_RCTL_SBP      0x00000004  /* Store Bad Packets */
#define E1000_RCTL_UPE      0x00000008  /* Unicast Promiscuous Enable */
#define E1000_RCTL_MPE      0x00000010  /* Multicast Promiscuous Enable */
#define E1000_RCTL_LPE      0x00000020  /* Long Packet Enable */
#define E1000_RCTL_LBM_NO   0x00000000  /* No Loopback */
#define E1000_RCTL_BAM      0x00008000  /* Broadcast Accept Mode */
#define E1000_RCTL_BSEX     0x02000000  /* Buffer Size Extension */
#define E1000_RCTL_SECRC    0x04000000  /* Strip Ethernet CRC */

/* Buffer sizes */
#define E1000_RCTL_BSIZE_2048   0x00000000
#define E1000_RCTL_BSIZE_1024   0x00010000
#define E1000_RCTL_BSIZE_512    0x00020000
#define E1000_RCTL_BSIZE_256    0x00030000

/* Transmit Control bits */
#define E1000_TCTL_EN       0x00000002  /* Transmitter Enable */
#define E1000_TCTL_PSP      0x00000008  /* Pad Short Packets */
#define E1000_TCTL_CT       0x00000FF0  /* Collision Threshold */
#define E1000_TCTL_COLD     0x003FF000  /* Collision Distance */

/* Interrupt bits */
#define E1000_ICR_TXDW      0x00000001  /* TX Descriptor Written Back */
#define E1000_ICR_TXQE      0x00000002  /* TX Queue Empty */
#define E1000_ICR_LSC       0x00000004  /* Link Status Change */
#define E1000_ICR_RXSEQ     0x00000008  /* RX Sequence Error */
#define E1000_ICR_RXDMT0    0x00000010  /* RX Descriptor Minimum Threshold */
#define E1000_ICR_RXO       0x00000040  /* RX Overrun */
#define E1000_ICR_RXT0      0x00000080  /* RX Timer Interrupt */

/* Transmit Descriptor */
struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;       /* Checksum offset */
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;       /* Checksum start */
    uint16_t special;
} __attribute__((packed));

#define E1000_TXD_CMD_EOP   0x01    /* End of Packet */
#define E1000_TXD_CMD_IFCS  0x02    /* Insert FCS */
#define E1000_TXD_CMD_RS    0x08    /* Report Status */
#define E1000_TXD_STAT_DD   0x01    /* Descriptor Done */

/* Receive Descriptor */
struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

#define E1000_RXD_STAT_DD   0x01    /* Descriptor Done */
#define E1000_RXD_STAT_EOP  0x02    /* End of Packet */

/* Ring sizes (must be multiple of 8) */
#define E1000_TX_RING_SIZE  64
#define E1000_RX_RING_SIZE  64
#define E1000_RX_BUF_SIZE   2048

/* Per-device state */
struct e1000_softc {
    struct pci_dev    *pci;
    struct spinlock    lock;
    struct ifnet       ifn;             /* ifnet structure */
    
    /* Memory-mapped registers */
    volatile uint32_t *regs;
    
    /* MAC address */
    uint8_t mac[6];
    
    /* TX ring */
    struct e1000_tx_desc *tx_ring;
    uint16_t tx_head;
    uint16_t tx_tail;
    struct mbuf *tx_mbufs[E1000_TX_RING_SIZE];
    
    /* RX ring */
    struct e1000_rx_desc *rx_ring;
    uint16_t rx_tail;
    char *rx_bufs[E1000_RX_RING_SIZE];
};

static int e1000_output(struct ifnet *ifp, struct mbuf *m);

static struct ifnet_ops e1000_ifnet_ops = {
    .if_output = e1000_output,
};

/* Global array of E1000 devices */
#define MAX_E1000 4
static struct e1000_softc e1000_devices[MAX_E1000];
static int e1000_count = 0;
extern int ncpu;

/*
 * Read E1000 register
 */
static uint32_t
e1000_read(struct e1000_softc *sc, int reg)
{
    return sc->regs[reg / 4];
}

/*
 * Write E1000 register
 */
static void
e1000_write(struct e1000_softc *sc, int reg, uint32_t val)
{
    sc->regs[reg / 4] = val;
}

/*
 * Read MAC address from EEPROM
 */
static void
e1000_read_mac(struct e1000_softc *sc)
{
    /* Read from RAL/RAH first (set by emulator/firmware) */
    uint32_t ral = e1000_read(sc, E1000_RAL);
    uint32_t rah = e1000_read(sc, E1000_RAH);
    
    sc->mac[0] = ral & 0xFF;
    sc->mac[1] = (ral >> 8) & 0xFF;
    sc->mac[2] = (ral >> 16) & 0xFF;
    sc->mac[3] = (ral >> 24) & 0xFF;
    sc->mac[4] = rah & 0xFF;
    sc->mac[5] = (rah >> 8) & 0xFF;
    
    cprintf("e1000: MAC %x:%x:%x:%x:%x:%x\n",
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
}

/*
 * Reset the E1000
 */
static void
e1000_reset(struct e1000_softc *sc)
{
    /* Disable interrupts */
    e1000_write(sc, E1000_IMC, 0xFFFFFFFF);
    
    /* Global reset */
    e1000_write(sc, E1000_CTRL, e1000_read(sc, E1000_CTRL) | E1000_CTRL_RST);
    
    /* Wait for reset to complete */
    microdelay(10000);
    
    /* Disable interrupts again after reset */
    e1000_write(sc, E1000_IMC, 0xFFFFFFFF);
}

/*
 * Initialize TX ring
 */
static int
e1000_init_tx(struct e1000_softc *sc)
{
    /* Allocate TX descriptors */
    sc->tx_ring = (struct e1000_tx_desc *)kalloc();
    if (!sc->tx_ring)
        return -1;
    memset(sc->tx_ring, 0, sizeof(struct e1000_tx_desc) * E1000_TX_RING_SIZE);
    
    sc->tx_head = 0;
    sc->tx_tail = 0;
    
    /* Set TX descriptor base address */
    uint32_t tx_phys = V2P(sc->tx_ring);
    e1000_write(sc, E1000_TDBAL, tx_phys);
    e1000_write(sc, E1000_TDBAH, 0);
    
    /* Set TX descriptor length */
    e1000_write(sc, E1000_TDLEN, sizeof(struct e1000_tx_desc) * E1000_TX_RING_SIZE);
    
    /* Set TX head/tail */
    e1000_write(sc, E1000_TDH, 0);
    e1000_write(sc, E1000_TDT, 0);
    
    /* Enable transmitter */
    e1000_write(sc, E1000_TCTL,
        E1000_TCTL_EN |
        E1000_TCTL_PSP |
        (0x10 << 4) |           /* Collision threshold */
        (0x40 << 12));          /* Collision distance */
    
    return 0;
}

/*
 * Initialize RX ring
 */
static int
e1000_init_rx(struct e1000_softc *sc)
{
    /* Allocate RX descriptors */
    sc->rx_ring = (struct e1000_rx_desc *)kalloc();
    if (!sc->rx_ring)
        return -1;
    memset(sc->rx_ring, 0, sizeof(struct e1000_rx_desc) * E1000_RX_RING_SIZE);
    
    /* Allocate RX buffers */
    for (int i = 0; i < E1000_RX_RING_SIZE; i++) {
        sc->rx_bufs[i] = kalloc();
        if (!sc->rx_bufs[i])
            return -1;
        sc->rx_ring[i].addr = V2P(sc->rx_bufs[i]);
    }
    
    sc->rx_tail = E1000_RX_RING_SIZE - 1;
    
    /* Set RX descriptor base address */
    uint32_t rx_phys = V2P(sc->rx_ring);
    e1000_write(sc, E1000_RDBAL, rx_phys);
    e1000_write(sc, E1000_RDBAH, 0);
    
    /* Set RX descriptor length */
    e1000_write(sc, E1000_RDLEN, sizeof(struct e1000_rx_desc) * E1000_RX_RING_SIZE);
    
    /* Set RX head/tail */
    e1000_write(sc, E1000_RDH, 0);
    e1000_write(sc, E1000_RDT, sc->rx_tail);
    
    /* Clear multicast table */
    for (int i = 0; i < 128; i++)
        e1000_write(sc, E1000_MTA + i * 4, 0);
    
    /* Enable receiver */
    e1000_write(sc, E1000_RCTL,
        E1000_RCTL_EN |
        E1000_RCTL_BAM |
        E1000_RCTL_BSIZE_2048 |
        E1000_RCTL_SECRC);
    
    return 0;
}

/*
 * ifnet output function
 */
static int
e1000_output(struct ifnet *ifp, struct mbuf *m)
{
    struct e1000_softc *sc = (struct e1000_softc *)ifp->if_softc;
    
    if (!m || m->len == 0)
        return -1;
    
    acquire(&sc->lock);
    
    /* Check for space in TX ring */
    uint16_t next = (sc->tx_tail + 1) % E1000_TX_RING_SIZE;
    if (next == sc->tx_head) {
        release(&sc->lock);
        return -1;  /* Ring full */
    }
    
    /* Set up descriptor */
    struct e1000_tx_desc *desc = &sc->tx_ring[sc->tx_tail];
    desc->addr = V2P(m->data);
    desc->length = m->len;
    desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    desc->status = 0;
    
    sc->tx_mbufs[sc->tx_tail] = m;
    sc->tx_tail = next;
    
    /* Update tail register to start transmission */
    e1000_write(sc, E1000_TDT, sc->tx_tail);
    
    release(&sc->lock);
    
    return 0;
}

/*
 * Process completed TX descriptors
 */
static void
e1000_tx_complete(struct e1000_softc *sc)
{
    while (sc->tx_head != sc->tx_tail &&
           (sc->tx_ring[sc->tx_head].status & E1000_TXD_STAT_DD)) {
        /* Free TX mbuf */
        if (sc->tx_mbufs[sc->tx_head])
            mbuf_free(sc->tx_mbufs[sc->tx_head]);
        sc->tx_mbufs[sc->tx_head] = 0;
        sc->tx_head = (sc->tx_head + 1) % E1000_TX_RING_SIZE;
    }
}

/*
 * Process received packets
 */
static void
e1000_rx_complete(struct e1000_softc *sc)
{
    int i;
    struct mbuf *m;
    struct e1000_rx_desc *desc;
    int processed = 0;
    
    for (i = 0; i < E1000_RX_RING_SIZE && processed < 32; i++) {
        uint16_t idx = (sc->rx_tail + 1) % E1000_RX_RING_SIZE;
        desc = &sc->rx_ring[idx];
        
        /* Check if descriptor is done */
        if (!(desc->status & E1000_RXD_STAT_DD))
            break;
        
        if ((desc->status & E1000_RXD_STAT_EOP) && desc->errors == 0) {
            /* Valid complete packet */
            uint16_t len = desc->length;
            
            if (len > 0 && len <= E1000_RX_BUF_SIZE) {
                m = mbuf_alloc();
                if (m) {
                    memmove(m->data, sc->rx_bufs[idx], len);
                    m->len = len;
                    m->rcvif = &sc->ifn;
                    
                    /* Hand off to network stack outside lock */
                    release(&sc->lock);
                    if_input(&sc->ifn, m);
                    acquire(&sc->lock);
                }
            }
        }
        
        /* Reset descriptor for reuse */
        desc->status = 0;
        desc->addr = V2P(sc->rx_bufs[idx]);
        
        /* Update tail */
        sc->rx_tail = idx;
        e1000_write(sc, E1000_RDT, sc->rx_tail);
        processed++;
    }
}

/*
 * IRQ handler for dynamic registration
 */
static void
e1000_irq_handler(int irq, void *arg)
{
    struct e1000_softc *sc = (struct e1000_softc *)arg;
    uint32_t icr;
    
    (void)irq;
    if (!sc)
        return;
    
    icr = e1000_read(sc, E1000_ICR);
    if (icr == 0)
        return;
    
    acquire(&sc->lock);
    
    /* TX completion */
    if (icr & E1000_ICR_TXDW) {
        e1000_tx_complete(sc);
    }
    
    /* RX completion */
    if (icr & E1000_ICR_RXT0) {
        e1000_rx_complete(sc);
    }
    
    /* Link status change */
    if (icr & E1000_ICR_LSC) {
        uint32_t status = e1000_read(sc, E1000_STATUS);
        if (status & E1000_STATUS_LU)
            sc->ifn.if_flags |= IFF_RUNNING;
        else
            sc->ifn.if_flags &= ~IFF_RUNNING;
        cprintf("e1000: link %s\n", (status & E1000_STATUS_LU) ? "up" : "down");
    }
    
    release(&sc->lock);
}

/*
 * PCI probe function
 */
int
e1000_probe(struct pci_dev *pci)
{
    struct e1000_softc *sc;
    uint32_t status;
    
    if (e1000_count >= MAX_E1000)
        return -1;
    
    sc = &e1000_devices[e1000_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "e1000");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "e1000");
    sc->pci = pci;
    
    /* Map BAR0 (memory-mapped registers) */
    sc->regs = pci_map_bar(pci, 0);
    if (!sc->regs) {
        cprintf("e1000: failed to map BAR0\n");
        return -1;
    }
    
    /* Enable PCI bus master and memory */
    pci_enable_mem(pci);
    pci_set_master(pci);
    
    cprintf("e1000: found device at %d:%d.%d regs=%p\n",
            pci->bus, pci->slot, pci->func, sc->regs);
    
    /* Reset the device */
    e1000_reset(sc);
    
    /* Read MAC address */
    e1000_read_mac(sc);
    
    /* Initialize TX/RX rings */
    if (e1000_init_tx(sc) < 0 || e1000_init_rx(sc) < 0) {
        cprintf("e1000: failed to initialize rings\n");
        return -1;
    }
    
    /* Set link up */
    e1000_write(sc, E1000_CTRL,
        e1000_read(sc, E1000_CTRL) | E1000_CTRL_SLU | E1000_CTRL_ASDE);
    
    /* Register IRQ handler */
    if (irq_register(pci->irq_line, e1000_irq_handler, sc, "e1000") < 0) {
        cprintf("e1000: failed to register IRQ %d\n", pci->irq_line);
        return -1;
    }
    
    /* Enable interrupts */
    e1000_write(sc, E1000_IMS,
        E1000_ICR_TXDW | E1000_ICR_RXT0 | E1000_ICR_LSC | E1000_ICR_RXO);
    
    /* Enable PCI interrupt */
    pci_enable_irq(pci, ncpu - 1);
    
    /* Set up ifnet structure */
    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "em0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[2] = '0' + e1000_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_UP | IFF_BROADCAST;
    
    /* Check link status */
    status = e1000_read(sc, E1000_STATUS);
    if (status & E1000_STATUS_LU)
        sc->ifn.if_flags |= IFF_RUNNING;
    
    memmove(sc->ifn.if_hwaddr, sc->mac, sizeof(sc->ifn.if_hwaddr));
    sc->ifn.if_softc = sc;
    sc->ifn.if_input = ether_input;
    sc->ifn.if_ops = &e1000_ifnet_ops;
    
    /* Register with network stack */
    if (if_register(&sc->ifn) < 0) {
        cprintf("e1000: failed to register ifnet\n");
        irq_unregister(pci->irq_line, "e1000");
        return -1;
    }
    
    e1000_count++;
    cprintf("e1000: attached %s irq=%d\n", sc->ifn.if_xname, pci->irq_line);
    
    return 0;
}

/*
 * Module init
 */
void
e1000_init(void)
{
    BOOTDBG("e1000: initializing driver\n");
    
    /* Search for E1000 PCI devices */
    for (int i = 0; i < pci_device_count(); i++) {
        struct pci_dev *dev = pci_get_device(i);
        if (!dev)
            continue;
        
        if (dev->vendor_id == PCI_VENDOR_INTEL &&
            (dev->device_id == PCI_DEVICE_E1000 ||
             dev->device_id == PCI_DEVICE_E1000E)) {
            e1000_probe(dev);
        }
    }
}
