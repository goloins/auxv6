/*
 * PCI Bus Driver for auxv6
 *
 * Implements PCI configuration space access and device enumeration.
 * Uses legacy I/O port method (0xCF8/0xCFC) for x86 compatibility.
 *
 * TODO Phase 1:
 * - [ ] Basic config space read/write
 * - [ ] Device enumeration (bus 0 only initially)
 * - [ ] BAR decoding and size detection
 * - [ ] Driver registration and probe callbacks
 *
 * TODO Phase 2:
 * - [ ] Multi-bus enumeration (PCI bridges)
 * - [ ] MSI/MSI-X support
 * - [ ] Power management (D0-D3 states)
 * - [ ] Hotplug support
 *
 * Reference: PCI Local Bus Specification 3.0
 * See also: NetBSD sys/dev/pci/pci.c, OpenBSD sys/dev/pci/pci.c
 */

#include "types.h"
#include "defs.h"
#include "memlayout.h"
#include "x86.h"
#include "spinlock.h"
#include "pci.h"
#include "traps.h"

/* PCI device table (discovered devices) */
static struct pci_dev pci_devices[PCI_MAX_DEVICES];
static int pci_ndevices = 0;

/* Registered drivers */
static struct pci_driver *pci_drivers[PCI_MAX_DRIVERS];
static int pci_ndrivers = 0;

/* PCI subsystem lock */
static struct spinlock pci_lock;
static struct spinlock pci_irqvec_lock;

/* Use high IDT vectors for MSI/MSI-X to avoid legacy ISA IRQ overlap. */
#define PCI_MSI_IRQ_BASE    64   /* trap vector 96 */
#define PCI_MSI_IRQ_LIMIT   224  /* trap vector 255 */

/* One bit per trap.c IRQ index in [PCI_MSI_IRQ_BASE, PCI_MSI_IRQ_LIMIT). */
static uint32_t pci_irqvec_bitmap[(PCI_MSI_IRQ_LIMIT + 31) / 32];

/* Debug: error injection for testing fallback paths */
static int pci_inject_alloc_failure = 0;

static int
pci_irqvec_alloc_locked(void)
{
    /* Error injection: fail allocation if flag is set */
    if (pci_inject_alloc_failure)
        return -1;
    
    for (int irq = PCI_MSI_IRQ_BASE; irq < PCI_MSI_IRQ_LIMIT; irq++) {
        int word = irq >> 5;
        uint32_t bit = 1U << (irq & 31);
        if ((pci_irqvec_bitmap[word] & bit) == 0) {
            pci_irqvec_bitmap[word] |= bit;
            return irq;
        }
    }
    return -1;
}

static void
pci_irqvec_free_locked(int irq)
{
    if (irq < PCI_MSI_IRQ_BASE || irq >= PCI_MSI_IRQ_LIMIT)
        return;
    pci_irqvec_bitmap[irq >> 5] &= ~(1U << (irq & 31));
}

static int
pci_alloc_irq_slots(uint8_t *out, int count)
{
    int i;

    if (count <= 0 || count > PCI_IRQ_MAX_VECTORS)
        return -1;

    acquire(&pci_irqvec_lock);
    for (i = 0; i < count; i++) {
        int irq = pci_irqvec_alloc_locked();
        if (irq < 0)
            break;
        out[i] = (uint8_t)irq;
    }
    if (i != count) {
        while (--i >= 0)
            pci_irqvec_free_locked((int)out[i]);
        release(&pci_irqvec_lock);
        return -1;
    }
    release(&pci_irqvec_lock);
    return 0;
}

static void
pci_free_irq_slots(uint8_t *vec, int count)
{
    if (count <= 0)
        return;
    acquire(&pci_irqvec_lock);
    for (int i = 0; i < count; i++)
        pci_irqvec_free_locked((int)vec[i]);
    release(&pci_irqvec_lock);
}

static uint8_t
pci_pick_dest_apicid(int cpu)
{
    (void)cpu;
    return (uint8_t)lapicid();
}

static void
pci_disable_msix_function(struct pci_dev *dev, uint8_t cap)
{
    uint16_t mc = pci_read16(dev, cap + 2);
    mc &= ~(uint16_t)PCI_MSIX_CTRL_ENABLE;
    mc |= (uint16_t)PCI_MSIX_CTRL_MASKALL;
    pci_write16(dev, cap + 2, mc);
}

static void
pci_disable_msi_function(struct pci_dev *dev, uint8_t cap)
{
    uint16_t mc = pci_read16(dev, cap + 2);
    mc &= ~(uint16_t)PCI_MSI_CTRL_ENABLE;
    pci_write16(dev, cap + 2, mc);
}

/*
 * PCI Configuration Space Address Format (for 0xCF8):
 *  31      | 30-24    | 23-16 | 15-11 | 10-8 | 7-2       | 1-0
 *  Enable  | Reserved | Bus   | Slot  | Func | Register  | 00
 */
