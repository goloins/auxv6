/*
 * Virtio Core Framework for auxv6
 *
 * Provides common virtio infrastructure for device drivers:
 * - PCI probe and initialization
 * - Feature negotiation
 * - Virtqueue setup and management
 * - Interrupt handling
 *
 * TODO Phase 1:
 * - [ ] Legacy interface support (I/O ports)
 * - [ ] Basic virtqueue create/destroy
 * - [ ] Buffer add/get operations
 * - [ ] Single-buffer scatter-gather
 *
 * TODO Phase 2:
 * - [ ] Modern interface support (capabilities)
 * - [ ] MSI-X interrupt support
 * - [ ] Multi-segment scatter-gather
 * - [ ] Indirect descriptors
 *
 * Reference: VIRTIO 1.1 Specification
 * See also: Linux drivers/virtio/virtio_pci.c
 */

#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "pci.h"
#include "virtio.h"

#define PCI_STATUS_CAP_LIST            0x0010
#define PCI_CAP_ID_VENDOR_SPECIFIC     0x09

#define VIRTIO_PCI_TRANSITIONAL_MIN    0x1000
#define VIRTIO_PCI_TRANSITIONAL_MAX    0x103F
#define VIRTIO_PCI_MODERN_MIN          0x1040
#define VIRTIO_PCI_MODERN_MAX          0x107F

#define VIRTIO_PCI_COMMON_DFSELECT     0x00
#define VIRTIO_PCI_COMMON_DF           0x04
#define VIRTIO_PCI_COMMON_GFSELECT     0x08
#define VIRTIO_PCI_COMMON_GF           0x0C
#define VIRTIO_PCI_COMMON_MSIX_CFG     0x10
#define VIRTIO_PCI_COMMON_NUM_QUEUES   0x12
#define VIRTIO_PCI_COMMON_STATUS       0x14
#define VIRTIO_PCI_COMMON_CFGGEN       0x15
#define VIRTIO_PCI_COMMON_Q_SELECT     0x16
#define VIRTIO_PCI_COMMON_Q_SIZE       0x18
#define VIRTIO_PCI_COMMON_Q_MSIX       0x1A
#define VIRTIO_PCI_COMMON_Q_ENABLE     0x1C
#define VIRTIO_PCI_COMMON_Q_NOTIFY_OFF 0x1E
#define VIRTIO_PCI_COMMON_Q_DESC       0x20
#define VIRTIO_PCI_COMMON_Q_DRIVER     0x28
#define VIRTIO_PCI_COMMON_Q_DEVICE     0x30

static int
virtio_is_modern(struct virtio_dev *vdev)
{
    return vdev && vdev->common_cfg != 0;
}

static uint8_t
virtio_mmio_read8(void *base, int offset)
{
    volatile uint8_t *ptr;

    ptr = (volatile uint8_t *)((char *)base + offset);
    return *ptr;
}

static uint16_t
virtio_mmio_read16(void *base, int offset)
{
    volatile uint16_t *ptr;

    ptr = (volatile uint16_t *)((char *)base + offset);
    return *ptr;
}

static uint32_t
virtio_mmio_read32(void *base, int offset)
{
    volatile uint32_t *ptr;

    ptr = (volatile uint32_t *)((char *)base + offset);
    return *ptr;
}

static void
virtio_mmio_write8(void *base, int offset, uint8_t val)
{
    volatile uint8_t *ptr;

    ptr = (volatile uint8_t *)((char *)base + offset);
    *ptr = val;
}

static void
virtio_mmio_write16(void *base, int offset, uint16_t val)
{
    volatile uint16_t *ptr;

    ptr = (volatile uint16_t *)((char *)base + offset);
    *ptr = val;
}

static void
virtio_mmio_write32(void *base, int offset, uint32_t val)
{
    volatile uint32_t *ptr;

    ptr = (volatile uint32_t *)((char *)base + offset);
    *ptr = val;
}

static void
virtio_mmio_write64(void *base, int offset, uint64_t val)
{
    virtio_mmio_write32(base, offset, (uint32_t)val);
    virtio_mmio_write32(base, offset + 4, (uint32_t)(val >> 32));
}

