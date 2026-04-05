/*
 * Amazon ENA NIC stub driver for auxv6.
 *
 * Covers common Elastic Network Adapter virtual PCI IDs.
 */

#include "types.h"
#include "defs.h"
#include "pci.h"

#define ENA_VENDOR_AMAZON 0x1D0F
#define ENA_DEV_VF        0xEC20
#define ENA_DEV_PF        0xEC21

static int
ena_match(struct pci_dev *dev)
{
    if(dev->vendor_id != ENA_VENDOR_AMAZON)
        return 0;

    switch(dev->device_id){
    case ENA_DEV_VF:
    case ENA_DEV_PF:
        return 1;
    default:
        return 0;
    }
}

static void
ena_probe(struct pci_dev *dev)
{
    cprintf("ena: found Amazon device %x (stub) at %d:%d.%d irq=%d\n",
            dev->device_id, dev->bus, dev->slot, dev->func, dev->irq_line);
}

void
ena_init(void)
{
    int i;

    BOOTDBG("ena: probing supported PCI IDs (stub)\n");
    for(i = 0; i < pci_device_count(); i++){
        struct pci_dev *dev = pci_get_device(i);
        if(dev && ena_match(dev))
            ena_probe(dev);
    }
}
