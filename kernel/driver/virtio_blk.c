/*
 * Virtio Block Device Driver for auxv6
 *
 * Implements virtio-blk specification for paravirtualized block devices.
 *
 * Architecture:
 * - Single request virtqueue
 * - Request header + data + status structure
 * - Integrates with block device layer via bdev_register()
 *
 * TODO Phase 1:
 * - [ ] Basic read/write operations
 * - [ ] Block device registration
 * - [ ] Capacity detection
 *
 * TODO Phase 2:
 * - [ ] Multi-queue support
 * - [ ] Discard/TRIM support
 * - [ ] Flush support
 * - [ ] Scatter-gather I/O
 *
 * Reference: Virtio 1.1 Specification Section 5.2
 * See also: Linux drivers/block/virtio_blk.c
 */

#include "types.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "buf.h"
#include "fcntl.h"
#include "pci.h"
#include "virtio.h"
#include "blockdev.h"

extern int ncpu;

/* Virtio block feature bits */
#define VIRTIO_BLK_F_SIZE_MAX       (1ULL << 1)
#define VIRTIO_BLK_F_SEG_MAX        (1ULL << 2)
#define VIRTIO_BLK_F_GEOMETRY       (1ULL << 4)
#define VIRTIO_BLK_F_RO             (1ULL << 5)
#define VIRTIO_BLK_F_BLK_SIZE       (1ULL << 6)
#define VIRTIO_BLK_F_FLUSH          (1ULL << 9)
#define VIRTIO_BLK_F_TOPOLOGY       (1ULL << 10)
#define VIRTIO_BLK_F_CONFIG_WCE     (1ULL << 11)
#define VIRTIO_BLK_F_DISCARD        (1ULL << 13)
#define VIRTIO_BLK_F_WRITE_ZEROES   (1ULL << 14)

/* Request types */
#define VIRTIO_BLK_T_IN             0   /* Read */
#define VIRTIO_BLK_T_OUT            1   /* Write */
#define VIRTIO_BLK_T_FLUSH          4   /* Flush */
#define VIRTIO_BLK_T_DISCARD        11  /* Discard */
#define VIRTIO_BLK_T_WRITE_ZEROES   13  /* Write zeroes */

/* Status codes */
#define VIRTIO_BLK_S_OK             0
#define VIRTIO_BLK_S_IOERR          1
#define VIRTIO_BLK_S_UNSUPP         2

/* Request header */
struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

struct virtio_blk_discard_write_zeroes {
    uint64_t sector;
    uint32_t num_sectors;
    uint32_t flags;
} __attribute__((packed));

/* Config structure */
struct virtio_blk_config {
    uint64_t capacity;      /* Capacity in 512-byte sectors */
    uint32_t size_max;
    uint32_t seg_max;
    struct {
        uint16_t cylinders;
        uint8_t  heads;
        uint8_t  sectors;
    } geometry;
    uint32_t blk_size;
    /* ... more fields in spec */
} __attribute__((packed));

/* Per-device state */
struct virtio_blk_softc {
    struct virtio_dev   vdev;
    struct spinlock     lock;
    struct sleeplock    req_lock;   /* For request serialization */
    
    uint64_t capacity;      /* In 512-byte sectors */
    uint32_t blk_size;      /* Logical block size */
    int      read_only;
    int      has_flush;
    int      has_discard;
    int      has_write_zeroes;
    uint32_t writes_since_flush;

    uint32_t io_ok;
    uint32_t io_ioerr;
    uint32_t io_unsupp;
    uint32_t io_transport;
    uint32_t io_retry;
    uint32_t flush_ok;
    uint32_t flush_fail;
    uint32_t discard_ok;
    uint32_t discard_fail;
    uint32_t write_zeroes_ok;
    uint32_t write_zeroes_fail;
    
    /* Block device ID (for blockdev registration) */
    uint dev_id;
    
    /* Pending request tracking */
    struct {
        struct buf *bp;
        struct virtio_blk_req hdr;
        uint8_t status;
        int done;
    } request;
};

