/*
 * AHCI (Advanced Host Controller Interface) Driver for auxv6
 *
 * Supports SATA controllers with AHCI interface.
 *
 * Architecture:
 * - Memory-mapped I/O via PCI BAR5 (ABAR)
 * - Port-based architecture (up to 32 ports)
 * - Command list and FIS receive area per port
 * - Native Command Queuing (NCQ) support structure
 *
 * TODO Phase 1:
 * - [ ] PCI detection and ABAR mapping
 * - [ ] HBA reset and initialization
 * - [ ] Port detection and device identification
 * - [ ] Basic IDENTIFY DEVICE command
 *
 * TODO Phase 2:
 * - [ ] Read/write command implementation
 * - [ ] Block device integration
 * - [ ] Interrupt handling
 * - [ ] ATAPI support (CD-ROM)
 *
 * TODO Phase 3:
 * - [ ] NCQ support
 * - [ ] Hot-plug support
 * - [ ] Power management
 *
 * Reference: Serial ATA AHCI 1.3.1 Specification
 * See also: Linux drivers/ata/libahci.c, FreeBSD sys/dev/ahci/
 */

#include "types.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "buf.h"
#include "pci.h"
#include "blockdev.h"
#include "stdint.h"
#include "fcntl.h"
#include "memlayout.h"
#include "mmu.h"

extern int ncpu;


/* AHCI PCI Class/Subclass */
#define PCI_CLASS_STORAGE_SATA  0x0106

/* AHCI Generic Host Control Registers (offset from ABAR) */
#define AHCI_CAP        0x00    /* Host Capabilities */
#define AHCI_GHC        0x04    /* Global Host Control */
#define AHCI_IS         0x08    /* Interrupt Status */
#define AHCI_PI         0x0C    /* Ports Implemented */
#define AHCI_VS         0x10    /* Version */
#define AHCI_CCC_CTL    0x14    /* Command Completion Coalescing Control */
#define AHCI_CCC_PORTS  0x18    /* CCC Ports */
#define AHCI_EM_LOC     0x1C    /* Enclosure Management Location */
#define AHCI_EM_CTL     0x20    /* Enclosure Management Control */
#define AHCI_CAP2       0x24    /* Host Capabilities Extended */
#define AHCI_BOHC       0x28    /* BIOS/OS Handoff Control */

/* Port Registers (offset from ABAR + 0x100 + port * 0x80) */
#define AHCI_PxCLB      0x00    /* Command List Base Address */
#define AHCI_PxCLBU     0x04    /* Command List Base Address Upper */
#define AHCI_PxFB       0x08    /* FIS Base Address */
#define AHCI_PxFBU      0x0C    /* FIS Base Address Upper */
#define AHCI_PxIS       0x10    /* Interrupt Status */
#define AHCI_PxIE       0x14    /* Interrupt Enable */
#define AHCI_PxCMD      0x18    /* Command and Status */
#define AHCI_PxTFD      0x20    /* Task File Data */
#define AHCI_PxSIG      0x24    /* Signature */
#define AHCI_PxSSTS     0x28    /* SATA Status (SCR0: SStatus) */
#define AHCI_PxSCTL     0x2C    /* SATA Control (SCR2: SControl) */
#define AHCI_PxSERR     0x30    /* SATA Error (SCR1: SError) */
#define AHCI_PxSACT     0x34    /* SATA Active (NCQ) */
#define AHCI_PxCI       0x38    /* Command Issue */
#define AHCI_PxSNTF     0x3C    /* SATA Notification */
#define AHCI_PxFBS      0x40    /* FIS-based Switching Control */

/* Port interrupt status bits */
#define AHCI_PxIS_DHRS  0x00000001  /* Device-to-host register FIS */
#define AHCI_PxIS_DPS   0x00000020  /* Descriptor processed */
#define AHCI_PxIS_PCS   0x00000040  /* Port connect change */
#define AHCI_PxIS_PRCS  0x00400000  /* PhyRdy change */
#define AHCI_PxIS_TFES  0x40000000  /* Task File Error Status */

/* Driver timeouts for polling paths */
#define AHCI_TIMEOUT_PORT_IDLE_US   1000000
#define AHCI_TIMEOUT_CMD_US         1000000

/* CAP bits */
#define AHCI_CAP_NP         0x0000001F  /* Number of Ports (0-based) */
#define AHCI_CAP_SXS        0x00000020  /* External SATA */
#define AHCI_CAP_EMS        0x00000040  /* Enclosure Management */
#define AHCI_CAP_CCCS       0x00000080  /* Command Completion Coalescing */
#define AHCI_CAP_NCS        0x00001F00  /* Number of Command Slots */
#define AHCI_CAP_PSC        0x00002000  /* Partial State Capable */
#define AHCI_CAP_SSC        0x00004000  /* Slumber State Capable */
#define AHCI_CAP_PMD        0x00008000  /* PIO Multiple DRQ Block */
#define AHCI_CAP_FBSS       0x00010000  /* FIS-based Switching */
#define AHCI_CAP_SPM        0x00020000  /* Port Multiplier */
#define AHCI_CAP_SAM        0x00040000  /* AHCI Mode Only */
#define AHCI_CAP_ISS        0x00F00000  /* Interface Speed Support */
#define AHCI_CAP_SCLO       0x01000000  /* Command List Override */
#define AHCI_CAP_SAL        0x02000000  /* Activity LED */
#define AHCI_CAP_SALP       0x04000000  /* Aggressive Link PM */
#define AHCI_CAP_SSS        0x08000000  /* Staggered Spin-up */
#define AHCI_CAP_SMPS       0x10000000  /* Mech Presence Switch */
#define AHCI_CAP_SSNTF      0x20000000  /* SNotification Register */
#define AHCI_CAP_SNCQ       0x40000000  /* Native Command Queueing */
#define AHCI_CAP_S64A       0x80000000  /* 64-bit Addressing */

/* GHC bits */
#define AHCI_GHC_HR         0x00000001  /* HBA Reset */
#define AHCI_GHC_IE         0x00000002  /* Interrupt Enable */
#define AHCI_GHC_MRSM       0x00000004  /* MSI Revert to Single Message */
#define AHCI_GHC_AE         0x80000000  /* AHCI Enable */

/* PxCMD bits */
#define AHCI_PxCMD_ST       0x00000001  /* Start */
#define AHCI_PxCMD_SUD      0x00000002  /* Spin-Up Device */
#define AHCI_PxCMD_POD      0x00000004  /* Power On Device */
#define AHCI_PxCMD_CLO      0x00000008  /* Command List Override */
#define AHCI_PxCMD_FRE      0x00000010  /* FIS Receive Enable */
#define AHCI_PxCMD_CCS      0x00001F00  /* Current Command Slot */
#define AHCI_PxCMD_MPSS     0x00002000  /* Mech Presence Switch State */
#define AHCI_PxCMD_FR       0x00004000  /* FIS Receive Running */
#define AHCI_PxCMD_CR       0x00008000  /* Command List Running */
#define AHCI_PxCMD_CPS      0x00010000  /* Cold Presence State */
#define AHCI_PxCMD_PMA      0x00020000  /* Port Multiplier Attached */
#define AHCI_PxCMD_HPCP     0x00040000  /* Hot Plug Capable Port */
#define AHCI_PxCMD_MPSP     0x00080000  /* Mech Presence Switch Present */
#define AHCI_PxCMD_CPD      0x00100000  /* Cold Presence Detection */
#define AHCI_PxCMD_ESP      0x00200000  /* External SATA Port */
#define AHCI_PxCMD_FBSCP    0x00400000  /* FIS-based Switch Capable Port */
#define AHCI_PxCMD_APSTE    0x00800000  /* Auto Partial to Slumber Trans */
#define AHCI_PxCMD_ATAPI    0x01000000  /* Device is ATAPI */
#define AHCI_PxCMD_DLAE     0x02000000  /* Drive LED on ATAPI Enable */
#define AHCI_PxCMD_ALPE     0x04000000  /* Aggressive Link PM Enable */
#define AHCI_PxCMD_ASP      0x08000000  /* Aggressive Slumber/Partial */
#define AHCI_PxCMD_ICC      0xF0000000  /* Interface Communication Ctrl */

/* PxSSTS (SATA Status) bits */
#define AHCI_PxSSTS_DET     0x0000000F  /* Device Detection */
#define AHCI_PxSSTS_SPD     0x000000F0  /* Speed */
#define AHCI_PxSSTS_IPM     0x00000F00  /* Interface Power Management */

#define AHCI_PxSSTS_DET_NONE  0x0  /* No device detected */
#define AHCI_PxSSTS_DET_PRESENT 0x1  /* Device present, no comm */
#define AHCI_PxSSTS_DET_COMM  0x3  /* Device present and comm */
#define AHCI_PxSSTS_DET_OFFLINE 0x4  /* Phy offline */

/* PxTFD (Task File Data) bits */
#define AHCI_PxTFD_STS_ERR  0x01  /* Error */
#define AHCI_PxTFD_STS_DRQ  0x08  /* Data Request */
#define AHCI_PxTFD_STS_BSY  0x80  /* Busy */

