#include "types.h"
#include "defs.h"
#include "pci.h"
#include "usb_hcd.h"

#define EHCI_REG_CAPLENGTH   0x00
#define EHCI_REG_HCSPARAMS   0x04

#define EHCI_OP_USBCMD       0x00
#define EHCI_OP_USBSTS       0x04

#define EHCI_USBCMD_RUNSTOP  (1U << 0)
#define EHCI_USBCMD_HCRESET  (1U << 1)

#define EHCI_USBSTS_HALTED   (1U << 12)

#define EHCI_POLL_TRIES      2000
#define EHCI_POLL_DELAY_US   10

static volatile uint*
ehci_regs(struct pci_dev *dev)
{
  return (volatile uint*)pci_map_bar(dev, 0);
}

static uint
ehci_read(volatile uint *base, uint off)
{
  return *(volatile uint *)((char*)base + off);
}

static void
ehci_write(volatile uint *base, uint off, uint val)
{
  *(volatile uint *)((char*)base + off) = val;
}

static int
ehci_wait_bits(volatile uint *base, uint off, uint mask, uint expect_set)
{
  int i;

  for(i = 0; i < EHCI_POLL_TRIES; i++){
    uint v = ehci_read(base, off);
    if(expect_set){
      if((v & mask) == mask)
        return 0;
    } else {
      if((v & mask) == 0)
        return 0;
    }
    microdelay(EHCI_POLL_DELAY_US);
  }

  return -1;
}

int
usb_probe_ehci_regs(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  volatile uint *regs;
  uint r0;
  uint r1;

  if(!sc || !dev)
    return -1;
  if(sc->bar0 == 0 || sc->bar0_size == 0)
    return -1;
  if(sc->bar0_is_io)
    return -1;

  regs = ehci_regs(dev);
  if(!regs)
    return -1;

  r0 = ehci_read(regs, EHCI_REG_CAPLENGTH);
  r1 = ehci_read(regs, EHCI_REG_HCSPARAMS);

  sc->reg0 = r0;
  sc->reg1 = r1;
  sc->cap_length = (uchar)(r0 & 0xFF);
  sc->hciversion = (ushort)((r0 >> 16) & 0xFFFF);
  sc->rh_present = 1;
  sc->rh_ports = (uchar)(r1 & 0x0F);
  sc->rh_change_bits = 0;
  sc->reg_probe_ok = 1;
  sc->phase = USB_HC_PHASE_READY;
  return 0;
}

int
usb_ehci_reset(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  volatile uint *regs;
  uint opbase;
  uint cmd;

  if(!sc || !sc->reg_probe_ok)
    return -1;

  regs = ehci_regs(dev);
  if(!regs)
    return -1;

  opbase = (uint)sc->cap_length;
  if(opbase < 0x10 || opbase > 0x40)
    return -1;

  /* Force controller to halt before issuing host-controller reset. */
  cmd = ehci_read(regs, opbase + EHCI_OP_USBCMD);
  cmd &= ~EHCI_USBCMD_RUNSTOP;
  ehci_write(regs, opbase + EHCI_OP_USBCMD, cmd);
  if(ehci_wait_bits(regs, opbase + EHCI_OP_USBSTS, EHCI_USBSTS_HALTED, 1) < 0)
    return -1;

  cmd = ehci_read(regs, opbase + EHCI_OP_USBCMD);
  cmd |= EHCI_USBCMD_HCRESET;
  ehci_write(regs, opbase + EHCI_OP_USBCMD, cmd);
  if(ehci_wait_bits(regs, opbase + EHCI_OP_USBCMD, EHCI_USBCMD_HCRESET, 0) < 0)
    return -1;

  return 0;
}

int
usb_ehci_halt(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  volatile uint *regs;
  uint opbase;
  uint cmd;

  if(!sc || !sc->reg_probe_ok)
    return -1;

  regs = ehci_regs(dev);
  if(!regs)
    return -1;

  opbase = (uint)sc->cap_length;
  if(opbase < 0x10 || opbase > 0x40)
    return -1;

  cmd = ehci_read(regs, opbase + EHCI_OP_USBCMD);
  cmd &= ~EHCI_USBCMD_RUNSTOP;
  ehci_write(regs, opbase + EHCI_OP_USBCMD, cmd);

  if(ehci_wait_bits(regs, opbase + EHCI_OP_USBSTS, EHCI_USBSTS_HALTED, 1) < 0)
    return -1;

  return 0;
}

int
usb_ehci_start(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  volatile uint *regs;
  uint opbase;
  uint cmd;

  if(!sc || !sc->reg_probe_ok)
    return -1;

  regs = ehci_regs(dev);
  if(!regs)
    return -1;

  opbase = (uint)sc->cap_length;
  if(opbase < 0x10 || opbase > 0x40)
    return -1;

  cmd = ehci_read(regs, opbase + EHCI_OP_USBCMD);
  cmd |= EHCI_USBCMD_RUNSTOP;
  ehci_write(regs, opbase + EHCI_OP_USBCMD, cmd);

  if(ehci_wait_bits(regs, opbase + EHCI_OP_USBSTS, EHCI_USBSTS_HALTED, 0) < 0)
    return -1;

  return 0;
}
