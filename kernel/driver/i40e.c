/*
 * Intel i40e-family NIC stub driver for auxv6.
 *
 * Covers common 10/25GbE parts from 2014-2020 era:
 * X710, XL710, XXV710.
 */

#include "types.h"
#include "defs.h"
#include "pci.h"

#define I40E_VENDOR_INTEL 0x8086
#define I40E_DEV_X710     0x1572
#define I40E_DEV_XL710    0x1583
#define I40E_DEV_XXV710   0x158B

static int
i40e_match(struct pci_dev *dev)
{
    if(dev->vendor_id != I40E_VENDOR_INTEL)
        return 0;

    switch(dev->device_id){
    case I40E_DEV_X710:
    case I40E_DEV_XL710:
    case I40E_DEV_XXV710:
        return 1;
    default:
        return 0;
    }
}

static void
i40e_probe(struct pci_dev *dev)
{
    cprintf("i40e: found Intel device %x (stub) at %d:%d.%d irq=%d\n",
            dev->device_id, dev->bus, dev->slot, dev->func, dev->irq_line);
}

void
i40e_init(void)
{
    int i;

    BOOTDBG("i40e: probing supported PCI IDs (stub)\n");
    for(i = 0; i < pci_device_count(); i++){
        struct pci_dev *dev = pci_get_device(i);
        if(dev && i40e_match(dev))
            i40e_probe(dev);
    }
}