/* Device signatures */
#define SATA_SIG_ATA    0x00000101  /* SATA drive */
#define SATA_SIG_ATAPI  0xEB140101  /* SATAPI drive */
#define SATA_SIG_SEMB   0xC33C0101  /* Enclosure management bridge */
#define SATA_SIG_PM     0x96690101  /* Port multiplier */

/* ATA Commands */
#define ATA_CMD_IDENTIFY       0xEC
#define ATA_CMD_READ_DMA_EX    0x25
#define ATA_CMD_WRITE_DMA_EX   0x35
#define ATA_CMD_PACKET         0xA0

/* ATAPI Packet Commands */
#define ATAPI_PKT_READ_CAPACITY 0x25
#define ATAPI_PKT_READ_10        0x28

/* FIS Types */
#define FIS_TYPE_REG_H2D    0x27  /* Host to Device Register */
#define FIS_TYPE_REG_D2H    0x34  /* Device to Host Register */
#define FIS_TYPE_DMA_ACT    0x39  /* DMA Activate */
#define FIS_TYPE_DMA_SETUP  0x41  /* DMA Setup */
#define FIS_TYPE_DATA       0x46  /* Data */
#define FIS_TYPE_BIST       0x58  /* BIST Activate */
#define FIS_TYPE_PIO_SETUP  0x5F  /* PIO Setup */
#define FIS_TYPE_DEV_BITS   0xA1  /* Set Device Bits */

/* Host to Device Register FIS */
struct fis_reg_h2d {
    uint8_t type;
    uint8_t flags;      /* bit 7: C (command/control), bits 4:0: PM Port */
    uint8_t command;
    uint8_t featurel;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;
    uint8_t countl;
    uint8_t counth;
    uint8_t icc;
    uint8_t control;
    uint8_t reserved[4];
} __attribute__((packed));

/* Command Header (Command List entry) */
struct ahci_cmd_hdr {
    uint16_t flags;     /* Bits 0-4: CFL, 5: A, 6: W, 7: P, 8-9: R, 10: B, 11: C, 12-15: PMP */
    uint16_t prdtl;     /* PRDT Length (entries) */
    uint32_t prdbc;     /* PRD Byte Count */
    uint32_t ctba;      /* Command Table Base Address */
    uint32_t ctbau;     /* Command Table Base Address Upper */
    uint32_t reserved[4];
} __attribute__((packed));

#define AHCI_CMD_FIS_LEN(dw)  ((dw) & 0x1F)
#define AHCI_CMD_ATAPI        (1 << 5)
#define AHCI_CMD_WRITE        (1 << 6)
#define AHCI_CMD_PREFETCH     (1 << 7)
#define AHCI_CMD_RESET        (1 << 8)
#define AHCI_CMD_BIST         (1 << 9)
#define AHCI_CMD_CLR_BUSY     (1 << 10)

/* Physical Region Descriptor Table Entry */
struct ahci_prdt_entry {
    uint32_t dba;       /* Data Base Address */
    uint32_t dbau;      /* Data Base Address Upper */
    uint32_t reserved;
    uint32_t dbc;       /* Data Byte Count (bit 31: Interrupt on Completion) */
} __attribute__((packed));

#define AHCI_PRDT_DBC_MASK  0x003FFFFF  /* Max 4MB per entry */
#define AHCI_PRDT_IOC       0x80000000  /* Interrupt on Completion */

/* Command Table */
#define AHCI_MAX_PRDT_ENTRIES 8

struct ahci_cmd_tbl {
    uint8_t cfis[64];   /* Command FIS */
    uint8_t acmd[16];   /* ATAPI Command */
    uint8_t reserved[48];
    struct ahci_prdt_entry prdt[AHCI_MAX_PRDT_ENTRIES];
} __attribute__((packed));

/* Received FIS structure */
struct ahci_recv_fis {
    uint8_t dsfis[28];  /* DMA Setup FIS */
    uint8_t reserved1[4];
    uint8_t psfis[20];  /* PIO Setup FIS */
    uint8_t reserved2[12];
    uint8_t rfis[20];   /* D2H Register FIS */
    uint8_t reserved3[4];
    uint8_t sdbfis[8];  /* Set Device Bits FIS */
    uint8_t ufis[64];   /* Unknown FIS */
    uint8_t reserved4[96];
} __attribute__((packed));

struct ahci_softc;

/* Per-port state */
struct ahci_port {
    struct ahci_softc *sc;
    int port_num;
    uint32_t sig;       /* Device signature */
    int type;           /* 0=none, 1=SATA, 2=SATAPI, 3=SEMB, 4=PM */
    
    /* Command list (1KB aligned) */
    struct ahci_cmd_hdr *cmd_list;
    
    /* Received FIS (256-byte aligned) */
    struct ahci_recv_fis *recv_fis;
    
    /* Command tables */
    struct ahci_cmd_tbl *cmd_tbl[32];
    
    /* Device info */
    uint64_t sectors;
    uint32_t sector_size;
    int dev_id;
    int online;
    int num_slots;
    uint32_t slot_inuse;

    /* Bring-up diagnostics counters */
    uint32_t io_ok;
    uint32_t io_err;
    uint32_t io_timeout;
    uint32_t io_tfes;
    uint32_t io_retry;
    uint32_t recover_fail;
    uint32_t recover_ok;
    uint32_t resets;
    uint32_t identify_fail;
    uint32_t intr_count;
    uint32_t last_is;
    uint32_t last_tfd;
    uint32_t last_serr;
    uint32_t last_ci;
    uint32_t last_sact;
    int use_interrupts;
    int irq_waiters;
    uint32_t recover_seq;
    
    struct sleeplock lock;
    struct spinlock irq_lock;
};

#define AHCI_PORT_TYPE_NONE   0
#define AHCI_PORT_TYPE_SATA   1
#define AHCI_PORT_TYPE_SATAPI 2
#define AHCI_PORT_TYPE_SEMB   3
#define AHCI_PORT_TYPE_PM     4

/* Per-HBA state */
struct ahci_softc {
    struct pci_dev    *pci;
    struct spinlock    lock;
    
    volatile uint32_t *regs;    /* ABAR mapped */
    uint32_t cap;               /* Capabilities */
    int num_ports;
    int num_cmd_slots;
    int use_interrupts;
    
    struct ahci_port ports[32];
};

/* Global array of AHCI controllers */
#define MAX_AHCI 4
static struct ahci_softc ahci_devices[MAX_AHCI];
static int ahci_count = 0;

static int ahci_cmd_timeout_us = AHCI_TIMEOUT_CMD_US;
static int ahci_idle_timeout_us = AHCI_TIMEOUT_PORT_IDLE_US;
static int ahci_rw_retries = 1;
static int ahci_test_fail_mode = 0;
static int ahci_test_fail_remaining = 0;
static int ahci_last_fail_class = 0;

#define AHCI_TEST_FAIL_NONE    0
#define AHCI_TEST_FAIL_TIMEOUT 1
#define AHCI_TEST_FAIL_TFES    2
#define AHCI_TEST_FAIL_IDLE    3
#define AHCI_TEST_FAIL_RECOVER 4

static uint32_t ahci_port_read(struct ahci_softc *sc, int port, int reg);
static void ahci_port_write(struct ahci_softc *sc, int port, int reg, uint32_t val);
static int ahci_rw(struct buf *b);
static uint ahci_nblocks(uint dev);
static struct ahci_port *ahci_find_port_by_dev(uint dev);
static int ahci_identify_port(struct ahci_softc *sc, struct ahci_port *p);
static int ahci_submit_rw(struct ahci_port *p, uint64_t lba, void *data,
                          uint32_t data_len, int is_write);
static int ahci_atapi_read_capacity(struct ahci_port *p);
static int ahci_submit_atapi(struct ahci_port *p, uint8_t *packet,
                             void *data, uint32_t data_len, int is_write);
static int ahci_atapi_rw(struct buf *b);
static uint ahci_atapi_nblocks(uint dev);
static int ahci_wait_cmd_done(struct ahci_port *p, uint32_t slot_mask,
                              int timeout_us, int *timed_out, uint32_t *is_out);
static int ahci_port_stop(struct ahci_softc *sc, int port);
static int ahci_port_start(struct ahci_softc *sc, int port);
static int ahci_port_recover(struct ahci_port *p, const char *why);
static void ahci_diag_port(struct ahci_softc *sc, int port, const char *tag);
static void ahci_reset_all_stats(void);
static int ahci_append_str(char *buf, int max, int pos, const char *s);
static int ahci_append_uint(char *buf, int max, int pos, uint v);
static uint32_t ahci_read(struct ahci_softc *sc, int reg);
static void ahci_write(struct ahci_softc *sc, int reg, uint32_t val);
static int ahci_alloc_slot(struct ahci_port *p);
static void ahci_free_slot(struct ahci_port *p, int slot);
static void ahci_snapshot_error(struct ahci_port *p, uint32_t is);
static void ahci_irq_handler(int irq, void *arg);

extern uint ticks;
extern struct spinlock tickslock;

