/*
 * NVMe (Non-Volatile Memory Express) Driver for auxv6
 *
 * Supports NVMe 1.x storage controllers.
 *
 * Architecture:
 * - Memory-mapped I/O via PCI BAR0
 * - Admin queue for management commands
 * - I/O queues (one per CPU ideally) for data commands
 * - Submission/Completion queue pairs
 *
 * TODO Phase 1:
 * - [ ] PCI detection and BAR0 mapping
 * - [ ] Controller reset and initialization
 * - [ ] Admin queue setup
 * - [ ] IDENTIFY controller/namespace
 * - [ ] Basic I/O queue setup
 *
 * TODO Phase 2:
 * - [ ] Read/write command implementation
 * - [ ] Block device integration
 * - [ ] Interrupt handling
 * - [ ] Multiple I/O queues
 *
 * TODO Phase 3:
 * - [ ] Namespace management
 * - [ ] MSI-X support
 * - [ ] Power management
 *
 * Reference: NVM Express 1.4 Specification
 * See also: Linux drivers/nvme/host/pci.c
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "pci.h"
#include "blockdev.h"
#include "stdint.h" 
#include "fcntl.h"
#include "memlayout.h"

extern int ncpu;

/* NVMe Register Offsets */
#define NVME_REG_CAP        0x00    /* Controller Capabilities (64-bit) */
#define NVME_REG_VS         0x08    /* Version */
#define NVME_REG_INTMS      0x0C    /* Interrupt Mask Set */
#define NVME_REG_INTMC      0x10    /* Interrupt Mask Clear */
#define NVME_REG_CC         0x14    /* Controller Configuration */
#define NVME_REG_CSTS       0x1C    /* Controller Status */
#define NVME_REG_NSSR       0x20    /* NVM Subsystem Reset */
#define NVME_REG_AQA        0x24    /* Admin Queue Attributes */
#define NVME_REG_ASQ        0x28    /* Admin Submission Queue Base (64-bit) */
#define NVME_REG_ACQ        0x30    /* Admin Completion Queue Base (64-bit) */

/* Doorbell registers start at 0x1000 */
#define NVME_REG_SQ0TDBL    0x1000  /* Submission Queue 0 Tail Doorbell */

/* CAP Register Fields */
#define NVME_CAP_MQES(cap)     ((cap) & 0xFFFF)         /* Max Queue Entries Supported */
#define NVME_CAP_CQR(cap)      (((cap) >> 16) & 0x1)    /* Contiguous Queues Required */
#define NVME_CAP_AMS(cap)      (((cap) >> 17) & 0x3)    /* Arbitration Mechanism Supported */
#define NVME_CAP_TO(cap)       (((cap) >> 24) & 0xFF)   /* Timeout (500ms units) */
#define NVME_CAP_DSTRD(cap)    (((cap) >> 32) & 0xF)    /* Doorbell Stride */
#define NVME_CAP_NSSRS(cap)    (((cap) >> 36) & 0x1)    /* NVM Subsystem Reset Supported */
#define NVME_CAP_CSS(cap)      (((cap) >> 37) & 0xFF)   /* Command Sets Supported */
#define NVME_CAP_MPSMIN(cap)   (((cap) >> 48) & 0xF)    /* Memory Page Size Minimum */
#define NVME_CAP_MPSMAX(cap)   (((cap) >> 52) & 0xF)    /* Memory Page Size Maximum */

/* CC Register Fields */
#define NVME_CC_EN          (1 << 0)    /* Enable */
#define NVME_CC_CSS_NVM     (0 << 4)    /* NVM Command Set */
#define NVME_CC_MPS(n)      (((n) & 0xF) << 7)   /* Memory Page Size (2^(12+n)) */
#define NVME_CC_AMS_RR      (0 << 11)   /* Round Robin arbitration */
#define NVME_CC_SHN_NONE    (0 << 14)   /* No shutdown notification */
#define NVME_CC_SHN_NORMAL  (1 << 14)   /* Normal shutdown */
#define NVME_CC_SHN_ABRUPT  (2 << 14)   /* Abrupt shutdown */
#define NVME_CC_IOSQES(n)   (((n) & 0xF) << 16)  /* I/O SQ Entry Size (2^n) */
#define NVME_CC_IOCQES(n)   (((n) & 0xF) << 20)  /* I/O CQ Entry Size (2^n) */

/* CSTS Register Fields */
#define NVME_CSTS_RDY       (1 << 0)    /* Ready */
#define NVME_CSTS_CFS       (1 << 1)    /* Controller Fatal Status */
#define NVME_CSTS_SHST_MASK (3 << 2)    /* Shutdown Status */
#define NVME_CSTS_NSSRO     (1 << 4)    /* NVM Subsystem Reset Occurred */
#define NVME_CSTS_PP        (1 << 5)    /* Processing Paused */

