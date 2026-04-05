/*
 * PCTel modem stub driver for auxv6.
 *
 * Covers common PCI softmodem families from PCTel.
 */

#include "types.h"
#include "defs.h"
#include "pci.h"

#define PCTEL_VENDOR_PCTEL  PCI_VENDOR_PCTEL
#define PCTEL_DEV_2304W     0x7890
#define PCTEL_DEV_2304WT    0x7891
#define PCTEL_DEV_5057      0x5057

static int
pctel_match(struct pci_dev *dev)
{
  if(dev->vendor_id != PCTEL_VENDOR_PCTEL)
    return 0;

  if(dev->class_code == PCI_CLASS_COMM && dev->subclass == PCI_SUBCLASS_COMM_MODEM)
    return 1;

  switch(dev->device_id){
  case PCTEL_DEV_2304W:
  case PCTEL_DEV_2304WT:
  case PCTEL_DEV_5057:
    return 1;
  default:
    return 0;
  }
}

static void
pctel_probe(struct pci_dev *dev)
{
  cprintf("pctel: found device %x (stub) at %d:%d.%d irq=%d\n",
          dev->device_id, dev->bus, dev->slot, dev->func, dev->irq_line);
}

void
pctel_init(void)
{
  int i;

  BOOTDBG("pctel: probing supported PCI IDs (stub)\n");
  for(i = 0; i < pci_device_count(); i++){
    struct pci_dev *dev = pci_get_device(i);
    if(dev && pctel_match(dev))
      pctel_probe(dev);
  }
}