int
ahci_get_tune(char *buf, int max)
{
    int pos;
    int i;
    int j;

    if (!buf || max <= 0)
        return -1;

    pos = 0;
    pos = ahci_append_str(buf, max, pos, "cmd_timeout_us=");
    pos = ahci_append_uint(buf, max, pos, (uint)ahci_cmd_timeout_us);
    pos = ahci_append_str(buf, max, pos, "\n");

    pos = ahci_append_str(buf, max, pos, "idle_timeout_us=");
    pos = ahci_append_uint(buf, max, pos, (uint)ahci_idle_timeout_us);
    pos = ahci_append_str(buf, max, pos, "\n");

    pos = ahci_append_str(buf, max, pos, "rw_retries=");
    pos = ahci_append_uint(buf, max, pos, (uint)ahci_rw_retries);
    pos = ahci_append_str(buf, max, pos, "\n");

    pos = ahci_append_str(buf, max, pos, "dbg_build=");
    pos = ahci_append_uint(buf, max, pos, (uint)DBG_AHCI);
    pos = ahci_append_str(buf, max, pos, "\n");

    pos = ahci_append_str(buf, max, pos, "test_fail_mode=");
    pos = ahci_append_uint(buf, max, pos, (uint)ahci_test_fail_mode);
    pos = ahci_append_str(buf, max, pos, "\n");

    pos = ahci_append_str(buf, max, pos, "test_fail_remaining=");
    pos = ahci_append_uint(buf, max, pos, (uint)ahci_test_fail_remaining);
    pos = ahci_append_str(buf, max, pos, "\n");

    pos = ahci_append_str(buf, max, pos, "last_fail_class=");
    pos = ahci_append_uint(buf, max, pos, (uint)ahci_last_fail_class);
    pos = ahci_append_str(buf, max, pos, "\n");

    for (i = 0; i < ahci_count; i++) {
        for (j = 0; j < 32; j++) {
            struct ahci_port *p = &ahci_devices[i].ports[j];
            if (!p->online)
                continue;

            pos = ahci_append_str(buf, max, pos, "hba=");
            pos = ahci_append_uint(buf, max, pos, (uint)i);
            pos = ahci_append_str(buf, max, pos, " port=");
            pos = ahci_append_uint(buf, max, pos, (uint)p->port_num);
            pos = ahci_append_str(buf, max, pos, " dev=");
            pos = ahci_append_uint(buf, max, pos, (uint)p->dev_id);
            pos = ahci_append_str(buf, max, pos, " type=");
            if (p->type == AHCI_PORT_TYPE_SATAPI)
                pos = ahci_append_str(buf, max, pos, "atapi");
            else if (p->type == AHCI_PORT_TYPE_SATA)
                pos = ahci_append_str(buf, max, pos, "sata");
            else
                pos = ahci_append_str(buf, max, pos, "other");
            pos = ahci_append_str(buf, max, pos, " ok=");
            pos = ahci_append_uint(buf, max, pos, p->io_ok);
            pos = ahci_append_str(buf, max, pos, " err=");
            pos = ahci_append_uint(buf, max, pos, p->io_err);
            pos = ahci_append_str(buf, max, pos, " timeout=");
            pos = ahci_append_uint(buf, max, pos, p->io_timeout);
            pos = ahci_append_str(buf, max, pos, " tfes=");
            pos = ahci_append_uint(buf, max, pos, p->io_tfes);
            pos = ahci_append_str(buf, max, pos, " retry=");
            pos = ahci_append_uint(buf, max, pos, p->io_retry);
            pos = ahci_append_str(buf, max, pos, " recover_fail=");
            pos = ahci_append_uint(buf, max, pos, p->recover_fail);
            pos = ahci_append_str(buf, max, pos, " recover_ok=");
            pos = ahci_append_uint(buf, max, pos, p->recover_ok);
            pos = ahci_append_str(buf, max, pos, " resets=");
            pos = ahci_append_uint(buf, max, pos, p->resets);
            pos = ahci_append_str(buf, max, pos, " identify_fail=");
            pos = ahci_append_uint(buf, max, pos, p->identify_fail);
            pos = ahci_append_str(buf, max, pos, " intr=");
            pos = ahci_append_uint(buf, max, pos, p->intr_count);
            pos = ahci_append_str(buf, max, pos, " last_is=");
            pos = ahci_append_uint(buf, max, pos, p->last_is);
            pos = ahci_append_str(buf, max, pos, " last_tfd=");
            pos = ahci_append_uint(buf, max, pos, p->last_tfd);
            pos = ahci_append_str(buf, max, pos, " last_serr=");
            pos = ahci_append_uint(buf, max, pos, p->last_serr);
            pos = ahci_append_str(buf, max, pos, " last_ci=");
            pos = ahci_append_uint(buf, max, pos, p->last_ci);
            pos = ahci_append_str(buf, max, pos, " last_sact=");
            pos = ahci_append_uint(buf, max, pos, p->last_sact);
            pos = ahci_append_str(buf, max, pos, "\n");

            if (pos >= max - 1)
                return pos;
        }
    }

    return pos;
}

int
ahci_set_tune(const char *buf, int n)
{
    uint v;
    int i;

    if (!buf || n <= 0)
        return -1;

    if (n >= 32)
        n = 31;

    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' ||
                     buf[n - 1] == ' ' || buf[n - 1] == '\t'))
        n--;

    if (n <= 0)
        return -1;

    if (n >= 15 && memcmp(buf, "cmd_timeout_us=", 15) == 0) {
        v = 0;
        for (i = 15; i < n; i++) {
            if (buf[i] < '0' || buf[i] > '9')
                break;
            v = v * 10 + (uint)(buf[i] - '0');
        }
        if (v < 1000 || v > 30000000)
            return -1;
        ahci_cmd_timeout_us = (int)v;
        return 0;
    }

    if (n >= 16 && memcmp(buf, "idle_timeout_us=", 16) == 0) {
        v = 0;
        for (i = 16; i < n; i++) {
            if (buf[i] < '0' || buf[i] > '9')
                break;
            v = v * 10 + (uint)(buf[i] - '0');
        }
        if (v < 1000 || v > 30000000)
            return -1;
        ahci_idle_timeout_us = (int)v;
        return 0;
    }

    if (n >= 11 && memcmp(buf, "rw_retries=", 11) == 0) {
        v = 0;
        for (i = 11; i < n; i++) {
            if (buf[i] < '0' || buf[i] > '9')
                break;
            v = v * 10 + (uint)(buf[i] - '0');
        }
        if (v > 8)
            return -1;
        ahci_rw_retries = (int)v;
        return 0;
    }

    if (n >= 15 && memcmp(buf, "test_fail_mode=", 15) == 0) {
        const char *mode = buf + 15;
        int mode_len = n - 15;

        if (mode_len == 1 && mode[0] >= '0' && mode[0] <= '3') {
            ahci_test_fail_mode = mode[0] - '0';
            return 0;
        }
        if (mode_len == 4 && memcmp(mode, "none", 4) == 0) {
            ahci_test_fail_mode = AHCI_TEST_FAIL_NONE;
            return 0;
        }
        if (mode_len == 7 && memcmp(mode, "timeout", 7) == 0) {
            ahci_test_fail_mode = AHCI_TEST_FAIL_TIMEOUT;
            return 0;
        }
        if (mode_len == 4 && memcmp(mode, "tfes", 4) == 0) {
            ahci_test_fail_mode = AHCI_TEST_FAIL_TFES;
            return 0;
        }
        if (mode_len == 4 && memcmp(mode, "idle", 4) == 0) {
            ahci_test_fail_mode = AHCI_TEST_FAIL_IDLE;
            return 0;
        }
        return -1;
    }

    if (n >= 16 && memcmp(buf, "test_fail_count=", 16) == 0) {
        v = 0;
        for (i = 16; i < n; i++) {
            if (buf[i] < '0' || buf[i] > '9')
                break;
            v = v * 10 + (uint)(buf[i] - '0');
        }
        if (v > 32)
            return -1;
        ahci_test_fail_remaining = (int)v;
        return 0;
    }

    if (n >= 13 && memcmp(buf, "reset_stats=1", 13) == 0) {
        ahci_reset_all_stats();
        ahci_last_fail_class = 0;
        return 0;
    }

    return -1;
}

static const struct bdevsw ahci_bdevsw = {
    .rw = ahci_rw,
    .nblocks = ahci_nblocks,
    .name = "ahci"
};

static const struct bdevsw ahci_atapi_bdevsw = {
    .rw = ahci_atapi_rw,
    .nblocks = ahci_atapi_nblocks,
    .name = "ahci-atapi"
};

static struct ahci_port *
ahci_find_port_by_dev(uint dev)
{
    int i;
    int j;

    for (i = 0; i < ahci_count; i++) {
        for (j = 0; j < 32; j++) {
            struct ahci_port *p = &ahci_devices[i].ports[j];
            if (p->online && p->dev_id == (int)dev)
                return p;
        }
    }

    return 0;
}