static uint8_t
virtio_pci_find_next_vendor_cap(struct pci_dev *pci, uint8_t pos)
{
    int guard;

    if(!pci)
        return 0;
    if(!(pci_read16(pci, PCI_STATUS) & PCI_STATUS_CAP_LIST))
        return 0;

    if(pos == 0)
        pos = pci_read8(pci, PCI_CAPABILITIES) & ~0x3;
    else
        pos = pci_read8(pci, pos + 1) & ~0x3;

    for(guard = 0; pos >= 0x40 && guard < 48; guard++) {
        if(pci_read8(pci, pos) == PCI_CAP_ID_VENDOR_SPECIFIC)
            return pos;
        if((pos = (pci_read8(pci, pos + 1) & ~0x3)) == 0)
            break;
    }

    return 0;
}

static int
virtio_pci_map_cap(struct pci_dev *pci, uint8_t cfg_type,
                   void **base_out, uint32_t *length_out,
                   uint32_t *extra_out)
{
    uint8_t pos;

    if(base_out)
        *base_out = 0;
    if(length_out)
        *length_out = 0;
    if(extra_out)
        *extra_out = 0;

    for(pos = 0; (pos = virtio_pci_find_next_vendor_cap(pci, pos)) != 0; ) {
        uint8_t cap_len;
        uint8_t type;
        uint8_t bar;
        uint32_t offset;
        uint32_t length;
        uint32_t bar_size;
        void *bar_base;

        cap_len = pci_read8(pci, pos + 2);
        type = pci_read8(pci, pos + 3);
        if(type != cfg_type)
            continue;
        if(cap_len < 16)
            return -1;

        bar = pci_read8(pci, pos + 4);
        if(bar >= 6)
            return -1;
        if(pci_bar_type(pci, bar) & PCI_BAR_IO)
            return -1;

        bar_base = pci_map_bar(pci, bar);
        if(!bar_base)
            return -1;

        offset = pci_read32(pci, pos + 8);
        length = pci_read32(pci, pos + 12);
        bar_size = pci_bar_size(pci, bar);
        if(bar_size != 0 && (uint64_t)offset + (uint64_t)length > (uint64_t)bar_size)
            return -1;

        if(base_out)
            *base_out = (void *)((char *)bar_base + offset);
        if(length_out)
            *length_out = length;
        if(cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG && extra_out) {
            if(cap_len < 20)
                return -1;
            *extra_out = pci_read32(pci, pos + 16);
        }
        return 0;
    }

    return -1;
}

static int
virtio_probe_modern_pci(struct pci_dev *pci, struct virtio_dev *vdev)
{
    uint32_t cfg_len;

    pci_enable_mem(pci);
    pci_set_master(pci);

    if(virtio_pci_map_cap(pci, VIRTIO_PCI_CAP_COMMON_CFG,
                          &vdev->common_cfg, &cfg_len, 0) < 0 ||
       !vdev->common_cfg || cfg_len < VIRTIO_PCI_COMMON_Q_DEVICE + 8) {
        cprintf("virtio: missing modern common cfg\n");
        return -1;
    }
    if(virtio_pci_map_cap(pci, VIRTIO_PCI_CAP_NOTIFY_CFG,
                          &vdev->notify_base, &cfg_len,
                          &vdev->notify_off_multiplier) < 0 ||
       !vdev->notify_base || cfg_len < 2) {
        cprintf("virtio: missing modern notify cfg\n");
        return -1;
    }
    if(virtio_pci_map_cap(pci, VIRTIO_PCI_CAP_ISR_CFG,
                          &vdev->isr_cfg, &cfg_len, 0) < 0 ||
       !vdev->isr_cfg || cfg_len < 1) {
        cprintf("virtio: missing modern ISR cfg\n");
        return -1;
    }
    (void)virtio_pci_map_cap(pci, VIRTIO_PCI_CAP_DEVICE_CFG,
                             &vdev->device_cfg, &cfg_len, 0);

    vdev->irq = pci->irq_line;

    virtio_reset(vdev);
    if(vdev->status & VIRTIO_STATUS_FAILED)
        return -1;

    BOOTDBG("virtio: found modern device type %d at %d:%d.%d irq=%d\n",
            vdev->device_id, pci->bus, pci->slot, pci->func, vdev->irq);

    return 0;
}

