/*
 * Intel I226-V (2.5GbE) Ethernet Driver Stub for auxv6
 *
 * BSD-oriented attach skeleton in the style of if_em/if_igc families:
 * probe via PCI, map BAR0, sample MAC/link registers, and publish
 * an ifnet instance with a stub transmit callback.
 *
 * Datapath, interrupts, and PHY management are intentionally TODO.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "net.h"

/* IGC/I225/I226 style core registers used during minimal attach. */
#define I226_CTRL       0x00000
#define I226_STATUS     0x00008
#define I226_RAL0       0x05400
#define I226_RAH0       0x05404

#define I226_STATUS_LU  0x00000002

/* Common Intel I226 family IDs (LM/V/IT variants). */
#define PCI_DEVICE_I226_LM 0x125B
#define PCI_DEVICE_I226_V  0x125C
#define PCI_DEVICE_I226_IT 0x125D

#define MAX_I226 4

struct i226_softc {
    struct pci_dev *pci;
    struct spinlock lock;
    struct ifnet ifn;

    volatile uint32_t *regs;
    uint8_t mac[6];
    uint32_t status;
};

static struct i226_softc i226_devices[MAX_I226];
static int i226_count;

static int
i226_output(struct ifnet *ifp, struct mbuf *m)
{
    (void)ifp;
    if (m)
        mbuf_free(m);
    return -1;
}

static struct ifnet_ops i226_ifnet_ops = {
    .if_output = i226_output,
};

static int
i226_match(struct pci_dev *dev)
{
    if (dev == 0)
        return 0;
    if (dev->vendor_id != PCI_VENDOR_INTEL)
        return 0;

    switch (dev->device_id) {
    case PCI_DEVICE_I226_LM:
    case PCI_DEVICE_I226_V:
    case PCI_DEVICE_I226_IT:
        return 1;
    default:
        return 0;
    }
}

static uint32_t
i226_read(struct i226_softc *sc, int reg)
{
    return sc->regs[reg / 4];
}

static void
i226_read_mac(struct i226_softc *sc)
{
    uint32_t ral;
    uint32_t rah;

    ral = i226_read(sc, I226_RAL0);
    rah = i226_read(sc, I226_RAH0);

    sc->mac[0] = ral & 0xFF;
    sc->mac[1] = (ral >> 8) & 0xFF;
    sc->mac[2] = (ral >> 16) & 0xFF;
    sc->mac[3] = (ral >> 24) & 0xFF;
    sc->mac[4] = rah & 0xFF;
    sc->mac[5] = (rah >> 8) & 0xFF;
}

static int
i226_probe(struct pci_dev *dev)
{
    struct i226_softc *sc;

    if (i226_count >= MAX_I226)
        return -1;

    sc = &i226_devices[i226_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "i226");
    sc->pci = dev;

    pci_enable_mem(dev);
    pci_set_master(dev);

    sc->regs = pci_map_bar(dev, 0);
    if (!sc->regs) {
        cprintf("i226: failed to map BAR0 at %d:%d.%d\n",
                dev->bus, dev->slot, dev->func);
        return -1;
    }

    sc->status = i226_read(sc, I226_STATUS);
    i226_read_mac(sc);

    cprintf("i226: found at %d:%d.%d devid=%x rev=%d irq=%d ctrl=%x status=%x\n",
            dev->bus, dev->slot, dev->func,
            dev->device_id, dev->revision, dev->irq_line,
            i226_read(sc, I226_CTRL), sc->status);
    cprintf("i226: MAC %x:%x:%x:%x:%x:%x\n",
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);

    memset(&sc->ifn, 0, sizeof(sc->ifn));
    safestrcpy(sc->ifn.if_xname, "igc0", sizeof(sc->ifn.if_xname));
    sc->ifn.if_xname[3] = '0' + i226_count;
    sc->ifn.if_mtu = 1500;
    sc->ifn.if_flags = IFF_BROADCAST;
    if (sc->status & I226_STATUS_LU)
        sc->ifn.if_flags |= IFF_RUNNING;
    memmove(sc->ifn.if_hwaddr, sc->mac, sizeof(sc->ifn.if_hwaddr));
    sc->ifn.if_softc = sc;
    sc->ifn.if_input = ether_input;
    sc->ifn.if_ops = &i226_ifnet_ops;

    if (if_register(&sc->ifn) < 0) {
        cprintf("i226: failed to register ifnet\n");
        return -1;
    }

    cprintf("i226: attached %s (stub, TX/RX not implemented yet)\n", sc->ifn.if_xname);
    i226_count++;
    return 0;
}

void
i226_init(void)
{
    int i;
    struct pci_dev *dev;

    cprintf("i226: initializing driver stub\n");

    for (i = 0; i < pci_device_count(); i++) {
        dev = pci_get_device(i);
        if (i226_match(dev))
            i226_probe(dev);
    }
}
