/*
 * Mellanox mlx4_en-family NIC stub driver for auxv6.
 *
 * Covers common ConnectX-3 generation IDs.
 */

#include "types.h"
#include "defs.h"
#include "pci.h"

#define MLX4_VENDOR_MELLANOX 0x15B3
#define MLX4_DEV_CX3_1       0x1003
#define MLX4_DEV_CX3_2       0x1007
#define MLX4_DEV_CX3_PRO     0x1011

static int
mlx4_en_match(struct pci_dev *dev)
{
    if(dev->vendor_id != MLX4_VENDOR_MELLANOX)
        return 0;

    switch(dev->device_id){
    case MLX4_DEV_CX3_1:
    case MLX4_DEV_CX3_2:
    case MLX4_DEV_CX3_PRO:
        return 1;
    default:
        return 0;
    }
}

static void
mlx4_en_probe(struct pci_dev *dev)
{
    cprintf("mlx4_en: found Mellanox device %x (stub) at %d:%d.%d irq=%d\n",
            dev->device_id, dev->bus, dev->slot, dev->func, dev->irq_line);
}

void
mlx4_en_init(void)
{
    int i;

    BOOTDBG("mlx4_en: probing supported PCI IDs (stub)\n");
    for(i = 0; i < pci_device_count(); i++){
        struct pci_dev *dev = pci_get_device(i);
        if(dev && mlx4_en_match(dev))
            mlx4_en_probe(dev);
    }
}
