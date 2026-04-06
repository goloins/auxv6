#include "types.h"
#include "defs.h"
#include "pci.h"
#include "usb_hcd.h"

int
usb_probe_ohci_regs(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  volatile uint *regs;
  uint r0;
  uint r1;
  uint rhda;

  if(!sc || !dev)
    return -1;
  if(sc->bar0 == 0 || sc->bar0_size == 0)
    return -1;
  if(sc->bar0_is_io)
    return -1;

  regs = (volatile uint*)pci_map_bar(dev, 0);
  if(!regs)
    return -1;

  r0 = *(volatile uint *)((char*)regs + 0x00);
  r1 = *(volatile uint *)((char*)regs + 0x04);
  rhda = *(volatile uint *)((char*)regs + 0x48);

  sc->reg0 = r0;
  sc->reg1 = r1;
  sc->hciversion = (ushort)(r0 & 0xFF);
  sc->rh_present = 1;
  sc->rh_ports = (uchar)(rhda & 0xFF);
  sc->rh_change_bits = 0;
  sc->reg_probe_ok = 1;
  sc->phase = USB_HC_PHASE_READY;
  return 0;
}

int
usb_ohci_reset(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  (void)dev;
  if(!sc || !sc->reg_probe_ok)
    return -1;
  return 0;
}

int
usb_ohci_halt(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  (void)dev;
  if(!sc || !sc->reg_probe_ok)
    return -1;
  return 0;
}

int
usb_ohci_start(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  (void)dev;
  if(!sc || !sc->reg_probe_ok)
    return -1;
  return 0;
}