/*
 * Read from virtio legacy I/O port configuration
 */
static uint8_t
virtio_ioread8(struct virtio_dev *vdev, int offset)
{
    return inb(vdev->iobase + offset);
}

static uint16_t
virtio_ioread16(struct virtio_dev *vdev, int offset)
{
    return inw(vdev->iobase + offset);
}

static uint32_t
virtio_ioread32(struct virtio_dev *vdev, int offset)
{
    return inl(vdev->iobase + offset);
}

static void
virtio_iowrite8(struct virtio_dev *vdev, int offset, uint8_t val)
{
    outb(vdev->iobase + offset, val);
}

static void
virtio_iowrite16(struct virtio_dev *vdev, int offset, uint16_t val)
{
    outw(vdev->iobase + offset, val);
}

static void
virtio_iowrite32(struct virtio_dev *vdev, int offset, uint32_t val)
{
    outl(vdev->iobase + offset, val);
}

static char *
virtio_alloc_contiguous_pages(int pages, uint32_t *phys_out)
{
    char *base;
    int attempt;

    if (pages <= 0)
        return 0;

    for (attempt = 0; attempt < 64; attempt++) {
        char *allocs[8];
        uint32_t paddrs[8];
        int i;
        int ok;

        if (pages > (int)(sizeof(allocs) / sizeof(allocs[0])))
            return 0;

        for (i = 0; i < pages; i++) {
            allocs[i] = kalloc();
            if (!allocs[i]) {
                while (--i >= 0)
                    kfree(allocs[i]);
                return 0;
            }
            memset(allocs[i], 0, PGSIZE);
            paddrs[i] = V2P(allocs[i]);
        }

        ok = 1;
        for (i = 1; i < pages; i++) {
            if (paddrs[i] != paddrs[0] + i * PGSIZE) {
                ok = 0;
                break;
            }
        }

        if (ok) {
            base = allocs[0];
            if (phys_out)
                *phys_out = paddrs[0];
            return base;
        }

        for (i = 0; i < pages; i++)
            kfree(allocs[i]);
    }

    return 0;
}

/*
 * Calculate virtqueue size based on queue size
 * Returns total bytes needed for desc + avail + used rings
 */
static uint32_t
virtq_size_bytes(int qsize)
{
    uint32_t desc_size = sizeof(struct virtq_desc) * qsize;
    uint32_t avail_size = sizeof(struct virtq_avail) + sizeof(uint16_t) * qsize + 2;
    uint32_t used_size = sizeof(struct virtq_used) + sizeof(struct virtq_used_elem) * qsize + 2;
    
    /* Align avail to 2, used to 4096 */
    uint32_t size = desc_size;
    size = (size + 1) & ~1;  /* Align to 2 for avail */
    size += avail_size;
    size = (size + 4095) & ~4095;  /* Align to page for used */
    size += used_size;
    
    return size;
}

/*
 * Probe a PCI device for virtio support
 * Initializes vdev structure if successful
 */
int
virtio_probe_pci(struct pci_dev *pci, struct virtio_dev *vdev)
{
    int is_legacy;

    if (!pci || !vdev)
        return -1;
    
    memset(vdev, 0, sizeof(*vdev));
    vdev->pci = pci;
    
    /* Check for virtio vendor ID */
    if (pci->vendor_id != PCI_VENDOR_VIRTIO)
        return -1;

    is_legacy = 0;
    
    /* Map PCI device ID to virtio device type. */
    /* Transitional devices use 0x1000 + virtio device ID - 1. */
    if (pci->device_id >= VIRTIO_PCI_TRANSITIONAL_MIN &&
        pci->device_id <= VIRTIO_PCI_TRANSITIONAL_MAX) {
        vdev->device_id = pci->device_id - 0x0FFF;
        is_legacy = 1;
    } else if (pci->device_id >= VIRTIO_PCI_MODERN_MIN &&
               pci->device_id <= VIRTIO_PCI_MODERN_MAX) {
        vdev->device_id = pci->device_id - VIRTIO_PCI_MODERN_MIN;
        return virtio_probe_modern_pci(pci, vdev);
    } else {
        vdev->device_id = pci_config_read16(pci->bus, pci->slot, pci->func, 0x2E);
    }

    if(!is_legacy && (pci_bar_type(pci, 0) & PCI_BAR_IO) == 0)
        return virtio_probe_modern_pci(pci, vdev);
    
    /* Get I/O base from BAR0 (legacy interface) */
    vdev->iobase = pci_bar_base(pci, 0) & 0xFFFF;
    if (vdev->iobase == 0) {
        cprintf("virtio: no I/O base in BAR0\n");
        return -1;
    }
    
    /* Enable I/O and bus master */
    pci_enable_io(pci);
    pci_set_master(pci);

    /* Legacy virtio requires the guest page size before queue setup. */
    virtio_iowrite32(vdev, VIRTIO_PCI_GUEST_PAGE_SIZE, PGSIZE);
    
    /* Read IRQ */
    vdev->irq = pci->irq_line;
    
    /* Reset device */
    virtio_reset(vdev);
    
    BOOTDBG("virtio: found device type %d at %d:%d.%d io=0x%x irq=%d\n",
            vdev->device_id, pci->bus, pci->slot, pci->func,
            vdev->iobase, vdev->irq);
    
    return 0;
}