static int
ahci_rw(struct buf *b)
{
    struct ahci_port *p;
    uint64_t lba;
    int is_write;

    if (!b)
        return -1;

    p = ahci_find_port_by_dev(b->dev);
    if (!p)
        return -1;

    if (!p->online || p->sectors == 0)
        return -1;

    if (b->blockno >= (uint)p->sectors)
        return -1;

    lba = (uint64_t)b->blockno * (BSIZE / 512);
    if (lba + (BSIZE / 512) > p->sectors)
        return -1;

    is_write = (b->flags & B_DIRTY) != 0;

    if (ahci_submit_rw(p, lba, b->data, BSIZE, is_write) < 0) {
        cprintf("ahci: rw failed dev=%d block=%d write=%d\n",
                b->dev, b->blockno, is_write);
        return -1;
    }

    acquire(&p->irq_lock);
    p->io_ok++;
    release(&p->irq_lock);

    b->flags |= B_VALID;
    b->flags &= ~B_DIRTY;
    return 0;
}

static uint
ahci_nblocks(uint dev)
{
    struct ahci_port *p = ahci_find_port_by_dev(dev);
    if (!p)
        return 0;

    return (uint)p->sectors;
}

static uint
ahci_atapi_nblocks(uint dev)
{
    struct ahci_port *p = ahci_find_port_by_dev(dev);
    uint blocks_per_sector;
    uint64_t total_blocks;

    if (!p || !p->online)
        return 0;
    if (p->sector_size == 0)
        return 0;
    if ((p->sector_size % BSIZE) != 0)
        return 0;

    blocks_per_sector = p->sector_size / BSIZE;
    total_blocks = (uint64_t)p->sectors * (uint64_t)blocks_per_sector;
    if (total_blocks > 0xFFFFFFFFULL)
        return 0xFFFFFFFFU;
    return (uint)total_blocks;
}

static int
ahci_atapi_rw(struct buf *b)
{
    struct ahci_port *p;
    uint blocks_per_sector;
    uint64_t lba;
    uint offset;
    uint8_t packet[12];
    char *tmp;

    if (!b)
        return -1;
    if (b->flags & B_DIRTY)
        return -1;

    p = ahci_find_port_by_dev(b->dev);
    if (!p || !p->online)
        return -1;
    if (p->sector_size == 0)
        return -1;
    if ((p->sector_size % BSIZE) != 0)
        return -1;
    if (p->sector_size > PGSIZE)
        return -1;

    blocks_per_sector = p->sector_size / BSIZE;
    lba = b->blockno / blocks_per_sector;
    offset = (b->blockno % blocks_per_sector) * BSIZE;
    if (lba >= p->sectors)
        return -1;

    tmp = kalloc();
    if (!tmp)
        return -1;
    memset(tmp, 0, PGSIZE);

    memset(packet, 0, sizeof(packet));
    packet[0] = ATAPI_PKT_READ_10;
    packet[2] = (uint8_t)((lba >> 24) & 0xFF);
    packet[3] = (uint8_t)((lba >> 16) & 0xFF);
    packet[4] = (uint8_t)((lba >> 8) & 0xFF);
    packet[5] = (uint8_t)(lba & 0xFF);
    packet[7] = 0;
    packet[8] = 1;

    if (ahci_submit_atapi(p, packet, tmp, p->sector_size, 0) < 0) {
        kfree(tmp);
        return -1;
    }

    acquire(&p->irq_lock);
    p->io_ok++;
    release(&p->irq_lock);

    memmove(b->data, tmp + offset, BSIZE);
    kfree(tmp);

    b->flags |= B_VALID;
    b->flags &= ~B_DIRTY;
    return 0;
}

static int
ahci_wait_port_idle(struct ahci_softc *sc, int port, int timeout_us)
{
    int waited;

    for (waited = 0; waited < timeout_us; waited += 10) {
        uint32_t tfd = ahci_port_read(sc, port, AHCI_PxTFD);
        if ((tfd & (AHCI_PxTFD_STS_BSY | AHCI_PxTFD_STS_DRQ)) == 0)
            return 0;
        microdelay(10);
    }

    return -1;
}

static int
ahci_wait_cmd_done(struct ahci_port *p, uint32_t slot_mask,
                   int timeout_us, int *timed_out, uint32_t *is_out)
{
    struct ahci_softc *sc;
    int waited;
    uint ticks_start;
    uint ticks_now;
    int timeout_ticks;
    int port;

    if (!p || !p->sc)
        return -1;

    sc = p->sc;
    port = p->port_num;

    if (timed_out)
        *timed_out = 0;
    if (is_out)
        *is_out = 0;

    if (!p->use_interrupts) {
        for (waited = 0; waited < timeout_us; waited += 10) {
            uint32_t is = ahci_port_read(sc, port, AHCI_PxIS);
            uint32_t ci = ahci_port_read(sc, port, AHCI_PxCI);

            if (is_out)
                *is_out = is;

            if (is & AHCI_PxIS_TFES)
                return -1;
            if ((ci & slot_mask) == 0)
                return 0;
            microdelay(10);
        }

        if (timed_out)
            *timed_out = 1;
        return -1;
    }

    timeout_ticks = timeout_us / 10000;
    if (timeout_ticks < 1)
        timeout_ticks = 1;

    acquire(&tickslock);
    ticks_start = ticks;
    release(&tickslock);

    acquire(&p->irq_lock);
    for (;;) {
        uint32_t is = ahci_port_read(sc, port, AHCI_PxIS);
        uint32_t ci = ahci_port_read(sc, port, AHCI_PxCI);

        if (is_out)
            *is_out = is;

        if (is & AHCI_PxIS_TFES) {
            release(&p->irq_lock);
            return -1;
        }
        if ((ci & slot_mask) == 0) {
            release(&p->irq_lock);
            return 0;
        }

        acquire(&tickslock);
        ticks_now = ticks;
        release(&tickslock);
        if ((int)(ticks_now - ticks_start) >= timeout_ticks) {
            if (timed_out)
                *timed_out = 1;
            release(&p->irq_lock);
            return -1;
        }

        p->irq_waiters++;
        sleep(p, &p->irq_lock);
        p->irq_waiters--;
    }
}

static int
ahci_port_stop(struct ahci_softc *sc, int port)
{
    int i;
    uint32_t cmd;

    cmd = ahci_port_read(sc, port, AHCI_PxCMD);
    ahci_port_write(sc, port, AHCI_PxCMD, cmd & ~AHCI_PxCMD_ST);

    for (i = 0; i < 1000; i++) {
        if ((ahci_port_read(sc, port, AHCI_PxCMD) & AHCI_PxCMD_CR) == 0)
            break;
        microdelay(1000);
    }
    if (i == 1000)
        return -1;

    cmd = ahci_port_read(sc, port, AHCI_PxCMD);
    ahci_port_write(sc, port, AHCI_PxCMD, cmd & ~AHCI_PxCMD_FRE);

    for (i = 0; i < 1000; i++) {
        if ((ahci_port_read(sc, port, AHCI_PxCMD) & AHCI_PxCMD_FR) == 0)
            break;
        microdelay(1000);
    }
    if (i == 1000)
        return -1;

    return 0;
}

static int
ahci_port_start(struct ahci_softc *sc, int port)
{
    uint32_t cmd;

    cmd = ahci_port_read(sc, port, AHCI_PxCMD);
    ahci_port_write(sc, port, AHCI_PxCMD, cmd | AHCI_PxCMD_FRE);
    cmd = ahci_port_read(sc, port, AHCI_PxCMD);
    ahci_port_write(sc, port, AHCI_PxCMD, cmd | AHCI_PxCMD_ST);
    return 0;
}

static int
ahci_port_recover(struct ahci_port *p, const char *why)
{
    struct ahci_softc *sc;

    if (!p || !p->sc)
        return -1;

    sc = p->sc;
    p->resets++;
    cprintf("ahci: port %d recover (%s)\n", p->port_num, why ? why : "unknown");
    ahci_diag_port(sc, p->port_num, "before-recover");

    if (ahci_port_stop(sc, p->port_num) < 0)
        return -1;

    ahci_port_write(sc, p->port_num, AHCI_PxCI, 0);
    ahci_port_write(sc, p->port_num, AHCI_PxSACT, 0);
    ahci_port_write(sc, p->port_num, AHCI_PxIS, 0xFFFFFFFF);
    ahci_port_write(sc, p->port_num, AHCI_PxSERR, 0xFFFFFFFF);

    if (ahci_port_start(sc, p->port_num) < 0)
        return -1;

    if (ahci_wait_port_idle(sc, p->port_num, ahci_idle_timeout_us) < 0)
        return -1;

    ahci_diag_port(sc, p->port_num, "after-recover");

    acquire(&p->irq_lock);
    p->recover_ok++;
    p->recover_seq++;
    p->slot_inuse = 0;
    wakeup(p);
    release(&p->irq_lock);
    return 0;
}

static void
ahci_diag_port(struct ahci_softc *sc, int port, const char *tag)
{
    if (!DBG_AHCI)
        return;

    AHCIDBG("ahci:diag port=%d tag=%s cmd=0x%x ci=0x%x sact=0x%x is=0x%x serr=0x%x tfd=0x%x ssts=0x%x\n",
            port,
            tag ? tag : "-",
            ahci_port_read(sc, port, AHCI_PxCMD),
            ahci_port_read(sc, port, AHCI_PxCI),
            ahci_port_read(sc, port, AHCI_PxSACT),
            ahci_port_read(sc, port, AHCI_PxIS),
            ahci_port_read(sc, port, AHCI_PxSERR),
            ahci_port_read(sc, port, AHCI_PxTFD),
            ahci_port_read(sc, port, AHCI_PxSSTS));
}