/* Admin Commands */
#define NVME_ADMIN_DELETE_SQ    0x00
#define NVME_ADMIN_CREATE_SQ    0x01
#define NVME_ADMIN_GET_LOG_PAGE 0x02
#define NVME_ADMIN_DELETE_CQ    0x04
#define NVME_ADMIN_CREATE_CQ    0x05
#define NVME_ADMIN_IDENTIFY     0x06
#define NVME_ADMIN_ABORT        0x08
#define NVME_ADMIN_SET_FEATURES 0x09
#define NVME_ADMIN_GET_FEATURES 0x0A
#define NVME_ADMIN_ASYNC_EVENT  0x0C
#define NVME_ADMIN_NS_MGMT      0x0D
#define NVME_ADMIN_FW_COMMIT    0x10
#define NVME_ADMIN_FW_DOWNLOAD  0x11
#define NVME_ADMIN_NS_ATTACH    0x15
#define NVME_ADMIN_FORMAT_NVM   0x80
#define NVME_ADMIN_SECURITY_SEND 0x81
#define NVME_ADMIN_SECURITY_RECV 0x82

/* I/O Commands (NVM Command Set) */
#define NVME_CMD_FLUSH          0x00
#define NVME_CMD_WRITE          0x01
#define NVME_CMD_READ           0x02
#define NVME_CMD_WRITE_UNCOR    0x04
#define NVME_CMD_COMPARE        0x05
#define NVME_CMD_WRITE_ZEROES   0x08
#define NVME_CMD_DSM            0x09    /* Dataset Management */
#define NVME_CMD_VERIFY         0x0C
#define NVME_CMD_RESERVATION    0x0D
#define NVME_CMD_RESERVATION_R  0x0E
#define NVME_CMD_RESERVATION_A  0x11
#define NVME_CMD_RESERVATION_E  0x15

/* Submission Queue Entry (64 bytes) */
struct nvme_sqe {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t cid;           /* Command ID */
    uint32_t nsid;          /* Namespace ID */
    uint64_t reserved;
    uint64_t mptr;          /* Metadata Pointer */
    uint64_t prp1;          /* PRP Entry 1 */
    uint64_t prp2;          /* PRP Entry 2 */
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed));

/* Completion Queue Entry (16 bytes) */
struct nvme_cqe {
    uint32_t result;        /* Command-specific */
    uint32_t reserved;
    uint16_t sq_head;       /* SQ Head Pointer */
    uint16_t sq_id;         /* SQ Identifier */
    uint16_t cid;           /* Command ID */
    uint16_t status;        /* Status Field */
} __attribute__((packed));

#define NVME_CQE_STATUS_PHASE(s)  ((s) & 0x1)
#define NVME_CQE_STATUS_SC(s)     (((s) >> 1) & 0xFF)     /* Status Code */
#define NVME_CQE_STATUS_SCT(s)    (((s) >> 9) & 0x7)      /* Status Code Type */
#define NVME_CQE_STATUS_MORE(s)   (((s) >> 14) & 0x1)
#define NVME_CQE_STATUS_DNR(s)    (((s) >> 15) & 0x1)     /* Do Not Retry */

/* Identify Controller Data Structure (4KB) */
struct nvme_id_ctrl {
    uint16_t vid;           /* PCI Vendor ID */
    uint16_t ssvid;         /* PCI Subsystem Vendor ID */
    char     sn[20];        /* Serial Number */
    char     mn[40];        /* Model Number */
    char     fr[8];         /* Firmware Revision */
    uint8_t  rab;           /* Recommended Arbitration Burst */
    uint8_t  ieee[3];       /* IEEE OUI */
    uint8_t  cmic;          /* Controller Multi-Path I/O & NS Sharing */
    uint8_t  mdts;          /* Max Data Transfer Size (2^n * MPS) */
    uint16_t cntlid;        /* Controller ID */
    uint32_t ver;           /* Version */
    uint32_t rtd3r;         /* RTD3 Resume Latency */
    uint32_t rtd3e;         /* RTD3 Entry Latency */
    uint32_t oaes;          /* Optional Async Events Supported */
    uint32_t ctratt;        /* Controller Attributes */
    uint8_t  reserved[156];
    /* ... more fields omitted for brevity ... */
    uint8_t  padding[4096 - 256];
} __attribute__((packed));

