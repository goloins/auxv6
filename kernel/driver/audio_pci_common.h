#ifndef AUXV6_AUDIO_PCI_COMMON_H
#define AUXV6_AUDIO_PCI_COMMON_H

#include "types.h"
#include "pci.h"

#define AUDIO_PCI_ANY_VENDOR 0xFFFFU
#define AUDIO_PCI_ANY_DEVICE 0xFFFFU
#define AUDIO_PCI_ANY_CLASS  0xFFU
#define AUDIO_PCI_ANY_SUBCLASS 0xFFU

struct audio_pci_stub_match {
  const char *name;
  uint16_t vendor;
  uint16_t device;
  uint8_t class_code;
  uint8_t subclass;
  uint32_t flags;
  uint32_t profile;
};

struct audio_pci_stub_softc {
  struct pci_dev *pci;
  volatile uint32_t *regs;
  uint32_t mmio_base;
  uint32_t mmio_size;
  int mmio_bar;
};

int audio_pci_stub_match_dev(const struct audio_pci_stub_match *m,
                             const struct pci_dev *dev);
int audio_pci_stub_pick_bar(struct pci_dev *dev);
int audio_pci_stub_attach(struct audio_pci_stub_softc *sc,
                          struct pci_dev *dev,
                          const struct audio_pci_stub_match *m,
                          int prefer_bus_master);

#endif
