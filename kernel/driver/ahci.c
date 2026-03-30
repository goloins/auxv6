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
#include "spinlock.h"
#include "sleeplock.h"
#include "pci.h"
#include "blockdev.h"
#include "stdint.h"


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
#define ATA_CMD_IDENTIFY    0xEC
#define ATA_CMD_READ_DMA_EX 0x25
#define ATA_CMD_WRITE_DMA_EX 0x35

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

/* Per-port state */
struct ahci_port {
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
    
    struct sleeplock lock;
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
    
    struct ahci_port ports[32];
};

/* Global array of AHCI controllers */
#define MAX_AHCI 4
static struct ahci_softc ahci_devices[MAX_AHCI];
static int ahci_count = 0;

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
    
    p->port_num = port;
    initsleeplock(&p->lock, "ahci_port");
    
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
    
    /* Allocate command table for slot 0 */
    p->cmd_tbl[0] = (struct ahci_cmd_tbl *)kalloc();
    if (!p->cmd_tbl[0])
        return -1;
    memset(p->cmd_tbl[0], 0, sizeof(*p->cmd_tbl[0]));
    
    /* Set up command header for slot 0 */
    p->cmd_list[0].ctba = V2P(p->cmd_tbl[0]);
    p->cmd_list[0].ctbau = 0;
    
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
    
    /* Read device signature */
    p->sig = ahci_port_read(sc, port, AHCI_PxSIG);
    p->type = ahci_detect_device_type(p->sig);
    
    cprintf("ahci: port %d: type=%d sig=0x%x\n", port, p->type, p->sig);
    
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
    
    cprintf("ahci: %d ports, %d command slots, cap=0x%x\n",
            sc->num_ports, sc->num_cmd_slots, sc->cap);
    
    /* Read ports implemented */
    uint32_t pi = ahci_read(sc, AHCI_PI);
    cprintf("ahci: ports implemented: 0x%x\n", pi);
    
    /* Initialize each port */
    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            ahci_port_init(sc, i);
        }
    }
    
    /* Enable interrupts */
    ahci_write(sc, AHCI_GHC, ahci_read(sc, AHCI_GHC) | AHCI_GHC_IE);
    pci_enable_irq(pci, ncpu - 1);
    
    ahci_count++;
    
    return 0;
}

/*
 * Module init
 */
void
ahci_init(void)
{
    cprintf("ahci: initializing driver\n");
    
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