static uint32_t
pci_make_addr(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    return (uint32_t)((1U << 31) |        /* Enable bit */
                      ((uint32_t)bus << 16) |
                      ((uint32_t)slot << 11) |
                      ((uint32_t)func << 8) |
                      (offset & 0xFC));   /* Aligned to 32-bit */
}

/*
 * Raw PCI configuration space read (32-bit)
 */
uint32_t
pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t addr = pci_make_addr(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDRESS, addr);
    return inl(PCI_CONFIG_DATA);
}

/*
 * Raw PCI configuration space read (16-bit)
 */
uint16_t
pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t val = pci_config_read32(bus, slot, func, offset & ~3);
    return (val >> ((offset & 2) * 8)) & 0xFFFF;
}

/*
 * Raw PCI configuration space read (8-bit)
 */
uint8_t
pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t val = pci_config_read32(bus, slot, func, offset & ~3);
    return (val >> ((offset & 3) * 8)) & 0xFF;
}

/*
 * Raw PCI configuration space write (32-bit)
 */
void
pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value)
{
    uint32_t addr = pci_make_addr(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDRESS, addr);
    outl(PCI_CONFIG_DATA, value);
}

/*
 * Raw PCI configuration space write (16-bit)
 */
void
pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value)
{
    uint32_t addr = pci_make_addr(bus, slot, func, offset & ~3);
    outl(PCI_CONFIG_ADDRESS, addr);
    uint32_t current = inl(PCI_CONFIG_DATA);
    int shift = (offset & 2) * 8;
    current = (current & ~(0xFFFF << shift)) | ((uint32_t)value << shift);
    outl(PCI_CONFIG_DATA, current);
}

/*
 * Raw PCI configuration space write (8-bit)
 */
void
pci_config_write8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value)
{
    uint32_t addr = pci_make_addr(bus, slot, func, offset & ~3);
    outl(PCI_CONFIG_ADDRESS, addr);
    uint32_t current = inl(PCI_CONFIG_DATA);
    int shift = (offset & 3) * 8;
    current = (current & ~(0xFF << shift)) | ((uint32_t)value << shift);
    outl(PCI_CONFIG_DATA, current);
}

/*
 * Device-level read wrappers
 */
uint32_t pci_read32(struct pci_dev *dev, uint8_t offset)
{
    return pci_config_read32(dev->bus, dev->slot, dev->func, offset);
}

uint16_t pci_read16(struct pci_dev *dev, uint8_t offset)
{
    return pci_config_read16(dev->bus, dev->slot, dev->func, offset);
}

uint8_t pci_read8(struct pci_dev *dev, uint8_t offset)
{
    return pci_config_read8(dev->bus, dev->slot, dev->func, offset);
}

void pci_write32(struct pci_dev *dev, uint8_t offset, uint32_t value)
{
    pci_config_write32(dev->bus, dev->slot, dev->func, offset, value);
}

void pci_write16(struct pci_dev *dev, uint8_t offset, uint16_t value)
{
    pci_config_write16(dev->bus, dev->slot, dev->func, offset, value);
}

void pci_write8(struct pci_dev *dev, uint8_t offset, uint8_t value)
{
    pci_config_write8(dev->bus, dev->slot, dev->func, offset, value);
}

/*
 * Determine the size of a BAR by writing all 1s and reading back
 */
static uint32_t
pci_bar_probe_size(struct pci_dev *dev, int bar)
{
    uint8_t bar_off = PCI_BAR0 + (bar * 4);
    uint32_t orig = pci_read32(dev, bar_off);
    
    pci_write32(dev, bar_off, 0xFFFFFFFF);
    uint32_t size_mask = pci_read32(dev, bar_off);
    pci_write32(dev, bar_off, orig);
    
    if (size_mask == 0 || size_mask == 0xFFFFFFFF)
        return 0;
    
    /* For I/O BARs, mask the type bits */
    if (orig & PCI_BAR_IO)
        size_mask &= ~0x3;
    else
        size_mask &= ~0xF;
    
    /* Size is complement + 1 */
    return (~size_mask) + 1;
}

/*
 * Decode a BAR and store its information
 */
static void
pci_decode_bar(struct pci_dev *dev, int bar)
{
    uint8_t bar_off = PCI_BAR0 + (bar * 4);
    uint32_t value = pci_read32(dev, bar_off);
    
    dev->bar[bar] = value;
    dev->bar_size[bar] = pci_bar_probe_size(dev, bar);
    
    if (value & PCI_BAR_IO) {
        dev->bar_type[bar] = PCI_BAR_IO;
    } else {
        dev->bar_type[bar] = (value >> 1) & 0x03;
        if ((value & 0x06) == 0x04) {
            /* 64-bit BAR: skip next BAR */
            dev->bar_type[bar] |= 0x80;  /* Mark as 64-bit lower half */
        }
    }
}

