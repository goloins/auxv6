#include "types.h"
#include "defs.h"
#include "pci.h"
#include "usb_hcd.h"

int
usb_probe_uhci_regs(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  (void)dev;

  if(!sc)
    return -1;
  if(sc->bar0 == 0 || sc->bar0_size == 0)
    return -1;
  if(!sc->bar0_is_io)
    return -1;

  /* UHCI typically uses I/O BARs; defer deeper I/O register probing. */
  sc->reg_probe_ok = 1;
  sc->rh_present = 1;
  sc->rh_ports = 0;
  sc->rh_change_bits = 0;
  sc->phase = USB_HC_PHASE_READY;
  return 0;
}

int
usb_uhci_reset(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  (void)dev;
  if(!sc || !sc->reg_probe_ok)
    return -1;
  return 0;
}

int
usb_uhci_halt(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  (void)dev;
  if(!sc || !sc->reg_probe_ok)
    return -1;
  return 0;
}

int
usb_uhci_start(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  (void)dev;
  if(!sc || !sc->reg_probe_ok)
    return -1;
  return 0;
}