/* Global array of virtio-blk devices */
#define MAX_VIRTIO_BLK 4
static struct virtio_blk_softc virtio_blk_devices[MAX_VIRTIO_BLK];
static int virtio_blk_count = 0;
static int virtio_blk_next_unit = 0;  /* Track next unit to allocate */

/*
 * Tunable flush cadence for dirty writes.
 * 1 = flush every write, N = flush every N writes, 0 = disable explicit flush.
 */
static int virtio_blk_flush_every_writes = 1;
static int virtio_blk_max_retries = 2;

#define VIRTIO_BLK_REQ_OK       0
#define VIRTIO_BLK_REQ_ERR      -1
#define VIRTIO_BLK_REQ_UNSUPP   -2
#define VIRTIO_BLK_REQ_IOERR    -3

/* Forward declarations */
static int virtio_blk_rw(struct buf *b);
static uint virtio_blk_nblocks(uint dev);
static int virtio_blk_alloc_devid(void);
static struct virtio_blk_softc *virtio_blk_find_by_dev(uint dev);
static int virtio_blk_submit_locked(struct virtio_blk_softc *sc, uint32_t type,
                                    uint64_t sector, void *data, uint32_t data_len,
                                    int data_is_write);
static int virtio_blk_submit_with_retry_locked(struct virtio_blk_softc *sc,
                                               uint32_t type, uint64_t sector,
                                               void *data, uint32_t data_len,
                                               int data_is_write);
static int virtio_blk_write_zeroes_locked(struct virtio_blk_softc *sc,
                                          uint64_t sector, uint32_t num_sectors,
                                          int unmap);
static int virtio_blk_discard_locked(struct virtio_blk_softc *sc,
                                     uint64_t sector, uint32_t num_sectors);
static int virtio_blk_buf_is_zero(char *data, int n);
static int virtio_blk_append_str(char *buf, int max, int pos, const char *s);
static int virtio_blk_append_uint(char *buf, int max, int pos, uint v);

int
virtio_blk_get_flush_every_writes(void)
{
    return virtio_blk_flush_every_writes;
}

int
virtio_blk_set_flush_every_writes(int value)
{
    if (value < 0)
        return -1;
    if (value > 1000000)
        return -1;

    virtio_blk_flush_every_writes = value;
    return 0;
}

