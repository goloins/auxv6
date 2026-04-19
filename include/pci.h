/*
 * PCI Bus Subsystem Header
 *
 * Provides PCI configuration space access, device enumeration,
 * BAR mapping, and interrupt routing for auxv6.
 *
 * Design follows BSD/NetBSD pci_attach patterns:
 * - Static probe at boot via pci_enumerate()
 * - Per-device pci_dev structure with vendor/device IDs
 * - BAR (Base Address Register) access for memory-mapped I/O
 * - Interrupt pin to IOAPIC routing
 *
 * Future: MSI/MSI-X support, ACPI-based routing
 */

#ifndef _PCI_H_
#define _PCI_H_

#include "types.h"
#include "stdint.h"

/* PCI Configuration Space I/O ports */
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

/* PCI Configuration Space Register Offsets */
#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_STATUS          0x06
#define PCI_REVISION_ID     0x08
#define PCI_PROG_IF         0x09
#define PCI_SUBCLASS        0x0A
#define PCI_CLASS           0x0B
#define PCI_CACHE_LINE      0x0C
#define PCI_LATENCY_TIMER   0x0D
#define PCI_HEADER_TYPE     0x0E
#define PCI_BIST            0x0F
#define PCI_BAR0            0x10
#define PCI_BAR1            0x14
#define PCI_BAR2            0x18
#define PCI_BAR3            0x1C
#define PCI_BAR4            0x20
#define PCI_BAR5            0x24
#define PCI_CARDBUS_CIS     0x28
#define PCI_SUBSYSTEM_VENDOR 0x2C
#define PCI_SUBSYSTEM_ID    0x2E
#define PCI_ROM_ADDRESS     0x30
#define PCI_CAPABILITIES    0x34
#define PCI_INTERRUPT_LINE  0x3C
#define PCI_INTERRUPT_PIN   0x3D
#define PCI_MIN_GNT         0x3E
#define PCI_MAX_LAT         0x3F

/* PCI Command Register bits */
#define PCI_CMD_IO_SPACE    0x0001
#define PCI_CMD_MEM_SPACE   0x0002
#define PCI_CMD_BUS_MASTER  0x0004
#define PCI_CMD_SPECIAL     0x0008
#define PCI_CMD_MWI         0x0010
#define PCI_CMD_VGA_SNOOP   0x0020
#define PCI_CMD_PARITY      0x0040
#define PCI_CMD_STEPPING    0x0080
#define PCI_CMD_SERR        0x0100
#define PCI_CMD_FASTB2B     0x0200
#define PCI_CMD_INT_DISABLE 0x0400

/* PCI Status Register bits */
#define PCI_STATUS_CAP_LIST 0x0010

/* PCI Capability IDs */
#define PCI_CAP_ID_MSI      0x05
#define PCI_CAP_ID_MSIX     0x11

/* MSI capability control bits */
#define PCI_MSI_CTRL_ENABLE     0x0001
#define PCI_MSI_CTRL_MMC_SHIFT  1
#define PCI_MSI_CTRL_MMC_MASK   0x000E
#define PCI_MSI_CTRL_MME_SHIFT  4
#define PCI_MSI_CTRL_MME_MASK   0x0070
#define PCI_MSI_CTRL_64BIT      0x0080
#define PCI_MSI_CTRL_PVMASK     0x0100

/* MSI-X capability control bits */
#define PCI_MSIX_CTRL_ENABLE    0x8000
#define PCI_MSIX_CTRL_MASKALL   0x4000
#define PCI_MSIX_CTRL_TSIZE_MASK 0x07FF

/* MSI-X table layout */
#define PCI_MSIX_TABLE_BIR_MASK 0x00000007
#define PCI_MSIX_TABLE_OFF_MASK 0xFFFFFFF8

/* PCI interrupt mode */
#define PCI_IRQ_MODE_INTX   0
#define PCI_IRQ_MODE_MSI    1
#define PCI_IRQ_MODE_MSIX   2

/* Generic interrupt-vector allocation flags */
#define PCI_IRQ_F_INTX      0x01
#define PCI_IRQ_F_MSI       0x02
#define PCI_IRQ_F_MSIX      0x04
#define PCI_IRQ_F_ALL       (PCI_IRQ_F_INTX | PCI_IRQ_F_MSI | PCI_IRQ_F_MSIX)

#define PCI_IRQ_MAX_VECTORS 32

/* PCI Header Type */
#define PCI_TYPE_DEVICE     0x00
#define PCI_TYPE_BRIDGE     0x01
#define PCI_TYPE_CARDBUS    0x02
#define PCI_TYPE_MULTIFUNC  0x80

