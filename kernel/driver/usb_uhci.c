#include "types.h"
#include "defs.h"
#include "pci.h"
#include "x86.h"
#include "usb_hcd.h"

/* UHCI register offsets (16-bit I/O space, relative to I/O BAR base). */
#define UHCI_REG_USBCMD    0x00
#define UHCI_REG_USBSTS    0x02

/* USBCMD bits. */
#define UHCI_CMD_RS        (1U << 0)    /* Run/Stop */
#define UHCI_CMD_HCRESET   (1U << 1)    /* Host Controller Reset */

/* USBSTS bits. */
#define UHCI_STS_HCH       (1U << 5)    /* HCHalted */

#define UHCI_POLL_TRIES    2000
#define UHCI_POLL_DELAY_US 10

static ushort
uhci_iobase(struct usb_hc_probe *sc)
{
  return (ushort)(sc->bar0 & 0xFFFF);
}

static int
uhci_wait_reg(ushort iobase, ushort off, ushort mask, int expect_set)
{
  int i;

  for(i = 0; i < UHCI_POLL_TRIES; i++){
    ushort v = inw(iobase + off);
    if(expect_set){
      if((v & mask) == mask)
        return 0;
    } else {
      if((v & mask) == 0)
        return 0;
    }
    microdelay(UHCI_POLL_DELAY_US);
  }
  return -1;
}

int
usb_probe_uhci_regs(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  ushort iobase;

  (void)dev;

  if(!sc)
    return -1;
  if(sc->bar0 == 0 || sc->bar0_size == 0)
    return -1;
  if(!sc->bar0_is_io)
    return -1;

  iobase = uhci_iobase(sc);
  sc->reg0 = inw(iobase + UHCI_REG_USBCMD);
  sc->reg1 = inw(iobase + UHCI_REG_USBSTS);
  sc->reg_probe_ok = 1;
  sc->rh_present = 1;
  sc->rh_ports = 2;   /* UHCI root hub has 2 ports per spec */
  sc->rh_change_bits = 0;
  sc->phase = USB_HC_PHASE_READY;
  return 0;
}

int
usb_uhci_reset(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  ushort iobase;
  ushort cmd;

  (void)dev;

  if(!sc || !sc->reg_probe_ok)
    return -1;

  iobase = uhci_iobase(sc);

  /* Stop the controller, wait for HCHalted. */
  cmd = inw(iobase + UHCI_REG_USBCMD);
  cmd = (ushort)(cmd & ~UHCI_CMD_RS);
  outw(iobase + UHCI_REG_USBCMD, cmd);
  if(uhci_wait_reg(iobase, UHCI_REG_USBSTS, UHCI_STS_HCH, 1) < 0)
    return -1;

  /* Soft host-controller reset; wait for HCRESET to clear. */
  cmd = inw(iobase + UHCI_REG_USBCMD);
  cmd = (ushort)(cmd | UHCI_CMD_HCRESET);
  outw(iobase + UHCI_REG_USBCMD, cmd);
  if(uhci_wait_reg(iobase, UHCI_REG_USBCMD, UHCI_CMD_HCRESET, 0) < 0)
    return -1;

  return 0;
}

int
usb_uhci_halt(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  ushort iobase;
  ushort cmd;

  (void)dev;

  if(!sc || !sc->reg_probe_ok)
    return -1;

  iobase = uhci_iobase(sc);
  cmd = inw(iobase + UHCI_REG_USBCMD);
  cmd = (ushort)(cmd & ~UHCI_CMD_RS);
  outw(iobase + UHCI_REG_USBCMD, cmd);
  if(uhci_wait_reg(iobase, UHCI_REG_USBSTS, UHCI_STS_HCH, 1) < 0)
    return -1;

  return 0;
}

int
usb_uhci_start(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  ushort iobase;
  ushort cmd;

  (void)dev;

  if(!sc || !sc->reg_probe_ok)
    return -1;

  iobase = uhci_iobase(sc);
  cmd = inw(iobase + UHCI_REG_USBCMD);
  cmd = (ushort)(cmd | UHCI_CMD_RS);
  outw(iobase + UHCI_REG_USBCMD, cmd);
  if(uhci_wait_reg(iobase, UHCI_REG_USBSTS, UHCI_STS_HCH, 0) < 0)
    return -1;

  return 0;
}

/* Port Status/Control registers: iobase+0x10 (port 1), iobase+0x12 (port 2) (UHCI spec §2.1.3). */
#define UHCI_REG_PORTSC1     0x10
#define UHCI_PORTSC_CCS      (1U << 0)   /* Current Connect Status */
#define UHCI_PORTSC_CSC      (1U << 1)   /* Connect Status Change (RWC) */

int
usb_uhci_scan_ports(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  ushort iobase;
  uint n;

  (void)dev;

  if(!sc || !sc->reg_probe_ok || sc->rh_ports == 0)
    return 0;

  iobase = uhci_iobase(sc);
  sc->rh_connect_bits = 0;
  sc->rh_change_bits = 0;

  for(n = 0; n < sc->rh_ports && n < 2; n++){
    ushort ps = inw(iobase + UHCI_REG_PORTSC1 + (ushort)(n * 2));
    if(ps & UHCI_PORTSC_CCS)
      sc->rh_connect_bits |= (1U << n);
    if(ps & UHCI_PORTSC_CSC)
      sc->rh_change_bits |= (1U << n);
  }
  return 0;
}
