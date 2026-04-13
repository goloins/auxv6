/*
 * Motorola SM56 modem stub driver for auxv6.
 *
 * Covers common Motorola softmodem chipsets used in legacy Wintel systems.
 */

#include "types.h"
#include "defs.h"
#include "pci.h"

#define SM56_VENDOR_MOTOROLA  PCI_VENDOR_MOTOROLA
#define SM56_DEV_GENERIC1     0x3052
#define SM56_DEV_GENERIC2     0x3410

static int
motorola_sm56_match(struct pci_dev *dev)
{
  if(dev->vendor_id != SM56_VENDOR_MOTOROLA)
    return 0;

  if(dev->class_code == PCI_CLASS_COMM && dev->subclass == PCI_SUBCLASS_COMM_MODEM)
    return 1;

  switch(dev->device_id){
  case SM56_DEV_GENERIC1:
  case SM56_DEV_GENERIC2:
    return 1;
  default:
    return 0;
  }
}

static void
motorola_sm56_probe(struct pci_dev *dev)
{
  modem_register_stub_probe("motorola_sm56", dev);
  cprintf("motorola_sm56: found device %x (stub) at %d:%d.%d irq=%d\n",
          dev->device_id, dev->bus, dev->slot, dev->func, dev->irq_line);
}

void
motorola_sm56_init(void)
{
  int i;

  BOOTDBG("motorola_sm56: probing supported PCI IDs (stub)\n");
  for(i = 0; i < pci_device_count(); i++){
    struct pci_dev *dev = pci_get_device(i);
    if(dev && motorola_sm56_match(dev))
      motorola_sm56_probe(dev);
  }
}
