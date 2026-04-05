/*
 * Broadcom bnx2-family NIC stub driver for auxv6.
 *
 * Covers common NetXtreme II 1GbE parts (5706/5708/5709 era).
 */

#include "types.h"
#include "defs.h"
#include "pci.h"

#define BNX2_VENDOR_BROADCOM 0x14E4
#define BNX2_DEV_5706        0x164A
#define BNX2_DEV_5708        0x164C
#define BNX2_DEV_5709        0x1639

static int
bnx2_match(struct pci_dev *dev)
{
    if(dev->vendor_id != BNX2_VENDOR_BROADCOM)
        return 0;

    switch(dev->device_id){
    case BNX2_DEV_5706:
    case BNX2_DEV_5708:
    case BNX2_DEV_5709:
        return 1;
    default:
        return 0;
    }
}

static void
bnx2_probe(struct pci_dev *dev)
{
    cprintf("bnx2: found Broadcom device %x (stub) at %d:%d.%d irq=%d\n",
            dev->device_id, dev->bus, dev->slot, dev->func, dev->irq_line);
}

void
bnx2_init(void)
{
    int i;

    BOOTDBG("bnx2: probing supported PCI IDs (stub)\n");
    for(i = 0; i < pci_device_count(); i++){
        struct pci_dev *dev = pci_get_device(i);
        if(dev && bnx2_match(dev))
            bnx2_probe(dev);
    }
}