/* Identify Namespace Data Structure (4KB) */
struct nvme_id_ns {
    uint64_t nsze;          /* Namespace Size */
    uint64_t ncap;          /* Namespace Capacity */
    uint64_t nuse;          /* Namespace Utilization */
    uint8_t  nsfeat;        /* Namespace Features */
    uint8_t  nlbaf;         /* Number of LBA Formats */
    uint8_t  flbas;         /* Formatted LBA Size */
    uint8_t  mc;            /* Metadata Capabilities */
    uint8_t  dpc;           /* End-to-end Data Protection */
    uint8_t  dps;           /* Data Protection Settings */
    uint8_t  nmic;          /* NS Multi-path I/O & Sharing */
    uint8_t  rescap;        /* Reservation Capabilities */
    uint8_t  fpi;           /* Format Progress Indicator */
    uint8_t  reserved[99];
    uint8_t  lbaf[64];      /* LBA Format Support */
    uint8_t  padding[4096 - 192];
} __attribute__((packed));

/* Queue configuration */
#define NVME_ADMIN_QUEUE_SIZE  64
#define NVME_IO_QUEUE_SIZE     256

/* Per-queue state */
struct nvme_queue {
    int qid;
    int depth;
    
    struct nvme_sqe *sq;    /* Submission Queue */
    struct nvme_cqe *cq;    /* Completion Queue */
    
    uint16_t sq_tail;       /* SQ tail (written by driver) */
    uint16_t cq_head;       /* CQ head (written by driver) */
    uint8_t  cq_phase;      /* Expected phase bit */
    
    volatile uint32_t *sq_doorbell;
    volatile uint32_t *cq_doorbell;
    
    struct sleeplock lock;
};

/* Per-controller state */
struct nvme_softc {
    struct pci_dev    *pci;
    struct spinlock    lock;
    
    volatile uint32_t *regs;
    uint64_t cap;
    uint32_t db_stride;     /* Doorbell stride in bytes */
    
    /* Admin queue */
    struct nvme_queue admin_q;
    
    /* I/O queues */
    struct nvme_queue *io_queues;
    int num_io_queues;
    
    /* Controller info */
    struct nvme_id_ctrl *id_ctrl;
    
    /* Namespace info */
    uint32_t nsid;          /* Currently active namespace */
    uint64_t nsze;          /* Namespace size in blocks */
    uint32_t lba_size;      /* Logical block size */
    uint64_t nblocks_512;   /* Capacity normalized to 512-byte blocks */

    int offline;
    uint32_t io_ok;
    uint32_t io_err;
    uint32_t io_timeout;
    uint32_t io_retry;
    uint32_t resets;
    uint32_t recover_fail;
    
    /* Block device ID */
    uint dev_id;
};

/* Global array of NVMe controllers */
#define MAX_NVME 4
static struct nvme_softc nvme_devices[MAX_NVME];
static int nvme_count = 0;

static int nvme_cmd_timeout_us = 1000000;
static int nvme_rw_retries = 1;

static int nvme_rw(struct buf *b);
static uint nvme_nblocks(uint dev);
static struct nvme_softc *nvme_find_by_dev(uint dev);
static int nvme_identify_namespace(struct nvme_softc *sc, uint32_t nsid);
static int nvme_enable_controller(struct nvme_softc *sc);
static int nvme_create_io_queues(struct nvme_softc *sc);
static int nvme_recover_controller(struct nvme_softc *sc, const char *why);
static int nvme_poll_completion(struct nvme_softc *sc, struct nvme_queue *q,
                                uint16_t cid, uint32_t *result);
static int nvme_append_str(char *buf, int max, int pos, const char *s);
static int nvme_append_uint(char *buf, int max, int pos, uint v);

static const struct bdevsw nvme_bdevsw = {
    .rw = nvme_rw,
    .nblocks = nvme_nblocks,
    .name = "nvme"
};

