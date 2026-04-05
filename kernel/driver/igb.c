/*
 * Intel igb-family NIC stub driver for auxv6.
 *
 * Covers common 1GbE parts from 2007-2014 era:
 * 82575/82576/82580, I350, I210, I211.
 */

#include "types.h"
#include "defs.h"
#include "pci.h"

#define IGB_VENDOR_INTEL 0x8086
#define IGB_DEV_82575    0x10A7
#define IGB_DEV_82576    0x10C9
#define IGB_DEV_82580    0x150E
#define IGB_DEV_I350     0x1521
#define IGB_DEV_I210     0x1533
#define IGB_DEV_I211     0x1539

static int
igb_match(struct pci_dev *dev)
{
    if(dev->vendor_id != IGB_VENDOR_INTEL)
        return 0;

    switch(dev->device_id){
    case IGB_DEV_82575:
    case IGB_DEV_82576:
    case IGB_DEV_82580:
    case IGB_DEV_I350:
    case IGB_DEV_I210:
    case IGB_DEV_I211:
        return 1;
    default:
        return 0;
    }
}

static void
igb_probe(struct pci_dev *dev)
{
    cprintf("igb: found Intel device %x (stub) at %d:%d.%d irq=%d\n",
            dev->device_id, dev->bus, dev->slot, dev->func, dev->irq_line);
}

void
igb_init(void)
{
    int i;

    BOOTDBG("igb: probing supported PCI IDs (stub)\n");
    for(i = 0; i < pci_device_count(); i++){
        struct pci_dev *dev = pci_get_device(i);
        if(dev && igb_match(dev))
            igb_probe(dev);
    }
}
