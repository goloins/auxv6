/*
 * Qualcomm Atheros alx/atl1c-family NIC stub driver for auxv6.
 *
 * Covers common AR81xx desktop/laptop parts.
 */

#include "types.h"
#include "defs.h"
#include "pci.h"

#define ALX_VENDOR_ATTANSIC 0x1969
#define ALX_DEV_AR8131      0x1063
#define ALX_DEV_AR8151      0x1062
#define ALX_DEV_AR8161      0x1091
#define ALX_DEV_AR8171      0x10A1

static int
alx_match(struct pci_dev *dev)
{
    if(dev->vendor_id != ALX_VENDOR_ATTANSIC)
        return 0;

    switch(dev->device_id){
    case ALX_DEV_AR8131:
    case ALX_DEV_AR8151:
    case ALX_DEV_AR8161:
    case ALX_DEV_AR8171:
        return 1;
    default:
        return 0;
    }
}

static void
alx_probe(struct pci_dev *dev)
{
    cprintf("alx: found Atheros device %x (stub) at %d:%d.%d irq=%d\n",
            dev->device_id, dev->bus, dev->slot, dev->func, dev->irq_line);
}

void
alx_init(void)
{
    int i;

    BOOTDBG("alx: probing supported PCI IDs (stub)\n");
    for(i = 0; i < pci_device_count(); i++){
        struct pci_dev *dev = pci_get_device(i);
        if(dev && alx_match(dev))
            alx_probe(dev);
    }
}