int
nvme_get_tune(char *buf, int max)
{
    int pos;
    int i;

    if (!buf || max <= 0)
        return -1;

    pos = 0;
    pos = nvme_append_str(buf, max, pos, "cmd_timeout_us=");
    pos = nvme_append_uint(buf, max, pos, (uint)nvme_cmd_timeout_us);
    pos = nvme_append_str(buf, max, pos, "\n");

    pos = nvme_append_str(buf, max, pos, "rw_retries=");
    pos = nvme_append_uint(buf, max, pos, (uint)nvme_rw_retries);
    pos = nvme_append_str(buf, max, pos, "\n");

    for (i = 0; i < nvme_count; i++) {
        struct nvme_softc *sc = &nvme_devices[i];
        pos = nvme_append_str(buf, max, pos, "dev=");
        pos = nvme_append_uint(buf, max, pos, sc->dev_id == (uint)-1 ? 0xFFFFFFFFU : sc->dev_id);
        pos = nvme_append_str(buf, max, pos, " nsid=");
        pos = nvme_append_uint(buf, max, pos, sc->nsid);
        pos = nvme_append_str(buf, max, pos, " offline=");
        pos = nvme_append_uint(buf, max, pos, (uint)sc->offline);
        pos = nvme_append_str(buf, max, pos, " ok=");
        pos = nvme_append_uint(buf, max, pos, sc->io_ok);
        pos = nvme_append_str(buf, max, pos, " err=");
        pos = nvme_append_uint(buf, max, pos, sc->io_err);
        pos = nvme_append_str(buf, max, pos, " timeout=");
        pos = nvme_append_uint(buf, max, pos, sc->io_timeout);
        pos = nvme_append_str(buf, max, pos, " retry=");
        pos = nvme_append_uint(buf, max, pos, sc->io_retry);
        pos = nvme_append_str(buf, max, pos, " resets=");
        pos = nvme_append_uint(buf, max, pos, sc->resets);
        pos = nvme_append_str(buf, max, pos, " recover_fail=");
        pos = nvme_append_uint(buf, max, pos, sc->recover_fail);
        pos = nvme_append_str(buf, max, pos, "\n");

        if (pos >= max - 1)
            return pos;
    }

    return pos;
}

int
nvme_set_tune(const char *buf, int n)
{
    uint v;
    int i;

    if (!buf || n <= 0)
        return -1;

    if (n > 0 && buf[n - 1] == '\n')
        n--;

    if (n >= 15 && memcmp(buf, "cmd_timeout_us=", 15) == 0) {
        v = 0;
        for (i = 15; i < n; i++) {
            if (buf[i] < '0' || buf[i] > '9')
                return -1;
            v = v * 10 + (uint)(buf[i] - '0');
        }
        if (v < 1000 || v > 30000000)
            return -1;
        nvme_cmd_timeout_us = (int)v;
        return 0;
    }

    if (n >= 11 && memcmp(buf, "rw_retries=", 11) == 0) {
        v = 0;
        for (i = 11; i < n; i++) {
            if (buf[i] < '0' || buf[i] > '9')
                return -1;
            v = v * 10 + (uint)(buf[i] - '0');
        }
        if (v > 8)
            return -1;
        nvme_rw_retries = (int)v;
        return 0;
    }

    return -1;
}

/*
 * Read controller register (32-bit)
 */
static uint32_t
nvme_read32(struct nvme_softc *sc, uint32_t off)
{
    return sc->regs[off / 4];
}

/*
 * Write controller register (32-bit)
 */
static void
nvme_write32(struct nvme_softc *sc, uint32_t off, uint32_t val)
{
    sc->regs[off / 4] = val;
}

/*
 * Read controller register (64-bit)
 */
static uint64_t
nvme_read64(struct nvme_softc *sc, uint32_t off)
{
    uint64_t lo = nvme_read32(sc, off);
    uint64_t hi = nvme_read32(sc, off + 4);
    return lo | (hi << 32);
}

/*
 * Write controller register (64-bit)
 */
static void
nvme_write64(struct nvme_softc *sc, uint32_t off, uint64_t val)
{
    nvme_write32(sc, off, val & 0xFFFFFFFF);
    nvme_write32(sc, off + 4, val >> 32);
}

/*
 * Wait for controller ready
 */
static int
nvme_wait_ready(struct nvme_softc *sc, int expected)
{
    uint32_t timeout = NVME_CAP_TO(sc->cap) * 500;  /* timeout in ms */
    
    for (uint32_t i = 0; i < timeout; i++) {
        uint32_t csts = nvme_read32(sc, NVME_REG_CSTS);
        
        if (csts & NVME_CSTS_CFS) {
            cprintf("nvme: controller fatal status\n");
            return -1;
        }
        
        if ((csts & NVME_CSTS_RDY) == expected)
            return 0;
        
        microdelay(1000);
    }
    
    cprintf("nvme: timeout waiting for ready=%d\n", expected);
    return -1;
}

/*
 * Reset controller
 */
static int
nvme_reset(struct nvme_softc *sc)
{
    /* Disable controller */
    nvme_write32(sc, NVME_REG_CC, 0);
    
    /* Wait for not ready */
    if (nvme_wait_ready(sc, 0) < 0)
        return -1;
    
    return 0;
}

/*
 * Initialize a queue
 */