static int
ahci_alloc_slot(struct ahci_port *p)
{
    int i;

    if (!p || p->num_slots <= 0)
        return -1;

    acquire(&p->irq_lock);
    for (;;) {
        for (i = 0; i < p->num_slots && i < 32; i++) {
            if ((p->slot_inuse & (1U << i)) == 0) {
                p->slot_inuse |= (1U << i);
                release(&p->irq_lock);
                return i;
            }
        }

        sleep(p, &p->irq_lock);
    }
}

static void
ahci_free_slot(struct ahci_port *p, int slot)
{
    if (!p || slot < 0 || slot >= 32)
        return;

    acquire(&p->irq_lock);
    p->slot_inuse &= ~(1U << slot);
    wakeup(p);
    release(&p->irq_lock);
}

static void
ahci_snapshot_error(struct ahci_port *p, uint32_t is)
{
    struct ahci_softc *sc;

    if (!p || !p->sc)
        return;

    sc = p->sc;
    p->last_is = is;
    p->last_tfd = ahci_port_read(sc, p->port_num, AHCI_PxTFD);
    p->last_serr = ahci_port_read(sc, p->port_num, AHCI_PxSERR);
    p->last_ci = ahci_port_read(sc, p->port_num, AHCI_PxCI);
    p->last_sact = ahci_port_read(sc, p->port_num, AHCI_PxSACT);
}

static void
ahci_irq_handler(int irq, void *arg)
{
    struct ahci_softc *sc = arg;
    uint32_t is;
    int port;

    if (!sc)
        return;

    is = ahci_read(sc, AHCI_IS);
    if (is == 0)
        return;

    ahci_write(sc, AHCI_IS, is);

    for (port = 0; port < sc->num_ports && port < 32; port++) {
        struct ahci_port *p;
        uint32_t pis;

        if ((is & (1U << port)) == 0)
            continue;

        p = &sc->ports[port];
        pis = ahci_port_read(sc, port, AHCI_PxIS);
        if (pis == 0)
            continue;

        ahci_port_write(sc, port, AHCI_PxIS, pis);

        acquire(&p->irq_lock);
        p->intr_count++;
        p->last_is = pis;
        p->last_tfd = ahci_port_read(sc, port, AHCI_PxTFD);
        p->last_serr = ahci_port_read(sc, port, AHCI_PxSERR);
        p->last_ci = ahci_port_read(sc, port, AHCI_PxCI);
        p->last_sact = ahci_port_read(sc, port, AHCI_PxSACT);
        if (p->irq_waiters)
            wakeup(p);
        release(&p->irq_lock);
    }
}

static void
ahci_reset_all_stats(void)
{
    int i;
    int j;

    for (i = 0; i < ahci_count; i++) {
        for (j = 0; j < 32; j++) {
            struct ahci_port *p = &ahci_devices[i].ports[j];
            p->io_ok = 0;
            p->io_err = 0;
            p->io_timeout = 0;
            p->io_tfes = 0;
            p->io_retry = 0;
            p->recover_fail = 0;
            p->recover_ok = 0;
            p->resets = 0;
            p->identify_fail = 0;
            p->intr_count = 0;
            p->last_is = 0;
            p->last_tfd = 0;
            p->last_serr = 0;
            p->last_ci = 0;
            p->last_sact = 0;
        }
    }
}

static int
ahci_append_str(char *buf, int max, int pos, const char *s)
{
    while (s && *s && pos < max - 1)
        buf[pos++] = *s++;
    if (pos < max)
        buf[pos] = 0;
    return pos;
}

static int
ahci_append_uint(char *buf, int max, int pos, uint v)
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
ahci_identify_port(struct ahci_softc *sc, struct ahci_port *p)
{
    uint16_t *id;
    uint64_t sectors;
    uint32_t ci;
    int timed_out;
    uint32_t is;
    int slot;

    if (!sc || !p || !p->cmd_list || !p->cmd_tbl[0])
        return -1;

    id = (uint16_t *)kalloc();
    if (!id)
        return -1;
    memset(id, 0, PGSIZE);

    if (ahci_wait_port_idle(sc, p->port_num, ahci_idle_timeout_us) < 0) {
        cprintf("ahci: port %d busy before IDENTIFY\n", p->port_num);
        p->identify_fail++;
        kfree((char *)id);
        return -1;
    }

    slot = ahci_alloc_slot(p);
    if (slot < 0) {
        kfree((char *)id);
        return -1;
    }

    memset(&p->cmd_list[slot], 0, sizeof(p->cmd_list[slot]));
    memset(p->cmd_tbl[slot], 0, sizeof(*p->cmd_tbl[slot]));

    p->cmd_list[slot].flags = AHCI_CMD_FIS_LEN(sizeof(struct fis_reg_h2d) / 4);
    p->cmd_list[slot].prdtl = 1;
    p->cmd_list[slot].ctba = V2P(p->cmd_tbl[slot]);
    p->cmd_list[slot].ctbau = 0;

    p->cmd_tbl[slot]->prdt[0].dba = V2P(id);
    p->cmd_tbl[slot]->prdt[0].dbau = 0;
    p->cmd_tbl[slot]->prdt[0].dbc = (512 - 1) | AHCI_PRDT_IOC;

    struct fis_reg_h2d *fis = (struct fis_reg_h2d *)p->cmd_tbl[slot]->cfis;
    memset(fis, 0, sizeof(*fis));
    fis->type = FIS_TYPE_REG_H2D;
    fis->flags = 1 << 7;  /* Command */
    fis->command = ATA_CMD_IDENTIFY;
    fis->device = 0;
    fis->countl = 1;

    ahci_port_write(sc, p->port_num, AHCI_PxIS, 0xFFFFFFFF);

    ci = ahci_port_read(sc, p->port_num, AHCI_PxCI);
    ahci_port_write(sc, p->port_num, AHCI_PxCI, ci | (1U << slot));

    if (ahci_wait_cmd_done(p, (1U << slot), ahci_cmd_timeout_us,
                           &timed_out, &is) < 0) {
        p->identify_fail++;
        ahci_snapshot_error(p, is);
        if (timed_out)
            cprintf("ahci: port %d IDENTIFY timeout\n", p->port_num);
        else
            cprintf("ahci: port %d IDENTIFY taskfile error is=0x%x\n", p->port_num, is);
        ahci_diag_port(sc, p->port_num, "identify-fail");
        ahci_port_recover(p, "identify");
        ahci_free_slot(p, slot);
        kfree((char *)id);
        return -1;
    }

    ahci_free_slot(p, slot);

    sectors = ((uint64_t)id[103] << 48) |
              ((uint64_t)id[102] << 32) |
              ((uint64_t)id[101] << 16) |
              ((uint64_t)id[100]);
    if (sectors == 0) {
        sectors = ((uint64_t)id[61] << 16) | id[60];
    }

    p->sectors = sectors;
    p->sector_size = 512;

    cprintf("ahci: port %d identify sectors=%d sector_size=%d\n",
            p->port_num, (uint32_t)p->sectors, p->sector_size);

    kfree((char *)id);
    return p->sectors > 0 ? 0 : -1;
}

static int
ahci_atapi_read_capacity(struct ahci_port *p)
{
    uint8_t packet[12];
    char *buf;
    uint32_t last_lba;
    uint32_t block_size;

    if (!p || !p->sc)
        return -1;

    buf = kalloc();
    if (!buf)
        return -1;
    memset(buf, 0, PGSIZE);

    memset(packet, 0, sizeof(packet));
    packet[0] = ATAPI_PKT_READ_CAPACITY;

    if (ahci_submit_atapi(p, packet, buf, 8, 0) < 0) {
        kfree(buf);
        return -1;
    }

    last_lba = ((uint8_t)buf[0] << 24) | ((uint8_t)buf[1] << 16) |
               ((uint8_t)buf[2] << 8) | (uint8_t)buf[3];
    block_size = ((uint8_t)buf[4] << 24) | ((uint8_t)buf[5] << 16) |
                 ((uint8_t)buf[6] << 8) | (uint8_t)buf[7];

    kfree(buf);

    if (block_size == 0)
        return -1;

    p->sector_size = block_size;
    p->sectors = (uint64_t)last_lba + 1;
    return 0;
}

