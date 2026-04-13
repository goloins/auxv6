#include "types.h"
#include "defs.h"
#include "pci.h"
#include "audio.h"
#include "audio_pci_common.h"

int
audio_pci_stub_match_dev(const struct audio_pci_stub_match *m,
                         const struct pci_dev *dev)
{
  if(m == 0 || dev == 0)
    return 0;
  if(m->vendor != AUDIO_PCI_ANY_VENDOR && dev->vendor_id != m->vendor)
    return 0;
  if(m->device != AUDIO_PCI_ANY_DEVICE && dev->device_id != m->device)
    return 0;
  if(m->class_code != AUDIO_PCI_ANY_CLASS && dev->class_code != m->class_code)
    return 0;
  if(m->subclass != AUDIO_PCI_ANY_SUBCLASS && dev->subclass != m->subclass)
    return 0;
  return 1;
}

int
audio_pci_stub_pick_bar(struct pci_dev *dev)
{
  int bar;

  if(dev == 0)
    return -1;

  for(bar = 0; bar < 6; bar++){
    if((dev->bar_type[bar] & PCI_BAR_IO) != 0)
      continue;
    if(pci_bar_size(dev, bar) < 4096)
      continue;
    return bar;
  }
  return -1;
}

int
audio_pci_stub_attach(struct audio_pci_stub_softc *sc,
                      struct pci_dev *dev,
                      const struct audio_pci_stub_match *m,
                      int prefer_bus_master)
{
  int bar;

  if(sc == 0 || dev == 0 || m == 0)
    return -1;

  memset(sc, 0, sizeof(*sc));
  sc->pci = dev;

  pci_enable_mem(dev);
  if(prefer_bus_master)
    pci_set_master(dev);

  bar = audio_pci_stub_pick_bar(dev);
  sc->mmio_bar = bar;
  if(bar >= 0){
    sc->mmio_base = pci_bar_base(dev, bar);
    sc->mmio_size = pci_bar_size(dev, bar);
    sc->regs = (volatile uint32_t *)pci_map_bar(dev, bar);
  }

  if(sc->regs){
    BOOTDBG("audio/%s: attach %d:%d.%d ven=%x dev=%x irq=%d bar%d base=%x size=%x\n",
            m->name,
            dev->bus, dev->slot, dev->func,
            dev->vendor_id, dev->device_id, dev->irq_line,
            sc->mmio_bar, sc->mmio_base, sc->mmio_size);
  } else {
    BOOTDBG("audio/%s: attach %d:%d.%d ven=%x dev=%x irq=%d (no MMIO bar)\n",
            m->name,
            dev->bus, dev->slot, dev->func,
            dev->vendor_id, dev->device_id, dev->irq_line);
  }

  if(audio_register_hw_device(dev->vendor_id, dev->device_id,
                              AUDIO_CARD_AUTO, 0,
                              AUDIO_DIR_PLAYBACK,
                              m->flags,
                              m->profile,
                              m->name) < 0)
    return -1;
  return 0;
}