/*
 * Probe a single PCI function
 */
static int
pci_probe_function(uint8_t bus, uint8_t slot, uint8_t func)
{
    uint32_t id = pci_config_read32(bus, slot, func, PCI_VENDOR_ID);
    uint16_t vendor = id & 0xFFFF;
    uint16_t device = (id >> 16) & 0xFFFF;
    
    /* No device present */
    if (vendor == 0xFFFF || vendor == 0x0000)
        return 0;
    
    if (pci_ndevices >= PCI_MAX_DEVICES)
        return 0;
    
    struct pci_dev *dev = &pci_devices[pci_ndevices];
    memset(dev, 0, sizeof(*dev));
    
    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->vendor_id = vendor;
    dev->device_id = device;
    
    uint32_t class_rev = pci_config_read32(bus, slot, func, PCI_REVISION_ID);
    dev->revision = class_rev & 0xFF;
    dev->prog_if = (class_rev >> 8) & 0xFF;
    dev->subclass = (class_rev >> 16) & 0xFF;
    dev->class_code = (class_rev >> 24) & 0xFF;
    
    dev->header_type = pci_config_read8(bus, slot, func, PCI_HEADER_TYPE) & 0x7F;
    dev->irq_line = pci_config_read8(bus, slot, func, PCI_INTERRUPT_LINE);
    dev->irq_pin = pci_config_read8(bus, slot, func, PCI_INTERRUPT_PIN);
    dev->irq_mode = PCI_IRQ_MODE_INTX;
    dev->irq_nvec = 1;
    dev->irq_vectors[0] = dev->irq_line;
    
    /* Decode BARs (only for standard devices, not bridges) */
    if (dev->header_type == PCI_TYPE_DEVICE) {
        for (int i = 0; i < 6; i++) {
            pci_decode_bar(dev, i);
        }
    }
    
    BOOTDBG("pci %d:%d.%d: %x:%x class %x:%x irq %d\n",
            bus, slot, func, vendor, device,
            dev->class_code, dev->subclass, dev->irq_line);
    
    pci_ndevices++;
    return 1;
}

int
pci_find_capability(struct pci_dev *dev, uint8_t cap_id)
{
    uint16_t status;
    uint8_t cap;
    int limit;

    if (!dev)
        return 0;

    status = pci_read16(dev, PCI_STATUS);
    if ((status & PCI_STATUS_CAP_LIST) == 0)
        return 0;

    cap = pci_read8(dev, PCI_CAPABILITIES) & 0xFC;
    limit = 48;
    while (cap && limit-- > 0) {
        uint8_t id = pci_read8(dev, cap);
        uint8_t next = pci_read8(dev, cap + 1) & 0xFC;
        if (id == cap_id)
            return cap;
        if (next == cap)
            break;
        cap = next;
    }
    return 0;
}

/*
 * Enumerate all PCI devices on bus 0
 * TODO: Add bridge traversal for multi-bus systems
 */
int
pci_enumerate(void)
{
    acquire(&pci_lock);
    
    pci_ndevices = 0;
    
    /* Scan bus 0 (TODO: follow bridges for additional buses) */
    for (int slot = 0; slot < 32; slot++) {
        /* Check function 0 first */
        if (!pci_probe_function(0, slot, 0))
            continue;
        
        /* If multi-function device, probe other functions */
        uint8_t header = pci_config_read8(0, slot, 0, PCI_HEADER_TYPE);
        if (header & PCI_TYPE_MULTIFUNC) {
            for (int func = 1; func < 8; func++) {
                pci_probe_function(0, slot, func);
            }
        }
    }
    
    release(&pci_lock);
    
    BOOTDBG("pci: found %d devices\n", pci_ndevices);
    return pci_ndevices;
}

/*
 * Initialize PCI subsystem
 */
void
pci_init(void)
{
    initlock(&pci_lock, "pci");
    initlock(&pci_irqvec_lock, "pci-irqvec");
    memset(pci_irqvec_bitmap, 0, sizeof(pci_irqvec_bitmap));
    pci_ndevices = 0;
    pci_ndrivers = 0;
    
    BOOTDBG("pci: initializing PCI bus driver\n");
    
    pci_enumerate();
    
    /* TODO: Call registered driver probe functions */
}

/*
 * Boot-time IRQ vector audit report
 * Call this after all driver probes to show allocation results
 */