static int
ahci_submit_rw(struct ahci_port *p, uint64_t lba, void *data,
               uint32_t data_len, int is_write)
{
    struct ahci_softc *sc;
    struct fis_reg_h2d *fis;
    uint32_t ci;
    int timed_out;
    uint16_t nsectors;
    uint32_t is;
    int attempt;
    int max_attempts;
    int slot;
    uint32_t seq;
    int seq_changed;

    if (!p || !p->sc || !p->cmd_list || !p->cmd_tbl[0] || !data)
        return -1;
    if (data_len == 0 || data_len > BSIZE)
        return -1;
    if ((data_len % 512) != 0)
        return -1;

    sc = p->sc;
    nsectors = (uint16_t)(data_len / 512);

    max_attempts = 1 + ahci_rw_retries;
    if (max_attempts < 1)
        max_attempts = 1;

    for (attempt = 0; attempt < max_attempts; attempt++) {
        slot = ahci_alloc_slot(p);
        if (slot < 0)
            return -1;

        acquire(&p->irq_lock);
        seq = p->recover_seq;
        release(&p->irq_lock);

        if (ahci_test_fail_mode == AHCI_TEST_FAIL_IDLE && ahci_test_fail_remaining > 0) {
            ahci_test_fail_remaining--;
            ahci_last_fail_class = AHCI_TEST_FAIL_IDLE;
            p->io_err++;
            p->io_timeout++;
            cprintf("ahci: port %d injected idle timeout write=%d lba=%d\n",
                    p->port_num, is_write, (uint32_t)lba);
            ahci_snapshot_error(p, ahci_port_read(sc, p->port_num, AHCI_PxIS));
            if (ahci_port_recover(p, "inject-idle") < 0) {
                p->recover_fail++;
                ahci_free_slot(p, slot);
                return -1;
            }
            if (attempt + 1 < max_attempts) {
                p->io_retry++;
                ahci_free_slot(p, slot);
                microdelay(50 * (attempt + 1));
                continue;
            }
            ahci_free_slot(p, slot);
            return -1;
        }

        if (ahci_wait_port_idle(sc, p->port_num, ahci_idle_timeout_us) < 0) {
            ahci_last_fail_class = AHCI_TEST_FAIL_IDLE;
            p->io_err++;
            p->io_timeout++;
            cprintf("ahci: port %d idle timeout before cmd write=%d lba=%d\n",
                    p->port_num, is_write, (uint32_t)lba);
            ahci_diag_port(sc, p->port_num, "port-idle-timeout");
            ahci_snapshot_error(p, ahci_port_read(sc, p->port_num, AHCI_PxIS));
            if (ahci_port_recover(p, "port-idle-timeout") < 0) {
                p->recover_fail++;
                ahci_free_slot(p, slot);
                return -1;
            }
            if (attempt + 1 < max_attempts) {
                p->io_retry++;
                ahci_free_slot(p, slot);
                microdelay(50 * (attempt + 1));
                continue;
            }
            ahci_free_slot(p, slot);
            return -1;
        }

        AHCIDBG("ahci: cmd port=%d dev=%d lba=%d sectors=%d write=%d attempt=%d\n",
                p->port_num, p->dev_id, (uint32_t)lba, (int)nsectors,
                is_write, attempt + 1);

        memset(&p->cmd_list[slot], 0, sizeof(p->cmd_list[slot]));
        memset(p->cmd_tbl[slot], 0, sizeof(*p->cmd_tbl[slot]));

        p->cmd_list[slot].flags = AHCI_CMD_FIS_LEN(sizeof(struct fis_reg_h2d) / 4);
        if (is_write)
            p->cmd_list[slot].flags |= AHCI_CMD_WRITE;
        p->cmd_list[slot].prdtl = 1;
        p->cmd_list[slot].ctba = V2P(p->cmd_tbl[slot]);
        p->cmd_list[slot].ctbau = 0;

        p->cmd_tbl[slot]->prdt[0].dba = V2P(data);
        p->cmd_tbl[slot]->prdt[0].dbau = 0;
        p->cmd_tbl[slot]->prdt[0].dbc = (data_len - 1) | AHCI_PRDT_IOC;

        fis = (struct fis_reg_h2d *)p->cmd_tbl[slot]->cfis;
        memset(fis, 0, sizeof(*fis));
        fis->type = FIS_TYPE_REG_H2D;
        fis->flags = 1 << 7;  /* Command */
        fis->command = is_write ? ATA_CMD_WRITE_DMA_EX : ATA_CMD_READ_DMA_EX;
        fis->device = 1 << 6;  /* LBA mode */
        fis->lba0 = (uint8_t)(lba & 0xFF);
        fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
        fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
        fis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
        fis->lba4 = (uint8_t)((lba >> 32) & 0xFF);
        fis->lba5 = (uint8_t)((lba >> 40) & 0xFF);
        fis->countl = (uint8_t)(nsectors & 0xFF);
        fis->counth = (uint8_t)((nsectors >> 8) & 0xFF);

        if (ahci_test_fail_remaining > 0 &&
            (ahci_test_fail_mode == AHCI_TEST_FAIL_TIMEOUT ||
             ahci_test_fail_mode == AHCI_TEST_FAIL_TFES)) {
            ahci_test_fail_remaining--;
            p->io_err++;
            if (ahci_test_fail_mode == AHCI_TEST_FAIL_TIMEOUT) {
                ahci_last_fail_class = AHCI_TEST_FAIL_TIMEOUT;
                p->io_timeout++;
                cprintf("ahci: port %d injected command timeout write=%d lba=%d\n",
                        p->port_num, is_write, (uint32_t)lba);
                ahci_snapshot_error(p, ahci_port_read(sc, p->port_num, AHCI_PxIS));
                if (ahci_port_recover(p, "inject-timeout") < 0) {
                    p->recover_fail++;
                    ahci_free_slot(p, slot);
                    return -1;
                }
            } else {
                ahci_last_fail_class = AHCI_TEST_FAIL_TFES;
                p->io_tfes++;
                cprintf("ahci: port %d injected taskfile error write=%d lba=%d\n",
                        p->port_num, is_write, (uint32_t)lba);
                ahci_snapshot_error(p, ahci_port_read(sc, p->port_num, AHCI_PxIS));
                if (ahci_port_recover(p, "inject-tfes") < 0) {
                    p->recover_fail++;
                    ahci_free_slot(p, slot);
                    return -1;
                }
            }

            if (attempt + 1 < max_attempts) {
                p->io_retry++;
                ahci_free_slot(p, slot);
                microdelay(50 * (attempt + 1));
                continue;
            }
            ahci_free_slot(p, slot);
            return -1;
        }

        ahci_port_write(sc, p->port_num, AHCI_PxIS, 0xFFFFFFFF);
        ahci_port_write(sc, p->port_num, AHCI_PxSERR, 0xFFFFFFFF);

        ci = ahci_port_read(sc, p->port_num, AHCI_PxCI);
        ahci_port_write(sc, p->port_num, AHCI_PxCI, ci | (1U << slot));

        if (ahci_wait_cmd_done(p, (1U << slot), ahci_cmd_timeout_us,
                               &timed_out, &is) == 0) {
            acquire(&p->irq_lock);
            seq_changed = (p->recover_seq != seq);
            release(&p->irq_lock);
            if (seq_changed) {
                ahci_last_fail_class = AHCI_TEST_FAIL_RECOVER;
                p->io_err++;
                if (attempt + 1 < max_attempts) {
                    p->io_retry++;
                    ahci_free_slot(p, slot);
                    microdelay(50 * (attempt + 1));
                    continue;
                }
                ahci_free_slot(p, slot);
                return -1;
            }
            ahci_free_slot(p, slot);
            return 0;
        }

        acquire(&p->irq_lock);
        seq_changed = (p->recover_seq != seq);
        release(&p->irq_lock);
        if (seq_changed) {
            ahci_last_fail_class = AHCI_TEST_FAIL_RECOVER;
            p->io_err++;
            if (attempt + 1 < max_attempts) {
                p->io_retry++;
                ahci_free_slot(p, slot);
                microdelay(50 * (attempt + 1));
                continue;
            }
            ahci_free_slot(p, slot);
            return -1;
        }

        p->io_err++;
        if (timed_out) {
            ahci_last_fail_class = AHCI_TEST_FAIL_TIMEOUT;
            p->io_timeout++;
            cprintf("ahci: port %d command timeout write=%d lba=%d\n",
                    p->port_num, is_write, (uint32_t)lba);
            ahci_diag_port(sc, p->port_num, "cmd-timeout");
            ahci_snapshot_error(p, is);
            if (ahci_port_recover(p, "cmd-timeout") < 0) {
                p->recover_fail++;
                ahci_free_slot(p, slot);
                return -1;
            }
        } else {
            ahci_last_fail_class = AHCI_TEST_FAIL_TFES;
            p->io_tfes++;
            cprintf("ahci: port %d taskfile error is=0x%x write=%d lba=%d\n",
                    p->port_num, is, is_write, (uint32_t)lba);
            ahci_diag_port(sc, p->port_num, "cmd-tfes");
            ahci_snapshot_error(p, is);
            if (ahci_port_recover(p, "cmd-tfes") < 0) {
                p->recover_fail++;
                ahci_free_slot(p, slot);
                return -1;
            }
        }

        if (attempt + 1 < max_attempts) {
            p->io_retry++;
            ahci_free_slot(p, slot);
            microdelay(50 * (attempt + 1));
        }
        ahci_free_slot(p, slot);
    }

    return -1;
}

