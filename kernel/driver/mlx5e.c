/*
 * Mellanox mlx5e-family NIC stub driver for auxv6.
 *
 * Covers common ConnectX-4/ConnectX-5 generation IDs.
 */

#include "types.h"
#include "defs.h"
#include "pci.h"

#define MLX5_VENDOR_MELLANOX 0x15B3
#define MLX5_DEV_CX4         0x1013
#define MLX5_DEV_CX4_LX      0x1015
#define MLX5_DEV_CX5         0x1017

static int
mlx5e_match(struct pci_dev *dev)
{
    if(dev->vendor_id != MLX5_VENDOR_MELLANOX)
        return 0;

    switch(dev->device_id){
    case MLX5_DEV_CX4:
    case MLX5_DEV_CX4_LX:
    case MLX5_DEV_CX5:
        return 1;
    default:
        return 0;
    }
}

static void
mlx5e_probe(struct pci_dev *dev)
{
    cprintf("mlx5e: found Mellanox device %x (stub) at %d:%d.%d irq=%d\n",
            dev->device_id, dev->bus, dev->slot, dev->func, dev->irq_line);
}

void
mlx5e_init(void)
{
    int i;

    BOOTDBG("mlx5e: probing supported PCI IDs (stub)\n");
    for(i = 0; i < pci_device_count(); i++){
        struct pci_dev *dev = pci_get_device(i);
        if(dev && mlx5e_match(dev))
            mlx5e_probe(dev);
    }
}