static int
nvme_queue_init(struct nvme_softc *sc, struct nvme_queue *q, int qid, int depth)
{
    q->qid = qid;
    q->depth = depth;
    q->sq_tail = 0;
    q->cq_head = 0;
    q->cq_phase = 1;
    
    initsleeplock(&q->lock, "nvme_q");
    
    /* Allocate submission queue */
    q->sq = (struct nvme_sqe *)kalloc();
    if (!q->sq)
        return -1;
    memset(q->sq, 0, depth * sizeof(struct nvme_sqe));
    
    /* Allocate completion queue */
    q->cq = (struct nvme_cqe *)kalloc();
    if (!q->cq)
        return -1;
    memset(q->cq, 0, depth * sizeof(struct nvme_cqe));
    
    /* Set doorbell pointers */
    uint32_t db_off = 0x1000 + qid * 2 * sc->db_stride;
    q->sq_doorbell = (volatile uint32_t *)((char *)sc->regs + db_off);
    q->cq_doorbell = (volatile uint32_t *)((char *)sc->regs + db_off + sc->db_stride);
    
    return 0;
}

/*
 * Submit a command to a queue
 */
static int
nvme_submit_cmd(struct nvme_queue *q, struct nvme_sqe *cmd)
{
    acquiresleep(&q->lock);
    
    /* Copy command to submission queue */
    memmove(&q->sq[q->sq_tail], cmd, sizeof(*cmd));
    
    /* Advance tail */
    q->sq_tail = (q->sq_tail + 1) % q->depth;
    
    /* Ring doorbell */
    *q->sq_doorbell = q->sq_tail;
    
    releasesleep(&q->lock);
    
    return 0;
}

/*
 * Poll for completion
 */
static int
nvme_poll_completion(struct nvme_softc *sc, struct nvme_queue *q,
                     uint16_t cid, uint32_t *result)
{
    struct nvme_cqe *cqe;
    int i;
    int loops;

    loops = nvme_cmd_timeout_us / 10;
    if (loops < 1)
        loops = 1;

    /* Poll for completion */
    for (i = 0; i < loops; i++) {
        uint32_t csts;

        csts = nvme_read32(sc, NVME_REG_CSTS);
        if (csts & NVME_CSTS_CFS) {
            cprintf("nvme: controller fatal status during poll\n");
            return -3;
        }

        cqe = &q->cq[q->cq_head];
        
        if (NVME_CQE_STATUS_PHASE(cqe->status) == q->cq_phase) {
            /* Got a completion */
            if (cqe->cid == cid) {
                if (result)
                    *result = cqe->result;
                
                /* Advance head */
                q->cq_head++;
                if (q->cq_head >= q->depth) {
                    q->cq_head = 0;
                    q->cq_phase ^= 1;
                }
                
                /* Update doorbell */
                *q->cq_doorbell = q->cq_head;
                
                /* Check status */
                if (NVME_CQE_STATUS_SC(cqe->status) != 0) {
                    cprintf("nvme: command error: sc=%d sct=%d\n",
                            NVME_CQE_STATUS_SC(cqe->status),
                            NVME_CQE_STATUS_SCT(cqe->status));
                    return -1;
                }
                
                return 0;
            }
        }
        
        microdelay(10);
    }
    
    cprintf("nvme: completion timeout\n");
    return -2;
}

/*
 * Send IDENTIFY command
 */
static int
nvme_identify(struct nvme_softc *sc, uint32_t nsid, uint32_t cns, void *data)
{
    struct nvme_sqe cmd;
    memset(&cmd, 0, sizeof(cmd));
    
    cmd.opcode = NVME_ADMIN_IDENTIFY;
    cmd.nsid = nsid;
    cmd.prp1 = V2P(data);
    cmd.cdw10 = cns;
    cmd.cid = 1;
    
    nvme_submit_cmd(&sc->admin_q, &cmd);
    return nvme_poll_completion(sc, &sc->admin_q, 1, 0);
}

static struct nvme_softc *
nvme_find_by_dev(uint dev)
{
    for (int i = 0; i < nvme_count; i++) {
        if (nvme_devices[i].dev_id == dev)
            return &nvme_devices[i];
    }
    return 0;
}

/*
 * Create an I/O queue pair
 */