/*
 * Reset virtio device to initial state
 */
void
virtio_reset(struct virtio_dev *vdev)
{
    int tries;

    /* Writing 0 to status resets the device */
    if(virtio_is_modern(vdev))
        virtio_mmio_write8(vdev->common_cfg, VIRTIO_PCI_COMMON_STATUS, 0);
    else
        virtio_iowrite8(vdev, VIRTIO_PCI_STATUS, 0);
    
    /* Wait for reset to complete by reading status (should be 0) */
    if(virtio_is_modern(vdev)) {
        for(tries = 0; tries < 1000000; tries++) {
            if(virtio_mmio_read8(vdev->common_cfg, VIRTIO_PCI_COMMON_STATUS) == 0)
                break;
        }
        if(tries == 1000000) {
            cprintf("virtio: modern reset timeout at %d:%d.%d status=%x\n",
                    vdev->pci ? vdev->pci->bus : 0,
                    vdev->pci ? vdev->pci->slot : 0,
                    vdev->pci ? vdev->pci->func : 0,
                    virtio_mmio_read8(vdev->common_cfg, VIRTIO_PCI_COMMON_STATUS));
            vdev->status = VIRTIO_STATUS_FAILED;
            vdev->features = 0;
            return;
        }
    } else {
        while (virtio_ioread8(vdev, VIRTIO_PCI_STATUS) != 0)
            ;
    }
    
    vdev->status = 0;
    vdev->features = 0;
}

/*
 * Set device status bits
 */
void
virtio_set_status(struct virtio_dev *vdev, uint8_t status)
{
    vdev->status |= status;
    if(virtio_is_modern(vdev))
        virtio_mmio_write8(vdev->common_cfg, VIRTIO_PCI_COMMON_STATUS, vdev->status);
    else
        virtio_iowrite8(vdev, VIRTIO_PCI_STATUS, vdev->status);
}

/*
 * Negotiate device features
 * Returns 0 on success, -1 if required features not available
 */
int
virtio_negotiate_features(struct virtio_dev *vdev, uint64_t requested)
{
    uint64_t offered;

    if(virtio_is_modern(vdev)) {
        requested |= VIRTIO_F_VERSION_1;

        virtio_mmio_write32(vdev->common_cfg, VIRTIO_PCI_COMMON_DFSELECT, 0);
        offered = virtio_mmio_read32(vdev->common_cfg, VIRTIO_PCI_COMMON_DF);
        virtio_mmio_write32(vdev->common_cfg, VIRTIO_PCI_COMMON_DFSELECT, 1);
        offered |= ((uint64_t)virtio_mmio_read32(vdev->common_cfg,
                                                 VIRTIO_PCI_COMMON_DF)) << 32;
    } else {
        /* Read device-offered features */
        offered = virtio_ioread32(vdev, VIRTIO_PCI_HOST_FEATURES);
    }
    
    /* Intersect with what we want */
    vdev->features = offered & requested;

    if(virtio_is_modern(vdev) && !(vdev->features & VIRTIO_F_VERSION_1)) {
        cprintf("virtio: modern device missing VERSION_1\n");
        return -1;
    }
    
    /* Write back negotiated features */
    if(virtio_is_modern(vdev)) {
        virtio_mmio_write32(vdev->common_cfg, VIRTIO_PCI_COMMON_GFSELECT, 0);
        virtio_mmio_write32(vdev->common_cfg, VIRTIO_PCI_COMMON_GF,
                            (uint32_t)vdev->features);
        virtio_mmio_write32(vdev->common_cfg, VIRTIO_PCI_COMMON_GFSELECT, 1);
        virtio_mmio_write32(vdev->common_cfg, VIRTIO_PCI_COMMON_GF,
                            (uint32_t)(vdev->features >> 32));
    } else {
        virtio_iowrite32(vdev, VIRTIO_PCI_GUEST_FEATURES, (uint32_t)vdev->features);
    }
    
    return 0;
}