void
pci_irq_audit_report(void)
{
    int used_count = 0;
    int total_available = (PCI_MSI_IRQ_LIMIT - PCI_MSI_IRQ_BASE);
    
    /* Count used vectors */
    acquire(&pci_irqvec_lock);
    for (int irq = PCI_MSI_IRQ_BASE; irq < PCI_MSI_IRQ_LIMIT; irq++) {
        if (pci_irqvec_bitmap[irq >> 5] & (1U << (irq & 31)))
            used_count++;
    }
    release(&pci_irqvec_lock);
    
    cprintf("pci: irq vector allocation: %d/%d allocated, %d available\n",
            used_count, total_available, total_available - used_count);
    
    /* Report per-device allocation */
    for (int i = 0; i < pci_ndevices; i++) {
        struct pci_dev *dev = &pci_devices[i];
        if (dev->irq_nvec == 0)
            continue;
        const char *mode_str = "intx";
        if (dev->irq_mode == PCI_IRQ_MODE_MSI)
            mode_str = "msi";
        else if (dev->irq_mode == PCI_IRQ_MODE_MSIX)
            mode_str = "msix";
        cprintf("pci:   %d:%d.%d: %d vectors, mode=%s\n",
                dev->bus, dev->slot, dev->func, dev->irq_nvec, mode_str);
    }
}

/*
 * BAR operations
 */
uint32_t pci_bar_base(struct pci_dev *dev, int bar)
{
    if (bar < 0 || bar >= 6)
        return 0;
    
    uint32_t value = dev->bar[bar];
    if (value & PCI_BAR_IO)
        return value & ~0x3;
    else
        return value & ~0xF;
}

uint32_t pci_bar_size(struct pci_dev *dev, int bar)
{
    if (bar < 0 || bar >= 6)
        return 0;
    return dev->bar_size[bar];
}

int pci_bar_type(struct pci_dev *dev, int bar)
{
    if (bar < 0 || bar >= 6)
        return -1;
    return dev->bar_type[bar];
}

/*
 * Map a BAR into kernel virtual address space
 * 
 * Memory layout:
 * - PHYSTOP (0x0E000000): End of usable RAM
 * - DEVSPACE (0xFE000000): Start of device memory (identity mapped)
 * 
 * For MMIO BARs:
 * - >= DEVSPACE: Return physical address (identity mapped in kmap)
 * - < PHYSTOP: Return P2V(addr)
 * - Between: Not currently mapped (returns NULL)
 */
void *
pci_map_bar(struct pci_dev *dev, int bar)
{
    uint32_t base = pci_bar_base(dev, bar);
    uint32_t size = pci_bar_size(dev, bar);
    
    if (base == 0 || size == 0)
        return 0;
    
    /* I/O BARs don't need virtual mapping - use port I/O functions */
    if (dev->bar_type[bar] & PCI_BAR_IO)
        return 0;
    
    /* Check which memory region the BAR falls into */
    if (base >= DEVSPACE) {
        /* Device memory region - identity mapped in kernel page table */
        return (void *)(uintptr_t)base;
    } else if (base < PHYSTOP) {
        /* Low memory - use standard kernel mapping */
        return (void *)P2V(base);
    } else {
        /* Gap between PHYSTOP and DEVSPACE - not mapped */
        /* TODO: Could implement dynamic mapping here if needed */
        cprintf("pci_map_bar: BAR at 0x%x not in mapped region\n", base);
        return 0;
    }
}

/*
 * Enable bus master mode (required for DMA)
 */
void
pci_set_master(struct pci_dev *dev)
{
    uint16_t cmd = pci_read16(dev, PCI_COMMAND);
    cmd |= PCI_CMD_BUS_MASTER;
    pci_write16(dev, PCI_COMMAND, cmd);
}

/*
 * Enable I/O space access
 */
void
pci_enable_io(struct pci_dev *dev)
{
    uint16_t cmd = pci_read16(dev, PCI_COMMAND);
    cmd |= PCI_CMD_IO_SPACE;
    pci_write16(dev, PCI_COMMAND, cmd);
}

/*
 * Enable memory space access
 */
void
pci_enable_mem(struct pci_dev *dev)
{
    uint16_t cmd = pci_read16(dev, PCI_COMMAND);
    cmd |= PCI_CMD_MEM_SPACE;
    pci_write16(dev, PCI_COMMAND, cmd);
}

void
pci_disable_interrupts(struct pci_dev *dev)
{
    uint16_t cmd = pci_read16(dev, PCI_COMMAND);
    cmd |= PCI_CMD_INT_DISABLE;
    pci_write16(dev, PCI_COMMAND, cmd);
}

void
pci_enable_interrupts(struct pci_dev *dev)
{
    uint16_t cmd = pci_read16(dev, PCI_COMMAND);
    cmd &= ~PCI_CMD_INT_DISABLE;
    pci_write16(dev, PCI_COMMAND, cmd);
}

/*
 * Walk the PCI capability list and disable any MSI or MSI-X capability found.
 * Must be called before the device raises any interrupts.
 */