static int
nvme_create_io_queues(struct nvme_softc *sc)
{
    struct nvme_sqe cmd;
    struct nvme_queue *ioq;
    uint32_t dw10;
    
    /* Allocate I/O queue structure */
    sc->io_queues = (struct nvme_queue *)kalloc();
    if (!sc->io_queues)
        return -1;
    memset(sc->io_queues, 0, sizeof(struct nvme_queue));
    
    ioq = &sc->io_queues[0];
    
    /* Initialize the I/O queue */
    if (nvme_queue_init(sc, ioq, 1, NVME_IO_QUEUE_SIZE) < 0)
        return -1;
    
    /* Create I/O Completion Queue (IOCQ) */
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_CREATE_CQ;
    cmd.cid = 2;
    cmd.prp1 = V2P(ioq->cq);
    dw10 = ((NVME_IO_QUEUE_SIZE - 1) << 16) | 1;  /* size | qid */
    cmd.cdw10 = dw10;
    cmd.cdw11 = 1;  /* Physically contiguous */
    
    nvme_submit_cmd(&sc->admin_q, &cmd);
    if (nvme_poll_completion(sc, &sc->admin_q, 2, 0) < 0) {
        cprintf("nvme: create IO CQ failed\n");
        return -1;
    }
    
    /* Create I/O Submission Queue (IOSQ) */
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_CREATE_SQ;
    cmd.cid = 3;
    cmd.prp1 = V2P(ioq->sq);
    dw10 = ((NVME_IO_QUEUE_SIZE - 1) << 16) | 1;  /* size | qid */
    cmd.cdw10 = dw10;
    cmd.cdw11 = (1 << 16) | 1;  /* CQID=1 | Physically contiguous */
    
    nvme_submit_cmd(&sc->admin_q, &cmd);
    if (nvme_poll_completion(sc, &sc->admin_q, 3, 0) < 0) {
        cprintf("nvme: create IO SQ failed\n");
        return -1;
    }
    
    sc->num_io_queues = 1;
    cprintf("nvme: created I/O queue pair\n");
    
    return 0;
}

/*
 * Perform a READ or WRITE operation
 */
static int
nvme_do_rw(struct nvme_softc *sc, uint64_t lba, void *data, int is_write)
{
    struct nvme_queue *ioq;
    struct nvme_sqe cmd;
    uint16_t cid;
    uint32_t nlb;
    int max_attempts;
    int attempt;
    int rc;
    
    if (!sc || !sc->io_queues || sc->num_io_queues == 0)
        return -1;
    if (!data)
        return -1;
    
    ioq = &sc->io_queues[0];
    
    /* Calculate number of LBAs based on BSIZE and device LBA size */
    nlb = BSIZE / sc->lba_size;
    if (nlb == 0)
        nlb = 1;
    
    /* Prepare command */
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = is_write ? NVME_CMD_WRITE : NVME_CMD_READ;
    cmd.nsid = sc->nsid;
    cmd.prp1 = V2P(data);
    cmd.prp2 = 0;  /* Single page transfer */
    
    /* CDW10-11: Starting LBA (64-bit) */
    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
    cmd.cdw11 = (uint32_t)(lba >> 32);
    
    /* CDW12: NLB (0-based count) */
    cmd.cdw12 = nlb - 1;
    
    /* Command ID - use a simple counter */
    cid = (uint16_t)((ioq->sq_tail + 100) & 0xFFFF);
    cmd.cid = cid;
    
    max_attempts = 1 + nvme_rw_retries;
    if (max_attempts < 1)
        max_attempts = 1;

    for (attempt = 0; attempt < max_attempts; attempt++) {
        acquiresleep(&ioq->lock);

        /* Copy command to submission queue */
        memmove(&ioq->sq[ioq->sq_tail], &cmd, sizeof(cmd));

        /* Advance tail and ring doorbell */
        ioq->sq_tail = (ioq->sq_tail + 1) % ioq->depth;
        *ioq->sq_doorbell = ioq->sq_tail;

        releasesleep(&ioq->lock);

        /* Poll for completion */
        rc = nvme_poll_completion(sc, ioq, cid, 0);
        if (rc == 0)
            return 0;

        sc->io_err++;
        if (rc == -2)
            sc->io_timeout++;

        if (attempt + 1 >= max_attempts)
            break;

        if (rc == -2 || rc == -3) {
            if (nvme_recover_controller(sc, rc == -2 ? "io-timeout" : "fatal") < 0)
                break;
        }

        sc->io_retry++;
        microdelay(50 * (attempt + 1));
    }

    cprintf("nvme: %s failed lba=%d\n", is_write ? "write" : "read", (uint32_t)lba);
    return -1;

    
}

static int
nvme_recover_controller(struct nvme_softc *sc, const char *why)
{
    if (!sc)
        return -1;
    if (sc->offline)
        return -1;

    cprintf("nvme: recovering controller (%s)\n", why ? why : "unknown");
    sc->resets++;

    if (nvme_reset(sc) < 0)
        goto fail;
    if (nvme_enable_controller(sc) < 0)
        goto fail;
    if (nvme_identify_namespace(sc, sc->nsid ? sc->nsid : 1) < 0)
        goto fail;
    if (nvme_create_io_queues(sc) < 0)
        goto fail;

    return 0;

fail:
    sc->recover_fail++;
    sc->offline = 1;
    cprintf("nvme: controller marked offline after recovery failure\n");
    
    return -1;
}