/*
 * Finalize feature negotiation
 */
int
virtio_finalize_features(struct virtio_dev *vdev)
{
    uint8_t status;

    /* Set FEATURES_OK status */
    virtio_set_status(vdev, VIRTIO_STATUS_FEATURES_OK);
    
    /* Re-read status to confirm device accepted features */
    if(virtio_is_modern(vdev))
        status = virtio_mmio_read8(vdev->common_cfg, VIRTIO_PCI_COMMON_STATUS);
    else
        status = virtio_ioread8(vdev, VIRTIO_PCI_STATUS);
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        cprintf("virtio: device rejected features\n");
        return -1;
    }
    
    return 0;
}

/*
 * Create a virtqueue
 */
struct virtqueue *
virtq_create(struct virtio_dev *vdev, int index, int size)
{
    int modern;
    uint16_t max_size;

    modern = virtio_is_modern(vdev);

    /* Select the queue */
    if(modern)
        virtio_mmio_write16(vdev->common_cfg, VIRTIO_PCI_COMMON_Q_SELECT, index);
    else
        virtio_iowrite16(vdev, VIRTIO_PCI_QUEUE_SEL, index);
    
    /* Get maximum queue size */
    if(modern)
        max_size = virtio_mmio_read16(vdev->common_cfg, VIRTIO_PCI_COMMON_Q_SIZE);
    else
        max_size = virtio_ioread16(vdev, VIRTIO_PCI_QUEUE_SIZE);
    if (max_size == 0) {
        cprintf("virtio: queue %d not available\n", index);
        return 0;
    }
    
    if (size == 0 || size > max_size)
        size = max_size;
    
    /* Power of 2 check */
    if (size & (size - 1)) {
        /* Round down to power of 2 */
        int n = 0;
        while ((1 << n) <= size) n++;
        size = 1 << (n - 1);
    }
    
    /* Allocate virtqueue structure */
    struct virtqueue *vq = (struct virtqueue *)kalloc();
    if (!vq)
        return 0;
    memset(vq, 0, sizeof(*vq));
    
    vq->index = index;
    vq->size = size;
    vq->vdev = vdev;
    
    /* Allocate virtqueue rings (must be physically contiguous) */
    /* TODO: Use proper DMA allocation when available */
    uint32_t ring_bytes = virtq_size_bytes(size);
    int pages_needed = (ring_bytes + PGSIZE - 1) / PGSIZE;
    
    uint32_t ring_paddr = 0;
    char *ring_mem = virtio_alloc_contiguous_pages(pages_needed, &ring_paddr);
    if (!ring_mem) {
        cprintf("virtio: failed to allocate %d contiguous queue pages\n", pages_needed);
        kfree((char *)vq);
        return 0;
    }

    vq->ring_mem = ring_mem;
    vq->ring_paddr = ring_paddr;
    vq->ring_pages = pages_needed;
    
    /* Set up ring pointers */
    vq->desc = (struct virtq_desc *)ring_mem;
    vq->avail = (struct virtq_avail *)(ring_mem + sizeof(struct virtq_desc) * size);
    
    /* Used ring starts at page boundary after avail */
    uint32_t used_offset = sizeof(struct virtq_desc) * size;
    used_offset = (used_offset + sizeof(struct virtq_avail) + sizeof(uint16_t) * size + 2);
    used_offset = (used_offset + 4095) & ~4095;
    vq->used = (struct virtq_used *)(ring_mem + used_offset);
    
    /* Initialize free list (chain all descriptors) */
    vq->free_head = 0;
    vq->num_free = size;
    for (int i = 0; i < size - 1; i++) {
        vq->desc[i].next = i + 1;
    }
    vq->desc[size - 1].next = 0xFFFF;  /* End of list */
    
    /* Allocate per-descriptor state */
    vq->desc_state = (void **)kalloc();
    if (!vq->desc_state) {
        /* Cleanup */
        kfree(ring_mem);
        kfree((char *)vq);
        return 0;
    }
    memset(vq->desc_state, 0, PGSIZE);
    
    if(modern) {
        uint16_t notify_off;
        uint32_t notify_addr_off;
        uint64_t desc_paddr;
        uint64_t avail_paddr;
        uint64_t used_paddr;

        desc_paddr = (uint64_t)ring_paddr;
        avail_paddr = (uint64_t)(ring_paddr + ((char *)vq->avail - ring_mem));
        used_paddr = (uint64_t)(ring_paddr + ((char *)vq->used - ring_mem));

        virtio_mmio_write16(vdev->common_cfg, VIRTIO_PCI_COMMON_Q_SIZE, size);
        notify_off = virtio_mmio_read16(vdev->common_cfg,
                                        VIRTIO_PCI_COMMON_Q_NOTIFY_OFF);
        virtio_mmio_write64(vdev->common_cfg, VIRTIO_PCI_COMMON_Q_DESC, desc_paddr);
        virtio_mmio_write64(vdev->common_cfg, VIRTIO_PCI_COMMON_Q_DRIVER, avail_paddr);
        virtio_mmio_write64(vdev->common_cfg, VIRTIO_PCI_COMMON_Q_DEVICE, used_paddr);
        virtio_mmio_write16(vdev->common_cfg, VIRTIO_PCI_COMMON_Q_ENABLE, 1);

        notify_addr_off = (uint32_t)notify_off * vdev->notify_off_multiplier;
        vq->notify = (volatile uint16_t *)((char *)vdev->notify_base + notify_addr_off);
    } else {
        /* Tell device about the queue (legacy: PFN = page frame number) */
        virtio_iowrite32(vdev, VIRTIO_PCI_QUEUE_PFN, ring_paddr / PGSIZE);
    }
    
    /* Store queue in device */
    if (index < 16)
        vdev->vqs[index] = vq;
    if (index >= vdev->nvqs)
        vdev->nvqs = index + 1;
    
    BOOTDBG("virtio: created queue %d with %d entries\n", index, size);
    
    return vq;
}

