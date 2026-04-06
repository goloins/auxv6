#ifndef _AUXV6_USB_HCD_H_
#define _AUXV6_USB_HCD_H_

#include "types.h"

struct pci_dev;

#define USB_HC_MAX 16

#define USB_HC_KIND_UNKNOWN 0
#define USB_HC_KIND_UHCI    1
#define USB_HC_KIND_OHCI    2
#define USB_HC_KIND_EHCI    3
#define USB_HC_KIND_XHCI    4

#define USB_HC_PHASE_INIT      0
#define USB_HC_PHASE_READY     1
#define USB_HC_PHASE_DEGRADED  2

#define USB_HC_ERR_NONE         0
#define USB_HC_ERR_NO_BACKEND   1
#define USB_HC_ERR_PROBE        2
#define USB_HC_ERR_RESET        3
#define USB_HC_ERR_HALT         4
#define USB_HC_ERR_START        5

struct usb_hc_probe {
  ushort vendor_id;
  ushort device_id;
  uchar bus;
  uchar slot;
  uchar func;
  uchar class_code;
  uchar subclass;
  uchar prog_if;
  uchar irq_line;
  uchar bar0_is_io;
  uchar kind;
  uchar phase;
  uchar reg_probe_ok;
  uchar cap_length;
  ushort hciversion;
  uint bar0;
  uint bar0_size;
  uint reg0;
  uint reg1;
  uchar rh_present;
  uchar rh_ports;
  uint rh_change_bits;
  uint rh_connect_bits;
  uint probe_attempts;
  uint probe_successes;
  uint probe_failures;
  uint reset_attempts;
  uint reset_successes;
  uint reset_failures;
  uint halt_attempts;
  uint halt_successes;
  uint halt_failures;
  uint start_attempts;
  uint start_successes;
  uint start_failures;
  uint scan_attempts;
  uint scan_successes;
  uint scan_failures;
  uint last_error;
  uint init_failures;
};

struct usb_hc_ops {
  const char *name;
  int (*probe_regs)(struct usb_hc_probe *sc, struct pci_dev *dev);
  int (*reset)(struct usb_hc_probe *sc, struct pci_dev *dev);
  int (*halt)(struct usb_hc_probe *sc, struct pci_dev *dev);
  int (*start)(struct usb_hc_probe *sc, struct pci_dev *dev);
  int (*scan_ports)(struct usb_hc_probe *sc, struct pci_dev *dev);
};

int usb_probe_uhci_regs(struct usb_hc_probe *sc, struct pci_dev *dev);
int usb_probe_ohci_regs(struct usb_hc_probe *sc, struct pci_dev *dev);
int usb_probe_ehci_regs(struct usb_hc_probe *sc, struct pci_dev *dev);
int usb_probe_xhci_regs(struct usb_hc_probe *sc, struct pci_dev *dev);
int usb_uhci_reset(struct usb_hc_probe *sc, struct pci_dev *dev);
int usb_uhci_halt(struct usb_hc_probe *sc, struct pci_dev *dev);
int usb_uhci_start(struct usb_hc_probe *sc, struct pci_dev *dev);
int usb_ohci_reset(struct usb_hc_probe *sc, struct pci_dev *dev);
int usb_ohci_halt(struct usb_hc_probe *sc, struct pci_dev *dev);
int usb_ohci_start(struct usb_hc_probe *sc, struct pci_dev *dev);
int usb_ehci_reset(struct usb_hc_probe *sc, struct pci_dev *dev);
int usb_ehci_halt(struct usb_hc_probe *sc, struct pci_dev *dev);
int usb_ehci_start(struct usb_hc_probe *sc, struct pci_dev *dev);
int usb_xhci_reset(struct usb_hc_probe *sc, struct pci_dev *dev);
int usb_xhci_halt(struct usb_hc_probe *sc, struct pci_dev *dev);
int usb_xhci_start(struct usb_hc_probe *sc, struct pci_dev *dev);
int usb_uhci_scan_ports(struct usb_hc_probe *sc, struct pci_dev *dev);
int usb_ohci_scan_ports(struct usb_hc_probe *sc, struct pci_dev *dev);
int usb_ehci_scan_ports(struct usb_hc_probe *sc, struct pci_dev *dev);
int usb_xhci_scan_ports(struct usb_hc_probe *sc, struct pci_dev *dev);

#endif