static int
nvme_rw(struct buf *b)
{
    struct nvme_softc *sc;
    uint64_t lba;
    int is_write;

    if (!b)
        return -1;

    sc = nvme_find_by_dev(b->dev);
    if (!sc || sc->nsid == 0 || sc->num_io_queues == 0)
        return -1;
    if (sc->offline)
        return -1;

    /* Convert block number to LBA */
    lba = (uint64_t)b->blockno * (BSIZE / sc->lba_size);
    is_write = (b->flags & B_DIRTY) != 0;

    if (nvme_do_rw(sc, lba, b->data, is_write) < 0)
        return -1;

    sc->io_ok++;

    b->flags |= B_VALID;
    b->flags &= ~B_DIRTY;
    return 0;
}

static uint
nvme_nblocks(uint dev)
{
    struct nvme_softc *sc;

    sc = nvme_find_by_dev(dev);
    if (!sc)
        return 0;

    if (sc->nblocks_512 > 0xFFFFFFFFULL)
        return 0xFFFFFFFFU;
    return (uint)sc->nblocks_512;
}

static int
nvme_identify_namespace(struct nvme_softc *sc, uint32_t nsid)
{
    struct nvme_id_ns *idns;
    uint8_t lbaf_index;
    uint8_t lbads;
    uint64_t lba_count;
    int shift;

    idns = (struct nvme_id_ns *)kalloc();
    if (!idns)
        return -1;
    memset(idns, 0, 4096);

    if (nvme_identify(sc, nsid, 0, idns) < 0) {
        cprintf("nvme: identify namespace %d failed\n", nsid);
        kfree((char *)idns);
        return -1;
    }

    lbaf_index = idns->flbas & 0x0F;
    if (lbaf_index > idns->nlbaf || lbaf_index >= 16) {
        cprintf("nvme: invalid lbaf index %d\n", lbaf_index);
        kfree((char *)idns);
        return -1;
    }

    lbads = idns->lbaf[lbaf_index * 4 + 2];
    if (lbads < 9 || lbads > 31) {
        cprintf("nvme: unsupported lba size shift %d\n", lbads);
        kfree((char *)idns);
        return -1;
    }

    lba_count = idns->ncap ? idns->ncap : idns->nsze;
    if (lba_count == 0) {
        cprintf("nvme: namespace %d reports zero capacity\n", nsid);
        kfree((char *)idns);
        return -1;
    }

    sc->nsid = nsid;
    sc->nsze = idns->nsze;
    sc->lba_size = 1U << lbads;

    sc->nblocks_512 = lba_count;
    shift = (int)lbads - 9;
    if (shift > 0)
        sc->nblocks_512 <<= shift;
    else if (shift < 0)
        sc->nblocks_512 >>= -shift;

    cprintf("nvme: nsid=%d lba=%d bytes blocks512=%d\n",
            sc->nsid, sc->lba_size, (uint32_t)sc->nblocks_512);

    kfree((char *)idns);

    return 0;
}

static int
nvme_enable_controller(struct nvme_softc *sc)
{
    uint32_t cc;

    if (!sc)
        return -1;

    nvme_write32(sc, NVME_REG_AQA,
        ((NVME_ADMIN_QUEUE_SIZE - 1) << 16) |
        (NVME_ADMIN_QUEUE_SIZE - 1));

    nvme_write64(sc, NVME_REG_ASQ, V2P(sc->admin_q.sq));
    nvme_write64(sc, NVME_REG_ACQ, V2P(sc->admin_q.cq));

    cc = NVME_CC_EN |
         NVME_CC_CSS_NVM |
         NVME_CC_MPS(0) |
         NVME_CC_AMS_RR |
         NVME_CC_SHN_NONE |
         NVME_CC_IOSQES(6) |
         NVME_CC_IOCQES(4);

    nvme_write32(sc, NVME_REG_CC, cc);
    if (nvme_wait_ready(sc, NVME_CSTS_RDY) < 0)
        return -1;

    return 0;
}

/*
 * Initialize controller
 */
