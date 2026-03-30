#ifndef AUXV6_MSDOSFS_H
#define AUXV6_MSDOSFS_H

/* MS-DOS (FAT12/FAT16/FAT32) filesystem structures and constants
 * References:
 *   - Microsoft FAT specification
 *   - FreeBSD msdosfs implementation
 *   - Linux VFAT/FAT driver
 */

#include "types.h"

/* FAT boot sector (simplified, common fields) */
struct fat_boot {
  uchar   jump[3];              /* 0x00: Jump instruction (EB ?? 90) */
  char    oem[8];               /* 0x03: OEM identifier */
  ushort  sector_size;          /* 0x0B: Bytes per sector */
  uchar   cluster_size;         /* 0x0D: Sectors per cluster */
  ushort  reserved_sectors;     /* 0x0E: Number of reserved sectors */
  uchar   num_fats;             /* 0x10: Number of FAT copies */
  ushort  root_entries;         /* 0x11: Number of root directory entries (FAT12/16) */
  ushort  total_sectors_16;     /* 0x13: Total sectors (FAT12/16) */
  uchar   media_desc;           /* 0x15: Media descriptor */
  ushort  sectors_per_fat_16;   /* 0x16: Sectors per FAT (FAT12/16) */
  ushort  sectors_per_track;    /* 0x18: Sectors per track (CHS) */
  ushort  heads;                /* 0x1A: Number of heads (CHS) */
  uint    hidden_sectors;       /* 0x1C: Hidden sectors before partition */
  uint    total_sectors_32;     /* 0x20: Total sectors (FAT16/32) */
  
  /* FAT16-specific fields: offset 0x24 */
  uchar   drive_number_16;      /* 0x24: BIOS drive number */
  uchar   reserved_16;          /* 0x25: Reserved */
  uchar   boot_sig_16;          /* 0x26: Boot signature (0x29) */
  uint    serial_16;            /* 0x27: Serial number */
  char    label_16[11];         /* 0x2B: Volume label */
  char    fs_type_16[8];        /* 0x36: Filesystem type ("FAT16   ") */
  
  /* FAT32-specific fields: offset 0x24 (extends differently) */
  uint    sectors_per_fat_32;   /* 0x24: Sectors per FAT (FAT32) */
  ushort  ext_flags;            /* 0x28: Extended flags */
  ushort  fs_version;           /* 0x2A: Filesystem version */
  uint    root_cluster;         /* 0x2C: Root directory cluster (FAT32) */
  ushort  info_sector;          /* 0x30: FSInfo sector number (FAT32) */
  ushort  backup_boot;          /* 0x32: Backup boot sector (FAT32) */
  uchar   reserved_32[12];      /* 0x34: Reserved */
  uchar   drive_number_32;      /* 0x40: BIOS drive number */
  uchar   reserved2_32;         /* 0x41: Reserved */
  uchar   boot_sig_32;          /* 0x42: Boot signature (0x29) */
  uint    serial_32;            /* 0x43: Serial number */
  char    label_32[11];         /* 0x47: Volume label */
  char    fs_type_32[8];        /* 0x52: Filesystem type ("FAT32   ") */
  
  ushort  boot_signature;       /* 0x1FE: Boot signature (0xAA55) */
} __attribute__((packed));

/* Directory entry (32 bytes) */
struct fat_dir_entry {
  char    name[8];              /* 0x00: Filename (8.3 format) */
  char    ext[3];               /* 0x08: Extension */
  uchar   attr;                 /* 0x0B: File attributes */
  uchar   nt_res;               /* 0x0C: NT reserved */
  uchar   crt_time_tenth;       /* 0x0D: Creation time (tenths of second) */
  ushort  crt_time;             /* 0x0E: Creation time */
  ushort  crt_date;             /* 0x10: Creation date */
  ushort  lst_acc_date;         /* 0x12: Last access date */
  ushort  high_cluster;         /* 0x14: High word of first cluster (FAT32) */
  ushort  wrt_time;             /* 0x16: Write time */
  ushort  wrt_date;             /* 0x18: Write date */
  ushort  low_cluster;          /* 0x1A: Low word of first cluster */
  uint    file_size;            /* 0x1C: File size in bytes */
} __attribute__((packed));

