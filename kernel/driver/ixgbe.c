/*
 * Intel ixgbe-family NIC stub driver for auxv6.
 *
 * Covers common 10GbE parts from 2008-2020 era:
 * 82598, 82599, X540, X550.
 */

#include "types.h"
#include "defs.h"
#include "pci.h"

#define IXGBE_VENDOR_INTEL 0x8086
#define IXGBE_DEV_82598    0x10F8
#define IXGBE_DEV_82599    0x10FB
#define IXGBE_DEV_X540     0x1528
#define IXGBE_DEV_X550     0x1563

static int
ixgbe_match(struct pci_dev *dev)
{
    if(dev->vendor_id != IXGBE_VENDOR_INTEL)
        return 0;

    switch(dev->device_id){
    case IXGBE_DEV_82598:
    case IXGBE_DEV_82599:
    case IXGBE_DEV_X540:
    case IXGBE_DEV_X550:
        return 1;
    default:
        return 0;
    }
}

static void
ixgbe_probe(struct pci_dev *dev)
{
    cprintf("ixgbe: found Intel device %x (stub) at %d:%d.%d irq=%d\n",
            dev->device_id, dev->bus, dev->slot, dev->func, dev->irq_line);
}

void
ixgbe_init(void)
{
    int i;

    BOOTDBG("ixgbe: probing supported PCI IDs (stub)\n");
    for(i = 0; i < pci_device_count(); i++){
        struct pci_dev *dev = pci_get_device(i);
        if(dev && ixgbe_match(dev))
            ixgbe_probe(dev);
    }
}