int
virtio_blk_get_tune(char *buf, int max)
{
    int pos;
    int i;

    if (!buf || max <= 0)
        return -1;

    pos = 0;
    pos = virtio_blk_append_str(buf, max, pos, "flush_every_writes=");
    pos = virtio_blk_append_uint(buf, max, pos, (uint)virtio_blk_flush_every_writes);
    pos = virtio_blk_append_str(buf, max, pos, "\n");

    pos = virtio_blk_append_str(buf, max, pos, "max_retries=");
    pos = virtio_blk_append_uint(buf, max, pos, (uint)virtio_blk_max_retries);
    pos = virtio_blk_append_str(buf, max, pos, "\n");

    for (i = 0; i < virtio_blk_count; i++) {
        struct virtio_blk_softc *sc = &virtio_blk_devices[i];

        pos = virtio_blk_append_str(buf, max, pos, "dev=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->dev_id);
        pos = virtio_blk_append_str(buf, max, pos, " ok=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->io_ok);
        pos = virtio_blk_append_str(buf, max, pos, " ioerr=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->io_ioerr);
        pos = virtio_blk_append_str(buf, max, pos, " unsupp=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->io_unsupp);
        pos = virtio_blk_append_str(buf, max, pos, " transport=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->io_transport);
        pos = virtio_blk_append_str(buf, max, pos, " retries=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->io_retry);
        pos = virtio_blk_append_str(buf, max, pos, " flush_ok=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->flush_ok);
        pos = virtio_blk_append_str(buf, max, pos, " flush_fail=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->flush_fail);
        pos = virtio_blk_append_str(buf, max, pos, " discard_ok=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->discard_ok);
        pos = virtio_blk_append_str(buf, max, pos, " discard_fail=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->discard_fail);
        pos = virtio_blk_append_str(buf, max, pos, " wz_ok=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->write_zeroes_ok);
        pos = virtio_blk_append_str(buf, max, pos, " wz_fail=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->write_zeroes_fail);
        pos = virtio_blk_append_str(buf, max, pos, "\n");

        if (pos >= max - 1)
            return pos;
    }

    return pos;
}

int
virtio_blk_set_tune(const char *buf, int n)
{
    uint v;
    int i;

    if (!buf || n <= 0)
        return -1;

    if (n > 0 && buf[n - 1] == '\n')
        n--;

    if (n >= 18 && memcmp(buf, "flush_every_writes=", 18) == 0) {
        v = 0;
        for (i = 18; i < n; i++) {
            if (buf[i] < '0' || buf[i] > '9')
                return -1;
            v = v * 10 + (uint)(buf[i] - '0');
            if (v > 1000000)
                return -1;
        }
        return virtio_blk_set_flush_every_writes((int)v);
    }

    if (n >= 12 && memcmp(buf, "max_retries=", 12) == 0) {
        v = 0;
        for (i = 12; i < n; i++) {
            if (buf[i] < '0' || buf[i] > '9')
                return -1;
            v = v * 10 + (uint)(buf[i] - '0');
            if (v > 16)
                return -1;
        }
        virtio_blk_max_retries = (int)v;
        return 0;
    }

    /* Backward-compatible mode: plain integer updates flush cadence. */
    v = 0;
    for (i = 0; i < n; i++) {
        if (buf[i] < '0' || buf[i] > '9')
            return -1;
        v = v * 10 + (uint)(buf[i] - '0');
        if (v > 1000000)
            return -1;
    }
    return virtio_blk_set_flush_every_writes((int)v);
}

/* Block device switch entry */
static const struct bdevsw virtio_blk_bdevsw = {
    .rw = virtio_blk_rw,
    .nblocks = virtio_blk_nblocks,
    .name = "virtio-blk"
};

/*
 * IRQ handler wrapper (called from trap.c)
 */
static void
virtio_blk_irq_handler(int irq, void *arg)
{
    struct virtio_blk_softc *sc = arg;
    
    /* Read and clear ISR status */
    virtio_handle_interrupt(&sc->vdev);
}

/*
 * Virtio ISR callback (called from virtio_handle_interrupt)
 */
static void
virtio_blk_intr(struct virtio_dev *vdev)
{
    struct virtio_blk_softc *sc = vdev->driver_data;
    struct virtqueue *vq = vdev->vqs[0];
    uint32_t len;
    void *cookie;
    
    acquire(&sc->lock);
    
    while ((cookie = virtq_get_buf(vq, &len)) != 0) {
        /* Our request completed */
        sc->request.done = 1;
        wakeup(&sc->request);
    }
    
    release(&sc->lock);
}

static struct virtio_blk_softc *
virtio_blk_find_by_dev(uint dev)
{
    int i;

    for (i = 0; i < virtio_blk_count; i++) {
        if (virtio_blk_devices[i].dev_id == dev)
            return &virtio_blk_devices[i];
    }

    return 0;
}

/*
 * Block device read/write callback
 */
static int
virtio_blk_rw(struct buf *b)
{
    if (!b)
        return -1;

    struct virtio_blk_softc *sc = virtio_blk_find_by_dev(b->dev);
    if (!sc)
        return -1;

    struct virtqueue *vq = sc->vdev.vqs[0];
    
    if (!vq)
        return -1;
    
    acquiresleep(&sc->req_lock);
    int is_write = (b->flags & B_DIRTY) != 0;
    uint32_t req_type = is_write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    uint64_t sector = b->blockno * (BSIZE / 512);
    int rc;

    /*
     * Keep filesystem writes on the normal data path.
     * Rewriting arbitrary zero-filled buffers into discard/write-zeroes is too
     * aggressive for metadata traffic and newly allocated blocks.
     */
    rc = virtio_blk_submit_with_retry_locked(sc, req_type, sector,
                                             b->data, BSIZE, is_write);

    if (rc < 0) {
        releasesleep(&sc->req_lock);
        cprintf("virtio_blk: data request failed dev=%d block=%d rc=%d\n",
                b->dev, b->blockno, rc);
        return -1;
    }

    if (is_write)
        sc->writes_since_flush++;

    if (is_write && sc->has_flush && virtio_blk_flush_every_writes > 0 &&
        sc->writes_since_flush >= (uint32_t)virtio_blk_flush_every_writes) {
        if (virtio_blk_submit_with_retry_locked(sc, VIRTIO_BLK_T_FLUSH, 0, 0, 0, 0) < 0) {
            sc->flush_fail++;
            releasesleep(&sc->req_lock);
            cprintf("virtio_blk: flush request failed dev=%d cadence=%d\n",
                    b->dev, virtio_blk_flush_every_writes);
            return -1;
        }
        sc->flush_ok++;
        sc->writes_since_flush = 0;
    }

    sc->io_ok++;

    releasesleep(&sc->req_lock);
    
    /* Mark buffer as valid and not dirty */
    b->flags |= B_VALID;
    b->flags &= ~B_DIRTY;
    
    return 0;
}

static int
virtio_blk_submit_locked(struct virtio_blk_softc *sc, uint32_t type,
                         uint64_t sector, void *data, uint32_t data_len,
                         int data_is_write)
{
    struct virtqueue *vq = sc->vdev.vqs[0];
    void *bufs[3];
    uint32_t lens[3];
    int out_num;
    int in_num;

    if (!vq)
        return -1;

    sc->request.hdr.type = type;
    sc->request.hdr.reserved = 0;
    sc->request.hdr.sector = sector;
    sc->request.bp = 0;
    sc->request.status = 0xFF;
    sc->request.done = 0;

    bufs[0] = &sc->request.hdr;
    lens[0] = sizeof(sc->request.hdr);

    if (data && data_len > 0) {
        bufs[1] = data;
        lens[1] = data_len;
        bufs[2] = &sc->request.status;
        lens[2] = 1;
        if (data_is_write) {
            out_num = 2;
            in_num = 1;
        } else {
            out_num = 1;
            in_num = 2;
        }
    } else {
        bufs[1] = &sc->request.status;
        lens[1] = 1;
        out_num = 1;
        in_num = 1;
    }

    acquire(&sc->lock);
    if (virtq_add_buf(vq, bufs, lens, out_num, in_num, &sc->request) < 0) {
        release(&sc->lock);
        return -1;
    }
    virtq_kick(vq);

    while (!sc->request.done) {
        sleep(&sc->request, &sc->lock);
    }
    release(&sc->lock);

    if (sc->request.status == VIRTIO_BLK_S_UNSUPP)
        return VIRTIO_BLK_REQ_UNSUPP;
    if (sc->request.status == VIRTIO_BLK_S_IOERR)
        return VIRTIO_BLK_REQ_IOERR;
    if (sc->request.status != VIRTIO_BLK_S_OK)
        return VIRTIO_BLK_REQ_ERR;

    return VIRTIO_BLK_REQ_OK;
}

static int
virtio_blk_submit_with_retry_locked(struct virtio_blk_softc *sc,
                                    uint32_t type, uint64_t sector,
                                    void *data, uint32_t data_len,
                                    int data_is_write)
{
    int attempts;
    int try;
    int rc;

    attempts = 1 + virtio_blk_max_retries;
    if (attempts < 1)
        attempts = 1;

    for (try = 0; try < attempts; try++) {
        rc = virtio_blk_submit_locked(sc, type, sector, data, data_len, data_is_write);
        if (rc == VIRTIO_BLK_REQ_OK)
            return VIRTIO_BLK_REQ_OK;

        if (rc == VIRTIO_BLK_REQ_UNSUPP) {
            sc->io_unsupp++;
            return rc;
        }

        if (rc == VIRTIO_BLK_REQ_IOERR)
            sc->io_ioerr++;
        else
            sc->io_transport++;

        if (try + 1 >= attempts)
            return rc;

        sc->io_retry++;
        microdelay(50 * (try + 1));
    }

    return VIRTIO_BLK_REQ_ERR;
}

static int __attribute__((unused))
virtio_blk_write_zeroes_locked(struct virtio_blk_softc *sc,
                               uint64_t sector, uint32_t num_sectors,
                               int unmap)
{
    struct virtio_blk_discard_write_zeroes rz;
    int rc;

    if (!sc->has_write_zeroes)
        return VIRTIO_BLK_REQ_UNSUPP;

    rz.sector = sector;
    rz.num_sectors = num_sectors;
    rz.flags = unmap ? 1 : 0;

    rc = virtio_blk_submit_with_retry_locked(sc, VIRTIO_BLK_T_WRITE_ZEROES,
                                             0, &rz, sizeof(rz), 1);
    if (rc == VIRTIO_BLK_REQ_OK)
        sc->write_zeroes_ok++;
    else
        sc->write_zeroes_fail++;
    return rc;
}

static int __attribute__((unused))
virtio_blk_discard_locked(struct virtio_blk_softc *sc,
                          uint64_t sector, uint32_t num_sectors)
{
    struct virtio_blk_discard_write_zeroes d;
    int rc;

    if (!sc->has_discard)
        return VIRTIO_BLK_REQ_UNSUPP;

    d.sector = sector;
    d.num_sectors = num_sectors;
    d.flags = 0;

    rc = virtio_blk_submit_with_retry_locked(sc, VIRTIO_BLK_T_DISCARD,
                                             0, &d, sizeof(d), 1);
    if (rc == VIRTIO_BLK_REQ_OK)
        sc->discard_ok++;
    else
        sc->discard_fail++;
    return rc;
}

static int __attribute__((unused))
virtio_blk_buf_is_zero(char *data, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        if (data[i] != 0)
            return 0;
    }
    return 1;
}

static int
virtio_blk_append_str(char *buf, int max, int pos, const char *s)
{
    while (s && *s && pos < max - 1)
        buf[pos++] = *s++;
    if (pos < max)
        buf[pos] = 0;
    return pos;
}

static int
virtio_blk_append_uint(char *buf, int max, int pos, uint v)
{
    char tmp[16];
    int len;
    int i;

    len = 0;
    do {
        tmp[len++] = '0' + (v % 10);
        v /= 10;
    } while (v > 0 && len < (int)sizeof(tmp));

    for (i = len - 1; i >= 0 && pos < max - 1; i--)
        buf[pos++] = tmp[i];

    if (pos < max)
        buf[pos] = 0;
    return pos;
}

/*
 * Report device capacity
 */
static uint
virtio_blk_nblocks(uint dev)
{
    struct virtio_blk_softc *sc = virtio_blk_find_by_dev(dev);
    if (!sc)
        return 0;

    return sc->capacity / (BSIZE / 512);
}

static int
virtio_blk_alloc_devid(void)
{
    int unit;

    /* Find the next unallocated unit using static counter */
    for (unit = virtio_blk_next_unit; unit < VD_DISK_UNITS; unit++) {
        int dev = VD_DISK_DEV(unit);
        if (bdev_register(dev, &virtio_blk_bdevsw) == 0) {
            virtio_blk_next_unit = unit + 1;  /* Update for next allocation */
            return dev;
        }
    }

    return -1;
}

/*
 * PCI probe function
 */
int
virtio_blk_probe(struct pci_dev *pci)
{
    if (virtio_blk_count >= MAX_VIRTIO_BLK)
        return -1;
    
    struct virtio_blk_softc *sc = &virtio_blk_devices[virtio_blk_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "virtio_blk");
    initsleeplock(&sc->req_lock, "virtio_blk_req");
    
    /* Initialize virtio device */
    if (virtio_probe_pci(pci, &sc->vdev) < 0)
        return -1;
    
    if (sc->vdev.device_id != VIRTIO_DEV_BLK) {
        virtio_reset(&sc->vdev);
        return -1;
    }
    
    /* Set ACKNOWLEDGE and DRIVER status */
    virtio_set_status(&sc->vdev, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);
    
    /* Negotiate features. Keep optional bits so we can gate behavior by capability. */
    uint64_t features = VIRTIO_BLK_F_RO |
                        VIRTIO_BLK_F_BLK_SIZE |
                        VIRTIO_BLK_F_FLUSH |
                        VIRTIO_BLK_F_DISCARD |
                        VIRTIO_BLK_F_WRITE_ZEROES;
    virtio_negotiate_features(&sc->vdev, features);
    
    if (virtio_finalize_features(&sc->vdev) < 0) {
        virtio_reset(&sc->vdev);
        return -1;
    }
    
    /* Read configuration */
    sc->capacity = virtio_config_read32(&sc->vdev, 0);
    sc->capacity |= ((uint64_t)virtio_config_read32(&sc->vdev, 4)) << 32;
    sc->blk_size = 512;  /* Default */
    
    if (sc->vdev.features & VIRTIO_BLK_F_BLK_SIZE) {
        sc->blk_size = virtio_config_read32(&sc->vdev, 20);
    }
    
    sc->read_only = (sc->vdev.features & VIRTIO_BLK_F_RO) != 0;
    sc->has_flush = (sc->vdev.features & VIRTIO_BLK_F_FLUSH) != 0;
    sc->has_discard = (sc->vdev.features & VIRTIO_BLK_F_DISCARD) != 0;
    sc->has_write_zeroes = (sc->vdev.features & VIRTIO_BLK_F_WRITE_ZEROES) != 0;
    
        cprintf("virtio_blk: capacity=%d sectors, blk_size=%d, ro=%d, flush=%d, discard=%d, write_zeroes=%d, flush_every_writes=%d\n",
            (uint32_t)sc->capacity, sc->blk_size, sc->read_only,
            sc->has_flush, sc->has_discard, sc->has_write_zeroes,
            virtio_blk_flush_every_writes);
    
    /* Create virtqueue */
    struct virtqueue *vq = virtq_create(&sc->vdev, 0, 0);
    if (!vq) {
        virtio_reset(&sc->vdev);
        return -1;
    }
    
    /* Set driver data for interrupt handler */
    sc->vdev.driver_data = sc;
    sc->vdev.isr_handler = virtio_blk_intr;
    
    /* Register IRQ handler */
    if (irq_register(sc->vdev.irq, virtio_blk_irq_handler, sc, "virtio_blk") < 0) {
        cprintf("virtio_blk: failed to register IRQ %d\n", sc->vdev.irq);
        virtq_destroy(vq);
        virtio_reset(&sc->vdev);
        return -1;
    }
    
    /* Enable interrupt routing via IOAPIC */
    pci_enable_irq(pci, ncpu - 1);
    
    /* Mark device ready */
    virtio_set_status(&sc->vdev, VIRTIO_STATUS_DRIVER_OK);
    
    /* Register with block device layer using the first free vd* slot. */
    int dev_id = virtio_blk_alloc_devid();
    if (dev_id < 0) {
        irq_unregister(sc->vdev.irq, "virtio_blk");
        virtq_destroy(vq);
        virtio_reset(&sc->vdev);
        cprintf("virtio_blk: no free block device slots\n");
        return -1;
    }
    sc->dev_id = dev_id;
    bdev_set_nblocks(sc->dev_id, sc->capacity / (BSIZE / 512));
    
    virtio_blk_count++;
    cprintf("virtio_blk: attached device %d as dev=%d\n",
            virtio_blk_count - 1, sc->dev_id);
    
    return 0;
}

/*
 * Module init - register with PCI
 */
void
virtio_blk_init(void)
{
    BOOTDBG("virtio_blk: initializing driver\n");
    
    /* Look for virtio-blk PCI devices */
    for (int i = 0; i < pci_device_count(); i++) {
        struct pci_dev *dev = pci_get_device(i);
        if (!dev)
            continue;
        
        if (dev->vendor_id == PCI_VENDOR_VIRTIO &&
            (dev->device_id == PCI_DEVICE_VIRTIO_BLK ||
             (dev->device_id >= 0x1000 && dev->device_id <= 0x103F &&
              dev->device_id - 0x1000 == VIRTIO_DEV_BLK))) {
            virtio_blk_probe(dev);
        }
    }
}