/* BAR Types */
#define PCI_BAR_IO          0x01
#define PCI_BAR_MEM32       0x00
#define PCI_BAR_MEM64       0x04
#define PCI_BAR_PREFETCH    0x08

/* Common PCI Class Codes */
#define PCI_CLASS_STORAGE   0x01
#define PCI_CLASS_NETWORK   0x02
#define PCI_CLASS_DISPLAY   0x03
#define PCI_CLASS_MULTIMEDIA 0x04
#define PCI_CLASS_COMM      0x07
#define PCI_CLASS_BRIDGE    0x06
#define PCI_CLASS_SERIAL    0x0C

/* Multimedia Subclasses */
#define PCI_SUBCLASS_MULTIMEDIA_AUDIO 0x01
#define PCI_SUBCLASS_MULTIMEDIA_HDA   0x03

/* Communications Subclasses */
#define PCI_SUBCLASS_COMM_SERIAL 0x00
#define PCI_SUBCLASS_COMM_MODEM  0x03

/* Storage Subclasses */
#define PCI_SUBCLASS_SCSI   0x00
#define PCI_SUBCLASS_IDE    0x01
#define PCI_SUBCLASS_FLOPPY 0x02
#define PCI_SUBCLASS_SATA   0x06
#define PCI_SUBCLASS_NVME   0x08

/* Network Subclasses */
#define PCI_SUBCLASS_ETHERNET 0x00

/* Serial Bus Subclasses / Prog IF */
#define PCI_SUBCLASS_SERIAL_FIREWIRE 0x00
#define PCI_SUBCLASS_SERIAL_USB      0x03
#define PCI_PROGIF_IEEE1394_OHCI 0x10
#define PCI_PROGIF_USB_UHCI      0x00
#define PCI_PROGIF_USB_OHCI      0x10
#define PCI_PROGIF_USB_EHCI      0x20
#define PCI_PROGIF_USB_XHCI      0x30

/* Known Vendor IDs */
#define PCI_VENDOR_INTEL    0x8086
#define PCI_VENDOR_AMD      0x1022
#define PCI_VENDOR_QEMU     0x1234
#define PCI_VENDOR_VIRTIO   0x1AF4
#define PCI_VENDOR_REALTEK  0x10EC
#define PCI_VENDOR_BROADCOM 0x14E4
#define PCI_VENDOR_MARVELL  0x11AB
#define PCI_VENDOR_VIA      0x1106
#define PCI_VENDOR_AQUANTIA 0x1D6A
#define PCI_VENDOR_NVIDIA   0x10DE
#define PCI_VENDOR_CREATIVE 0x1102
#define PCI_VENDOR_CMEDIA   0x13F6
#define PCI_VENDOR_YAMAHA   0x1073
#define PCI_VENDOR_ESS      0x125D
#define PCI_VENDOR_ANALOG_DEVICES 0x11D4
#define PCI_VENDOR_SIGMATEL 0x8384
#define PCI_VENDOR_IDT      0x111D
#define PCI_VENDOR_CONEXANT 0x14F1
#define PCI_VENDOR_AGERE    0x11C1
#define PCI_VENDOR_SMARTLINK 0x163C
#define PCI_VENDOR_PCTEL    0x134D
#define PCI_VENDOR_MOTOROLA 0x1057

/* Known Device IDs */
#define PCI_DEVICE_E1000    0x100E  /* Intel 82540EM Gigabit Ethernet */
#define PCI_DEVICE_E1000E   0x10D3  /* Intel 82574L Gigabit Ethernet */
#define PCI_DEVICE_PCNET    0x2000  /* AMD PCNET-FAST III 79C973 */
#define PCI_DEVICE_AHCI     0x2922  /* Intel ICH9 AHCI */
#define PCI_DEVICE_NVIDIA_MCP79_AHCI 0x0AB5 /* NVIDIA MCP79 AHCI */
#define PCI_DEVICE_VIRTIO_NET  0x1000  /* Virtio Network Device */
#define PCI_DEVICE_VIRTIO_BLK  0x1001  /* Virtio Block Device */
#define PCI_DEVICE_VIRTIO_CON  0x1003  /* Virtio Console Device */
#define PCI_DEVICE_VIRTIO_RNG  0x1005  /* Virtio RNG Device */

/* Maximum PCI devices to enumerate */
#define PCI_MAX_DEVICES     64

