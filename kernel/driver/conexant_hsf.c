/*
 * Conexant HSF/HCF modem stub driver for auxv6.
 *
 * Covers common PCI softmodem families frequently shipped as "Winmodem"
 * parts on desktop and laptop chipsets.
 */

#include "types.h"
#include "defs.h"
#include "pci.h"

#define HSF_VENDOR_CONEXANT  PCI_VENDOR_CONEXANT
#define HSF_DEV_HSF_GENERIC1 0x2F00
#define HSF_DEV_HSF_GENERIC2 0x2F20
#define HSF_DEV_HCF_GENERIC1 0x2F30

static int
conexant_hsf_match(struct pci_dev *dev)
{
  if(dev->vendor_id != HSF_VENDOR_CONEXANT)
    return 0;

  if(dev->class_code == PCI_CLASS_COMM && dev->subclass == PCI_SUBCLASS_COMM_MODEM)
    return 1;

  switch(dev->device_id){
  case HSF_DEV_HSF_GENERIC1:
  case HSF_DEV_HSF_GENERIC2:
  case HSF_DEV_HCF_GENERIC1:
    return 1;
  default:
    return 0;
  }
}

static void
conexant_hsf_probe(struct pci_dev *dev)
{
  cprintf("conexant_hsf: found device %x (stub) at %d:%d.%d irq=%d\n",
          dev->device_id, dev->bus, dev->slot, dev->func, dev->irq_line);
}

void
conexant_hsf_init(void)
{
  int i;

  BOOTDBG("conexant_hsf: probing supported PCI IDs (stub)\n");
  for(i = 0; i < pci_device_count(); i++){
    struct pci_dev *dev = pci_get_device(i);
    if(dev && conexant_hsf_match(dev))
      conexant_hsf_probe(dev);
  }
}