void
pci_disable_msi(struct pci_dev *dev)
{
    uint8_t cap;
    uint16_t cmd = pci_read16(dev, PCI_COMMAND);
    uint16_t status = pci_read16(dev, PCI_STATUS);

    NVMEDBG("pci: %d:%d.%d disable_msi start cap=%x cmd=%x status=%x\n",
            dev->bus, dev->slot, dev->func,
            pci_read8(dev, PCI_CAPABILITIES) & 0xFC, cmd, status);

    cap = (uint8_t)pci_find_capability(dev, PCI_CAP_ID_MSI);
    if (cap)
        pci_disable_msi_function(dev, cap);
    cap = (uint8_t)pci_find_capability(dev, PCI_CAP_ID_MSIX);
    if (cap)
        pci_disable_msix_function(dev, cap);

    dev->msi_cap_off = (uint8_t)pci_find_capability(dev, PCI_CAP_ID_MSI);
    dev->msix_cap_off = (uint8_t)pci_find_capability(dev, PCI_CAP_ID_MSIX);

    NVMEDBG("pci: %d:%d.%d disable_msi done cmd=%x status=%x\n",
            dev->bus, dev->slot, dev->func,
            pci_read16(dev, PCI_COMMAND), pci_read16(dev, PCI_STATUS));
}

static int
pci_try_enable_msix(struct pci_dev *dev, int min_vec, int max_vec, uint8_t dest_apicid)
{
    uint8_t cap;
    uint16_t mc;
    int table_nvec;
    int grant;
    uint32_t table;
    int bir;
    uint32_t table_off;
    volatile uint32_t *table_base;
    uint32_t msg_addr_low;
    uint32_t msg_addr_hi;

    cap = dev->msix_cap_off;
    if (!cap)
        cap = (uint8_t)pci_find_capability(dev, PCI_CAP_ID_MSIX);
    if (!cap)
        return -1;

    mc = pci_read16(dev, cap + 2);
    table_nvec = (mc & PCI_MSIX_CTRL_TSIZE_MASK) + 1;
    grant = max_vec;
    if (grant > table_nvec)
        grant = table_nvec;
    if (grant < min_vec)
        return -1;
    if (grant > PCI_IRQ_MAX_VECTORS)
        grant = PCI_IRQ_MAX_VECTORS;

    if (pci_alloc_irq_slots(dev->irq_vectors, grant) < 0)
        return -1;

    table = pci_read32(dev, cap + 4);
    bir = table & PCI_MSIX_TABLE_BIR_MASK;
    table_off = table & PCI_MSIX_TABLE_OFF_MASK;
    table_base = (volatile uint32_t *)pci_map_bar(dev, bir);
    if (!table_base) {
        pci_free_irq_slots(dev->irq_vectors, grant);
        return -1;
    }

    msg_addr_low = 0xFEE00000U | ((uint32_t)dest_apicid << 12);
    msg_addr_hi = 0;

    /* Mask MSI-X function while programming table entries. */
    mc |= (uint16_t)PCI_MSIX_CTRL_MASKALL;
    mc &= ~(uint16_t)PCI_MSIX_CTRL_ENABLE;
    pci_write16(dev, cap + 2, mc);

    for (int i = 0; i < grant; i++) {
        volatile uint32_t *entry =
            (volatile uint32_t *)((uint8_t *)table_base + table_off + i * 16);
        uint32_t vector = (uint32_t)(T_IRQ0 + dev->irq_vectors[i]);

        /* message address low/high, message data, vector control(masked). */
        entry[0] = msg_addr_low;
        entry[1] = msg_addr_hi;
        entry[2] = vector & 0xFF;
        entry[3] = 1;
    }

    /* Enable MSI-X globally, then unmask selected entries. */
    mc = pci_read16(dev, cap + 2);
    mc |= (uint16_t)PCI_MSIX_CTRL_ENABLE;
    mc &= ~(uint16_t)PCI_MSIX_CTRL_MASKALL;
    pci_write16(dev, cap + 2, mc);

    for (int i = 0; i < grant; i++) {
        volatile uint32_t *entry =
            (volatile uint32_t *)((uint8_t *)table_base + table_off + i * 16);
        entry[3] = 0;
    }

    pci_disable_interrupts(dev);
    dev->msix_cap_off = cap;
    dev->irq_mode = PCI_IRQ_MODE_MSIX;
    dev->irq_nvec = (uint8_t)grant;
    return grant;
}