/* File attributes */
#define FAT_ATTR_READ_ONLY   0x01
#define FAT_ATTR_HIDDEN      0x02
#define FAT_ATTR_SYSTEM      0x04
#define FAT_ATTR_VOLUME_ID   0x08
#define FAT_ATTR_DIRECTORY   0x10
#define FAT_ATTR_ARCHIVE     0x20
#define FAT_ATTR_LFN         0x0F  /* Long filename entry (all flags set) */

/* Special directory entry markers */
#define FAT_DIRENTRY_FREE    0x00  /* Free entry (no filename) */
#define FAT_DIRENTRY_DELETED 0xE5  /* Deleted entry */
#define FAT_DIRENTRY_DOT     0x2E  /* "." or ".." entry */

/* FAT entry values */
#define FAT12_EOF            0xFFF
#define FAT16_EOF            0xFFFF
#define FAT32_EOF            0x0FFFFFFF
#define FAT_BAD_CLUSTER      0xFFFFFFF7
#define FAT_RESERVED_CLUSTER 0xFFFFFFF6

/* Cluster number constants */
#define FAT_CLUSTER_FIRST    2     /* First valid data cluster */
#define FAT_CLUSTER_BAD      0xFFFFFFF7
#define FAT_CLUSTER_EOF      0xFFFFFFF8

/* Filesystem type detection (based on cluster count) */
#define FAT12_MAX_CLUSTERS   4085
#define FAT16_MAX_CLUSTERS   65525
/* FAT32 is everything else */

/* In-memory filesystem info */
struct msdosfs {
  int     type;                 /* FAT type: 12, 16, or 32 */
  uint    sector_size;          /* Bytes per sector */
  uint    cluster_size;         /* Bytes per cluster */
  uint    cluster_sectors;      /* Sectors per cluster */
  uint    reserved_sectors;     /* Number of reserved sectors */
  uint    num_fats;             /* Number of FAT copies */
  uint    sectors_per_fat;      /* Sectors per FAT table */
  uint    root_entries;         /* Max root directory entries (FAT12/16) */
  uint    root_cluster;         /* Root cluster number (FAT32) */
  uint    root_start;           /* Starting sector of root directory */
  uint    root_size_sectors;    /* Size of root directory in sectors */
  uint    data_start;           /* Starting sector of data region */
  uint    total_clusters;       /* Total number of clusters */
  uint    fat_start;            /* Starting sector of FAT tables */
  
  /* FAT chain cache/working values */
  uint    last_cluster;         /* Last accessed cluster (for chain following) */
  uint    next_free_cluster;    /* Hint for next free cluster */
};

/* In-memory inode representation (one per open file) */
struct msdos_inode {
  uint    start_cluster;        /* First cluster of file/directory */
  uint    file_size;            /* File size in bytes */
  uint    dir_sector;           /* Sector containing dir entry (for updates) */
  uint    dir_entry_offset;     /* Offset within sector of dir entry */
  uchar   attr;                 /* File attributes */
  uchar   dirty;                /* Needs updating on disk */
};

/* FAT table sector cache */
struct fat_cache {
  uint    sector;               /* Which FAT sector is cached */
  uint    *entries;             /* Cached FAT entries */
  uchar   dirty;                /* Cache needs writing back */
};

/* Function prototypes for VFS operations */
void     msdosfs_init(void);
struct msdos_inode *msdosfs_root(void);
struct msdos_inode *msdosfs_dirlookup(struct msdos_inode *dp, char *name, uint *poff);
struct msdos_inode *msdosfs_iget(uint inum);
void     msdosfs_iput(struct msdos_inode *ip);
int      msdosfs_readi(struct msdos_inode *ip, uchar *dst, uint off, uint n);
int      msdosfs_writei(struct msdos_inode *ip, uchar *src, uint off, uint n);

#endif /* AUXV6_MSDOSFS_H */
