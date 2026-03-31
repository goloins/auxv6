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
#define VMXNET3_CMD_FIRST_SET           0xCAFE0000
#define VMXNET3_CMD_ACTIVATE_DEV        0xCAFE0001
#define VMXNET3_CMD_QUIESCE_DEV         0xCAFE0002
#define VMXNET3_CMD_RESET_DEV           0xCAFE0003
#define VMXNET3_CMD_UPDATE_RX_MODE      0xCAFE0004
#define VMXNET3_CMD_UPDATE_MAC_FILTERS  0xCAFE0005
#define VMXNET3_CMD_UPDATE_VLAN_FILTERS 0xCAFE0006
#define VMXNET3_CMD_UPDATE_RSSIDT       0xCAFE0007
#define VMXNET3_CMD_UPDATE_IML          0xCAFE0008
#define VMXNET3_CMD_UPDATE_PMCFG        0xCAFE0009
#define VMXNET3_CMD_UPDATE_FEATURE      0xCAFE000A
#define VMXNET3_CMD_LOAD_PLUGIN         0xCAFE000B
#define VMXNET3_CMD_GET_QUEUE_STATUS    0xF00D0000
#define VMXNET3_CMD_GET_STATS           0xF00D0001
#define VMXNET3_CMD_GET_LINK            0xF00D0002
#define VMXNET3_CMD_GET_PERM_MAC_LO     0xF00D0003
#define VMXNET3_CMD_GET_PERM_MAC_HI     0xF00D0004
#define VMXNET3_CMD_GET_DID_LO          0xF00D0005
#define VMXNET3_CMD_GET_DID_HI          0xF00D0006
#define VMXNET3_CMD_GET_DEV_EXTRA_INFO  0xF00D0007
#define VMXNET3_CMD_GET_CONF_INTR       0xF00D0008
#define VMXNET3_CMD_GET_ADAPTIVE_RING_INFO 0xF00D0009

/* Ring sizes */
#define VMXNET3_TX_RING_SIZE    128
#define VMXNET3_RX_RING_SIZE    128
#define VMXNET3_RX_BUF_SIZE     2048

/* Per-device state */
struct vmxnet3_softc {
    struct pci_dev    *pci;
    struct spinlock    lock;
    struct ifnet       ifn;
    
    volatile uint32_t *pt_regs;     /* Passthrough registers */
    volatile uint32_t *vd_regs;     /* Virtual device registers */
    
    uint8_t            mac[6];
    
    /* TODO: Add TX/RX rings when implementing */
};

static int vmxnet3_output(struct ifnet *ifp, struct mbuf *m);

static struct ifnet_ops vmxnet3_ifnet_ops = {
    .if_output = vmxnet3_output,
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
    vmxnet3_pt_write(sc, VMXNET3_REG_CMD, VMXNET3_CMD_GET_PERM_MAC_LO);
    macl = vmxnet3_pt_read(sc, VMXNET3_REG_CMD);
    
    vmxnet3_pt_write(sc, VMXNET3_REG_CMD, VMXNET3_CMD_GET_PERM_MAC_HI);
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

/* Stub output function - returns error since driver not fully implemented */
static int
vmxnet3_output(struct ifnet *ifp, struct mbuf *m)
{
    (void)ifp;
    if (m)
        mbuf_free(m);
    return -1;  /* Not implemented */
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
    
    BOOTDBG("vmxnet3: found at %d:%d.%d irq=%d pt=%p vd=%p (stub)\n",
            dev->bus, dev->slot, dev->func, dev->irq_line,
            sc->pt_regs, sc->vd_regs);
    
    /* Read MAC address */
    vmxnet3_read_mac(sc);
    
    /* TODO: Full initialization - for now just log detection */
    cprintf("vmxnet3: driver not fully implemented\n");
    
    /* Set up ifnet structure but don't register yet (stub) */
    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "vmx0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[3] = '0' + vmxnet3_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_BROADCAST;  /* Not UP - stub driver */
    memmove(sc->ifn.if_hwaddr, sc->mac, sizeof(sc->ifn.if_hwaddr));
    sc->ifn.if_softc = sc;
    sc->ifn.if_input = ether_input;
    sc->ifn.if_ops = &vmxnet3_ifnet_ops;
    
    /* Note: Not registering with ifnet since stub is incomplete */
    /* if (if_register(&sc->ifn) < 0) { ... } */
    
    vmxnet3_count++;
    
    return 0;
}

void
vmxnet3_init(void)
{
    int i;
    struct pci_dev *dev;
    
    BOOTDBG("vmxnet3: initializing driver (stub)\n");
    
    for (i = 0; i < pci_device_count(); i++) {
        dev = pci_get_device(i);
        if (vmxnet3_match(dev))
            vmxnet3_probe(dev);
    }
}