int
virtq_set_vector(struct virtio_dev *vdev, int index, int vector_index)
{
    uint16_t vec;

    if (!vdev || !vdev->common_cfg || !vdev->pci || index < 0)
        return -1;

    if (pci_irq_mode(vdev->pci) != PCI_IRQ_MODE_MSIX)
        return 0;

    if (vector_index < 0)
        vec = 0xFFFF;
    else if (vector_index > 0x7FFF)
        return -1;
    else
        vec = (uint16_t)vector_index;

    virtio_mmio_write16(vdev->common_cfg, VIRTIO_PCI_COMMON_Q_SELECT, (uint16_t)index);
    if (virtio_mmio_read16(vdev->common_cfg, VIRTIO_PCI_COMMON_Q_SIZE) == 0)
        return -1;

    virtio_mmio_write16(vdev->common_cfg, VIRTIO_PCI_COMMON_Q_MSIX, vec);
    if (virtio_mmio_read16(vdev->common_cfg, VIRTIO_PCI_COMMON_Q_MSIX) != vec)
        return -1;

    return 0;
}

/*
 * Destroy a virtqueue
 */
void
virtq_destroy(struct virtqueue *vq)
{
    if (!vq)
        return;
    
    if(virtio_is_modern(vq->vdev)) {
        if(vq->index < 16 && vq->vdev->vqs[vq->index] == vq)
            vq->vdev->vqs[vq->index] = 0;
    } else {
        /* Tell device queue is gone */
        virtio_iowrite16(vq->vdev, VIRTIO_PCI_QUEUE_SEL, vq->index);
        virtio_iowrite32(vq->vdev, VIRTIO_PCI_QUEUE_PFN, 0);
    }
    
    /* Free memory */
    if (vq->ring_mem) {
        for (int i = 0; i < vq->ring_pages; i++)
            kfree(vq->ring_mem + i * PGSIZE);
    }
    if (vq->desc_state)
        kfree((char *)vq->desc_state);
    
    kfree((char *)vq);
}