static int
pci_try_enable_msi(struct pci_dev *dev, int min_vec, int max_vec, uint8_t dest_apicid)
{
    uint8_t cap;
    uint16_t mc;
    uint8_t data_off;
    uint32_t msg_addr_low;

    if (min_vec > 1)
        return -1;
    if (max_vec < 1)
        return -1;

    cap = dev->msi_cap_off;
    if (!cap)
        cap = (uint8_t)pci_find_capability(dev, PCI_CAP_ID_MSI);
    if (!cap)
        return -1;

    if (pci_alloc_irq_slots(dev->irq_vectors, 1) < 0)
        return -1;

    mc = pci_read16(dev, cap + 2);
    msg_addr_low = 0xFEE00000U | ((uint32_t)dest_apicid << 12);

    pci_write32(dev, cap + 4, msg_addr_low);
    data_off = (mc & PCI_MSI_CTRL_64BIT) ? 12 : 8;
    if (mc & PCI_MSI_CTRL_64BIT)
        pci_write32(dev, cap + 8, 0);

    pci_write16(dev, cap + data_off, (uint16_t)((T_IRQ0 + dev->irq_vectors[0]) & 0xFF));

    mc &= ~(uint16_t)PCI_MSI_CTRL_MME_MASK;
    mc |= (uint16_t)PCI_MSI_CTRL_ENABLE;
    pci_write16(dev, cap + 2, mc);

    pci_disable_interrupts(dev);
    dev->msi_cap_off = cap;
    dev->irq_mode = PCI_IRQ_MODE_MSI;
    dev->irq_nvec = 1;
    return 1;
}

int
pci_irq_alloc_vectors(struct pci_dev *dev, int min_vec, int max_vec, int flags)
{
    int ret;
    uint8_t dest_apicid;

    if (!dev || min_vec <= 0 || max_vec <= 0 || min_vec > max_vec)
        return -1;

    if (max_vec > PCI_IRQ_MAX_VECTORS)
        max_vec = PCI_IRQ_MAX_VECTORS;

    pci_irq_free_vectors(dev);
    dev->msi_cap_off = (uint8_t)pci_find_capability(dev, PCI_CAP_ID_MSI);
    dev->msix_cap_off = (uint8_t)pci_find_capability(dev, PCI_CAP_ID_MSIX);

    if (flags == 0)
        flags = PCI_IRQ_F_ALL;
    dest_apicid = pci_pick_dest_apicid(0);

    if (flags & PCI_IRQ_F_MSIX) {
        ret = pci_try_enable_msix(dev, min_vec, max_vec, dest_apicid);
        if (ret > 0)
            return ret;
    }

    if (flags & PCI_IRQ_F_MSI) {
        ret = pci_try_enable_msi(dev, min_vec, max_vec, dest_apicid);
        if (ret > 0)
            return ret;
    }

    if ((flags & PCI_IRQ_F_INTX) && min_vec <= 1 && dev->irq_line > 0 && dev->irq_line < 255) {
        dev->irq_mode = PCI_IRQ_MODE_INTX;
        dev->irq_nvec = 1;
        dev->irq_vectors[0] = dev->irq_line;
        pci_enable_interrupts(dev);
        return 1;
    }

    dev->irq_mode = PCI_IRQ_MODE_INTX;
    dev->irq_nvec = 0;
    return -1;
}

void
pci_irq_free_vectors(struct pci_dev *dev)
{
    if (!dev)
        return;

    if (dev->irq_mode == PCI_IRQ_MODE_MSIX && dev->msix_cap_off)
        pci_disable_msix_function(dev, dev->msix_cap_off);
    if (dev->irq_mode == PCI_IRQ_MODE_MSI && dev->msi_cap_off)
        pci_disable_msi_function(dev, dev->msi_cap_off);

    if ((dev->irq_mode == PCI_IRQ_MODE_MSIX || dev->irq_mode == PCI_IRQ_MODE_MSI) &&
        dev->irq_nvec > 0)
        pci_free_irq_slots(dev->irq_vectors, dev->irq_nvec);

    dev->irq_mode = PCI_IRQ_MODE_INTX;
    dev->irq_nvec = 1;
    dev->irq_vectors[0] = dev->irq_line;
    pci_enable_interrupts(dev);
}

int
pci_irq_vector(struct pci_dev *dev, int index)
{
    if (!dev || index < 0 || index >= dev->irq_nvec)
        return -1;
    return dev->irq_vectors[index];
}

int
pci_irq_mode(struct pci_dev *dev)
{
    if (!dev)
        return PCI_IRQ_MODE_INTX;
    return dev->irq_mode;
}

/*
 * Device lookup functions
 */
struct pci_dev *
pci_find_device(uint16_t vendor, uint16_t device)
{
    for (int i = 0; i < pci_ndevices; i++) {
        if (pci_devices[i].vendor_id == vendor &&
            pci_devices[i].device_id == device)
            return &pci_devices[i];
    }
    return 0;
}

struct pci_dev *
pci_find_class(uint8_t class_code, uint8_t subclass)
{
    for (int i = 0; i < pci_ndevices; i++) {
        if (pci_devices[i].class_code == class_code &&
            pci_devices[i].subclass == subclass)
            return &pci_devices[i];
    }
    return 0;
}

