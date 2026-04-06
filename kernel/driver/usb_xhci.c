#include "types.h"
#include "defs.h"
#include "pci.h"
#include "usb_hcd.h"

#define XHCI_REG_CAPLENGTH   0x00
#define XHCI_REG_HCSPARAMS1  0x04

#define XHCI_OP_USBCMD       0x00
#define XHCI_OP_USBSTS       0x04

#define XHCI_USBCMD_RUNSTOP  (1U << 0)
#define XHCI_USBCMD_HCRST    (1U << 1)

#define XHCI_USBSTS_HCH      (1U << 0)
#define XHCI_USBSTS_CNR      (1U << 11)

#define XHCI_POLL_TRIES      4000
#define XHCI_POLL_DELAY_US   10

static volatile uint*
xhci_regs(struct pci_dev *dev)
{
  return (volatile uint*)pci_map_bar(dev, 0);
}

static uint
xhci_read(volatile uint *base, uint off)
{
  return *(volatile uint *)((char*)base + off);
}

static void
xhci_write(volatile uint *base, uint off, uint val)
{
  *(volatile uint *)((char*)base + off) = val;
}

static int
xhci_wait_bits(volatile uint *base, uint off, uint mask, uint expect_set)
{
  int i;

  for(i = 0; i < XHCI_POLL_TRIES; i++){
    uint v = xhci_read(base, off);
    if(expect_set){
      if((v & mask) == mask)
        return 0;
    } else {
      if((v & mask) == 0)
        return 0;
    }
    microdelay(XHCI_POLL_DELAY_US);
  }

  return -1;
}

int
usb_probe_xhci_regs(struct usb_hc_probe *sc, struct pci_dev *dev)
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

  regs = xhci_regs(dev);
  if(!regs)
    return -1;

  r0 = xhci_read(regs, XHCI_REG_CAPLENGTH);
  r1 = xhci_read(regs, XHCI_REG_HCSPARAMS1);

  sc->reg0 = r0;
  sc->reg1 = r1;
  sc->cap_length = (uchar)(r0 & 0xFF);
  sc->hciversion = (ushort)((r0 >> 16) & 0xFFFF);
  sc->rh_present = 1;
  sc->rh_ports = (uchar)((r1 >> 24) & 0xFF);
  sc->rh_change_bits = 0;
  sc->reg_probe_ok = 1;
  sc->phase = USB_HC_PHASE_READY;
  return 0;
}

int
usb_xhci_reset(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  volatile uint *regs;
  uint opbase;
  uint cmd;

  if(!sc || !sc->reg_probe_ok)
    return -1;

  regs = xhci_regs(dev);
  if(!regs)
    return -1;

  opbase = (uint)sc->cap_length;
  if(opbase < 0x10 || opbase > 0x80)
    return -1;

  cmd = xhci_read(regs, opbase + XHCI_OP_USBCMD);
  cmd &= ~XHCI_USBCMD_RUNSTOP;
  xhci_write(regs, opbase + XHCI_OP_USBCMD, cmd);
  if(xhci_wait_bits(regs, opbase + XHCI_OP_USBSTS, XHCI_USBSTS_HCH, 1) < 0)
    return -1;

  cmd = xhci_read(regs, opbase + XHCI_OP_USBCMD);
  cmd |= XHCI_USBCMD_HCRST;
  xhci_write(regs, opbase + XHCI_OP_USBCMD, cmd);
  if(xhci_wait_bits(regs, opbase + XHCI_OP_USBCMD, XHCI_USBCMD_HCRST, 0) < 0)
    return -1;

  if(xhci_wait_bits(regs, opbase + XHCI_OP_USBSTS, XHCI_USBSTS_CNR, 0) < 0)
    return -1;

  return 0;
}

int
usb_xhci_halt(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  volatile uint *regs;
  uint opbase;
  uint cmd;

  if(!sc || !sc->reg_probe_ok)
    return -1;

  regs = xhci_regs(dev);
  if(!regs)
    return -1;

  opbase = (uint)sc->cap_length;
  if(opbase < 0x10 || opbase > 0x80)
    return -1;

  cmd = xhci_read(regs, opbase + XHCI_OP_USBCMD);
  cmd &= ~XHCI_USBCMD_RUNSTOP;
  xhci_write(regs, opbase + XHCI_OP_USBCMD, cmd);

  if(xhci_wait_bits(regs, opbase + XHCI_OP_USBSTS, XHCI_USBSTS_HCH, 1) < 0)
    return -1;

  return 0;
}

int
usb_xhci_start(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  volatile uint *regs;
  uint opbase;
  uint cmd;

  if(!sc || !sc->reg_probe_ok)
    return -1;

  regs = xhci_regs(dev);
  if(!regs)
    return -1;

  opbase = (uint)sc->cap_length;
  if(opbase < 0x10 || opbase > 0x80)
    return -1;

  cmd = xhci_read(regs, opbase + XHCI_OP_USBCMD);
  cmd |= XHCI_USBCMD_RUNSTOP;
  xhci_write(regs, opbase + XHCI_OP_USBCMD, cmd);

  if(xhci_wait_bits(regs, opbase + XHCI_OP_USBSTS, XHCI_USBSTS_HCH, 0) < 0)
    return -1;

  return 0;
}

/* Port register sets: opbase + 0x400 + n*0x10, PORTSC at port_base+0x0 (xHCI spec 5.4.8). */
#define XHCI_OP_PORTSC_BASE  0x400
#define XHCI_PORTSC_CCS      (1U << 0)    /* Current Connect Status */
#define XHCI_PORTSC_CSC      (1U << 17)   /* Connect Status Change (RW1C) */

int
usb_xhci_scan_ports(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  volatile uint *regs;
  uint opbase;
  uint n;

  if(!sc || !sc->reg_probe_ok || sc->rh_ports == 0)
    return 0;

  regs = xhci_regs(dev);
  if(!regs)
    return -1;

  opbase = (uint)sc->cap_length;
  if(opbase < 0x10 || opbase > 0x80)
    return -1;

  sc->rh_connect_bits = 0;
  sc->rh_change_bits = 0;

  for(n = 0; n < sc->rh_ports && n < 32; n++){
    uint portsc = xhci_read(regs, opbase + XHCI_OP_PORTSC_BASE + n * 0x10);
    if(portsc & XHCI_PORTSC_CCS)
      sc->rh_connect_bits |= (1U << n);
    if(portsc & XHCI_PORTSC_CSC)
      sc->rh_change_bits |= (1U << n);
  }
  return 0;
}
