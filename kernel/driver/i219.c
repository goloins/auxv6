/*
 * Intel I219-V (PCH LAN) Ethernet Driver Stub for auxv6
 *
 * This is a BSD-style attach skeleton inspired by NetBSD if_wm and
 * OpenBSD em. It currently does PCI match/attach, MMIO mapping,
 * basic MAC/link discovery, and ifnet registration with a stub TX path.
 *
 * Hardware target:
 *   00:1f.6 Ethernet controller: Intel Ethernet Connection (7) I219-V
 *
 * Notes:
 * - I219 is an e1000e/ICH-style integrated MAC and not a standalone
 *   PCIe NIC; many datapath details (descriptor engines, PHY/MDIO,
 *   manageability, interrupt moderation) remain TODO.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"

/* e1000e/I219-like register offsets used for basic attach diagnostics. */
#define I219_CTRL       0x00000
#define I219_STATUS     0x00008
#define I219_RAL0       0x05400
#define I219_RAH0       0x05404

#define I219_STATUS_LU  0x00000002

/* Intel I219 family device IDs commonly seen on desktop/laptop PCH. */
#define PCI_DEVICE_I219_LM_1 0x15B7
#define PCI_DEVICE_I219_V_1  0x15B8
#define PCI_DEVICE_I219_LM_2 0x15D7
#define PCI_DEVICE_I219_V_2  0x15D8
#define PCI_DEVICE_I219_LM_3 0x0D4E
#define PCI_DEVICE_I219_V_3  0x0D4F

#define MAX_I219 4

struct i219_softc {
    struct pci_dev *pci;
    struct spinlock lock;
    struct ifnet ifn;

    volatile uint32_t *regs;
    uint8_t mac[6];
    uint32_t status;
};

static struct i219_softc i219_devices[MAX_I219];
static int i219_count;

static int
i219_output(struct ifnet *ifp, struct mbuf *m)
{
    (void)ifp;
    if (m)
        mbuf_free(m);
    return -1;
}

static struct ifnet_ops i219_ifnet_ops = {
    .if_output = i219_output,
};

static int
i219_match(struct pci_dev *dev)
{
    if (dev == 0)
        return 0;
    if (dev->vendor_id != PCI_VENDOR_INTEL)
        return 0;

    switch (dev->device_id) {
    case PCI_DEVICE_I219_LM_1:
    case PCI_DEVICE_I219_V_1:
    case PCI_DEVICE_I219_LM_2:
    case PCI_DEVICE_I219_V_2:
    case PCI_DEVICE_I219_LM_3:
    case PCI_DEVICE_I219_V_3:
        return 1;
    default:
        return 0;
    }
}

static uint32_t
i219_read(struct i219_softc *sc, int reg)
{
    return sc->regs[reg / 4];
}

static void
i219_read_mac(struct i219_softc *sc)
{
    uint32_t ral;
    uint32_t rah;

    ral = i219_read(sc, I219_RAL0);
    rah = i219_read(sc, I219_RAH0);

    sc->mac[0] = ral & 0xFF;
    sc->mac[1] = (ral >> 8) & 0xFF;
    sc->mac[2] = (ral >> 16) & 0xFF;
    sc->mac[3] = (ral >> 24) & 0xFF;
    sc->mac[4] = rah & 0xFF;
    sc->mac[5] = (rah >> 8) & 0xFF;
}

static int
i219_probe(struct pci_dev *dev)
{
    struct i219_softc *sc;

    if (i219_count >= MAX_I219)
        return -1;

    sc = &i219_devices[i219_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "i219");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    sc->regs = pci_map_bar(dev, 0);
    if (!sc->regs) {
        cprintf("i219: failed to map BAR0 at %d:%d.%d\n",
                dev->bus, dev->slot, dev->func);
        return -1;
    }

    sc->status = i219_read(sc, I219_STATUS);
    i219_read_mac(sc);

    BOOTDBG("i219: found at %d:%d.%d devid=%x rev=%d irq=%d ctrl=%x status=%x\n",
            dev->bus, dev->slot, dev->func,
            dev->device_id, dev->revision, dev->irq_line,
            i219_read(sc, I219_CTRL), sc->status);
    cprintf("i219: MAC %x:%x:%x:%x:%x:%x\n",
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "wm0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[2] = '0' + i219_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_BROADCAST;
    if (sc->status & I219_STATUS_LU)
        sc->ifn.if_flags |= IFF_RUNNING;
    memmove(sc->ifn.if_hwaddr, sc->mac, sizeof(sc->ifn.if_hwaddr));
    sc->ifn.if_softc = sc;
    sc->ifn.if_input = ether_input;
    sc->ifn.if_ops = &i219_ifnet_ops;

    if (if_register(&sc->ifn) < 0) {
        cprintf("i219: failed to register ifnet\n");
        return -1;
    }

    cprintf("i219: attached %s (stub, TX/RX not implemented yet)\n", sc->ifn.if_xname);
    i219_count++;
    return 0;
}

void
i219_init(void)
{
    int i;
    struct pci_dev *dev;

    BOOTDBG("i219: initializing driver stub\n");

    for (i = 0; i < pci_device_count(); i++) {
        dev = pci_get_device(i);
        if (i219_match(dev))
            i219_probe(dev);
    }
}
