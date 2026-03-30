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

static struct spinlock virtio_lock;

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
    if (!pci || !vdev)
        return -1;
    
    memset(vdev, 0, sizeof(*vdev));
    vdev->pci = pci;
    
    /* Check for virtio vendor ID */
    if (pci->vendor_id != PCI_VENDOR_VIRTIO)
        return -1;
    
    /* Map device ID to virtio device type */
    /* Transitional devices: Device ID 0x1000-0x103F maps to type 0x00-0x3F */
    if (pci->device_id >= 0x1000 && pci->device_id <= 0x103F) {
        vdev->device_id = pci->device_id - 0x1000;
    } else {
        /* Modern devices use subsystem ID */
        vdev->device_id = pci_config_read16(pci->bus, pci->slot, pci->func, 0x2E);
    }
    
    /* Get I/O base from BAR0 (legacy interface) */
    vdev->iobase = pci_bar_base(pci, 0) & 0xFFFF;
    if (vdev->iobase == 0) {
        cprintf("virtio: no I/O base in BAR0\n");
        return -1;
    }
    
    /* Enable I/O and bus master */
    pci_enable_io(pci);
    pci_set_master(pci);
    
    /* Read IRQ */
    vdev->irq = pci->irq_line;
    
    /* Reset device */
    virtio_reset(vdev);
    
    cprintf("virtio: found device type %d at %d:%d.%d io=0x%x irq=%d\n",
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
    /* Writing 0 to status resets the device */
    virtio_iowrite8(vdev, VIRTIO_PCI_STATUS, 0);
    
    /* Wait for reset to complete by reading status (should be 0) */
    while (virtio_ioread8(vdev, VIRTIO_PCI_STATUS) != 0)
        ;
    
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
    virtio_iowrite8(vdev, VIRTIO_PCI_STATUS, vdev->status);
}

/*
 * Negotiate device features
 * Returns 0 on success, -1 if required features not available
 */
int
virtio_negotiate_features(struct virtio_dev *vdev, uint64_t requested)
{
    /* Read device-offered features */
    uint64_t offered = virtio_ioread32(vdev, VIRTIO_PCI_HOST_FEATURES);
    
    /* Intersect with what we want */
    vdev->features = offered & requested;
    
    /* Write back negotiated features */
    virtio_iowrite32(vdev, VIRTIO_PCI_GUEST_FEATURES, (uint32_t)vdev->features);
    
    return 0;
}

/*
 * Finalize feature negotiation
 */
int
virtio_finalize_features(struct virtio_dev *vdev)
{
    /* Set FEATURES_OK status */
    virtio_set_status(vdev, VIRTIO_STATUS_FEATURES_OK);
    
    /* Re-read status to confirm device accepted features */
    uint8_t status = virtio_ioread8(vdev, VIRTIO_PCI_STATUS);
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
    /* Select the queue */
    virtio_iowrite16(vdev, VIRTIO_PCI_QUEUE_SEL, index);
    
    /* Get maximum queue size */
    uint16_t max_size = virtio_ioread16(vdev, VIRTIO_PCI_QUEUE_SIZE);
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
    
    char *ring_mem = 0;
    for (int i = 0; i < pages_needed; i++) {
        char *p = kalloc();
        if (!p) {
            /* Cleanup already allocated pages */
            /* TODO: Proper cleanup */
            kfree((char *)vq);
            return 0;
        }
        memset(p, 0, PGSIZE);
        if (i == 0)
            ring_mem = p;
    }
    
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
    
    /* Tell device about the queue (legacy: PFN = page frame number) */
    virtio_iowrite32(vdev, VIRTIO_PCI_QUEUE_PFN, V2P(ring_mem) / PGSIZE);
    
    /* Store queue in device */
    if (index < 16)
        vdev->vqs[index] = vq;
    if (index >= vdev->nvqs)
        vdev->nvqs = index + 1;
    
    cprintf("virtio: created queue %d with %d entries\n", index, size);
    
    return vq;
}

/*
 * Destroy a virtqueue
 */
void
virtq_destroy(struct virtqueue *vq)
{
    if (!vq)
        return;
    
    /* Tell device queue is gone */
    virtio_iowrite16(vq->vdev, VIRTIO_PCI_QUEUE_SEL, vq->index);
    virtio_iowrite32(vq->vdev, VIRTIO_PCI_QUEUE_PFN, 0);
    
    /* Free memory */
    if (vq->desc)
        kfree((char *)vq->desc);
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
    
    virtio_iowrite16(vq->vdev, VIRTIO_PCI_QUEUE_NOTIFY, vq->index);
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
    /* Read and clear ISR status */
    uint8_t isr = virtio_ioread8(vdev, VIRTIO_PCI_ISR);
    
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
    return virtio_ioread8(vdev, VIRTIO_PCI_CONFIG + offset);
}

uint16_t
virtio_config_read16(struct virtio_dev *vdev, int offset)
{
    return virtio_ioread16(vdev, VIRTIO_PCI_CONFIG + offset);
}

uint32_t
virtio_config_read32(struct virtio_dev *vdev, int offset)
{
    return virtio_ioread32(vdev, VIRTIO_PCI_CONFIG + offset);
}

void
virtio_config_write8(struct virtio_dev *vdev, int offset, uint8_t val)
{
    virtio_iowrite8(vdev, VIRTIO_PCI_CONFIG + offset, val);
}

void
virtio_config_write16(struct virtio_dev *vdev, int offset, uint16_t val)
{
    virtio_iowrite16(vdev, VIRTIO_PCI_CONFIG + offset, val);
}

void
virtio_config_write32(struct virtio_dev *vdev, int offset, uint32_t val)
{
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
