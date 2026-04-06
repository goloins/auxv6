#include "types.h"
#include "defs.h"
#include "pci.h"
#include "usb_hcd.h"

/* OHCI register offsets (relative to BAR0 MMIO base). */
#define OHCI_REG_CONTROL       0x04
#define OHCI_REG_CMDSTATUS     0x08

/* HcControl HCFS field (bits 7:6). */
#define OHCI_CTL_HCFS_MASK     (3U << 6)
#define OHCI_CTL_HCFS_RESET    (0U << 6)
#define OHCI_CTL_HCFS_RESUME   (1U << 6)
#define OHCI_CTL_HCFS_OPER     (2U << 6)
#define OHCI_CTL_HCFS_SUSPEND  (3U << 6)

/* HcCommandStatus bits. */
#define OHCI_CMD_HCR           (1U << 0)

#define OHCI_POLL_TRIES        1000
#define OHCI_POLL_DELAY_US     10

static volatile uint *
ohci_regs(struct pci_dev *dev)
{
  return (volatile uint *)pci_map_bar(dev, 0);
}

static uint
ohci_read(volatile uint *base, uint off)
{
  return *(volatile uint *)((char *)base + off);
}

static void
ohci_write(volatile uint *base, uint off, uint val)
{
  *(volatile uint *)((char *)base + off) = val;
}

static int
ohci_wait_bits(volatile uint *base, uint off, uint mask, uint expect_set)
{
  int i;

  for(i = 0; i < OHCI_POLL_TRIES; i++){
    uint v = ohci_read(base, off);
    if(expect_set){
      if((v & mask) == mask)
        return 0;
    } else {
      if((v & mask) == 0)
        return 0;
    }
    microdelay(OHCI_POLL_DELAY_US);
  }
  return -1;
}

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

  regs = ohci_regs(dev);
  if(!regs)
    return -1;

  r0 = ohci_read(regs, 0x00);  /* HcRevision */
  r1 = ohci_read(regs, OHCI_REG_CONTROL);
  rhda = ohci_read(regs, 0x48);  /* HcRhDescriptorA */

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
  volatile uint *regs;
  uint ctrl;

  if(!sc || !sc->reg_probe_ok)
    return -1;

  regs = ohci_regs(dev);
  if(!regs)
    return -1;

  /* Software host-controller reset: set HCR and wait for it to clear. */
  ohci_write(regs, OHCI_REG_CMDSTATUS, OHCI_CMD_HCR);
  if(ohci_wait_bits(regs, OHCI_REG_CMDSTATUS, OHCI_CMD_HCR, 0) < 0)
    return -1;

  /* After reset, HCFS is USB_SUSPEND; transition to USB_OPERATIONAL. */
  ctrl = ohci_read(regs, OHCI_REG_CONTROL);
  ctrl = (ctrl & ~OHCI_CTL_HCFS_MASK) | OHCI_CTL_HCFS_OPER;
  ohci_write(regs, OHCI_REG_CONTROL, ctrl);
  return 0;
}

int
usb_ohci_halt(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  volatile uint *regs;
  uint ctrl;

  if(!sc || !sc->reg_probe_ok)
    return -1;

  regs = ohci_regs(dev);
  if(!regs)
    return -1;

  ctrl = ohci_read(regs, OHCI_REG_CONTROL);
  ctrl = (ctrl & ~OHCI_CTL_HCFS_MASK) | OHCI_CTL_HCFS_SUSPEND;
  ohci_write(regs, OHCI_REG_CONTROL, ctrl);
  return 0;
}

int
usb_ohci_start(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  volatile uint *regs;
  uint ctrl;

  if(!sc || !sc->reg_probe_ok)
    return -1;

  regs = ohci_regs(dev);
  if(!regs)
    return -1;

  ctrl = ohci_read(regs, OHCI_REG_CONTROL);
  ctrl = (ctrl & ~OHCI_CTL_HCFS_MASK) | OHCI_CTL_HCFS_OPER;
  ohci_write(regs, OHCI_REG_CONTROL, ctrl);
  return 0;
}

/* HcRhPortStatus[n] at 0x54 + n*4 (OHCI spec 7.4.2). */
#define OHCI_RH_PORT_STATUS  0x54
#define OHCI_RH_CCS          (1U << 0)    /* CurrentConnectStatus */
#define OHCI_RH_CSC          (1U << 16)   /* ConnectStatusChange (RWC) */

int
usb_ohci_scan_ports(struct usb_hc_probe *sc, struct pci_dev *dev)
{
  volatile uint *regs;
  uint n;

  if(!sc || !sc->reg_probe_ok || sc->rh_ports == 0)
    return 0;

  regs = ohci_regs(dev);
  if(!regs)
    return -1;

  sc->rh_connect_bits = 0;
  sc->rh_change_bits = 0;

  for(n = 0; n < sc->rh_ports && n < 32; n++){
    uint ps = ohci_read(regs, OHCI_RH_PORT_STATUS + n * 4);
    if(ps & OHCI_RH_CCS)
      sc->rh_connect_bits |= (1U << n);
    if(ps & OHCI_RH_CSC)
      sc->rh_change_bits |= (1U << n);
  }
  return 0;
}