static int
ahci_submit_atapi(struct ahci_port *p, uint8_t *packet,
                  void *data, uint32_t data_len, int is_write)
{
    struct ahci_softc *sc;
    struct fis_reg_h2d *fis;
    uint32_t ci;
    int timed_out;
    uint32_t is;
    int attempt;
    int max_attempts;
    int slot;
    uint32_t seq;
    int seq_changed;

    if (!p || !p->sc || !p->cmd_list || !p->cmd_tbl[0] || !packet)
        return -1;
    if (!data || data_len == 0)
        return -1;
    if (data_len > PGSIZE)
        return -1;

    sc = p->sc;
    max_attempts = 1 + ahci_rw_retries;
    if (max_attempts < 1)
        max_attempts = 1;

    for (attempt = 0; attempt < max_attempts; attempt++) {
        slot = ahci_alloc_slot(p);
        if (slot < 0)
            return -1;

        acquire(&p->irq_lock);
        seq = p->recover_seq;
        release(&p->irq_lock);

        if (ahci_wait_port_idle(sc, p->port_num, ahci_idle_timeout_us) < 0) {
            ahci_last_fail_class = AHCI_TEST_FAIL_IDLE;
            p->io_err++;
            p->io_timeout++;
            cprintf("ahci: port %d idle timeout before atapi cmd\n", p->port_num);
            ahci_diag_port(sc, p->port_num, "atapi-port-idle");
            ahci_snapshot_error(p, ahci_port_read(sc, p->port_num, AHCI_PxIS));
            if (ahci_port_recover(p, "atapi-port-idle") < 0) {
                p->recover_fail++;
                ahci_free_slot(p, slot);
                return -1;
            }
            if (attempt + 1 < max_attempts) {
                p->io_retry++;
                ahci_free_slot(p, slot);
                microdelay(50 * (attempt + 1));
                continue;
            }
            ahci_free_slot(p, slot);
            return -1;
        }

        memset(&p->cmd_list[slot], 0, sizeof(p->cmd_list[slot]));
        memset(p->cmd_tbl[slot], 0, sizeof(*p->cmd_tbl[slot]));

        p->cmd_list[slot].flags = AHCI_CMD_FIS_LEN(sizeof(struct fis_reg_h2d) / 4) |
                                  AHCI_CMD_ATAPI;
        if (is_write)
            p->cmd_list[slot].flags |= AHCI_CMD_WRITE;
        p->cmd_list[slot].prdtl = 1;
        p->cmd_list[slot].ctba = V2P(p->cmd_tbl[slot]);
        p->cmd_list[slot].ctbau = 0;

        p->cmd_tbl[slot]->prdt[0].dba = V2P(data);
        p->cmd_tbl[slot]->prdt[0].dbau = 0;
        p->cmd_tbl[slot]->prdt[0].dbc = (data_len - 1) | AHCI_PRDT_IOC;
        memmove(p->cmd_tbl[slot]->acmd, packet, 12);

        fis = (struct fis_reg_h2d *)p->cmd_tbl[slot]->cfis;
        memset(fis, 0, sizeof(*fis));
        fis->type = FIS_TYPE_REG_H2D;
        fis->flags = 1 << 7;  /* Command */
        fis->command = ATA_CMD_PACKET;
        fis->featurel = 1;    /* DMA */

        ahci_port_write(sc, p->port_num, AHCI_PxIS, 0xFFFFFFFF);
        ahci_port_write(sc, p->port_num, AHCI_PxSERR, 0xFFFFFFFF);

        ci = ahci_port_read(sc, p->port_num, AHCI_PxCI);
        ahci_port_write(sc, p->port_num, AHCI_PxCI, ci | (1U << slot));

        if (ahci_wait_cmd_done(p, (1U << slot), ahci_cmd_timeout_us,
                               &timed_out, &is) == 0) {
            acquire(&p->irq_lock);
            seq_changed = (p->recover_seq != seq);
            release(&p->irq_lock);
            if (seq_changed) {
                ahci_last_fail_class = AHCI_TEST_FAIL_RECOVER;
                p->io_err++;
                if (attempt + 1 < max_attempts) {
                    p->io_retry++;
                    ahci_free_slot(p, slot);
                    microdelay(50 * (attempt + 1));
                    continue;
                }
                ahci_free_slot(p, slot);
                return -1;
            }
            ahci_free_slot(p, slot);
            return 0;
        }

        acquire(&p->irq_lock);
        seq_changed = (p->recover_seq != seq);
        release(&p->irq_lock);
        if (seq_changed) {
            ahci_last_fail_class = AHCI_TEST_FAIL_RECOVER;
            p->io_err++;
            if (attempt + 1 < max_attempts) {
                p->io_retry++;
                ahci_free_slot(p, slot);
                microdelay(50 * (attempt + 1));
                continue;
            }
            ahci_free_slot(p, slot);
            return -1;
        }

        p->io_err++;
        if (timed_out) {
            ahci_last_fail_class = AHCI_TEST_FAIL_TIMEOUT;
            p->io_timeout++;
            cprintf("ahci: port %d atapi timeout\n", p->port_num);
            ahci_diag_port(sc, p->port_num, "atapi-timeout");
            ahci_snapshot_error(p, is);
            if (ahci_port_recover(p, "atapi-timeout") < 0) {
                p->recover_fail++;
                ahci_free_slot(p, slot);
                return -1;
            }
        } else {
            ahci_last_fail_class = AHCI_TEST_FAIL_TFES;
            p->io_tfes++;
            cprintf("ahci: port %d atapi taskfile error is=0x%x\n",
                    p->port_num, is);
            ahci_diag_port(sc, p->port_num, "atapi-tfes");
            ahci_snapshot_error(p, is);
            if (ahci_port_recover(p, "atapi-tfes") < 0) {
                p->recover_fail++;
                ahci_free_slot(p, slot);
                return -1;
            }
        }

        if (attempt + 1 < max_attempts) {
            p->io_retry++;
            ahci_free_slot(p, slot);
            microdelay(50 * (attempt + 1));
        }
        ahci_free_slot(p, slot);
    }

    return -1;
}

/*
 * Read HBA register
 */
static uint32_t
ahci_read(struct ahci_softc *sc, int reg)
{
    return sc->regs[reg / 4];
}

/*
 * Write HBA register
 */
static void
ahci_write(struct ahci_softc *sc, int reg, uint32_t val)
{
    sc->regs[reg / 4] = val;
}

/*
 * Read port register
 */
static uint32_t
ahci_port_read(struct ahci_softc *sc, int port, int reg)
{
    return sc->regs[(0x100 + port * 0x80 + reg) / 4];
}

/*
 * Write port register
 */
static void
ahci_port_write(struct ahci_softc *sc, int port, int reg, uint32_t val)
{
    sc->regs[(0x100 + port * 0x80 + reg) / 4] = val;
}

/*
 * Reset HBA
 */
static int
ahci_reset(struct ahci_softc *sc)
{
    /* Enable AHCI mode */
    ahci_write(sc, AHCI_GHC, ahci_read(sc, AHCI_GHC) | AHCI_GHC_AE);
    
    /* Trigger HBA reset */
    ahci_write(sc, AHCI_GHC, ahci_read(sc, AHCI_GHC) | AHCI_GHC_HR);
    
    /* Wait for reset to complete */
    for (int i = 0; i < 1000; i++) {
        if (!(ahci_read(sc, AHCI_GHC) & AHCI_GHC_HR))
            break;
        microdelay(1000);
    }
    
    if (ahci_read(sc, AHCI_GHC) & AHCI_GHC_HR) {
        cprintf("ahci: reset timeout\n");
        return -1;
    }
    
    /* Re-enable AHCI mode after reset */
    ahci_write(sc, AHCI_GHC, ahci_read(sc, AHCI_GHC) | AHCI_GHC_AE);
    
    return 0;
}

/*
 * Detect device type from signature
 */
static int
ahci_detect_device_type(uint32_t sig)
{
    switch (sig) {
    case SATA_SIG_ATA:
        return AHCI_PORT_TYPE_SATA;
    case SATA_SIG_ATAPI:
        return AHCI_PORT_TYPE_SATAPI;
    case SATA_SIG_SEMB:
        return AHCI_PORT_TYPE_SEMB;
    case SATA_SIG_PM:
        return AHCI_PORT_TYPE_PM;
    default:
        return AHCI_PORT_TYPE_NONE;
    }
}

/*
 * Initialize a port
 */