/*
 * Add buffers to virtqueue
 * bufs/lens: arrays of buffer addresses and lengths
 * out_num: number of device-readable (output) buffers
 * in_num: number of device-writable (input) buffers
 * data: cookie to return when buffer is used
 * Returns 0 on success, -1 if no room
 */
int
virtq_add_buf(struct virtqueue *vq, void **bufs, uint32_t *lens,
              int out_num, int in_num, void *data)
{
    int total = out_num + in_num;
    
    if (vq->num_free < total)
        return -1;
    
    /* Get first free descriptor */
    uint16_t head = vq->free_head;
    uint16_t i = head;
    
    /* Fill in descriptors */
    for (int n = 0; n < total; n++) {
        vq->desc[i].addr = V2P(bufs[n]);
        vq->desc[i].len = lens[n];
        vq->desc[i].flags = 0;
        
        if (n >= out_num)
            vq->desc[i].flags |= VIRTQ_DESC_F_WRITE;
        
        if (n < total - 1) {
            vq->desc[i].flags |= VIRTQ_DESC_F_NEXT;
            i = vq->desc[i].next;
        }
    }
    
    /* Update free list head */
    vq->free_head = vq->desc[i].next;
    vq->num_free -= total;
    
    /* Store data cookie at head */
    vq->desc_state[head] = data;
    
    /* Add to available ring */
    uint16_t avail_idx = vq->avail->idx;
    vq->avail->ring[avail_idx % vq->size] = head;
    
    /* Memory barrier */
    __sync_synchronize();
    
    vq->avail->idx = avail_idx + 1;
    
    return 0;
}

/*
 * Get a used buffer from the virtqueue
 * Returns data cookie, and optionally the length written
 */
void *
virtq_get_buf(struct virtqueue *vq, uint32_t *len)
{
    /* Memory barrier */
    __sync_synchronize();
    
    /* Check if there's anything new */
    if (vq->last_used_idx == vq->used->idx)
        return 0;
    
    /* Get the used element */
    struct virtq_used_elem *e = &vq->used->ring[vq->last_used_idx % vq->size];
    uint16_t head = e->id;
    
    if (len)
        *len = e->len;
    
    /* Return descriptors to free list */
    uint16_t i = head;
    int count = 0;
    while (1) {
        count++;
        uint16_t next = vq->desc[i].next;
        if (!(vq->desc[i].flags & VIRTQ_DESC_F_NEXT))
            break;
        i = next;
    }
    
    /* Chain freed descriptors */
    vq->desc[i].next = vq->free_head;
    vq->free_head = head;
    vq->num_free += count;
    
    /* Get data cookie */
    void *data = vq->desc_state[head];
    vq->desc_state[head] = 0;
    
    vq->last_used_idx++;

    /* Ensure device DMA writes are visible before driver consumes data. */
    __sync_synchronize();
    
    return data;
}

/*
 * Notify device that there's new data in the queue
 */
void
virtq_kick(struct virtqueue *vq)
{
    /* Memory barrier */
    __sync_synchronize();
    
    /* Check if notification is needed */
    /* TODO: Implement event index checking for efficiency */

    if(virtio_is_modern(vq->vdev)) {
        if(vq->notify)
            *vq->notify = vq->index;
    } else {
        virtio_iowrite16(vq->vdev, VIRTIO_PCI_QUEUE_NOTIFY, vq->index);
    }
}

/*
 * Disable interrupts for this queue
 */
void
virtq_disable_interrupts(struct virtqueue *vq)
{
    vq->avail->flags |= VIRTQ_AVAIL_F_NO_INTERRUPT;
}

/*
 * Enable interrupts for this queue
 */
void
virtq_enable_interrupts(struct virtqueue *vq)
{
    vq->avail->flags &= ~VIRTQ_AVAIL_F_NO_INTERRUPT;
}

/*
 * Get number of free descriptors
 */
int
virtq_num_free(struct virtqueue *vq)
{
    return vq->num_free;
}

