/*
 * Intel softmodem stub driver for auxv6.
 *
 * Covers commonly seen Intel 536EP/537EP-era PCI softmodems.
 */

#include "types.h"
#include "defs.h"
#include "pci.h"

#define ISM_VENDOR_INTEL   PCI_VENDOR_INTEL
#define ISM_DEV_536EP      0x1040
#define ISM_DEV_537EP      0x1080
#define ISM_DEV_537SP      0x1059

static int
intel_softmodem_match(struct pci_dev *dev)
{
  if(dev->vendor_id != ISM_VENDOR_INTEL)
    return 0;

  if(dev->class_code == PCI_CLASS_COMM && dev->subclass == PCI_SUBCLASS_COMM_MODEM)
    return 1;

  switch(dev->device_id){
  case ISM_DEV_536EP:
  case ISM_DEV_537EP:
  case ISM_DEV_537SP:
    return 1;
  default:
    return 0;
  }
}

static void
intel_softmodem_probe(struct pci_dev *dev)
{
  cprintf("intel_softmodem: found device %x (stub) at %d:%d.%d irq=%d\n",
          dev->device_id, dev->bus, dev->slot, dev->func, dev->irq_line);
}

void
intel_softmodem_init(void)
{
  int i;

  BOOTDBG("intel_softmodem: probing supported PCI IDs (stub)\n");
  for(i = 0; i < pci_device_count(); i++){
    struct pci_dev *dev = pci_get_device(i);
    if(dev && intel_softmodem_match(dev))
      intel_softmodem_probe(dev);
  }
}