static int
ahci_port_init(struct ahci_softc *sc, int port)
{
    struct ahci_port *p = &sc->ports[port];
    
    p->sc = sc;
    p->port_num = port;
    p->dev_id = -1;
    p->online = 0;
    p->io_ok = 0;
    p->io_err = 0;
    p->io_timeout = 0;
    p->io_tfes = 0;
    p->io_retry = 0;
    p->recover_fail = 0;
    p->recover_ok = 0;
    p->resets = 0;
    p->identify_fail = 0;
    p->intr_count = 0;
    p->last_is = 0;
    p->last_tfd = 0;
    p->last_serr = 0;
    p->last_ci = 0;
    p->last_sact = 0;
    p->recover_seq = 0;
    p->slot_inuse = 0;
    p->num_slots = 0;
    p->use_interrupts = sc->use_interrupts;
    p->irq_waiters = 0;
    p->recover_seq = 0;
    initsleeplock(&p->lock, "ahci_port");
    initlock(&p->irq_lock, "ahci_irq");
    
    /* Check if device present */
    uint32_t ssts = ahci_port_read(sc, port, AHCI_PxSSTS);
    if ((ssts & AHCI_PxSSTS_DET) != AHCI_PxSSTS_DET_COMM) {
        p->type = AHCI_PORT_TYPE_NONE;
        return 0;
    }
    
    /* Stop port if running */
    uint32_t cmd = ahci_port_read(sc, port, AHCI_PxCMD);
    if (cmd & (AHCI_PxCMD_ST | AHCI_PxCMD_CR | AHCI_PxCMD_FRE | AHCI_PxCMD_FR)) {
        ahci_port_write(sc, port, AHCI_PxCMD, cmd & ~AHCI_PxCMD_ST);
        
        /* Wait for command list to become idle */
        for (int i = 0; i < 1000; i++) {
            if (!(ahci_port_read(sc, port, AHCI_PxCMD) & AHCI_PxCMD_CR))
                break;
            microdelay(1000);
        }
        
        ahci_port_write(sc, port, AHCI_PxCMD,
            ahci_port_read(sc, port, AHCI_PxCMD) & ~AHCI_PxCMD_FRE);
    }
    
    /* Allocate command list (1KB, 1KB-aligned) */
    p->cmd_list = (struct ahci_cmd_hdr *)kalloc();
    if (!p->cmd_list)
        return -1;
    memset(p->cmd_list, 0, 1024);
    
    /* Allocate received FIS area (256 bytes, 256-byte aligned) */
    p->recv_fis = (struct ahci_recv_fis *)kalloc();
    if (!p->recv_fis)
        return -1;
    memset(p->recv_fis, 0, sizeof(*p->recv_fis));
    
    p->num_slots = sc->num_cmd_slots;
    if (p->num_slots > 32)
        p->num_slots = 32;
    if (p->num_slots < 1)
        p->num_slots = 1;

    /* Allocate command tables for each slot */
    for (int i = 0; i < p->num_slots; i++) {
        p->cmd_tbl[i] = (struct ahci_cmd_tbl *)kalloc();
        if (!p->cmd_tbl[i]) {
            if (i == 0)
                return -1;
            p->num_slots = i;
            break;
        }
        memset(p->cmd_tbl[i], 0, sizeof(*p->cmd_tbl[i]));
        p->cmd_list[i].ctba = V2P(p->cmd_tbl[i]);
        p->cmd_list[i].ctbau = 0;
    }
    
    /* Set command list and FIS base addresses */
    ahci_port_write(sc, port, AHCI_PxCLB, V2P(p->cmd_list));
    ahci_port_write(sc, port, AHCI_PxCLBU, 0);
    ahci_port_write(sc, port, AHCI_PxFB, V2P(p->recv_fis));
    ahci_port_write(sc, port, AHCI_PxFBU, 0);
    
    /* Clear error register */
    ahci_port_write(sc, port, AHCI_PxSERR, 0xFFFFFFFF);
    
    /* Enable FIS receive and start port */
    ahci_port_write(sc, port, AHCI_PxCMD,
        ahci_port_read(sc, port, AHCI_PxCMD) | AHCI_PxCMD_FRE | AHCI_PxCMD_ST);

    ahci_port_write(sc, port, AHCI_PxIS, 0xFFFFFFFF);
    if (p->use_interrupts)
        ahci_port_write(sc, port, AHCI_PxIE, 0xFFFFFFFF);
    else
        ahci_port_write(sc, port, AHCI_PxIE, 0);
    
    /* Read device signature */
    p->sig = ahci_port_read(sc, port, AHCI_PxSIG);
    p->type = ahci_detect_device_type(p->sig);
    
    cprintf("ahci: port %d: type=%d sig=0x%x\n", port, p->type, p->sig);

    if (p->type == AHCI_PORT_TYPE_SATA) {
        int dev_id = HD_DISK_DEV(port);
        if (dev_id >= 0 && dev_id < NDEV) {
            if (ahci_identify_port(sc, p) == 0) {
                if (bdev_register(dev_id, &ahci_bdevsw) == 0) {
                    p->dev_id = dev_id;
                    p->online = 1;
                    if (p->sectors > 0)
                        bdev_set_nblocks(dev_id, (uint)p->sectors);
                    cprintf("ahci: port %d registered as dev=%d blocks=%d\n",
                            port, dev_id, (uint32_t)p->sectors);
                } else {
                    cprintf("ahci: port %d failed bdev_register dev=%d\n", port, dev_id);
                }
            } else {
                cprintf("ahci: port %d identify failed\n", port);
            }
        } else {
            cprintf("ahci: port %d has no valid dev slot\n", port);
        }
    } else if (p->type == AHCI_PORT_TYPE_SATAPI) {
        int dev_id = HD_DISK_DEV(port);
        if (dev_id >= 0 && dev_id < NDEV) {
            if (ahci_atapi_read_capacity(p) == 0) {
                uint blocks_per_sector;
                uint64_t total_blocks;
                uint nblocks;

                if ((p->sector_size % BSIZE) != 0) {
                    cprintf("ahci: port %d ATAPI sector size %d unsupported\n",
                            port, p->sector_size);
                    return -1;
                }

                blocks_per_sector = p->sector_size / BSIZE;
                total_blocks = (uint64_t)p->sectors * (uint64_t)blocks_per_sector;
                if (total_blocks > 0xFFFFFFFFULL)
                    total_blocks = 0xFFFFFFFFULL;
                nblocks = (uint)total_blocks;

                if (nblocks > 0 && bdev_register(dev_id, &ahci_atapi_bdevsw) == 0) {
                    p->dev_id = dev_id;
                    p->online = 1;
                    bdev_set_nblocks(dev_id, nblocks);
                    cprintf("ahci: port %d registered ATAPI dev=%d blocks=%d sector=%d\n",
                            port, dev_id, nblocks, p->sector_size);
                } else {
                    cprintf("ahci: port %d failed ATAPI bdev_register dev=%d\n",
                            port, dev_id);
                }
            } else {
                cprintf("ahci: port %d ATAPI READ CAPACITY failed\n", port);
            }
        } else {
            cprintf("ahci: port %d has no valid ATAPI dev slot\n", port);
        }
    }
    
    return 0;
}

/*
 * PCI probe function
 */
int
ahci_probe(struct pci_dev *pci)
{
    if (ahci_count >= MAX_AHCI)
        return -1;
    
    struct ahci_softc *sc = &ahci_devices[ahci_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "ahci");
    sc->pci = pci;
    
    /* Map BAR5 (ABAR) */
    sc->regs = pci_map_bar(pci, 5);
    if (!sc->regs) {
        cprintf("ahci: failed to map ABAR\n");
        return -1;
    }
    
    /* Enable memory and bus master */
    pci_enable_mem(pci);
    pci_set_master(pci);
    
    /* Read version */
    uint32_t vs = ahci_read(sc, AHCI_VS);
    cprintf("ahci: version %d.%d%d\n",
            (vs >> 16) & 0xFF, (vs >> 8) & 0xFF, vs & 0xFF);
    
    /* Reset HBA */
    if (ahci_reset(sc) < 0)
        return -1;
    
    /* Read capabilities */
    sc->cap = ahci_read(sc, AHCI_CAP);
    sc->num_ports = (sc->cap & AHCI_CAP_NP) + 1;
    sc->num_cmd_slots = ((sc->cap & AHCI_CAP_NCS) >> 8) + 1;
    if (sc->num_cmd_slots > 32)
        sc->num_cmd_slots = 32;
    if (sc->num_cmd_slots < 1)
        sc->num_cmd_slots = 1;
    
    cprintf("ahci: %d ports, %d command slots, cap=0x%x\n",
            sc->num_ports, sc->num_cmd_slots, sc->cap);
    
    /* Read ports implemented */
    uint32_t pi = ahci_read(sc, AHCI_PI);
    cprintf("ahci: ports implemented: 0x%x\n", pi);

    sc->use_interrupts = 0;
    if (irq_register(pci->irq_line, ahci_irq_handler, sc, "ahci") < 0) {
        cprintf("ahci: failed to register IRQ %d, falling back to polling\n",
                pci->irq_line);
    } else {
        sc->use_interrupts = 1;
        ahci_write(sc, AHCI_GHC, ahci_read(sc, AHCI_GHC) | AHCI_GHC_IE);
        pci_enable_irq(pci, ncpu - 1);
    }
    
    /* Initialize each port */
    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            ahci_port_init(sc, i);
        }
    }
    
    
    ahci_count++;
    
    return 0;
}

/*
 * Module init
 */
void
ahci_init(void)
{
    BOOTDBG("ahci: initializing driver\n");
    
    /* Search for AHCI PCI devices */
    for (int i = 0; i < pci_device_count(); i++) {
        struct pci_dev *dev = pci_get_device(i);
        if (!dev)
            continue;
        
        if (dev->class_code == PCI_CLASS_STORAGE &&
            dev->subclass == PCI_SUBCLASS_SATA &&
            dev->prog_if == 0x01) {  /* AHCI 1.0 */
            ahci_probe(dev);
        }
    }
}
