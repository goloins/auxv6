/*
 * ASIX AX88179 PCI-Oriented Driver Stub for auxv6
 *
 * AX88179 is most often USB-attached in the wild; this file intentionally
 * provides a PCI-only skeleton per project request, without xHCI/USB
 * dependencies. The attach flow follows the same BSD-like style used in
 * other auxv6 NIC stubs.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"

/* Provisional PCI vendor/device IDs for AX88179-like bring-up scaffolding. */
#define PCI_VENDOR_ASIX_PCI 0x125B
#define PCI_VENDOR_ASIX_USB 0x0B95
#define PCI_DEVICE_AX88179  0x1790
#define PCI_DEVICE_AX88179A 0x1791

/* e1000-like offsets used only for minimal diagnostic probing in the stub. */
#define AX88179_CTRL      0x00000
#define AX88179_STATUS    0x00008
#define AX88179_RAL0      0x05400
#define AX88179_RAH0      0x05404

#define AX88179_STATUS_LU 0x00000002

#define MAX_AX88179 4

struct ax88179_softc {
    struct pci_dev *pci;
    struct spinlock lock;
    struct ifnet ifn;

    volatile uint32_t *regs;
    uint8_t mac[6];
    uint32_t status;
};

static struct ax88179_softc ax88179_devices[MAX_AX88179];
static int ax88179_count;

static int
ax88179_output(struct ifnet *ifp, struct mbuf *m)
{
    (void)ifp;
    if (m)
        mbuf_free(m);
    return -1;
}

static struct ifnet_ops ax88179_ifnet_ops = {
    .if_output = ax88179_output,
};

static int
ax88179_match(struct pci_dev *dev)
{
    if (dev == 0)
        return 0;

    if (dev->device_id != PCI_DEVICE_AX88179 &&
        dev->device_id != PCI_DEVICE_AX88179A)
        return 0;

    if (dev->vendor_id == PCI_VENDOR_ASIX_PCI ||
        dev->vendor_id == PCI_VENDOR_ASIX_USB)
        return 1;

    return 0;
}

static uint32_t
ax88179_read(struct ax88179_softc *sc, int reg)
{
    return sc->regs[reg / 4];
}

static void
ax88179_read_mac(struct ax88179_softc *sc)
{
    uint32_t ral;
    uint32_t rah;

    ral = ax88179_read(sc, AX88179_RAL0);
    rah = ax88179_read(sc, AX88179_RAH0);

    sc->mac[0] = ral & 0xFF;
    sc->mac[1] = (ral >> 8) & 0xFF;
    sc->mac[2] = (ral >> 16) & 0xFF;
    sc->mac[3] = (ral >> 24) & 0xFF;
    sc->mac[4] = rah & 0xFF;
    sc->mac[5] = (rah >> 8) & 0xFF;
}

static int
ax88179_probe(struct pci_dev *dev)
{
    struct ax88179_softc *sc;

    if (ax88179_count >= MAX_AX88179)
        return -1;

    sc = &ax88179_devices[ax88179_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "ax88179");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    sc->regs = pci_map_bar(dev, 0);
    if (!sc->regs) {
        cprintf("ax88179: failed to map BAR0 at %d:%d.%d\n",
                dev->bus, dev->slot, dev->func);
        return -1;
    }

    sc->status = ax88179_read(sc, AX88179_STATUS);
    ax88179_read_mac(sc);

    cprintf("ax88179: found at %d:%d.%d vendor=%x devid=%x rev=%d irq=%d ctrl=%x status=%x\n",
            dev->bus, dev->slot, dev->func,
            dev->vendor_id, dev->device_id, dev->revision, dev->irq_line,
            ax88179_read(sc, AX88179_CTRL), sc->status);
    cprintf("ax88179: MAC %x:%x:%x:%x:%x:%x\n",
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "axp0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[3] = '0' + ax88179_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_BROADCAST;
    if (sc->status & AX88179_STATUS_LU)
        sc->ifn.if_flags |= IFF_RUNNING;
    memmove(sc->ifn.if_hwaddr, sc->mac, sizeof(sc->ifn.if_hwaddr));
    sc->ifn.if_softc = sc;
    sc->ifn.if_input = ether_input;
    sc->ifn.if_ops = &ax88179_ifnet_ops;

    if (if_register(&sc->ifn) < 0) {
        cprintf("ax88179: failed to register ifnet\n");
        return -1;
    }

    cprintf("ax88179: attached %s (pci stub, TX/RX not implemented yet)\n", sc->ifn.if_xname);
    ax88179_count++;
    return 0;
}

void
ax88179_pci_init(void)
{
    int i;
    struct pci_dev *dev;

    cprintf("ax88179: initializing PCI driver stub\n");

    for (i = 0; i < pci_device_count(); i++) {
        dev = pci_get_device(i);
        if (ax88179_match(dev))
            ax88179_probe(dev);
    }
}
