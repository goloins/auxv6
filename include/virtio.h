/*
 * Virtio Device Framework Header
 *
 * Implements the virtio specification for paravirtualized device drivers.
 * Supports both legacy (0.9.5) and modern (1.0+) virtio interfaces.
 *
 * Virtio devices communicate through:
 * - Configuration space (device-specific)
 * - Virtqueues (ring buffers for I/O)
 * - Status/feature negotiation
 *
 * Reference: Virtual I/O Device (VIRTIO) Version 1.1
 * See also: Linux drivers/virtio/, NetBSD sys/dev/virtio/
 */

#ifndef _VIRTIO_H_
#define _VIRTIO_H_

#include "types.h"
#include "stddef.h"

/*
 * Virtio Device IDs (PCI subsystem ID for transitional devices)
 */
#define VIRTIO_DEV_NET         1   /* Network card */
#define VIRTIO_DEV_BLK         2   /* Block device */
#define VIRTIO_DEV_CONSOLE     3   /* Console */
#define VIRTIO_DEV_ENTROPY     4   /* Entropy source (RNG) */
#define VIRTIO_DEV_BALLOON     5   /* Memory balloon */
#define VIRTIO_DEV_IOMEM       6   /* ioMemory */
#define VIRTIO_DEV_RPMSG       7   /* rpmsg */
#define VIRTIO_DEV_SCSI        8   /* SCSI host */
#define VIRTIO_DEV_9P          9   /* 9P transport */
#define VIRTIO_DEV_WWAN        10  /* mac80211 wlan */
#define VIRTIO_DEV_GPU         16  /* GPU device */
#define VIRTIO_DEV_INPUT       18  /* Input device */
#define VIRTIO_DEV_VSOCK       19  /* Vsock transport */
#define VIRTIO_DEV_FS          26  /* File system */

/*
 * Virtio PCI Capability Types (modern interface)
 */
#define VIRTIO_PCI_CAP_COMMON_CFG    1   /* Common configuration */
#define VIRTIO_PCI_CAP_NOTIFY_CFG    2   /* Notifications */
#define VIRTIO_PCI_CAP_ISR_CFG       3   /* ISR status */
#define VIRTIO_PCI_CAP_DEVICE_CFG    4   /* Device specific */
#define VIRTIO_PCI_CAP_PCI_CFG       5   /* PCI configuration access */

/*
 * Device Status Bits
 */
#define VIRTIO_STATUS_ACK            0x01  /* Guest found device */
#define VIRTIO_STATUS_DRIVER         0x02  /* Guest knows how to drive it */
#define VIRTIO_STATUS_DRIVER_OK      0x04  /* Driver setup complete */
#define VIRTIO_STATUS_FEATURES_OK    0x08  /* Feature negotiation complete */
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 0x40
#define VIRTIO_STATUS_FAILED         0x80  /* Something went wrong */

/*
 * Feature Bits (common)
 */
#define VIRTIO_F_NOTIFY_ON_EMPTY     (1ULL << 24)
#define VIRTIO_F_ANY_LAYOUT          (1ULL << 27)
#define VIRTIO_F_RING_INDIRECT_DESC  (1ULL << 28)
#define VIRTIO_F_RING_EVENT_IDX      (1ULL << 29)
#define VIRTIO_F_VERSION_1           (1ULL << 32)  /* Modern device */
#define VIRTIO_F_ACCESS_PLATFORM     (1ULL << 33)
#define VIRTIO_F_RING_PACKED         (1ULL << 34)
#define VIRTIO_F_IN_ORDER            (1ULL << 35)

/*
 * Virtqueue Descriptor Flags
 */
#define VIRTQ_DESC_F_NEXT     0x1   /* Buffer continues via 'next' field */
#define VIRTQ_DESC_F_WRITE    0x2   /* Device writes (vs reads) this buffer */
#define VIRTQ_DESC_F_INDIRECT 0x4   /* Buffer contains a list of descriptors */

/*
 * Virtqueue Available/Used Ring Flags
 */
#define VIRTQ_AVAIL_F_NO_INTERRUPT 0x1
#define VIRTQ_USED_F_NO_NOTIFY     0x1

/*
 * Virtqueue sizes (must be power of 2)
 */
#define VIRTQ_SIZE_MIN         16
#define VIRTQ_SIZE_DEFAULT     256
#define VIRTQ_SIZE_MAX         32768

/*
 * Virtqueue Descriptor
 */
struct virtq_desc {
    uint64_t addr;     /* Physical address of buffer */
    uint32_t len;      /* Length of buffer */
    uint16_t flags;    /* VIRTQ_DESC_F_* */
    uint16_t next;     /* Next descriptor if F_NEXT */
} __attribute__((packed));

/*
 * Virtqueue Available Ring
 */
