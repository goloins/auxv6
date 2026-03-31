#include "types.h"
#include "defs.h"
#include "pci.h"

#define RTL8111_DEVICE_8161 0x8161
#define RTL8111_DEVICE_8168 0x8168

static int
rtl8111_match(struct pci_dev *dev)
{
    if (dev == 0)
        return 0;
    if (dev->vendor_id != PCI_VENDOR_REALTEK)
        return 0;
    if (dev->device_id == RTL8111_DEVICE_8161 ||
        dev->device_id == RTL8111_DEVICE_8168)
        return 1;
    return 0;
}

static void
rtl8111_probe_stub(struct pci_dev *dev)
{
    void *regs;

    pci_enable_mem(dev);
    pci_set_master(dev);
    regs = pci_map_bar(dev, 2);
    if (regs == 0)
        regs = pci_map_bar(dev, 1);
    if (regs == 0)
        regs = pci_map_bar(dev, 0);

    cprintf("rtl8111: found %x at %d:%d.%d irq=%d regs=%p (stub)\n",
            dev->device_id, dev->bus, dev->slot, dev->func,
            dev->irq_line, regs);
}

void
rtl8111_init(void)
{
    int i;
    struct pci_dev *dev;

    cprintf("rtl8111: probe stub\n");
    for (i = 0; i < pci_device_count(); i++) {
        dev = pci_get_device(i);
        if (rtl8111_match(dev))
            rtl8111_probe_stub(dev);
    }
}