struct pci_dev *
pci_get_device(int index)
{
    if (index < 0 || index >= pci_ndevices)
        return 0;
    return &pci_devices[index];
}

int
pci_device_count(void)
{
    return pci_ndevices;
}

/*
 * Driver registration
 */
int
pci_register_driver(struct pci_driver *drv)
{
    if (pci_ndrivers >= PCI_MAX_DRIVERS)
        return -1;
    
    acquire(&pci_lock);
    pci_drivers[pci_ndrivers++] = drv;
    
    /* Probe existing devices */
    for (int i = 0; i < pci_ndevices; i++) {
        struct pci_dev *dev = &pci_devices[i];
        for (int j = 0; j < drv->id_table_len; j++) {
            const struct pci_device_id *id = &drv->id_table[j];
            int match = 1;
            
            if ((id->match_flags & PCI_MATCH_VENDOR) &&
                id->vendor_id != dev->vendor_id)
                match = 0;
            if ((id->match_flags & PCI_MATCH_DEVICE) &&
                id->device_id != dev->device_id)
                match = 0;
            if ((id->match_flags & PCI_MATCH_CLASS) &&
                id->class_code != dev->class_code)
                match = 0;
            if ((id->match_flags & PCI_MATCH_SUBCLASS) &&
                id->subclass != dev->subclass)
                match = 0;
            
            if (match && drv->probe) {
                drv->probe(dev);
                break;
            }
        }
    }
    
    release(&pci_lock);
    return 0;
}

void
pci_unregister_driver(struct pci_driver *drv)
{
    acquire(&pci_lock);
    for (int i = 0; i < pci_ndrivers; i++) {
        if (pci_drivers[i] == drv) {
            /* Call remove for all attached devices */
            /* TODO: Track which devices are attached to which driver */
            for (int j = i; j < pci_ndrivers - 1; j++)
                pci_drivers[j] = pci_drivers[j + 1];
            pci_ndrivers--;
            break;
        }
    }
    release(&pci_lock);
}

/*
 * Get interrupt line for device
 */
int
pci_get_irq(struct pci_dev *dev)
{
    if (dev->irq_nvec > 0)
        return dev->irq_vectors[0];
    return dev->irq_line;
}

/*
 * Enable interrupt for device via IOAPIC
 */
void
pci_enable_irq(struct pci_dev *dev, int cpu)
{
    if (!dev)
        return;
    if (dev->irq_mode != PCI_IRQ_MODE_INTX)
        return;
    if (dev->irq_line > 0 && dev->irq_line < 255) {
        ioapicenable(dev->irq_line, cpu);
    }
}

/*
 * Debug: dump all PCI devices
 */
void
pci_dump_devices(void)
{
    cprintf("PCI devices:\n");
    for (int i = 0; i < pci_ndevices; i++) {
        struct pci_dev *dev = &pci_devices[i];
        cprintf("  %d:%d.%d vendor=%x device=%x class=%x:%x irq=%d\n",
                dev->bus, dev->slot, dev->func,
                dev->vendor_id, dev->device_id,
                dev->class_code, dev->subclass,
                dev->irq_line);
    }
}

/*
 * Debug: error injection control for testing fallback paths
 */
void
pci_inject_alloc_failures(int enable)
{
    pci_inject_alloc_failure = enable;
    cprintf("pci: error injection %s\n", enable ? "enabled" : "disabled");
}

int
pci_get_inject_alloc_failures(void)
{
    return pci_inject_alloc_failure;
}

/* Helper: write hex nibble */
static char
hexchar(int n)
{
    return n < 10 ? '0' + n : 'a' + n - 10;
}

/*
 * Format PCI devices list for /proc/pci
 * Returns number of bytes written (up to maxlen)
 */