static int
nvme_init_controller(struct nvme_softc *sc)
{
    /* Read capabilities */
    sc->cap = nvme_read64(sc, NVME_REG_CAP);
    sc->db_stride = 4 << NVME_CAP_DSTRD(sc->cap);
    
    cprintf("nvme: cap=0x%x%x mqes=%d dstrd=%d\n",
            (uint32_t)(sc->cap >> 32), (uint32_t)sc->cap,
            (int)NVME_CAP_MQES(sc->cap), sc->db_stride);
    
    /* Reset controller */
    if (nvme_reset(sc) < 0)
        return -1;
    
    /* Initialize admin queue */
    if (nvme_queue_init(sc, &sc->admin_q, 0, NVME_ADMIN_QUEUE_SIZE) < 0)
        return -1;
    
    if (nvme_enable_controller(sc) < 0)
        return -1;
    
    cprintf("nvme: controller enabled\n");
    
    /* Allocate and execute IDENTIFY CONTROLLER */
    sc->id_ctrl = (struct nvme_id_ctrl *)kalloc();
    if (!sc->id_ctrl)
        return -1;
    memset(sc->id_ctrl, 0, 4096);
    
    if (nvme_identify(sc, 0, 1, sc->id_ctrl) < 0) {
        cprintf("nvme: identify controller failed\n");
        return -1;
    }
    
    /* Print controller info */
    char sn[21], mn[41];
    memmove(sn, sc->id_ctrl->sn, 20); sn[20] = 0;
    memmove(mn, sc->id_ctrl->mn, 40); mn[40] = 0;
    cprintf("nvme: model=%s serial=%s\n", mn, sn);

    /* Choose namespace 1 first and extract capacity metadata. */
    if (nvme_identify_namespace(sc, 1) < 0)
        cprintf("nvme: no usable namespace discovered yet\n");
    
    /* Create I/O queues for data transfer */
    if (sc->nsid != 0) {
        if (nvme_create_io_queues(sc) < 0)
            cprintf("nvme: failed to create I/O queues\n");
    }
    
    return 0;
}

static int
nvme_append_str(char *buf, int max, int pos, const char *s)
{
    while (s && *s && pos < max - 1)
        buf[pos++] = *s++;
    if (pos < max)
        buf[pos] = 0;
    return pos;
}

static int
nvme_append_uint(char *buf, int max, int pos, uint v)
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
 * PCI probe function
 */
int
nvme_probe(struct pci_dev *pci)
{
    if (nvme_count >= MAX_NVME)
        return -1;
    
    struct nvme_softc *sc = &nvme_devices[nvme_count];
    memset(sc, 0, sizeof(*sc));
    sc->dev_id = (uint)-1;
    initlock(&sc->lock, "nvme");
    sc->pci = pci;
    
    /* Map BAR0 */
    sc->regs = pci_map_bar(pci, 0);
    if (!sc->regs) {
        cprintf("nvme: failed to map BAR0\n");
        return -1;
    }
    
    cprintf("nvme: found at %d:%d.%d regs=%p\n",
            pci->bus, pci->slot, pci->func, sc->regs);
    
    /* Enable memory and bus master */
    pci_enable_mem(pci);
    pci_set_master(pci);
    
    /* Disable interrupts during init */
    nvme_write32(sc, NVME_REG_INTMS, 0xFFFFFFFF);
    
    /* Initialize controller */
    if (nvme_init_controller(sc) < 0)
        return -1;
    
    /* Enable interrupts */
    nvme_write32(sc, NVME_REG_INTMC, 0xFFFFFFFF);
    pci_enable_irq(pci, ncpu - 1);

    if (sc->nsid != 0 && sc->num_io_queues > 0 && nvme_count < ND_DISK_UNITS) {
        uint dev_id = ND_DISK_DEV(nvme_count);
        if (bdev_register(dev_id, &nvme_bdevsw) == 0) {
            uint blocks = sc->nblocks_512 > 0xFFFFFFFFULL ?
                          0xFFFFFFFFU : (uint)sc->nblocks_512;
            sc->dev_id = dev_id;
            if (blocks > 0)
                bdev_set_nblocks(dev_id, blocks);
            cprintf("nvme: registered dev=%d blocks=%d\n", dev_id, blocks);
        } else {
            cprintf("nvme: failed bdev_register dev=%d\n", dev_id);
        }
    }
    
    nvme_count++;
    
    return 0;
}

/*
 * Module init
 */
void
nvme_init(void)
{
    BOOTDBG("nvme: initializing driver\n");
    
    /* Search for NVMe PCI devices */
    for (int i = 0; i < pci_device_count(); i++) {
        struct pci_dev *dev = pci_get_device(i);
        if (!dev)
            continue;
        
        if (dev->class_code == PCI_CLASS_STORAGE &&
            dev->subclass == PCI_SUBCLASS_NVME) {
            nvme_probe(dev);
        }
    }
}