/*
 * Handle virtio interrupt
 */
void
virtio_handle_interrupt(struct virtio_dev *vdev)
{
    uint8_t isr;

    /* Read and clear ISR status */
    if(virtio_is_modern(vdev)) {
        if(!vdev->isr_cfg)
            return;
        isr = virtio_mmio_read8(vdev->isr_cfg, 0);
    } else {
        isr = virtio_ioread8(vdev, VIRTIO_PCI_ISR);
    }
    
    if (isr & 0x01) {
        /* Used buffer notification - process all queues */
        for (int i = 0; i < vdev->nvqs; i++) {
            if (vdev->vqs[i] && vdev->isr_handler) {
                /* Let driver-specific handler process */
            }
        }
    }
    
    if (isr & 0x02) {
        /* Configuration change - device config has changed */
        /* TODO: Notify driver */
    }
    
    if (vdev->isr_handler)
        vdev->isr_handler(vdev);
}

/*
 * Device configuration space access (legacy)
 */
uint8_t
virtio_config_read8(struct virtio_dev *vdev, int offset)
{
    if(virtio_is_modern(vdev)) {
        if(!vdev->device_cfg)
            return 0;
        return virtio_mmio_read8(vdev->device_cfg, offset);
    }
    return virtio_ioread8(vdev, VIRTIO_PCI_CONFIG + offset);
}

uint16_t
virtio_config_read16(struct virtio_dev *vdev, int offset)
{
    if(virtio_is_modern(vdev)) {
        if(!vdev->device_cfg)
            return 0;
        return virtio_mmio_read16(vdev->device_cfg, offset);
    }
    return virtio_ioread16(vdev, VIRTIO_PCI_CONFIG + offset);
}

uint32_t
virtio_config_read32(struct virtio_dev *vdev, int offset)
{
    if(virtio_is_modern(vdev)) {
        if(!vdev->device_cfg)
            return 0;
        return virtio_mmio_read32(vdev->device_cfg, offset);
    }
    return virtio_ioread32(vdev, VIRTIO_PCI_CONFIG + offset);
}

void
virtio_config_write8(struct virtio_dev *vdev, int offset, uint8_t val)
{
    if(virtio_is_modern(vdev)) {
        if(vdev->device_cfg)
            virtio_mmio_write8(vdev->device_cfg, offset, val);
        return;
    }
    virtio_iowrite8(vdev, VIRTIO_PCI_CONFIG + offset, val);
}

void
virtio_config_write16(struct virtio_dev *vdev, int offset, uint16_t val)
{
    if(virtio_is_modern(vdev)) {
        if(vdev->device_cfg)
            virtio_mmio_write16(vdev->device_cfg, offset, val);
        return;
    }
    virtio_iowrite16(vdev, VIRTIO_PCI_CONFIG + offset, val);
}

void
virtio_config_write32(struct virtio_dev *vdev, int offset, uint32_t val)
{
    if(virtio_is_modern(vdev)) {
        if(vdev->device_cfg)
            virtio_mmio_write32(vdev->device_cfg, offset, val);
        return;
    }
    virtio_iowrite32(vdev, VIRTIO_PCI_CONFIG + offset, val);
}

/*
 * Debug: dump virtio device info
 */
void
virtio_dump_device(struct virtio_dev *vdev)
{
    cprintf("virtio device type=%d status=0x%x features=0x%x\n",
            vdev->device_id, vdev->status, (uint32_t)vdev->features);
    if(virtio_is_modern(vdev))
    cprintf("  PCI: %d:%d.%d modern irq=%d\n",
        vdev->pci->bus, vdev->pci->slot, vdev->pci->func,
        vdev->irq);
    else
    cprintf("  PCI: %d:%d.%d iobase=0x%x irq=%d\n",
        vdev->pci->bus, vdev->pci->slot, vdev->pci->func,
        vdev->iobase, vdev->irq);
    cprintf("  queues: %d\n", vdev->nvqs);
    for (int i = 0; i < vdev->nvqs; i++) {
        if (vdev->vqs[i]) {
            cprintf("    vq[%d]: size=%d free=%d\n",
                    i, vdev->vqs[i]->size, vdev->vqs[i]->num_free);
        }
    }
}