int
pci_format_devices(char *buf, int maxlen)
{
    int len = 0;
    
    for (int i = 0; i < pci_ndevices && len < maxlen - 32; i++) {
        struct pci_dev *dev = &pci_devices[i];
        
        /* Format: BB:SS.F VVVV:DDDD CC:SS IRQ\n */
        buf[len++] = hexchar(dev->bus >> 4);
        buf[len++] = hexchar(dev->bus & 0xF);
        buf[len++] = ':';
        buf[len++] = hexchar(dev->slot >> 4);
        buf[len++] = hexchar(dev->slot & 0xF);
        buf[len++] = '.';
        buf[len++] = '0' + dev->func;
        buf[len++] = ' ';
        
        buf[len++] = hexchar((dev->vendor_id >> 12) & 0xF);
        buf[len++] = hexchar((dev->vendor_id >> 8) & 0xF);
        buf[len++] = hexchar((dev->vendor_id >> 4) & 0xF);
        buf[len++] = hexchar(dev->vendor_id & 0xF);
        buf[len++] = ':';
        buf[len++] = hexchar((dev->device_id >> 12) & 0xF);
        buf[len++] = hexchar((dev->device_id >> 8) & 0xF);
        buf[len++] = hexchar((dev->device_id >> 4) & 0xF);
        buf[len++] = hexchar(dev->device_id & 0xF);
        buf[len++] = ' ';
        
        buf[len++] = hexchar(dev->class_code >> 4);
        buf[len++] = hexchar(dev->class_code & 0xF);
        buf[len++] = ':';
        buf[len++] = hexchar(dev->subclass >> 4);
        buf[len++] = hexchar(dev->subclass & 0xF);
        buf[len++] = ' ';
        
        /* IRQ (decimal), current active vector if allocated. */
        int irq = pci_get_irq(dev);
        if (irq >= 100) buf[len++] = '0' + (irq / 100);
        if (irq >= 10) buf[len++] = '0' + ((irq / 10) % 10);
        buf[len++] = '0' + (irq % 10);

        buf[len++] = ' ';
        if (dev->irq_mode == PCI_IRQ_MODE_MSIX)
            buf[len++] = 'X';
        else if (dev->irq_mode == PCI_IRQ_MODE_MSI)
            buf[len++] = 'M';
        else
            buf[len++] = 'I';

        buf[len++] = ':';
        if (dev->irq_nvec >= 10) buf[len++] = '0' + ((dev->irq_nvec / 10) % 10);
        buf[len++] = '0' + (dev->irq_nvec % 10);
        buf[len++] = '\n';
    }
    
    return len;
}

int
pci_irq_audit_format(char *buf, int maxlen)
{
    int len = 0;
    int used_count = 0;
    int total_available = (PCI_MSI_IRQ_LIMIT - PCI_MSI_IRQ_BASE);
    
    /* Header */
    const char *header = "PCI IRQ Vector Allocation Audit\n";
    int hlen = 0;
    for (const char *p = header; *p; p++) hlen++;
    if (len + hlen >= maxlen) return len;
    for (int i = 0; i < hlen; i++) buf[len++] = header[i];
    
    /* Count used vectors */
    acquire(&pci_irqvec_lock);
    for (int irq = PCI_MSI_IRQ_BASE; irq < PCI_MSI_IRQ_LIMIT; irq++) {
        if (pci_irqvec_bitmap[irq >> 5] & (1U << (irq & 31)))
            used_count++;
    }
    release(&pci_irqvec_lock);
    
    /* Summary line */
    const char *fmt = "Used: ";
    int flen = 0;
    for (const char *p = fmt; *p; p++) flen++;
    if (len + flen + 8 >= maxlen) return len;
    for (int i = 0; i < flen; i++) buf[len++] = fmt[i];
    
    /* Write used count */
    if (used_count >= 100) buf[len++] = '0' + (used_count / 100);
    if (used_count >= 10) buf[len++] = '0' + ((used_count / 10) % 10);
    buf[len++] = '0' + (used_count % 10);
    buf[len++] = '/';
    
    /* Write total available */
    int ta = total_available;
    if (ta >= 100) buf[len++] = '0' + (ta / 100);
    if (ta >= 10) buf[len++] = '0' + ((ta / 10) % 10);
    buf[len++] = '0' + (ta % 10);
    buf[len++] = '\n';
    
    /* Device list with allocated vectors */
    for (int i = 0; i < pci_ndevices && len < maxlen - 32; i++) {
        struct pci_dev *dev = &pci_devices[i];
        if (dev->irq_nvec == 0)
            continue;
        
        /* Format: "BB:SS.F vectors=N mode=X irq=I\n" */
        buf[len++] = hexchar(dev->bus >> 4);
        buf[len++] = hexchar(dev->bus & 0xF);
        buf[len++] = ':';
        buf[len++] = hexchar(dev->slot >> 4);
        buf[len++] = hexchar(dev->slot & 0xF);
        buf[len++] = '.';
        buf[len++] = '0' + dev->func;
        
        const char *vfmt = " vectors=";
        int vflen = 0;
        for (const char *p = vfmt; *p; p++) vflen++;
        for (int j = 0; j < vflen && len < maxlen - 16; j++)
            buf[len++] = vfmt[j];
        
        int nvec = dev->irq_nvec;
        if (nvec >= 10) buf[len++] = '0' + ((nvec / 10) % 10);
        buf[len++] = '0' + (nvec % 10);
        
        buf[len++] = ' ';
        buf[len++] = 'm';
        buf[len++] = 'o';
        buf[len++] = 'd';
        buf[len++] = 'e';
        buf[len++] = '=';
        if (dev->irq_mode == PCI_IRQ_MODE_MSIX)
            buf[len++] = 'X';
        else if (dev->irq_mode == PCI_IRQ_MODE_MSI)
            buf[len++] = 'M';
        else
            buf[len++] = 'I';
        
        buf[len++] = '\n';
    }
    
    return len;
}
