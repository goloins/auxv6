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
    uint32_t io_retry_ioerr;
    uint32_t io_retry_transport;
    uint32_t io_retry_exhausted;
    uint32_t io_last_type;
    int      io_last_rc;
    uint32_t io_last_attempts;
    uint32_t io_last_fail_class;
    uint32_t flush_ok;
    uint32_t flush_fail;
    uint32_t discard_ok;
    uint32_t discard_fail;
    uint32_t write_zeroes_ok;
    uint32_t write_zeroes_fail;
    uint32_t admin_ops;
    int      admin_last_op;
    int      admin_last_rc;
    uint64_t admin_last_sector;
    uint32_t admin_last_count;
    
    /* Block device ID (for blockdev registration) */
    uint dev_id;
    int dev_registered;

    /* Hardware-negotiated virtqueue depth (set once at probe, read-only thereafter) */
    uint32_t vq_size;

    /* Multi-vector MSI-X support */
    int irq_req;                /* Request queue IRQ */
    int num_vecs;              /* Number of vectors allocated (1 or 2) */

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
/*
 * Active queue depth cap.  Currently the driver serializes one request at a
 * time via req_lock, so the effective maximum is 1.  This knob is exposed
 * for observability and to lay the groundwork for future multi-inflight work.
 */
static int virtio_blk_queue_depth = 1;
static int virtio_blk_force_no_discard = 0;
static int virtio_blk_force_no_write_zeroes = 0;
static int virtio_blk_test_fail_mode = 0;
static int virtio_blk_test_fail_remaining = 0;

#define VIRTIO_BLK_TEST_FAIL_NONE       0
#define VIRTIO_BLK_TEST_FAIL_IOERR      1
#define VIRTIO_BLK_TEST_FAIL_TRANSPORT  2
#define VIRTIO_BLK_TEST_FAIL_UNSUPP     3

#define VIRTIO_BLK_REQ_OK       0
#define VIRTIO_BLK_REQ_ERR      -1
#define VIRTIO_BLK_REQ_UNSUPP   -2
#define VIRTIO_BLK_REQ_IOERR    -3

#define VIRTIO_BLK_FAILCLASS_NONE       0
#define VIRTIO_BLK_FAILCLASS_IOERR      1
#define VIRTIO_BLK_FAILCLASS_TRANSPORT  2
#define VIRTIO_BLK_FAILCLASS_UNSUPP     3

/* Forward declarations */
static int virtio_blk_rw(struct buf *b);
static uint virtio_blk_nblocks(uint dev);
static int virtio_blk_alloc_devid(void);
static struct virtio_blk_softc *virtio_blk_find_by_dev(uint dev);
static int virtio_blk_submit_locked(struct virtio_blk_softc *sc, uint32_t type,
                                    uint64_t sector, void *data, uint32_t data_len,
                                    int data_is_write);
static int virtio_blk_wait_complete_locked(struct virtio_blk_softc *sc,
                                           struct virtqueue *vq);
static int virtio_blk_submit_with_retry_locked(struct virtio_blk_softc *sc,
                                               uint32_t type, uint64_t sector,
                                               void *data, uint32_t data_len,
                                               int data_is_write);
static int __attribute__((unused)) virtio_blk_write_zeroes_locked(struct virtio_blk_softc *sc,
                                          uint64_t sector, uint32_t num_sectors,
                                          int unmap);
static int __attribute__((unused)) virtio_blk_discard_locked(struct virtio_blk_softc *sc,
                                     uint64_t sector, uint32_t num_sectors);
