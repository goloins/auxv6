/*
 * PCI USB host-controller discovery scaffold for auxv6.
 *
 * Scope of this tranche:
 * - Detect PCI USB controllers (UHCI/OHCI/EHCI/xHCI + unknown prog-if).
 * - Capture stable controller metadata for follow-on driver work.
 * - Expose the discovery snapshot through /proc/usb.
 *
 * This intentionally does not program host-controller operational registers yet.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "usb_hcd.h"

static struct spinlock usb_lock;
static struct usb_hc_probe usb_hc[USB_HC_MAX];
static uint usb_hc_count;
static const struct usb_hc_ops* usb_get_ops(uchar kind);

#define USB_DEV_CAND_MAX      64
#define USB_DEV_SPEED_UNKNOWN 0
#define USB_DEV_SPEED_LOW     1
#define USB_DEV_SPEED_FULL    2
#define USB_DEV_SPEED_HIGH    3
#define USB_DEV_SPEED_SUPER   4
#define USB_DEV_ADDR_NONE     0
#define USB_DEV_ADDR_MIN      1
#define USB_DEV_ADDR_MAX      127
#define USB_ROOT_PORT_MAX     32
#define USB_DEV_MAX           64
#define USB_DEV_EP_MAX        8
#define USB_DEV_IF_MAX        8
#define USB_CFG_BLOB_MAX      4096

#define USB_ATTACH_NONE       0
#define USB_ATTACH_R815X      1
#define USB_ATTACH_WPAN       2

#define USB_ENUM_STATE_NEW            0
#define USB_ENUM_STATE_CONTROL_PENDING 1
#define USB_ENUM_STATE_DESC8_OK       2
#define USB_ENUM_STATE_DESC18_OK      3
#define USB_ENUM_STATE_CFG_OK         4
#define USB_ENUM_STATE_CONFIGURED     5
#define USB_ENUM_STATE_FAILED         6

#define USB_ENUM_ERR_NONE             0
#define USB_ENUM_ERR_BAD_CANDIDATE    1
#define USB_ENUM_ERR_NO_CONTROLLER    2
#define USB_ENUM_ERR_NO_PCI_DEV       3
#define USB_ENUM_ERR_NO_EP0_BACKEND   4
#define USB_ENUM_ERR_EP0              5
#define USB_ENUM_ERR_DESC8_FORMAT     6
#define USB_ENUM_ERR_NO_DESC18_BACKEND 7
#define USB_ENUM_ERR_DESC18_FORMAT    8
#define USB_ENUM_ERR_NO_CFG_BACKEND   9
#define USB_ENUM_ERR_CFG_FORMAT       10
#define USB_ENUM_ERR_NO_SETCFG_BACKEND 11
#define USB_ENUM_ERR_SETCFG           12

struct usb_dev_candidate {
  uchar hc_index;
  uchar kind;
  uchar port;
  uchar present;
  uchar enabled;
  uchar speed;
  uchar address;
  uchar addr_ready;
  uint generation;
};

static struct usb_dev_candidate usb_dev[USB_DEV_CAND_MAX];
static uint usb_dev_count;

struct usb_ep_cap {
  uchar iface_index;
  uchar addr;
  uchar attr;
  ushort max_packet;
  uchar interval;
};

struct usb_if_cap {
  uchar ifnum;
  uchar alt;
  uchar ifclass;
  uchar subclass;
  uchar proto;
  uchar ep_count;
};

struct usb_device {
  uchar active;
  uchar hc_index;
  uchar kind;
  uchar port;
  uchar address;
  uchar speed;
  uchar enum_state;
  uchar address_set;
  uchar max_packet0;
  ushort bcd_usb;
  ushort vendor_id;
  ushort product_id;
  uchar dev_class;
  uchar dev_subclass;
  uchar dev_proto;
  uchar desc8_ok;
  uchar desc18_ok;
  uchar cfg_desc_ok;
  uchar cfg_value;
  uchar cfg_num_ifaces;
  uchar cfg_parsed_ifaces;
  uchar cfg_num_eps;
  uchar cfg_set_ok;
  uchar match_if_class;
  uchar match_if_subclass;
  uchar match_if_proto;
  uchar attach_driver;
  uchar attach_ok;
  uchar iface_count;
  uchar ep_count;
  ushort attach_vid;
  ushort attach_pid;
  uchar attach_cfg;
  uchar attach_if_number;
  uchar attach_if_alt;
  uchar attach_if_class;
  uchar attach_if_subclass;
  uchar attach_if_proto;
  uint attach_handle;
  uint attach_ep_sig;
  ushort cfg_total_len;
  struct usb_if_cap ifaces[USB_DEV_IF_MAX];
  struct usb_ep_cap eps[USB_DEV_EP_MAX];
  uint generation;
  uint attach_generation;
  uint enum_attempts;
  uint enum_successes;
  uint enum_failures;
  uint attach_attempts;
  uint attach_successes;
  uint attach_skips;
  uint attach_rebinds;
  uint last_error;
};

static struct usb_device usb_devices[USB_DEV_MAX];
static uint usb_device_count;
static uint usb_attach_dedup_count;
static uint usb_attach_retired_count;
static uint usb_attach_rebind_count;

struct usb_bus_state {
  uint addr_bits[4];
  uchar port_address[USB_ROOT_PORT_MAX];
  uint port_generation[USB_ROOT_PORT_MAX];
  uint addr_assigned;
  uint addr_conflicts;
  uint addr_exhausted;
};

static struct usb_bus_state usb_bus[USB_HC_MAX];

static const struct usb_hc_ops usb_hc_ops_unknown = {
  .name = "unknown",
  .probe_regs = 0,
  .reset = 0,
  .halt = 0,
  .start = 0,
  .scan_ports = 0,
  .service_ports = 0,
  .get_device_desc8 = 0,
  .get_device_desc18 = 0,
  .get_config_desc = 0,
  .set_configuration = 0,
};

static const struct usb_hc_ops usb_hc_ops_uhci = {
  .name = "uhci",
  .probe_regs = usb_probe_uhci_regs,
  .reset = usb_uhci_reset,
  .halt = usb_uhci_halt,
  .start = usb_uhci_start,
  .scan_ports = usb_uhci_scan_ports,
  .service_ports = usb_uhci_service_ports,
  .get_device_desc8 = 0,
  .get_device_desc18 = 0,
  .get_config_desc = 0,
  .set_configuration = 0,
};

static const struct usb_hc_ops usb_hc_ops_ohci = {
  .name = "ohci",
  .probe_regs = usb_probe_ohci_regs,
  .reset = usb_ohci_reset,
  .halt = usb_ohci_halt,
  .start = usb_ohci_start,
  .scan_ports = usb_ohci_scan_ports,
  .service_ports = usb_ohci_service_ports,
  .get_device_desc8 = 0,
  .get_device_desc18 = 0,
  .get_config_desc = 0,
  .set_configuration = 0,
};

static const struct usb_hc_ops usb_hc_ops_ehci = {
  .name = "ehci",
  .probe_regs = usb_probe_ehci_regs,
  .reset = usb_ehci_reset,
  .halt = usb_ehci_halt,
  .start = usb_ehci_start,
  .scan_ports = usb_ehci_scan_ports,
  .service_ports = usb_ehci_service_ports,
  .get_device_desc8 = 0,
  .get_device_desc18 = 0,
  .get_config_desc = 0,
  .set_configuration = 0,
};

static const struct usb_hc_ops usb_hc_ops_xhci = {
  .name = "xhci",
  .probe_regs = usb_probe_xhci_regs,
  .reset = usb_xhci_reset,
  .halt = usb_xhci_halt,
  .start = usb_xhci_start,
  .scan_ports = usb_xhci_scan_ports,
  .service_ports = usb_xhci_service_ports,
  .get_device_desc8 = usb_xhci_get_device_desc8,
  .get_device_desc18 = usb_xhci_get_device_desc18,
  .get_config_desc = usb_xhci_get_config_desc,
  .set_configuration = usb_xhci_set_configuration,
};

static int
usb_buf_putc(char *buf, uint max, uint *len, char c)
{
  if(*len >= max)
    return -1;
  buf[*len] = c;
  (*len)++;
  return 0;
}

static int
usb_buf_puts(char *buf, uint max, uint *len, const char *s)
{
  uint i;

  for(i = 0; s[i]; i++){
    if(usb_buf_putc(buf, max, len, s[i]) < 0)
      return -1;
  }
  return 0;
}

static uint
usb_write_uint(char *tmp, uint value)
{
  char rev[16];
  uint i;
  uint n;

  n = 0;
  do {
    rev[n++] = '0' + (value % 10);
    value /= 10;
  } while(value > 0);

  for(i = 0; i < n; i++)
    tmp[i] = rev[n - i - 1];
  return n;
}

static int
usb_buf_putu(char *buf, uint max, uint *len, uint v)
{
  char tmp[16];
  uint n;
  uint i;

  n = usb_write_uint(tmp, v);
  for(i = 0; i < n; i++){
    if(usb_buf_putc(buf, max, len, tmp[i]) < 0)
      return -1;
  }
  return 0;
}

static int
usb_buf_puthex8(char *buf, uint max, uint *len, uchar v)
{
  static const char hex[] = "0123456789abcdef";

  if(usb_buf_putc(buf, max, len, hex[(v >> 4) & 0xF]) < 0) return -1;
  if(usb_buf_putc(buf, max, len, hex[v & 0xF]) < 0) return -1;
  return 0;
}

static int
usb_buf_puthex16(char *buf, uint max, uint *len, ushort v)
{
  static const char hex[] = "0123456789abcdef";

  if(usb_buf_putc(buf, max, len, hex[(v >> 12) & 0xF]) < 0) return -1;
  if(usb_buf_putc(buf, max, len, hex[(v >> 8) & 0xF]) < 0) return -1;
  if(usb_buf_putc(buf, max, len, hex[(v >> 4) & 0xF]) < 0) return -1;
  if(usb_buf_putc(buf, max, len, hex[v & 0xF]) < 0) return -1;
  return 0;
}

static int
usb_buf_puthex32(char *buf, uint max, uint *len, uint v)
{
  static const char hex[] = "0123456789abcdef";
  int shift;

  for(shift = 28; shift >= 0; shift -= 4){
    if(usb_buf_putc(buf, max, len, hex[(v >> shift) & 0xF]) < 0)
      return -1;
  }
  return 0;
}

static uchar
usb_hc_kind_from_prog_if(uchar prog_if)
{
  switch(prog_if){
  case PCI_PROGIF_USB_UHCI:
    return USB_HC_KIND_UHCI;
  case PCI_PROGIF_USB_OHCI:
    return USB_HC_KIND_OHCI;
  case PCI_PROGIF_USB_EHCI:
    return USB_HC_KIND_EHCI;
  case PCI_PROGIF_USB_XHCI:
    return USB_HC_KIND_XHCI;
  default:
    return USB_HC_KIND_UNKNOWN;
  }
}

static const char*
usb_hc_kind_name(uchar kind)
{
  const struct usb_hc_ops *ops;

  switch(kind){
  case USB_HC_KIND_UHCI:
    ops = &usb_hc_ops_uhci;
    break;
  case USB_HC_KIND_OHCI:
    ops = &usb_hc_ops_ohci;
    break;
  case USB_HC_KIND_EHCI:
    ops = &usb_hc_ops_ehci;
    break;
  case USB_HC_KIND_XHCI:
    ops = &usb_hc_ops_xhci;
    break;
  default:
    ops = &usb_hc_ops_unknown;
    break;
  }

  return ops->name;
}

static const char*
usb_hc_phase_name(uchar phase)
{
  switch(phase){
  case USB_HC_PHASE_INIT:
    return "init";
  case USB_HC_PHASE_READY:
    return "ready";
  case USB_HC_PHASE_DEGRADED:
    return "degraded";
  default:
    return "unknown";
  }
}

static const char*
usb_dev_speed_name(uchar speed)
{
  switch(speed){
  case USB_DEV_SPEED_LOW:
    return "low";
  case USB_DEV_SPEED_FULL:
    return "full";
  case USB_DEV_SPEED_HIGH:
    return "high";
  case USB_DEV_SPEED_SUPER:
    return "super";
  case USB_DEV_SPEED_UNKNOWN:
  default:
    return "unknown";
  }
}

static const char*
usb_enum_state_name(uchar state)
{
  switch(state){
  case USB_ENUM_STATE_NEW:
    return "new";
  case USB_ENUM_STATE_CONTROL_PENDING:
    return "control_pending";
  case USB_ENUM_STATE_DESC8_OK:
    return "desc8_ok";
  case USB_ENUM_STATE_DESC18_OK:
    return "desc18_ok";
  case USB_ENUM_STATE_CFG_OK:
    return "cfg_ok";
  case USB_ENUM_STATE_CONFIGURED:
    return "configured";
  case USB_ENUM_STATE_FAILED:
    return "failed";
  default:
    return "unknown";
  }
}

static const char*
usb_enum_error_name(uint err)
{
  switch(err){
  case USB_ENUM_ERR_NONE:
    return "none";
  case USB_ENUM_ERR_BAD_CANDIDATE:
    return "bad_candidate";
  case USB_ENUM_ERR_NO_CONTROLLER:
    return "no_controller";
  case USB_ENUM_ERR_NO_PCI_DEV:
    return "no_pci_dev";
  case USB_ENUM_ERR_NO_EP0_BACKEND:
    return "no_ep0_backend";
  case USB_ENUM_ERR_EP0:
    return "ep0";
  case USB_ENUM_ERR_DESC8_FORMAT:
    return "desc8_format";
  case USB_ENUM_ERR_NO_DESC18_BACKEND:
    return "no_desc18_backend";
  case USB_ENUM_ERR_DESC18_FORMAT:
    return "desc18_format";
  case USB_ENUM_ERR_NO_CFG_BACKEND:
    return "no_cfg_backend";
  case USB_ENUM_ERR_CFG_FORMAT:
    return "cfg_format";
  case USB_ENUM_ERR_NO_SETCFG_BACKEND:
    return "no_setcfg_backend";
  case USB_ENUM_ERR_SETCFG:
    return "setcfg";
  default:
    return "unknown";
  }
}

static const char*
usb_attach_driver_name(uchar driver)
{
  switch(driver){
  case USB_ATTACH_R815X:
    return "r815x";
  case USB_ATTACH_WPAN:
    return "wpan";
  case USB_ATTACH_NONE:
  default:
    return "none";
  }
}

static void
usb_detach_class_driver(struct usb_device *ud)
{
  if(!ud || !ud->attach_ok)
    return;

  switch(ud->attach_driver){
  case USB_ATTACH_R815X:
    (void)rtl815x_usb_detach(ud->attach_handle);
    break;
  case USB_ATTACH_WPAN:
    (void)wpan_usb_detach(ud->attach_handle);
    break;
  default:
    break;
  }
}

static int
usb_iface_has_bulk_pair(struct usb_device *ud, int iface_index)
{
  uint i;
  int in;
  int out;

  if(!ud)
    return 0;

  in = 0;
  out = 0;
  for(i = 0; i < ud->ep_count; i++){
    uchar type;
    uchar addr;

    if(iface_index >= 0 && (int)ud->eps[i].iface_index != iface_index)
      continue;

    type = ud->eps[i].attr & 0x3;
    if(type != 2)
      continue;
    addr = ud->eps[i].addr;
    if(addr & 0x80)
      in = 1;
    else
      out = 1;
  }

  return (in && out) ? 1 : 0;
}

static int
usb_iface_has_any_in_out(struct usb_device *ud, int iface_index)
{
  uint i;
  int in;
  int out;

  if(!ud)
    return 0;

  in = 0;
  out = 0;
  for(i = 0; i < ud->ep_count; i++){
    if(iface_index >= 0 && (int)ud->eps[i].iface_index != iface_index)
      continue;
    if(ud->eps[i].addr & 0x80)
      in = 1;
    else
      out = 1;
  }

  return (in && out) ? 1 : 0;
}

static uint
usb_compute_ep_signature(struct usb_device *ud)
{
  uint i;
  uint sig;

  if(!ud)
    return 0;

  sig = 2166136261U;
  for(i = 0; i < ud->ep_count; i++){
    sig ^= (uint)ud->eps[i].iface_index;
    sig *= 16777619U;
    sig ^= (uint)ud->eps[i].addr;
    sig *= 16777619U;
    sig ^= (uint)ud->eps[i].attr;
    sig *= 16777619U;
    sig ^= (uint)ud->eps[i].max_packet;
    sig *= 16777619U;
    sig ^= (uint)ud->eps[i].interval;
    sig *= 16777619U;
  }
  return sig;
}

static int
usb_policy_select_r815x(struct usb_device *ud)
{
  uint i;

  if(!ud)
    return -1;
  if(!rtl815x_usb_match(ud->vendor_id, ud->product_id))
    return -1;

  for(i = 0; i < ud->iface_count; i++){
    struct usb_if_cap *ifc;

    ifc = &ud->ifaces[i];
    if(!(ifc->ifclass == 0xff || ifc->ifclass == 0x02 || ifc->ifclass == 0x0a))
      continue;
    if(!usb_iface_has_bulk_pair(ud, (int)i))
      continue;
    return (int)i;
  }

  return -1;
}

static int
usb_policy_select_wpan(struct usb_device *ud)
{
  uint i;

  if(!ud)
    return -1;
  if(!wpan_usb_match(ud->vendor_id, ud->product_id))
    return -1;

  for(i = 0; i < ud->iface_count; i++){
    struct usb_if_cap *ifc;

    ifc = &ud->ifaces[i];
    if(!(ifc->ifclass == 0xff || ifc->ifclass == 0x02 || ifc->ifclass == 0x0a))
      continue;
    if(!usb_iface_has_any_in_out(ud, (int)i))
      continue;
    return (int)i;
  }

  return -1;
}

static void
usb_retire_device_slot(struct usb_device *ud)
{
  if(!ud || !ud->active)
    return;

  usb_detach_class_driver(ud);

  if(ud->attach_ok)
    usb_attach_retired_count++;

  ud->active = 0;
  ud->attach_ok = 0;
  ud->attach_driver = USB_ATTACH_NONE;
  ud->attach_generation = 0;
  ud->attach_handle = 0;
}

static int
usb_attach_signature_matches(struct usb_device *ud, uchar driver,
                             struct usb_if_cap *ifc, uint ep_sig)
{
  if(!ud)
    return 0;
  if(!ifc)
    return 0;
  if(driver == USB_ATTACH_NONE)
    return 0;
  if(!ud->attach_ok)
    return 0;
  if(ud->attach_driver != driver)
    return 0;
  if(ud->attach_generation != ud->generation)
    return 0;
  if(ud->attach_vid != ud->vendor_id || ud->attach_pid != ud->product_id)
    return 0;
  if(ud->attach_cfg != ud->cfg_value)
    return 0;
  if(ud->attach_if_number != ifc->ifnum)
    return 0;
  if(ud->attach_if_alt != ifc->alt)
    return 0;
  if(ud->attach_if_class != ifc->ifclass)
    return 0;
  if(ud->attach_if_subclass != ifc->subclass)
    return 0;
  if(ud->attach_if_proto != ifc->proto)
    return 0;
  if(ud->attach_ep_sig != ep_sig)
    return 0;
  return 1;
}

static void
usb_try_class_attach(struct usb_device *ud)
{
  int sel_if;
  uchar driver;
  struct usb_if_cap *ifc;
  uint ep_sig;

  if(!ud)
    return;
  if(!ud->cfg_set_ok)
    return;

  driver = USB_ATTACH_NONE;
  sel_if = usb_policy_select_r815x(ud);
  if(sel_if >= 0)
    driver = USB_ATTACH_R815X;
  else {
    sel_if = usb_policy_select_wpan(ud);
    if(sel_if >= 0)
      driver = USB_ATTACH_WPAN;
  }

  if(driver == USB_ATTACH_NONE){
    if(ud->attach_ok){
      usb_detach_class_driver(ud);
      ud->attach_rebinds++;
      usb_attach_rebind_count++;
      ud->attach_ok = 0;
      ud->attach_driver = USB_ATTACH_NONE;
      ud->attach_generation = 0;
      ud->attach_handle = 0;
    }
    return;
  }

  ifc = &ud->ifaces[sel_if];
  ep_sig = usb_compute_ep_signature(ud);

  if(usb_attach_signature_matches(ud, driver, ifc, ep_sig)){
    ud->attach_skips++;
    usb_attach_dedup_count++;
    return;
  }

  if(ud->attach_ok){
    usb_detach_class_driver(ud);
    ud->attach_rebinds++;
    usb_attach_rebind_count++;
  }

  ud->attach_attempts++;
  ud->attach_driver = USB_ATTACH_NONE;
  ud->attach_ok = 0;
  ud->attach_generation = 0;
    ud->attach_handle = 0;

  if(driver == USB_ATTACH_R815X &&
      rtl815x_usb_attach(ud->vendor_id, ud->product_id,
                  &ud->attach_handle) == 0){
    ud->attach_driver = USB_ATTACH_R815X;
    ud->attach_ok = 1;
    ud->attach_vid = ud->vendor_id;
    ud->attach_pid = ud->product_id;
    ud->attach_cfg = ud->cfg_value;
    ud->attach_if_number = ifc->ifnum;
    ud->attach_if_alt = ifc->alt;
    ud->attach_if_class = ifc->ifclass;
    ud->attach_if_subclass = ifc->subclass;
    ud->attach_if_proto = ifc->proto;
    ud->attach_ep_sig = ep_sig;
    ud->attach_generation = ud->generation;
    ud->attach_successes++;
    return;
  }

  if(driver == USB_ATTACH_WPAN &&
      wpan_usb_attach(ud->vendor_id, ud->product_id,
                &ud->attach_handle) == 0){
    ud->attach_driver = USB_ATTACH_WPAN;
    ud->attach_ok = 1;
    ud->attach_vid = ud->vendor_id;
    ud->attach_pid = ud->product_id;
    ud->attach_cfg = ud->cfg_value;
    ud->attach_if_number = ifc->ifnum;
    ud->attach_if_alt = ifc->alt;
    ud->attach_if_class = ifc->ifclass;
    ud->attach_if_subclass = ifc->subclass;
    ud->attach_if_proto = ifc->proto;
    ud->attach_ep_sig = ep_sig;
    ud->attach_generation = ud->generation;
    ud->attach_successes++;
    return;
  }
}

static struct usb_device*
usb_find_device_slot(uint hc_index, uchar port, uint generation)
{
  uint i;
  int free_index;

  free_index = -1;

  for(i = 0; i < usb_device_count; i++){
    struct usb_device *ud;

    ud = &usb_devices[i];
    if(!ud->active){
      if(free_index < 0)
        free_index = (int)i;
      continue;
    }

    if(ud->hc_index != hc_index)
      continue;
    if(ud->port != port)
      continue;

    if(ud->generation == generation)
      return ud;

    usb_retire_device_slot(ud);
    if(free_index < 0)
      free_index = (int)i;
  }

  if(free_index >= 0){
    memset(&usb_devices[free_index], 0, sizeof(usb_devices[free_index]));
    usb_devices[free_index].active = 1;
    return &usb_devices[free_index];
  }

  if(usb_device_count >= USB_DEV_MAX)
    return 0;

  memset(&usb_devices[usb_device_count], 0, sizeof(usb_devices[usb_device_count]));
  usb_devices[usb_device_count].active = 1;
  return &usb_devices[usb_device_count++];
}

static void
usb_device_set_fail(struct usb_device *ud, uint err)
{
  if(!ud)
    return;
  ud->enum_failures++;
  ud->enum_state = USB_ENUM_STATE_FAILED;
  ud->last_error = err;
}

static void
usb_enumerate_desc8(uint hc_index, struct usb_dev_candidate *dc)
{
  struct usb_hc_probe *sc;
  const struct usb_hc_ops *ops;
  struct pci_dev *pdev;
  struct usb_device *ud;
  uchar desc8[8];
  uchar desc18[18];
  uchar cfg9[9];
  uchar *cfg_blob;
  ushort cfg_total;
  uint off;
  uchar cfg_ifaces;
  uchar parsed_ifaces;
  uchar parsed_eps;
  uchar first_if_seen;
  int current_if_slot;

  cfg_blob = 0;
  cfg_total = 0;
  cfg_ifaces = 0;
  parsed_ifaces = 0;
  parsed_eps = 0;
  first_if_seen = 0;
  current_if_slot = -1;

  if(!dc || !dc->present || !dc->enabled || !dc->addr_ready){
    return;
  }

  if(hc_index >= usb_hc_count)
    return;

  ud = usb_find_device_slot(hc_index, dc->port, dc->generation);
  if(!ud)
    return;

  ud->hc_index = (uchar)hc_index;
  ud->kind = dc->kind;
  ud->port = dc->port;
  ud->address = dc->address;
  ud->speed = dc->speed;
  ud->generation = dc->generation;
  ud->enum_attempts++;
  ud->desc8_ok = 0;
  ud->desc18_ok = 0;
  ud->cfg_desc_ok = 0;
  ud->cfg_total_len = 0;
  ud->cfg_value = 0;
  ud->cfg_num_ifaces = 0;
  ud->cfg_parsed_ifaces = 0;
  ud->cfg_num_eps = 0;
  ud->cfg_set_ok = 0;
  ud->iface_count = 0;
  ud->match_if_class = 0;
  ud->match_if_subclass = 0;
  ud->match_if_proto = 0;
  memset(ud->ifaces, 0, sizeof(ud->ifaces));
  ud->ep_count = 0;
  memset(ud->eps, 0, sizeof(ud->eps));
  ud->address_set = 0;

  sc = &usb_hc[hc_index];
  ops = usb_get_ops(sc->kind);
  if(!ops){
    usb_device_set_fail(ud, USB_ENUM_ERR_NO_CONTROLLER);
    return;
  }
  if(!ops->get_device_desc8){
    ud->enum_state = USB_ENUM_STATE_CONTROL_PENDING;
    ud->last_error = USB_ENUM_ERR_NO_EP0_BACKEND;
    return;
  }

  pdev = pci_get_device((int)sc->pci_index);
  if(!pdev){
    usb_device_set_fail(ud, USB_ENUM_ERR_NO_PCI_DEV);
    return;
  }

  memset(desc8, 0, sizeof(desc8));
  if(ops->get_device_desc8(sc, pdev, dc->port, dc->address, desc8) < 0){
    ud->enum_state = USB_ENUM_STATE_CONTROL_PENDING;
    ud->last_error = USB_ENUM_ERR_EP0;
    return;
  }

  if(desc8[0] < 8 || desc8[1] != 1 || desc8[7] == 0){
    usb_device_set_fail(ud, USB_ENUM_ERR_DESC8_FORMAT);
    return;
  }

  ud->max_packet0 = desc8[7];
  ud->desc8_ok = 1;
  if(ud->kind == USB_HC_KIND_XHCI)
    ud->address_set = 1;

  if(!ops->get_device_desc18){
    ud->enum_state = USB_ENUM_STATE_CONTROL_PENDING;
    ud->last_error = USB_ENUM_ERR_NO_DESC18_BACKEND;
    return;
  }

  memset(desc18, 0, sizeof(desc18));
  if(ops->get_device_desc18(sc, pdev, dc->port, dc->address, desc18) < 0){
    ud->enum_state = USB_ENUM_STATE_CONTROL_PENDING;
    ud->last_error = USB_ENUM_ERR_EP0;
    return;
  }

  if(desc18[0] < 18 || desc18[1] != 1){
    usb_device_set_fail(ud, USB_ENUM_ERR_DESC18_FORMAT);
    return;
  }

  ud->bcd_usb = (ushort)(desc18[2] | (desc18[3] << 8));
  ud->dev_class = desc18[4];
  ud->dev_subclass = desc18[5];
  ud->dev_proto = desc18[6];
  ud->vendor_id = (ushort)(desc18[8] | (desc18[9] << 8));
  ud->product_id = (ushort)(desc18[10] | (desc18[11] << 8));
  ud->desc18_ok = 1;

  if(!ops->get_config_desc){
    ud->enum_state = USB_ENUM_STATE_CONTROL_PENDING;
    ud->last_error = USB_ENUM_ERR_NO_CFG_BACKEND;
    return;
  }

  memset(cfg9, 0, sizeof(cfg9));
  if(ops->get_config_desc(sc, pdev, dc->port, dc->address, 9, cfg9) < 0){
    ud->enum_state = USB_ENUM_STATE_CONTROL_PENDING;
    ud->last_error = USB_ENUM_ERR_EP0;
    return;
  }

  if(cfg9[0] < 9 || cfg9[1] != 2){
    usb_device_set_fail(ud, USB_ENUM_ERR_CFG_FORMAT);
    return;
  }

  cfg_total = (ushort)(cfg9[2] | (cfg9[3] << 8));
  cfg_ifaces = cfg9[4];
  if(cfg_total < 9 || cfg_total > USB_CFG_BLOB_MAX){
    usb_device_set_fail(ud, USB_ENUM_ERR_CFG_FORMAT);
    return;
  }

  cfg_blob = (uchar*)kalloc();
  if(!cfg_blob){
    ud->enum_state = USB_ENUM_STATE_CONTROL_PENDING;
    ud->last_error = USB_ENUM_ERR_EP0;
    return;
  }
  memset(cfg_blob, 0, USB_CFG_BLOB_MAX);

  if(ops->get_config_desc(sc, pdev, dc->port, dc->address,
                          cfg_total, cfg_blob) < 0){
    kfree((char*)cfg_blob);
    ud->enum_state = USB_ENUM_STATE_CONTROL_PENDING;
    ud->last_error = USB_ENUM_ERR_EP0;
    return;
  }

  off = 0;
  while(off + 2 <= cfg_total){
    uchar blen;
    uchar btype;

    blen = cfg_blob[off];
    btype = cfg_blob[off + 1];
    if(blen < 2 || off + blen > cfg_total){
      kfree((char*)cfg_blob);
      usb_device_set_fail(ud, USB_ENUM_ERR_CFG_FORMAT);
      return;
    }

    if(btype == 4)
      parsed_ifaces++;
    else if(btype == 5)
      parsed_eps++;

    if(btype == 4 && blen >= 9){
      if(!first_if_seen){
        ud->match_if_class = cfg_blob[off + 5];
        ud->match_if_subclass = cfg_blob[off + 6];
        ud->match_if_proto = cfg_blob[off + 7];
        first_if_seen = 1;
      }

      current_if_slot = -1;
      if(ud->iface_count < USB_DEV_IF_MAX){
        struct usb_if_cap *ifc;

        current_if_slot = (int)ud->iface_count;
        ifc = &ud->ifaces[ud->iface_count++];
        ifc->ifnum = cfg_blob[off + 2];
        ifc->alt = cfg_blob[off + 3];
        ifc->ifclass = cfg_blob[off + 5];
        ifc->subclass = cfg_blob[off + 6];
        ifc->proto = cfg_blob[off + 7];
        ifc->ep_count = 0;
      }
    }

    if(btype == 5 && blen >= 7 && ud->ep_count < USB_DEV_EP_MAX){
      struct usb_ep_cap *ep;

      ep = &ud->eps[ud->ep_count++];
      ep->iface_index = (current_if_slot >= 0) ? (uchar)current_if_slot : 0xff;
      ep->addr = cfg_blob[off + 2];
      ep->attr = cfg_blob[off + 3];
      ep->max_packet = (ushort)(cfg_blob[off + 4] | (cfg_blob[off + 5] << 8));
      ep->interval = cfg_blob[off + 6];
      if(current_if_slot >= 0 && current_if_slot < (int)ud->iface_count)
        ud->ifaces[current_if_slot].ep_count++;
    }

    off += blen;
    if(off == cfg_total)
      break;
  }

  if(off != cfg_total){
    kfree((char*)cfg_blob);
    usb_device_set_fail(ud, USB_ENUM_ERR_CFG_FORMAT);
    return;
  }

  ud->cfg_desc_ok = 1;
  ud->cfg_total_len = cfg_total;
  ud->cfg_value = cfg9[5];
  ud->cfg_num_ifaces = cfg_ifaces;
  ud->cfg_parsed_ifaces = parsed_ifaces;
  ud->cfg_num_eps = parsed_eps;
  ud->enum_state = USB_ENUM_STATE_CFG_OK;
  ud->last_error = USB_ENUM_ERR_NONE;

  if(!ops->set_configuration){
    kfree((char*)cfg_blob);
    ud->enum_state = USB_ENUM_STATE_CONTROL_PENDING;
    ud->last_error = USB_ENUM_ERR_NO_SETCFG_BACKEND;
    return;
  }

  if(ops->set_configuration(sc, pdev, dc->port, dc->address,
                            ud->cfg_value) < 0){
    kfree((char*)cfg_blob);
    ud->enum_state = USB_ENUM_STATE_CONTROL_PENDING;
    ud->last_error = USB_ENUM_ERR_SETCFG;
    return;
  }

  ud->cfg_set_ok = 1;
  usb_try_class_attach(ud);
  kfree((char*)cfg_blob);

  ud->enum_successes++;
  ud->enum_state = USB_ENUM_STATE_CONFIGURED;
  ud->last_error = USB_ENUM_ERR_NONE;
}

static void
usb_materialize_devices(void)
{
  uint i;

  for(i = 0; i < usb_dev_count; i++)
    usb_enumerate_desc8(usb_dev[i].hc_index, &usb_dev[i]);
}

static struct usb_bus_state*
usb_get_bus_state(uint hc_index)
{
  if(hc_index >= USB_HC_MAX)
    return 0;
  return &usb_bus[hc_index];
}

static int
usb_addr_is_marked(struct usb_bus_state *bus, uchar address)
{
  uint word;
  uint bit;

  if(!bus || address < USB_DEV_ADDR_MIN || address > USB_DEV_ADDR_MAX)
    return 0;

  word = address >> 5;
  bit = 1U << (address & 31);
  return (bus->addr_bits[word] & bit) ? 1 : 0;
}

static void
usb_addr_mark(struct usb_bus_state *bus, uchar address)
{
  uint word;
  uint bit;

  if(!bus || address < USB_DEV_ADDR_MIN || address > USB_DEV_ADDR_MAX)
    return;

  word = address >> 5;
  bit = 1U << (address & 31);
  bus->addr_bits[word] |= bit;
}

static void
usb_addr_unmark(struct usb_bus_state *bus, uchar address)
{
  uint word;
  uint bit;

  if(!bus || address < USB_DEV_ADDR_MIN || address > USB_DEV_ADDR_MAX)
    return;

  word = address >> 5;
  bit = 1U << (address & 31);
  bus->addr_bits[word] &= ~bit;
}

static void
usb_release_port_address(struct usb_bus_state *bus, uint port_index)
{
  uchar address;

  if(!bus || port_index >= USB_ROOT_PORT_MAX)
    return;

  address = bus->port_address[port_index];
  if(address != USB_DEV_ADDR_NONE){
    if(!usb_addr_is_marked(bus, address))
      bus->addr_conflicts++;
    usb_addr_unmark(bus, address);
  }
  bus->port_address[port_index] = USB_DEV_ADDR_NONE;
}

static uchar
usb_alloc_address(struct usb_bus_state *bus)
{
  uchar address;

  if(!bus)
    return USB_DEV_ADDR_NONE;

  for(address = USB_DEV_ADDR_MIN; address <= USB_DEV_ADDR_MAX; address++){
    if(usb_addr_is_marked(bus, address))
      continue;
    usb_addr_mark(bus, address);
    bus->addr_assigned++;
    return address;
  }

  bus->addr_exhausted++;
  return USB_DEV_ADDR_NONE;
}

static uint
usb_port_generation(struct usb_bus_state *bus, uint port_index, int changed)
{
  uint generation;

  if(!bus || port_index >= USB_ROOT_PORT_MAX)
    return 0;

  generation = bus->port_generation[port_index];
  if(generation == 0 || changed){
    generation++;
    if(generation == 0)
      generation = 1;
    bus->port_generation[port_index] = generation;
  }

  return bus->port_generation[port_index];
}

static void
usb_assign_candidate_address(uint hc_index, struct usb_dev_candidate *dc)
{
  struct usb_bus_state *bus;
  uint port_index;
  uchar address;

  if(!dc || dc->port == 0)
    return;

  bus = usb_get_bus_state(hc_index);
  if(!bus)
    return;

  port_index = dc->port - 1;
  if(port_index >= USB_ROOT_PORT_MAX)
    return;

  dc->address = USB_DEV_ADDR_NONE;
  dc->addr_ready = 0;

  if(!dc->present || !dc->enabled){
    usb_release_port_address(bus, port_index);
    return;
  }

  address = bus->port_address[port_index];
  if(address != USB_DEV_ADDR_NONE &&
     bus->port_generation[port_index] == dc->generation){
    if(!usb_addr_is_marked(bus, address)){
      bus->addr_conflicts++;
      usb_addr_mark(bus, address);
    }
    dc->address = address;
    dc->addr_ready = 1;
    return;
  }

  if(address != USB_DEV_ADDR_NONE)
    usb_release_port_address(bus, port_index);

  address = usb_alloc_address(bus);
  if(address == USB_DEV_ADDR_NONE)
    return;

  bus->port_address[port_index] = address;
  bus->port_generation[port_index] = dc->generation;
  dc->address = address;
  dc->addr_ready = 1;
}

static void
usb_scrub_inactive_ports(uint hc_index, uint active_mask)
{
  struct usb_bus_state *bus;
  uint port_index;

  bus = usb_get_bus_state(hc_index);
  if(!bus)
    return;

  for(port_index = 0; port_index < USB_ROOT_PORT_MAX; port_index++){
    if(active_mask & (1U << port_index))
      continue;
    usb_release_port_address(bus, port_index);
  }
}

static void
usb_collect_candidates(uint hc_index, struct usb_hc_probe *sc)
{
  struct usb_bus_state *bus;
  uint active_mask;
  uint n;

  if(!sc || !sc->rh_present)
    return;

  bus = usb_get_bus_state(hc_index);
  if(!bus)
    return;

  active_mask = 0;

  for(n = 0; n < sc->rh_ports && n < 32; n++){
    uint mask;
    uint present;
    uint enabled;

    mask = (1U << n);
    present = (sc->rh_connect_bits & mask) ? 1 : 0;
    enabled = (sc->rh_enabled_bits & mask) ? 1 : 0;
    if(!present && !enabled)
      continue;

    active_mask |= mask;

    if(usb_dev_count >= USB_DEV_CAND_MAX){
      if(!present || !enabled)
        usb_release_port_address(bus, n);
      else
        usb_port_generation(bus, n, (sc->rh_change_bits & mask) ? 1 : 0);
      continue;
    }

    {
      struct usb_dev_candidate *dc;

      dc = &usb_dev[usb_dev_count++];
      memset(dc, 0, sizeof(*dc));
      dc->hc_index = (uchar)hc_index;
      dc->kind = sc->kind;
      dc->port = (uchar)(n + 1);
      dc->present = (uchar)present;
      dc->enabled = (uchar)enabled;
      dc->generation = usb_port_generation(bus, n,
                                           (sc->rh_change_bits & mask) ? 1 : 0);
      if(sc->rh_super_bits & mask)
        dc->speed = USB_DEV_SPEED_SUPER;
      else if(sc->rh_high_bits & mask)
        dc->speed = USB_DEV_SPEED_HIGH;
      else if(sc->rh_full_bits & mask)
        dc->speed = USB_DEV_SPEED_FULL;
      else if(sc->rh_low_bits & mask)
        dc->speed = USB_DEV_SPEED_LOW;
      else
        dc->speed = USB_DEV_SPEED_UNKNOWN;

      usb_assign_candidate_address(hc_index, dc);
    }
  }

  usb_scrub_inactive_ports(hc_index, active_mask);
}

static const char*
usb_hc_error_name(uint err)
{
  switch(err){
  case USB_HC_ERR_NONE:
    return "none";
  case USB_HC_ERR_NO_BACKEND:
    return "no_backend";
  case USB_HC_ERR_PROBE:
    return "probe";
  case USB_HC_ERR_RESET:
    return "reset";
  case USB_HC_ERR_HALT:
    return "halt";
  case USB_HC_ERR_START:
    return "start";
  default:
    return "unknown";
  }
}

static int
usb_is_match(struct pci_dev *dev)
{
  if(!dev)
    return 0;
  if(dev->class_code != PCI_CLASS_SERIAL)
    return 0;
  if(dev->subclass != PCI_SUBCLASS_SERIAL_USB)
    return 0;
  return 1;
}

static const struct usb_hc_ops*
usb_get_ops(uchar kind)
{
  switch(kind){
  case USB_HC_KIND_UHCI:
    return &usb_hc_ops_uhci;
  case USB_HC_KIND_OHCI:
    return &usb_hc_ops_ohci;
  case USB_HC_KIND_EHCI:
    return &usb_hc_ops_ehci;
  case USB_HC_KIND_XHCI:
    return &usb_hc_ops_xhci;
  default:
    return &usb_hc_ops_unknown;
  }
}

void
usb_init(void)
{
  int i;
  int ndev;

  initlock(&usb_lock, "usb");

  acquire(&usb_lock);
  memset(usb_hc, 0, sizeof(usb_hc));
  memset(usb_dev, 0, sizeof(usb_dev));
  memset(usb_devices, 0, sizeof(usb_devices));
  memset(usb_bus, 0, sizeof(usb_bus));
  usb_hc_count = 0;
  usb_dev_count = 0;
  usb_device_count = 0;
  usb_attach_dedup_count = 0;
  usb_attach_retired_count = 0;
  usb_attach_rebind_count = 0;

  ndev = pci_device_count();
  for(i = 0; i < ndev; i++){
    struct pci_dev *dev;
    struct usb_hc_probe *sc;
    const struct usb_hc_ops *ops;

    dev = pci_get_device(i);
    if(!usb_is_match(dev))
      continue;
    if(usb_hc_count >= USB_HC_MAX)
      break;

    sc = &usb_hc[usb_hc_count++];
    memset(sc, 0, sizeof(*sc));

    sc->pci_index = (uint)i;
    sc->vendor_id = dev->vendor_id;
    sc->device_id = dev->device_id;
    sc->bus = dev->bus;
    sc->slot = dev->slot;
    sc->func = dev->func;
    sc->class_code = dev->class_code;
    sc->subclass = dev->subclass;
    sc->prog_if = dev->prog_if;
    sc->irq_line = dev->irq_line;
    sc->kind = usb_hc_kind_from_prog_if(dev->prog_if);
    sc->phase = USB_HC_PHASE_INIT;
    sc->bar0 = pci_bar_base(dev, 0);
    sc->bar0_size = pci_bar_size(dev, 0);
    sc->bar0_is_io = (dev->bar[0] & PCI_BAR_IO) ? 1 : 0;
    sc->last_error = USB_HC_ERR_NONE;

    ops = usb_get_ops(sc->kind);
    if(!ops->probe_regs || !ops->reset || !ops->halt || !ops->start){
      sc->phase = USB_HC_PHASE_DEGRADED;
      sc->last_error = USB_HC_ERR_NO_BACKEND;
      sc->init_failures++;
    } else {
      sc->probe_attempts++;
      if(ops->probe_regs(sc, dev) < 0){
        sc->probe_failures++;
        sc->phase = USB_HC_PHASE_DEGRADED;
        sc->last_error = USB_HC_ERR_PROBE;
        sc->init_failures++;
      } else {
        sc->probe_successes++;

        sc->reset_attempts++;
        if(ops->reset(sc, dev) < 0){
          sc->reset_failures++;
          sc->phase = USB_HC_PHASE_DEGRADED;
          sc->last_error = USB_HC_ERR_RESET;
          sc->init_failures++;
        } else {
          sc->reset_successes++;

          sc->halt_attempts++;
          if(ops->halt(sc, dev) < 0){
            sc->halt_failures++;
            sc->phase = USB_HC_PHASE_DEGRADED;
            sc->last_error = USB_HC_ERR_HALT;
            sc->init_failures++;
          } else {
            sc->halt_successes++;

            sc->start_attempts++;
            if(ops->start(sc, dev) < 0){
              sc->start_failures++;
              sc->phase = USB_HC_PHASE_DEGRADED;
              sc->last_error = USB_HC_ERR_START;
              sc->init_failures++;
            } else {
              sc->start_successes++;
              if(ops->scan_ports){
                sc->scan_attempts++;
                if(ops->scan_ports(sc, dev) < 0)
                  sc->scan_failures++;
                else
                  sc->scan_successes++;
              }
              if(ops->service_ports){
                sc->service_attempts++;
                if(ops->service_ports(sc, dev) < 0)
                  sc->service_failures++;
                else
                  sc->service_successes++;
              }
              usb_collect_candidates(usb_hc_count - 1, sc);
            }
          }
        }
      }
    }

    BOOTDBG("usb: found %d:%d.%d %x:%x progif=%x kind=%s phase=%s err=%s irq=%d bar0=%x size=%x\n",
            sc->bus, sc->slot, sc->func,
            sc->vendor_id, sc->device_id,
            sc->prog_if, usb_hc_kind_name(sc->kind), usb_hc_phase_name(sc->phase),
            usb_hc_error_name(sc->last_error),
            sc->irq_line, sc->bar0, sc->bar0_size);
  }

  usb_materialize_devices();

  BOOTDBG("usb: host-controller scaffold discovered %d controller(s)\n", usb_hc_count);
  release(&usb_lock);
}

int
usb_procfs_dump(char *buf, uint max)
{
  uint len;
  uint i;
  uint uhci;
  uint ohci;
  uint ehci;
  uint xhci;
  uint unknown;
  uint ready;
  uint degraded;
  uint addr_assigned;
  uint addr_conflicts;
  uint addr_exhausted;
  uint enum_ok;
  uint enum_pending;
  uint enum_failed;

  if(!buf || max == 0)
    return -1;

  acquire(&usb_lock);

  len = 0;
  uhci = 0;
  ohci = 0;
  ehci = 0;
  xhci = 0;
  unknown = 0;
  ready = 0;
  degraded = 0;
  addr_assigned = 0;
  addr_conflicts = 0;
  addr_exhausted = 0;
  enum_ok = 0;
  enum_pending = 0;
  enum_failed = 0;

  for(i = 0; i < usb_hc_count; i++){
    switch(usb_hc[i].kind){
    case USB_HC_KIND_UHCI:
      uhci++;
      break;
    case USB_HC_KIND_OHCI:
      ohci++;
      break;
    case USB_HC_KIND_EHCI:
      ehci++;
      break;
    case USB_HC_KIND_XHCI:
      xhci++;
      break;
    default:
      unknown++;
      break;
    }
    if(usb_hc[i].phase == USB_HC_PHASE_READY)
      ready++;
    else if(usb_hc[i].phase == USB_HC_PHASE_DEGRADED)
      degraded++;

    addr_assigned += usb_bus[i].addr_assigned;
    addr_conflicts += usb_bus[i].addr_conflicts;
    addr_exhausted += usb_bus[i].addr_exhausted;
  }

  if(usb_buf_puts(buf, max, &len, "usb_hc_count ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, usb_hc_count) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "uhci ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, uhci) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "ohci ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, ohci) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "ehci ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, ehci) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "xhci ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, xhci) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "unknown ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, unknown) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "ready ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, ready) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "degraded ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, degraded) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "usb_dev_count ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, usb_dev_count) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "usb_device_count ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, usb_device_count) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "usb_addr_assigned ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, addr_assigned) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "usb_addr_conflict ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, addr_conflicts) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "usb_addr_exhausted ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, addr_exhausted) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  for(i = 0; i < usb_device_count; i++){
    struct usb_device *ud = &usb_devices[i];

    if(!ud->active)
      continue;
     if(ud->enum_state == USB_ENUM_STATE_DESC8_OK ||
       ud->enum_state == USB_ENUM_STATE_DESC18_OK ||
       ud->enum_state == USB_ENUM_STATE_CFG_OK ||
       ud->enum_state == USB_ENUM_STATE_CONFIGURED)
      enum_ok++;
    else if(ud->enum_state == USB_ENUM_STATE_CONTROL_PENDING)
      enum_pending++;
    else if(ud->enum_state == USB_ENUM_STATE_FAILED)
      enum_failed++;
  }

  if(usb_buf_puts(buf, max, &len, "usb_enum_ok ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, enum_ok) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "usb_enum_pending ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, enum_pending) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "usb_enum_failed ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, enum_failed) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "usb_attach_dedup ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, usb_attach_dedup_count) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "usb_attach_retired ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, usb_attach_retired_count) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  if(usb_buf_puts(buf, max, &len, "usb_attach_rebind ") < 0) goto out;
  if(usb_buf_putu(buf, max, &len, usb_attach_rebind_count) < 0) goto out;
  if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;

  for(i = 0; i < usb_hc_count; i++){
    struct usb_hc_probe *sc = &usb_hc[i];
    struct usb_bus_state *bus = &usb_bus[i];

    if(usb_buf_puts(buf, max, &len, "hc") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, i) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " bus=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->bus) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " slot=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->slot) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " func=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->func) < 0) goto out;

    if(usb_buf_puts(buf, max, &len, " vendor=0x") < 0) goto out;
    if(usb_buf_puthex16(buf, max, &len, sc->vendor_id) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " device=0x") < 0) goto out;
    if(usb_buf_puthex16(buf, max, &len, sc->device_id) < 0) goto out;

    if(usb_buf_puts(buf, max, &len, " prog_if=0x") < 0) goto out;
    if(usb_buf_puthex8(buf, max, &len, sc->prog_if) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " kind=") < 0) goto out;
    if(usb_buf_puts(buf, max, &len, usb_hc_kind_name(sc->kind)) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " phase=") < 0) goto out;
    if(usb_buf_puts(buf, max, &len, usb_hc_phase_name(sc->phase)) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " err=") < 0) goto out;
    if(usb_buf_puts(buf, max, &len, usb_hc_error_name(sc->last_error)) < 0) goto out;

    if(usb_buf_puts(buf, max, &len, " irq=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->irq_line) < 0) goto out;

    if(usb_buf_puts(buf, max, &len, " bar0=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->bar0) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " bar0_size=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->bar0_size) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " bar0_type=") < 0) goto out;
    if(usb_buf_puts(buf, max, &len, sc->bar0_is_io ? "io" : "mmio") < 0) goto out;

    if(usb_buf_puts(buf, max, &len, " reg_probe=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->reg_probe_ok) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " caplen=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->cap_length) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " hciver=0x") < 0) goto out;
    if(usb_buf_puthex16(buf, max, &len, sc->hciversion) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " rh_present=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->rh_present) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " rh_ports=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->rh_ports) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " rh_change=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->rh_change_bits) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " reg0=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->reg0) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " reg1=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->reg1) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " p=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->probe_attempts) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->probe_successes) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->probe_failures) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " r=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->reset_attempts) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->reset_successes) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->reset_failures) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " h=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->halt_attempts) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->halt_successes) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->halt_failures) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " s=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->start_attempts) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->start_successes) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->start_failures) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " sc=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->scan_attempts) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->scan_successes) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->scan_failures) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " sv=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->service_attempts) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->service_successes) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->service_failures) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " rh_connect=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->rh_connect_bits) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " rh_enabled=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->rh_enabled_bits) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " rh_low=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->rh_low_bits) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " rh_full=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->rh_full_bits) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " rh_high=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->rh_high_bits) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " rh_super=0x") < 0) goto out;
    if(usb_buf_puthex32(buf, max, &len, sc->rh_super_bits) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " failures=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, sc->init_failures) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " addr=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, bus->addr_assigned) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, bus->addr_conflicts) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, bus->addr_exhausted) < 0) goto out;

    if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;
  }

  for(i = 0; i < usb_dev_count; i++){
    struct usb_dev_candidate *dc = &usb_dev[i];

    if(usb_buf_puts(buf, max, &len, "dev") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, i) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " hc=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, dc->hc_index) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " kind=") < 0) goto out;
    if(usb_buf_puts(buf, max, &len, usb_hc_kind_name(dc->kind)) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " port=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, dc->port) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " present=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, dc->present) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " enabled=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, dc->enabled) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " speed=") < 0) goto out;
    if(usb_buf_puts(buf, max, &len, usb_dev_speed_name(dc->speed)) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " addr=") < 0) goto out;
    if(dc->address != USB_DEV_ADDR_NONE){
      if(usb_buf_putu(buf, max, &len, dc->address) < 0) goto out;
    } else {
      if(usb_buf_puts(buf, max, &len, "none") < 0) goto out;
    }
    if(usb_buf_puts(buf, max, &len, " addr_ready=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, dc->addr_ready) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " gen=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, dc->generation) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;
  }

  for(i = 0; i < usb_device_count; i++){
    struct usb_device *ud = &usb_devices[i];
    uint e;

    if(!ud->active)
      continue;

    if(usb_buf_puts(buf, max, &len, "udev") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, i) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " hc=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->hc_index) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " kind=") < 0) goto out;
    if(usb_buf_puts(buf, max, &len, usb_hc_kind_name(ud->kind)) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " port=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->port) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " speed=") < 0) goto out;
    if(usb_buf_puts(buf, max, &len, usb_dev_speed_name(ud->speed)) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " addr=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->address) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " state=") < 0) goto out;
    if(usb_buf_puts(buf, max, &len, usb_enum_state_name(ud->enum_state)) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " err=") < 0) goto out;
    if(usb_buf_puts(buf, max, &len, usb_enum_error_name(ud->last_error)) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " addr_set=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->address_set) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " mps0=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->max_packet0) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " bcd=0x") < 0) goto out;
    if(usb_buf_puthex16(buf, max, &len, ud->bcd_usb) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " vid=0x") < 0) goto out;
    if(usb_buf_puthex16(buf, max, &len, ud->vendor_id) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " pid=0x") < 0) goto out;
    if(usb_buf_puthex16(buf, max, &len, ud->product_id) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " class=0x") < 0) goto out;
    if(usb_buf_puthex8(buf, max, &len, ud->dev_class) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " subclass=0x") < 0) goto out;
    if(usb_buf_puthex8(buf, max, &len, ud->dev_subclass) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " proto=0x") < 0) goto out;
    if(usb_buf_puthex8(buf, max, &len, ud->dev_proto) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " desc=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->desc8_ok) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->desc18_ok) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->cfg_desc_ok) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " cfg=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->cfg_value) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " cfg_set=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->cfg_set_ok) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " cfg_total=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->cfg_total_len) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " if=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->cfg_num_ifaces) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->cfg_parsed_ifaces) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " if_tbl=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->iface_count) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " ep=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->cfg_num_eps) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " if_match=0x") < 0) goto out;
    if(usb_buf_puthex8(buf, max, &len, ud->match_if_class) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_puthex8(buf, max, &len, ud->match_if_subclass) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_puthex8(buf, max, &len, ud->match_if_proto) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " attach=") < 0) goto out;
    if(usb_buf_puts(buf, max, &len, usb_attach_driver_name(ud->attach_driver)) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " a=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->attach_attempts) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->attach_successes) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->attach_skips) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->attach_rebinds) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " h=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->attach_handle) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " attach_if=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->attach_if_number) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->attach_if_alt) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " ep_tbl=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->ep_count) < 0) goto out;
    for(e = 0; e < ud->ep_count; e++){
      struct usb_ep_cap *ep;

      ep = &ud->eps[e];
      if(usb_buf_puts(buf, max, &len, " ep") < 0) goto out;
      if(usb_buf_putu(buf, max, &len, e) < 0) goto out;
      if(usb_buf_puts(buf, max, &len, "=0x") < 0) goto out;
      if(usb_buf_puthex8(buf, max, &len, ep->addr) < 0) goto out;
      if(usb_buf_puts(buf, max, &len, "/0x") < 0) goto out;
      if(usb_buf_puthex8(buf, max, &len, ep->attr) < 0) goto out;
      if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
      if(usb_buf_putu(buf, max, &len, ep->max_packet) < 0) goto out;
      if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
      if(usb_buf_putu(buf, max, &len, ep->interval) < 0) goto out;
      if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
      if(usb_buf_putu(buf, max, &len, ep->iface_index) < 0) goto out;
    }
    if(usb_buf_puts(buf, max, &len, " gen=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->generation) < 0) goto out;
    if(usb_buf_puts(buf, max, &len, " e=") < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->enum_attempts) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->enum_successes) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '/') < 0) goto out;
    if(usb_buf_putu(buf, max, &len, ud->enum_failures) < 0) goto out;
    if(usb_buf_putc(buf, max, &len, '\n') < 0) goto out;
  }

out:
  release(&usb_lock);
  return (int)len;
}
