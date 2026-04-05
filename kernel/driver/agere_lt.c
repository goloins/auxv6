/*
 * Agere/Lucent LT modem stub driver for auxv6.
 *
 * Targets LT-class softmodems commonly encountered in legacy Wintel systems.
 */

#include "types.h"
#include "defs.h"
#include "pci.h"

#define LT_VENDOR_AGERE  PCI_VENDOR_AGERE
#define LT_DEV_LT_GENERIC1 0x0440
#define LT_DEV_LT_GENERIC2 0x0480

static int
agere_lt_match(struct pci_dev *dev)
{
  if(dev->vendor_id != LT_VENDOR_AGERE)
    return 0;

  if(dev->class_code == PCI_CLASS_COMM && dev->subclass == PCI_SUBCLASS_COMM_MODEM)
    return 1;

  switch(dev->device_id){
  case LT_DEV_LT_GENERIC1:
  case LT_DEV_LT_GENERIC2:
    return 1;
  default:
    return 0;
  }
}

static void
agere_lt_probe(struct pci_dev *dev)
{
  cprintf("agere_lt: found device %x (stub) at %d:%d.%d irq=%d\n",
          dev->device_id, dev->bus, dev->slot, dev->func, dev->irq_line);
}

void
agere_lt_init(void)
{
  int i;

  BOOTDBG("agere_lt: probing supported PCI IDs (stub)\n");
  for(i = 0; i < pci_device_count(); i++){
    struct pci_dev *dev = pci_get_device(i);
    if(dev && agere_lt_match(dev))
      agere_lt_probe(dev);
  }
}