struct virtq_avail {
    uint16_t flags;
    uint16_t idx;          /* Where driver puts next descriptor index */
    uint16_t ring[];       /* Descriptor indexes */
    /* followed by uint16_t used_event (if VIRTIO_F_EVENT_IDX) */
} __attribute__((packed));

/*
 * Virtqueue Used Ring Element
 */
struct virtq_used_elem {
    uint32_t id;       /* Index of descriptor chain head */
    uint32_t len;      /* Total length written to buffer */
} __attribute__((packed));

/*
 * Virtqueue Used Ring
 */
struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[];
    /* followed by uint16_t avail_event (if VIRTIO_F_EVENT_IDX) */
} __attribute__((packed));

/*
 * Virtqueue structure (driver-side tracking)
 */
struct virtqueue {
    uint16_t index;            /* Queue index */
    uint16_t size;             /* Number of entries */
    uint16_t free_head;        /* Head of free descriptor list */
    uint16_t num_free;         /* Number of free descriptors */
    uint16_t last_used_idx;    /* Last processed used index */
    
    struct virtq_desc  *desc;  /* Descriptor table */
    struct virtq_avail *avail; /* Available ring */
    struct virtq_used  *used;  /* Used ring */
    
    void **desc_state;         /* Per-descriptor driver state */
    
    /* Notification */
    volatile uint16_t *notify; /* Notify register address */
    
    struct virtio_dev *vdev;   /* Back pointer to device */
};

/*
 * Virtio device structure
 */
struct virtio_dev {
    struct pci_dev *pci;       /* Underlying PCI device */
    
    /* Legacy I/O port base (for transitional devices) */
    uint16_t iobase;
    
    /* Modern MMIO regions (from capabilities) */
    void *common_cfg;
    void *notify_base;
    void *isr_cfg;
    void *device_cfg;
    uint32_t notify_off_multiplier;
    
    /* Device info */
    uint32_t device_id;
    uint64_t features;         /* Negotiated features */
    uint8_t  status;
    
    /* Virtqueues */
    struct virtqueue *vqs[16]; /* Up to 16 queues */
    int nvqs;
    
    /* Interrupt handling */
    int irq;
    void (*isr_handler)(struct virtio_dev *);
    
    /* Driver-specific data */
    void *driver_data;
};

/*
 * Legacy virtio I/O port registers (offset from iobase)
 */
#define VIRTIO_PCI_HOST_FEATURES    0x00  /* 32-bit R */
#define VIRTIO_PCI_GUEST_FEATURES   0x04  /* 32-bit R/W */
#define VIRTIO_PCI_QUEUE_PFN        0x08  /* 32-bit R/W */
#define VIRTIO_PCI_QUEUE_SIZE       0x0C  /* 16-bit R */
#define VIRTIO_PCI_QUEUE_SEL        0x0E  /* 16-bit R/W */
#define VIRTIO_PCI_QUEUE_NOTIFY     0x10  /* 16-bit R/W */
#define VIRTIO_PCI_STATUS           0x12  /* 8-bit R/W */
#define VIRTIO_PCI_ISR              0x13  /* 8-bit R */
#define VIRTIO_PCI_CONFIG           0x14  /* Device-specific config */

/*
 * Function prototypes
 */

/* Device initialization */
int  virtio_probe_pci(struct pci_dev *pci, struct virtio_dev *vdev);
void virtio_reset(struct virtio_dev *vdev);
int  virtio_negotiate_features(struct virtio_dev *vdev, uint64_t requested);
void virtio_set_status(struct virtio_dev *vdev, uint8_t status);
int  virtio_finalize_features(struct virtio_dev *vdev);

/* Virtqueue management */
struct virtqueue *virtq_create(struct virtio_dev *vdev, int index, int size);
void virtq_destroy(struct virtqueue *vq);
int  virtq_add_buf(struct virtqueue *vq, void **bufs, uint32_t *lens,
                   int out_num, int in_num, void *data);
void *virtq_get_buf(struct virtqueue *vq, uint32_t *len);
void virtq_kick(struct virtqueue *vq);
void virtq_disable_interrupts(struct virtqueue *vq);
void virtq_enable_interrupts(struct virtqueue *vq);
int  virtq_num_free(struct virtqueue *vq);

/* Interrupt handling */
void virtio_handle_interrupt(struct virtio_dev *vdev);

/* Configuration space access */
uint8_t  virtio_config_read8(struct virtio_dev *vdev, int offset);
uint16_t virtio_config_read16(struct virtio_dev *vdev, int offset);
uint32_t virtio_config_read32(struct virtio_dev *vdev, int offset);
void     virtio_config_write8(struct virtio_dev *vdev, int offset, uint8_t val);
void     virtio_config_write16(struct virtio_dev *vdev, int offset, uint16_t val);
void     virtio_config_write32(struct virtio_dev *vdev, int offset, uint32_t val);

/* Debug */
void virtio_dump_device(struct virtio_dev *vdev);

#endif /* _VIRTIO_H_ */
