/*
 * Smart Link (SL) modem stub driver for auxv6.
 *
 * Covers Smart Link softmodem families present in legacy Wintel machines.
 */

#include "types.h"
#include "defs.h"
#include "pci.h"

#define SLM_VENDOR_SMARTLINK  PCI_VENDOR_SMARTLINK
#define SLM_DEV_SL2800        0x3052
#define SLM_DEV_SL2810        0x3055
#define SLM_DEV_SL1900        0x3059

static int
smartlink_match(struct pci_dev *dev)
{
  if(dev->vendor_id != SLM_VENDOR_SMARTLINK)
    return 0;

  if(dev->class_code == PCI_CLASS_COMM && dev->subclass == PCI_SUBCLASS_COMM_MODEM)
    return 1;

  switch(dev->device_id){
  case SLM_DEV_SL2800:
  case SLM_DEV_SL2810:
  case SLM_DEV_SL1900:
    return 1;
  default:
    return 0;
  }
}

static void
smartlink_probe(struct pci_dev *dev)
{
  cprintf("smartlink: found device %x (stub) at %d:%d.%d irq=%d\n",
          dev->device_id, dev->bus, dev->slot, dev->func, dev->irq_line);
}

void
smartlink_init(void)
{
  int i;

  BOOTDBG("smartlink: probing supported PCI IDs (stub)\n");
  for(i = 0; i < pci_device_count(); i++){
    struct pci_dev *dev = pci_get_device(i);
    if(dev && smartlink_match(dev))
      smartlink_probe(dev);
  }
}