/* PCI device structure */
struct pci_dev {
    uint8_t   bus;
    uint8_t   slot;
    uint8_t   func;
    uint16_t  vendor_id;
    uint16_t  device_id;
    uint8_t   class_code;
    uint8_t   subclass;
    uint8_t   prog_if;
    uint8_t   revision;
    uint8_t   header_type;
    uint8_t   irq_line;
    uint8_t   irq_pin;
    uint32_t  bar[6];         /* Base Address Registers */
    uint32_t  bar_size[6];    /* Size of each BAR region */
    uint8_t   bar_type[6];    /* Type flags for each BAR */
    uint8_t   msi_cap_off;    /* MSI capability offset (0 if absent) */
    uint8_t   msix_cap_off;   /* MSI-X capability offset (0 if absent) */
    uint8_t   irq_mode;       /* PCI_IRQ_MODE_* */
    uint8_t   irq_nvec;       /* Number of allocated vectors */
    uint8_t   irq_vectors[PCI_IRQ_MAX_VECTORS]; /* trap.c IRQ indexes */
    void     *driver_data;    /* Driver-private data */
};

/* PCI driver match structure (for driver registration) */
struct pci_device_id {
    uint16_t  vendor_id;
    uint16_t  device_id;
    uint8_t   class_code;
    uint8_t   subclass;
    uint8_t   match_flags;
};

#define PCI_MATCH_VENDOR    0x01
#define PCI_MATCH_DEVICE    0x02
#define PCI_MATCH_CLASS     0x04
#define PCI_MATCH_SUBCLASS  0x08

/* PCI driver structure */
struct pci_driver {
    const char            *name;
    const struct pci_device_id *id_table;
    int                   id_table_len;
    int                   (*probe)(struct pci_dev *dev);
    void                  (*remove)(struct pci_dev *dev);
};

/* Driver registration table (static, fixed size) */
#define PCI_MAX_DRIVERS     16

/* Function prototypes */

/* Initialization and enumeration */
void     pci_init(void);
int      pci_enumerate(void);

/* Configuration space access */
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t  pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void     pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
void     pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);
void     pci_config_write8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value);

/* Device-level convenience wrappers */
uint32_t pci_read32(struct pci_dev *dev, uint8_t offset);
uint16_t pci_read16(struct pci_dev *dev, uint8_t offset);
uint8_t  pci_read8(struct pci_dev *dev, uint8_t offset);
void     pci_write32(struct pci_dev *dev, uint8_t offset, uint32_t value);
void     pci_write16(struct pci_dev *dev, uint8_t offset, uint16_t value);
void     pci_write8(struct pci_dev *dev, uint8_t offset, uint8_t value);

/* BAR operations */
uint32_t pci_bar_base(struct pci_dev *dev, int bar);
uint32_t pci_bar_size(struct pci_dev *dev, int bar);
int      pci_bar_type(struct pci_dev *dev, int bar);
void    *pci_map_bar(struct pci_dev *dev, int bar);

/* Command register helpers */
void     pci_set_master(struct pci_dev *dev);
void     pci_enable_io(struct pci_dev *dev);
void     pci_enable_mem(struct pci_dev *dev);
void     pci_disable_interrupts(struct pci_dev *dev);
void     pci_disable_msi(struct pci_dev *dev);
void     pci_enable_interrupts(struct pci_dev *dev);
int      pci_find_capability(struct pci_dev *dev, uint8_t cap_id);

/* Device lookup */
struct pci_dev *pci_find_device(uint16_t vendor, uint16_t device);
struct pci_dev *pci_find_class(uint8_t class_code, uint8_t subclass);
struct pci_dev *pci_get_device(int index);
int      pci_device_count(void);

/* Driver registration (static model) */
int      pci_register_driver(struct pci_driver *drv);
void     pci_unregister_driver(struct pci_driver *drv);

/* Interrupt handling */
int      pci_get_irq(struct pci_dev *dev);
void     pci_enable_irq(struct pci_dev *dev, int cpu);
int      pci_irq_alloc_vectors(struct pci_dev *dev, int min_vec, int max_vec, int flags);
void     pci_irq_free_vectors(struct pci_dev *dev);
int      pci_irq_vector(struct pci_dev *dev, int index);
int      pci_irq_mode(struct pci_dev *dev);

/* Debug */
void     pci_dump_devices(void);
int      pci_irq_audit_format(char *buf, int maxlen);
void     pci_irq_audit_report(void);
void     pci_inject_alloc_failures(int enable);
int      pci_get_inject_alloc_failures(void);

#endif /* _PCI_H_ */
