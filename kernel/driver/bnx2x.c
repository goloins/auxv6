/*
 * Broadcom bnx2x-family NIC stub driver for auxv6.
 *
 * Covers common NetXtreme II 10GbE parts (57710/57711/57712 era).
 */

#include "types.h"
#include "defs.h"
#include "pci.h"

#define BNX2X_VENDOR_BROADCOM 0x14E4
#define BNX2X_DEV_57710       0x164E
#define BNX2X_DEV_57711       0x164F
#define BNX2X_DEV_57712       0x1662

static int
bnx2x_match(struct pci_dev *dev)
{
    if(dev->vendor_id != BNX2X_VENDOR_BROADCOM)
        return 0;

    switch(dev->device_id){
    case BNX2X_DEV_57710:
    case BNX2X_DEV_57711:
    case BNX2X_DEV_57712:
        return 1;
    default:
        return 0;
    }
}

static void
bnx2x_probe(struct pci_dev *dev)
{
    cprintf("bnx2x: found Broadcom device %x (stub) at %d:%d.%d irq=%d\n",
            dev->device_id, dev->bus, dev->slot, dev->func, dev->irq_line);
}

void
bnx2x_init(void)
{
    int i;

    BOOTDBG("bnx2x: probing supported PCI IDs (stub)\n");
    for(i = 0; i < pci_device_count(); i++){
        struct pci_dev *dev = pci_get_device(i);
        if(dev && bnx2x_match(dev))
            bnx2x_probe(dev);
    }
}