static int __attribute__((unused)) virtio_blk_buf_is_zero(char *data, int n);
static int virtio_blk_append_str(char *buf, int max, int pos, const char *s);
static int virtio_blk_append_uint(char *buf, int max, int pos, uint v);
static int virtio_blk_append_sint(char *buf, int max, int pos, int v);
static int virtio_blk_parse_u32(const char *s, int n, uint32_t *out, int *consumed);
static int virtio_blk_parse_u64(const char *s, int n, uint64_t *out, int *consumed);
static int virtio_blk_admin_discard_by_dev(uint dev, uint64_t sector, uint32_t count);
static int virtio_blk_admin_write_zeroes_by_dev(uint dev, uint64_t sector,
                                                uint32_t count, int unmap);
static int virtio_blk_admin_discard_all(uint64_t sector, uint32_t count);
static int virtio_blk_admin_write_zeroes_all(uint64_t sector, uint32_t count,
                                             int unmap);
static int virtio_blk_admin_flush_by_dev(uint dev);
static int virtio_blk_admin_flush_all(void);
static int virtio_blk_admin_flush_first(void);
static int virtio_blk_pci_probe(struct pci_dev *dev);
static void virtio_blk_pci_remove(struct pci_dev *dev);

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

    pos = virtio_blk_append_str(buf, max, pos, "queue_depth=");
    pos = virtio_blk_append_uint(buf, max, pos, (uint)virtio_blk_queue_depth);
    pos = virtio_blk_append_str(buf, max, pos, "\n");

    pos = virtio_blk_append_str(buf, max, pos, "force_no_discard=");
    pos = virtio_blk_append_uint(buf, max, pos, (uint)virtio_blk_force_no_discard);
    pos = virtio_blk_append_str(buf, max, pos, "\n");

    pos = virtio_blk_append_str(buf, max, pos, "force_no_write_zeroes=");
    pos = virtio_blk_append_uint(buf, max, pos, (uint)virtio_blk_force_no_write_zeroes);
    pos = virtio_blk_append_str(buf, max, pos, "\n");

    pos = virtio_blk_append_str(buf, max, pos, "test_fail_mode=");
    pos = virtio_blk_append_uint(buf, max, pos, (uint)virtio_blk_test_fail_mode);
    pos = virtio_blk_append_str(buf, max, pos, "\n");

    pos = virtio_blk_append_str(buf, max, pos, "test_fail_remaining=");
    pos = virtio_blk_append_uint(buf, max, pos, (uint)virtio_blk_test_fail_remaining);
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
        pos = virtio_blk_append_str(buf, max, pos, " retry_ioerr=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->io_retry_ioerr);
        pos = virtio_blk_append_str(buf, max, pos, " retry_transport=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->io_retry_transport);
        pos = virtio_blk_append_str(buf, max, pos, " retry_exhausted=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->io_retry_exhausted);
        pos = virtio_blk_append_str(buf, max, pos, " last_type=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->io_last_type);
        pos = virtio_blk_append_str(buf, max, pos, " last_rc=");
        pos = virtio_blk_append_sint(buf, max, pos, sc->io_last_rc);
        pos = virtio_blk_append_str(buf, max, pos, " last_attempts=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->io_last_attempts);
        pos = virtio_blk_append_str(buf, max, pos, " last_fail_class=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->io_last_fail_class);
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
        pos = virtio_blk_append_str(buf, max, pos, " has_discard=");
        pos = virtio_blk_append_uint(buf, max, pos, (uint)sc->has_discard);
        pos = virtio_blk_append_str(buf, max, pos, " has_wz=");
        pos = virtio_blk_append_uint(buf, max, pos, (uint)sc->has_write_zeroes);
        pos = virtio_blk_append_str(buf, max, pos, " admin_ops=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->admin_ops);
        pos = virtio_blk_append_str(buf, max, pos, " admin_last_op=");
        pos = virtio_blk_append_uint(buf, max, pos, (uint)sc->admin_last_op);
        pos = virtio_blk_append_str(buf, max, pos, " admin_last_rc=");
        pos = virtio_blk_append_sint(buf, max, pos, sc->admin_last_rc);
        pos = virtio_blk_append_str(buf, max, pos, " admin_last_count=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->admin_last_count);
        pos = virtio_blk_append_str(buf, max, pos, " vq_size=");
        pos = virtio_blk_append_uint(buf, max, pos, sc->vq_size);
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
    int consumed;
    uint32_t dev_u32;
    uint32_t count_u32;
    uint32_t unmap_u32;
    uint64_t sector_u64;

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

    /*
     * queue_depth: soft cap on active inflight descriptors.  Currently the
     * driver is single-inflight, so values > 1 are accepted but clamped to 1
     * at submission time until multi-inflight is implemented.
     */
    if (n >= 12 && memcmp(buf, "queue_depth=", 12) == 0) {
        v = 0;
        for (i = 12; i < n; i++) {
            if (buf[i] < '0' || buf[i] > '9')
                return -1;
            v = v * 10 + (uint)(buf[i] - '0');
            if (v > 256)
                return -1;
        }
        if (v < 1)
            v = 1;
        virtio_blk_queue_depth = (int)v;
        return 0;
    }

    if (n >= 17 && memcmp(buf, "force_no_discard=", 17) == 0) {
        if (n == 17 || (n == 18 && buf[17] == '0')) {
            virtio_blk_force_no_discard = 0;
            return 0;
        }
        if (n == 18 && buf[17] == '1') {
            virtio_blk_force_no_discard = 1;
            return 0;
        }
        return -1;
    }

    if (n >= 22 && memcmp(buf, "force_no_write_zeroes=", 22) == 0) {
        if (n == 22 || (n == 23 && buf[22] == '0')) {
            virtio_blk_force_no_write_zeroes = 0;
            return 0;
        }
        if (n == 23 && buf[22] == '1') {
            virtio_blk_force_no_write_zeroes = 1;
            return 0;
        }
        return -1;
    }

    if (n >= 15 && memcmp(buf, "test_fail_mode=", 15) == 0) {
        const char *s = buf + 15;
        int m = n - 15;

        if (m == 4 && memcmp(s, "none", 4) == 0) {
            virtio_blk_test_fail_mode = VIRTIO_BLK_TEST_FAIL_NONE;
            return 0;
        }
        if (m == 5 && memcmp(s, "ioerr", 5) == 0) {
            virtio_blk_test_fail_mode = VIRTIO_BLK_TEST_FAIL_IOERR;
            return 0;
        }
        if (m == 9 && memcmp(s, "transport", 9) == 0) {
            virtio_blk_test_fail_mode = VIRTIO_BLK_TEST_FAIL_TRANSPORT;
            return 0;
        }
        if (m == 6 && memcmp(s, "unsupp", 6) == 0) {
            virtio_blk_test_fail_mode = VIRTIO_BLK_TEST_FAIL_UNSUPP;
            return 0;
        }
        return -1;
    }

    if (n >= 16 && memcmp(buf, "test_fail_count=", 16) == 0) {
        v = 0;
        for (i = 16; i < n; i++) {
            if (buf[i] < '0' || buf[i] > '9')
                return -1;
            v = v * 10 + (uint)(buf[i] - '0');
            if (v > 1000000)
                return -1;
        }
        virtio_blk_test_fail_remaining = (int)v;
        return 0;
    }

    /* Runtime admin operation: flush=DEV */
    if (n >= 6 && memcmp(buf, "flush=", 6) == 0) {
        if (virtio_blk_parse_u32(buf + 6, n - 6, &dev_u32, &consumed) < 0)
            return -1;
        if (6 + consumed != n)
            return -1;
        return virtio_blk_admin_flush_by_dev((uint)dev_u32);
    }

    /* Runtime admin operation: flush_all */
    if (n == 9 && memcmp(buf, "flush_all", 9) == 0)
        return virtio_blk_admin_flush_all();

    /* Runtime admin operation: flush_first */
    if (n == 11 && memcmp(buf, "flush_first", 11) == 0)
        return virtio_blk_admin_flush_first();

    /* Runtime admin operation: discard=DEV:SECTOR:COUNT */
    if (n >= 8 && memcmp(buf, "discard=", 8) == 0) {
        {
            int p = 8;
            if (virtio_blk_parse_u32(buf + p, n - p, &dev_u32, &consumed) < 0)
                return -1;
            p += consumed;
            if (p >= n || buf[p++] != ':')
                return -1;
            if (virtio_blk_parse_u64(buf + p, n - p, &sector_u64, &consumed) < 0)
                return -1;
            p += consumed;
            if (p >= n || buf[p++] != ':')
                return -1;
            if (virtio_blk_parse_u32(buf + p, n - p, &count_u32, &consumed) < 0)
                return -1;
            p += consumed;
            if (p != n)
                return -1;
        }

        return virtio_blk_admin_discard_by_dev((uint)dev_u32, sector_u64, count_u32);
    }

    /* Runtime admin operation: discard_all=SECTOR:COUNT */
    if (n >= 12 && memcmp(buf, "discard_all=", 12) == 0) {
        int p = 12;

        if (virtio_blk_parse_u64(buf + p, n - p, &sector_u64, &consumed) < 0)
            return -1;
        p += consumed;
        if (p >= n || buf[p++] != ':')
            return -1;
        if (virtio_blk_parse_u32(buf + p, n - p, &count_u32, &consumed) < 0)
            return -1;
        p += consumed;
        if (p != n)
            return -1;

        return virtio_blk_admin_discard_all(sector_u64, count_u32);
    }

    /* Runtime admin operation: write_zeroes=DEV:SECTOR:COUNT[:UNMAP] */
    if (n >= 13 && memcmp(buf, "write_zeroes=", 13) == 0) {
        int p = 13;
        int unmap = 0;

        if (virtio_blk_parse_u32(buf + p, n - p, &dev_u32, &consumed) < 0)
            return -1;
        p += consumed;
        if (p >= n || buf[p++] != ':')
            return -1;
        if (virtio_blk_parse_u64(buf + p, n - p, &sector_u64, &consumed) < 0)
            return -1;
        p += consumed;
        if (p >= n || buf[p++] != ':')
            return -1;
        if (virtio_blk_parse_u32(buf + p, n - p, &count_u32, &consumed) < 0)
            return -1;
        p += consumed;
        if (p < n) {
            if (buf[p++] != ':')
                return -1;
            if (virtio_blk_parse_u32(buf + p, n - p, &unmap_u32, &consumed) < 0)
                return -1;
            if (unmap_u32 > 1)
                return -1;
            unmap = (int)unmap_u32;
            p += consumed;
        }
        if (p != n)
            return -1;

        return virtio_blk_admin_write_zeroes_by_dev((uint)dev_u32, sector_u64,
                                                    count_u32, unmap);
    }

    /* Runtime admin operation: write_zeroes_all=SECTOR:COUNT[:UNMAP] */
    if (n >= 17 && memcmp(buf, "write_zeroes_all=", 17) == 0) {
        int p = 17;
        int unmap = 0;

        if (virtio_blk_parse_u64(buf + p, n - p, &sector_u64, &consumed) < 0)
            return -1;
        p += consumed;
        if (p >= n || buf[p++] != ':')
            return -1;
        if (virtio_blk_parse_u32(buf + p, n - p, &count_u32, &consumed) < 0)
            return -1;
        p += consumed;
        if (p < n) {
            if (buf[p++] != ':')
                return -1;
            if (virtio_blk_parse_u32(buf + p, n - p, &unmap_u32, &consumed) < 0)
                return -1;
            if (unmap_u32 > 1)
                return -1;
            unmap = (int)unmap_u32;
            p += consumed;
        }
        if (p != n)
            return -1;

        return virtio_blk_admin_write_zeroes_all(sector_u64, count_u32, unmap);
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

static const struct pci_device_id virtio_blk_pci_ids[] = {
    { PCI_VENDOR_VIRTIO, PCI_DEVICE_VIRTIO_BLK, 0, 0, PCI_MATCH_VENDOR | PCI_MATCH_DEVICE },
};

static struct pci_driver virtio_blk_pci_driver = {
    .name = "virtio_blk",
    .id_table = virtio_blk_pci_ids,
    .id_table_len = sizeof(virtio_blk_pci_ids) / sizeof(virtio_blk_pci_ids[0]),
    .probe = virtio_blk_pci_probe,
    .remove = virtio_blk_pci_remove,
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

static void
virtio_blk_remove(struct virtio_blk_softc *sc)
{
    int unit;

    if (!sc)
        return;

    if (sc->dev_registered)
        bdev_unregister(sc->dev_id);

    unit = (int)sc->dev_id - VD_DISK_BASE;
    if (unit >= 0 && unit < VD_DISK_UNITS && unit < virtio_blk_next_unit)
        virtio_blk_next_unit = unit;

    if (sc->vdev.irq > 0)
        irq_unregister(sc->vdev.irq, "virtio_blk");

    if (sc->vdev.vqs[0])
        virtq_destroy(sc->vdev.vqs[0]);

    virtio_reset(&sc->vdev);
    if (sc->vdev.pci)
        pci_irq_free_vectors(sc->vdev.pci);

    memset(sc, 0, sizeof(*sc));
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

    if (virtio_blk_test_fail_remaining > 0) {
        virtio_blk_test_fail_remaining--;
        if (virtio_blk_test_fail_mode == VIRTIO_BLK_TEST_FAIL_IOERR)
            return VIRTIO_BLK_REQ_IOERR;
        if (virtio_blk_test_fail_mode == VIRTIO_BLK_TEST_FAIL_TRANSPORT)
            return VIRTIO_BLK_REQ_ERR;
        if (virtio_blk_test_fail_mode == VIRTIO_BLK_TEST_FAIL_UNSUPP)
            return VIRTIO_BLK_REQ_UNSUPP;
    }

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

    if (virtio_blk_wait_complete_locked(sc, vq) < 0) {
        release(&sc->lock);
        return -1;
    }
    release(&sc->lock);

    /* Pair with virtq completion path so DMA-updated status is visible. */
    __sync_synchronize();

    if (sc->request.status == VIRTIO_BLK_S_UNSUPP)
        return VIRTIO_BLK_REQ_UNSUPP;
    if (sc->request.status == VIRTIO_BLK_S_IOERR)
        return VIRTIO_BLK_REQ_IOERR;
    if (sc->request.status != VIRTIO_BLK_S_OK)
        return VIRTIO_BLK_REQ_ERR;

    return VIRTIO_BLK_REQ_OK;
}

static int
virtio_blk_wait_complete_locked(struct virtio_blk_softc *sc,
                                struct virtqueue *vq)
{
    uint32_t len;
    void *cookie;
    int spins;

    /*
     * Transitional virtio devices in QEMU can share a legacy IRQ line.
     * Poll the used ring with a bounded wait so a missed interrupt does not
     * wedge synchronous filesystem I/O forever.
     */
    for (spins = 0; spins < 200000; spins++) {
        if (sc->request.done)
            return 0;

        cookie = virtq_get_buf(vq, &len);
        if (cookie != 0) {
            sc->request.done = 1;
            return 0;
        }

        release(&sc->lock);
        microdelay(10);
        acquire(&sc->lock);
    }

    return -1;
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

    sc->io_last_type = type;
    sc->io_last_attempts = 0;
    sc->io_last_fail_class = VIRTIO_BLK_FAILCLASS_NONE;

    for (try = 0; try < attempts; try++) {
        sc->io_last_attempts = (uint32_t)(try + 1);
        rc = virtio_blk_submit_locked(sc, type, sector, data, data_len, data_is_write);
        if (rc == VIRTIO_BLK_REQ_OK) {
            sc->io_last_rc = rc;
            return VIRTIO_BLK_REQ_OK;
        }

        if (rc == VIRTIO_BLK_REQ_UNSUPP) {
            sc->io_unsupp++;
            sc->io_last_rc = rc;
            sc->io_last_fail_class = VIRTIO_BLK_FAILCLASS_UNSUPP;
            return rc;
        }

        if (rc == VIRTIO_BLK_REQ_IOERR) {
            sc->io_ioerr++;
            sc->io_last_fail_class = VIRTIO_BLK_FAILCLASS_IOERR;
        } else {
            sc->io_transport++;
            sc->io_last_fail_class = VIRTIO_BLK_FAILCLASS_TRANSPORT;
        }

        if (try + 1 >= attempts) {
            sc->io_last_rc = rc;
            sc->io_retry_exhausted++;
            return rc;
        }

        sc->io_retry++;
        if (rc == VIRTIO_BLK_REQ_IOERR)
            sc->io_retry_ioerr++;
        else
            sc->io_retry_transport++;
        microdelay(50 * (try + 1));
    }

    sc->io_last_rc = VIRTIO_BLK_REQ_ERR;
    return VIRTIO_BLK_REQ_ERR;
}

static int __attribute__((unused))
virtio_blk_write_zeroes_locked(struct virtio_blk_softc *sc,
                               uint64_t sector, uint32_t num_sectors,
                               int unmap)
{
    struct virtio_blk_discard_write_zeroes rz;
    int rc;

    if (!sc->has_write_zeroes || virtio_blk_force_no_write_zeroes)
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

    if (!sc->has_discard || virtio_blk_force_no_discard)
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

static int
virtio_blk_append_sint(char *buf, int max, int pos, int v)
{
    uint mag;
    long long sv;

    if (v < 0) {
        if (pos < max - 1)
            buf[pos++] = '-';
        sv = (long long)v;
        mag = (uint)(-sv);
    } else {
        mag = (uint)v;
    }

    return virtio_blk_append_uint(buf, max, pos, mag);
}

static int
virtio_blk_parse_u32(const char *s, int n, uint32_t *out, int *consumed)
{
    uint32_t v;
    int i;

    if (!s || n <= 0 || !out || !consumed)
        return -1;
    if (s[0] < '0' || s[0] > '9')
        return -1;

    v = 0;
    i = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        uint32_t d = (uint32_t)(s[i] - '0');
        if (v > (0xFFFFFFFFU - d) / 10U)
            return -1;
        v = v * 10U + d;
        i++;
    }

    *out = v;
    *consumed = i;
    return 0;
}

static int
virtio_blk_parse_u64(const char *s, int n, uint64_t *out, int *consumed)
{
    uint64_t v;
    int i;

    if (!s || n <= 0 || !out || !consumed)
        return -1;
    if (s[0] < '0' || s[0] > '9')
        return -1;

    v = 0;
    i = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        uint64_t d = (uint64_t)(s[i] - '0');
        if (v > (0xFFFFFFFFFFFFFFFFULL - d) / 10ULL)
            return -1;
        v = v * 10ULL + d;
        i++;
    }

    *out = v;
    *consumed = i;
    return 0;
}

static int
virtio_blk_admin_discard_by_dev(uint dev, uint64_t sector, uint32_t count)
{
    struct virtio_blk_softc *sc;
    int rc;

    if (count == 0)
        return -1;

    sc = virtio_blk_find_by_dev(dev);
    if (!sc)
        return -1;

    acquiresleep(&sc->req_lock);
    rc = virtio_blk_discard_locked(sc, sector, count);
    if (rc == VIRTIO_BLK_REQ_UNSUPP)
        sc->io_unsupp++;
    sc->admin_ops++;
    sc->admin_last_op = 1;
    sc->admin_last_rc = rc;
    sc->admin_last_sector = sector;
    sc->admin_last_count = count;
    releasesleep(&sc->req_lock);

    return 0;
}

static int
virtio_blk_admin_write_zeroes_by_dev(uint dev, uint64_t sector,
                                     uint32_t count, int unmap)
{
    struct virtio_blk_softc *sc;
    int rc;

    if (count == 0)
        return -1;

    sc = virtio_blk_find_by_dev(dev);
    if (!sc)
        return -1;

    acquiresleep(&sc->req_lock);
    rc = virtio_blk_write_zeroes_locked(sc, sector, count, unmap);
    if (rc == VIRTIO_BLK_REQ_UNSUPP)
        sc->io_unsupp++;
    sc->admin_ops++;
    sc->admin_last_op = 2;
    sc->admin_last_rc = rc;
    sc->admin_last_sector = sector;
    sc->admin_last_count = count;
    releasesleep(&sc->req_lock);

    return 0;
}

static int
virtio_blk_admin_discard_all(uint64_t sector, uint32_t count)
{
    int i;
    int found;

    if (count == 0)
        return -1;

    found = 0;
    for (i = 0; i < virtio_blk_count; i++) {
        virtio_blk_admin_discard_by_dev(virtio_blk_devices[i].dev_id, sector, count);
        found = 1;
    }

    return found ? 0 : -1;
}

static int
virtio_blk_admin_write_zeroes_all(uint64_t sector, uint32_t count, int unmap)
{
    int i;
    int found;

    if (count == 0)
        return -1;

    found = 0;
    for (i = 0; i < virtio_blk_count; i++) {
        virtio_blk_admin_write_zeroes_by_dev(virtio_blk_devices[i].dev_id,
                                             sector, count, unmap);
        found = 1;
    }

    return found ? 0 : -1;
}

static int
virtio_blk_admin_flush_by_dev(uint dev)
{
    struct virtio_blk_softc *sc;
    int rc;

    sc = virtio_blk_find_by_dev(dev);
    if (!sc)
        return -1;

    acquiresleep(&sc->req_lock);
    rc = virtio_blk_submit_with_retry_locked(sc, VIRTIO_BLK_T_FLUSH, 0, 0, 0, 0);
    if (rc == VIRTIO_BLK_REQ_OK)
        sc->flush_ok++;
    else
        sc->flush_fail++;
    sc->admin_ops++;
    sc->admin_last_op = 3;
    sc->admin_last_rc = rc;
    sc->admin_last_sector = 0;
    sc->admin_last_count = 0;
    releasesleep(&sc->req_lock);

    return 0;
}

static int
virtio_blk_admin_flush_all(void)
{
    int i;
    int found;

    found = 0;
    for (i = 0; i < virtio_blk_count; i++) {
        virtio_blk_admin_flush_by_dev(virtio_blk_devices[i].dev_id);
        found = 1;
    }

    return found ? 0 : -1;
}

static int
virtio_blk_admin_flush_first(void)
{
    if (virtio_blk_count < 1)
        return -1;
    return virtio_blk_admin_flush_by_dev(virtio_blk_devices[0].dev_id);
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
    int irq_mode;
    const char *irq_mode_name;

    if (virtio_blk_count >= MAX_VIRTIO_BLK)
        return -1;
    
    struct virtio_blk_softc *sc = &virtio_blk_devices[virtio_blk_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "virtio_blk");
    lockdep_set_rank(&sc->lock, LOCK_RANK_DEFAULT, "virtio_blk");
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
    
    /* Create virtqueue (size=0 → use hardware max) */
    struct virtqueue *vq = virtq_create(&sc->vdev, 0, 0);
    if (!vq) {
        virtio_reset(&sc->vdev);
        return -1;
    }
    sc->vq_size = (uint32_t)vq->size;
    
    /* Set driver data for interrupt handler */
    sc->vdev.driver_data = sc;
    sc->vdev.isr_handler = virtio_blk_intr;

    /* Allocate up to 2 vectors (current queue + future expansion) */
    int nvec = pci_irq_alloc_vectors(pci, 1, 2, PCI_IRQ_F_MSI | PCI_IRQ_F_INTX);
    if (nvec < 1) {
        cprintf("virtio_blk: failed to allocate IRQ vectors\n");
        virtq_destroy(vq);
        virtio_reset(&sc->vdev);
        return -1;
    }
    
    sc->num_vecs = nvec;
    sc->vdev.irq = pci_irq_vector(pci, 0);
    if (sc->vdev.irq < 0) {
        cprintf("virtio_blk: failed to get IRQ vector 0\n");
        pci_irq_free_vectors(pci);
        virtq_destroy(vq);
        virtio_reset(&sc->vdev);
        return -1;
    }
    
    /* Try to get vector 1 for future admin queue */
    sc->irq_req = pci_irq_vector(pci, 1);
    if (sc->irq_req < 0)
        sc->irq_req = sc->vdev.irq;  /* Fallback to vector 0 if vector 1 unavailable */

    if (virtq_set_vector(&sc->vdev, 0, 0) < 0) {
        cprintf("virtio_blk: failed to map queue 0 to vector 0\n");
        pci_irq_free_vectors(pci);
        virtq_destroy(vq);
        virtio_reset(&sc->vdev);
        return -1;
    }
    if (virtio_set_config_vector(&sc->vdev, 0) < 0) {
        cprintf("virtio_blk: failed to map config vector\n");
        pci_irq_free_vectors(pci);
        virtq_destroy(vq);
        virtio_reset(&sc->vdev);
        return -1;
    }
    
    /* Register IRQ handler on vector 0 */
    if (irq_register(sc->vdev.irq, virtio_blk_irq_handler, sc, "virtio_blk") < 0) {
        cprintf("virtio_blk: failed to register IRQ %d\n", sc->vdev.irq);
        pci_irq_free_vectors(pci);
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
        pci_irq_free_vectors(pci);
        virtq_destroy(vq);
        virtio_reset(&sc->vdev);
        cprintf("virtio_blk: no free block device slots\n");
        return -1;
    }
    sc->dev_id = dev_id;
    sc->dev_registered = 1;
    bdev_set_nblocks(sc->dev_id, sc->capacity / (BSIZE / 512));

    irq_mode = pci_irq_mode(pci);
    irq_mode_name = "intx";
    if (irq_mode == PCI_IRQ_MODE_MSI)
        irq_mode_name = "msi";
    else if (irq_mode == PCI_IRQ_MODE_MSIX)
        irq_mode_name = "msix";
    
    virtio_blk_count++;
    pci->driver_data = sc;
    cprintf("virtio_blk: attached device %d as dev=%d irq=%d mode=%s (nvecs=%d)\n",
            virtio_blk_count - 1, sc->dev_id, sc->vdev.irq, irq_mode_name, sc->num_vecs);
    
    return 0;
}

static int
virtio_blk_pci_probe(struct pci_dev *dev)
{
    return virtio_blk_probe(dev);
}

static void
virtio_blk_pci_remove(struct pci_dev *dev)
{
    struct virtio_blk_softc *sc;

    if (!dev)
        return;
    sc = (struct virtio_blk_softc *)dev->driver_data;
    if (!sc)
        return;

    virtio_blk_remove(sc);
    dev->driver_data = 0;
}

/*
 * Module init - register with PCI
 */
void
virtio_blk_init(void)
{
    int rc;

    BOOTDBG("virtio_blk: initializing driver\n");

    rc = pci_register_driver(&virtio_blk_pci_driver);
    if (rc < 0) {
        cprintf("virtio_blk: failed to register pci driver\n");
        return;
    }

    if (virtio_blk_count == 0)
        cprintf("virtio_blk: no compatible PCI device\n");
}
