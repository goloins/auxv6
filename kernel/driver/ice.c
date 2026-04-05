/*
 * Intel ice-family NIC stub driver for auxv6.
 *
 * Covers common 2019-2020 E810 generation IDs.
 */

#include "types.h"
#include "defs.h"
#include "pci.h"

#define ICE_VENDOR_INTEL 0x8086
#define ICE_DEV_E810_1   0x1591
#define ICE_DEV_E810_2   0x1592
#define ICE_DEV_E810_3   0x1593

static int
ice_match(struct pci_dev *dev)
{
    if(dev->vendor_id != ICE_VENDOR_INTEL)
        return 0;

    switch(dev->device_id){
    case ICE_DEV_E810_1:
    case ICE_DEV_E810_2:
    case ICE_DEV_E810_3:
        return 1;
    default:
        return 0;
    }
}

static void
ice_probe(struct pci_dev *dev)
{
    cprintf("ice: found Intel device %x (stub) at %d:%d.%d irq=%d\n",
            dev->device_id, dev->bus, dev->slot, dev->func, dev->irq_line);
}

void
ice_init(void)
{
    int i;

    BOOTDBG("ice: probing supported PCI IDs (stub)\n");
    for(i = 0; i < pci_device_count(); i++){
        struct pci_dev *dev = pci_get_device(i);
        if(dev && ice_match(dev))
            ice_probe(dev);
    }
}
