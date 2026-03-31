/*
 * ISO 9660 Filesystem (CDROM) for auxv6
 *
 * Read-only filesystem for CD-ROM and ISO images.
 *
 * Supports:
 * - ISO 9660 Level 1, 2, 3
 * - Rock Ridge extensions (optional)
 * - Joliet extensions (optional)
 *
 * TODO Phase 1:
 * - [ ] Volume descriptor parsing
 * - [ ] Directory traversal
 * - [ ] File reading
 * - [ ] Basic directory tree navigation
 *
 * TODO Phase 2:
 * - [ ] Rock Ridge extensions (long filenames, symlinks, POSIX attrs)
 * - [ ] Joliet extensions (Unicode filenames)
 * - [ ] Multi-extent files
 * - [ ] El Torito boot records
 *
 * Reference: ECMA-119 Volume and File Structure of CDROM
 * See also: NetBSD sys/fs/cd9660/
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "vfs.h"
#include "fs.h"
#include "file.h"
#include "stat.h"
#include "buf.h"
#include "blockdev.h"

/* ISO 9660 constants */
#define ISO_SECTOR_SIZE     2048
#define ISO_VOL_DESC_START  16      /* Volume descriptors start at sector 16 */

/* Volume descriptor types */
#define ISO_VD_BOOT         0
#define ISO_VD_PRIMARY      1
#define ISO_VD_SUPP         2
#define ISO_VD_PARTITION    3
#define ISO_VD_END          255

/* Volume descriptor header */
struct iso_volume_descriptor {
    uint8_t  type;
    char     id[5];             /* "CD001" */
    uint8_t  version;
    uint8_t  data[2041];
} __attribute__((packed));

/* Primary Volume Descriptor */
struct iso_primary_descriptor {
    uint8_t  type;              /* 1 */
    char     id[5];             /* "CD001" */
    uint8_t  version;           /* 1 */
    uint8_t  unused1;
    char     system_id[32];
    char     volume_id[32];
    uint8_t  unused2[8];
    uint32_t volume_space_size_le;
    uint32_t volume_space_size_be;
    uint8_t  unused3[32];
    uint16_t volume_set_size_le;
    uint16_t volume_set_size_be;
    uint16_t volume_sequence_number_le;
    uint16_t volume_sequence_number_be;
    uint16_t logical_block_size_le;
    uint16_t logical_block_size_be;
    uint32_t path_table_size_le;
    uint32_t path_table_size_be;
    uint32_t type_l_path_table;
    uint32_t opt_type_l_path_table;
    uint32_t type_m_path_table;
    uint32_t opt_type_m_path_table;
    uint8_t  root_directory_record[34];
    char     volume_set_id[128];
    char     publisher_id[128];
    char     preparer_id[128];
    char     application_id[128];
    char     copyright_file_id[37];
    char     abstract_file_id[37];
    char     bibliographic_file_id[37];
    char     creation_date[17];
    char     modification_date[17];
    char     expiration_date[17];
    char     effective_date[17];
    uint8_t  file_structure_version;
    uint8_t  reserved1;
    uint8_t  application_use[512];
    uint8_t  reserved2[653];
} __attribute__((packed));

/* Directory Record */
struct iso_directory_record {
    uint8_t  length;            /* Length of this record */
    uint8_t  ext_attr_length;   /* Extended attribute length */
    uint32_t extent_le;         /* Location of extent (LBA) */
    uint32_t extent_be;
    uint32_t size_le;           /* Data length */
    uint32_t size_be;
    uint8_t  date[7];           /* Recording date and time */
    uint8_t  flags;             /* File flags */
    uint8_t  file_unit_size;    /* Interleave unit size */
    uint8_t  interleave;        /* Interleave gap size */
    uint16_t volume_sequence_le;
    uint16_t volume_sequence_be;
    uint8_t  name_len;          /* File identifier length */
    char     name[1];           /* File identifier (variable length) */
} __attribute__((packed));

/* Directory record flags */
#define ISO_FLAG_HIDDEN     0x01
#define ISO_FLAG_DIRECTORY  0x02
#define ISO_FLAG_ASSOC      0x04
#define ISO_FLAG_RECORD     0x08
#define ISO_FLAG_PROTECTION 0x10
#define ISO_FLAG_MULTI      0x80    /* Multi-extent */

/* In-memory ISO 9660 mount info */
struct isofs_mount {
    uint dev;                   /* Device number */
    uint block_size;            /* Logical block size */
    uint root_extent;           /* Root directory extent */
    uint root_size;             /* Root directory size */
    struct spinlock lock;
};

/* In-memory ISO 9660 inode */
struct isofs_inode {
    uint extent;                /* Starting LBA */
    uint size;                  /* File size */
    uint flags;                 /* ISO flags */
    struct isofs_mount *mp;     /* Mount point */
};

/*
 * Read sectors from CD device
 */
static int
isofs_read_sectors(struct isofs_mount *mp, uint lba, void *buf, uint count)
{
    /* TODO: Implement block device read */
    /* For now, stub - need to integrate with block device layer */
    
    /* Convert LBA to block numbers (ISO uses 2048-byte sectors) */
    /* If underlying device uses 512-byte sectors, multiply by 4 */
    
    return -1;  /* Not implemented */
}

/*
 * Read a directory record at given position
 */
static int
isofs_read_dir_record(struct isofs_mount *mp, uint extent, uint offset,
                      struct iso_directory_record *dr)
{
    uint8_t sector[ISO_SECTOR_SIZE];
    uint sector_off = offset % ISO_SECTOR_SIZE;
    uint sector_num = extent + (offset / ISO_SECTOR_SIZE);
    
    if (isofs_read_sectors(mp, sector_num, sector, 1) < 0)
        return -1;
    
    if (sector[sector_off] == 0)
        return 0;  /* End of directory */
    
    memmove(dr, &sector[sector_off], sizeof(*dr));
    return dr->length;
}

/*
 * VFS: Lookup a name in a directory
 */
static struct vnode *
isofs_lookup(struct vnode *dir, char *name)
{
    struct isofs_inode *idir = dir->data;
    struct isofs_mount *mp = idir->mp;
    struct iso_directory_record dr;
    uint offset = 0;
    char namebuf[256];
    int namelen = strlen(name);
    
    while (offset < idir->size) {
        int len = isofs_read_dir_record(mp, idir->extent, offset, &dr);
        if (len <= 0)
            break;
        
        offset += len;
        
        /* Skip . and .. */
        if (dr.name_len == 1 && (dr.name[0] == 0 || dr.name[0] == 1))
            continue;
        
        /* Convert ISO filename to normal form */
        /* Remove version number (;1) and trailing dots */
        int nlen = dr.name_len;
        memmove(namebuf, dr.name, nlen);
        namebuf[nlen] = '\0';
        
        /* Strip version number */
        for (int i = nlen - 1; i >= 0; i--) {
            if (namebuf[i] == ';') {
                namebuf[i] = '\0';
                nlen = i;
                break;
            }
        }
        
        /* Strip trailing dot if it's a directory */
        if (nlen > 0 && namebuf[nlen-1] == '.' && (dr.flags & ISO_FLAG_DIRECTORY))
            namebuf[--nlen] = '\0';
        
        /* Case-insensitive comparison */
        if (nlen == namelen && strncmp(namebuf, name, nlen) == 0) {
            /* Found it - create vnode */
            struct vnode *vp = vget(ISOFS, mp->dev, dr.extent_le);
            if (!vp)
                return 0;
            
            struct isofs_inode *ip = kalloc();
            if (!ip) {
                vput(vp);
                return 0;
            }
            
            ip->extent = dr.extent_le;
            ip->size = dr.size_le;
            ip->flags = dr.flags;
            ip->mp = mp;
            
            vp->data = ip;
            vp->type = (dr.flags & ISO_FLAG_DIRECTORY) ? T_DIR : T_FILE;
            
            return vp;
        }
    }
    
    return 0;  /* Not found */
}

/*
 * VFS: Read file data
 */
static int
isofs_read(struct vnode *vp, char *buf, uint off, uint n)
{
    struct isofs_inode *ip = vp->data;
    struct isofs_mount *mp = ip->mp;
    uint8_t sector[ISO_SECTOR_SIZE];
    uint total = 0;
    
    if (off >= ip->size)
        return 0;
    
    if (off + n > ip->size)
        n = ip->size - off;
    
    while (n > 0) {
        uint sector_num = ip->extent + (off / ISO_SECTOR_SIZE);
        uint sector_off = off % ISO_SECTOR_SIZE;
        uint copy = ISO_SECTOR_SIZE - sector_off;
        if (copy > n)
            copy = n;
        
        if (isofs_read_sectors(mp, sector_num, sector, 1) < 0)
            break;
        
        memmove(buf, sector + sector_off, copy);
        
        buf += copy;
        off += copy;
        n -= copy;
        total += copy;
    }
    
    return total;
}

/*
 * VFS: Write (not supported - read-only filesystem)
 */
static int
isofs_write(struct vnode *vp, char *buf, uint off, uint n)
{
    return -1;  /* Read-only filesystem */
}

/*
 * VFS: Get file attributes
 */
static int
isofs_stat(struct vnode *vp, struct stat *st)
{
    struct isofs_inode *ip = vp->data;
    
    st->st_dev = ip->mp->dev;
    st->st_ino = ip->extent;
    st->st_type = (ip->flags & ISO_FLAG_DIRECTORY) ? T_DIR : T_FILE;
    st->st_nlink = 1;
    st->st_size = ip->size;
    
    /* ISO 9660 doesn't have traditional permissions */
    st->st_mode = (ip->flags & ISO_FLAG_DIRECTORY) ? 0555 : 0444;
    st->st_uid = 0;
    st->st_gid = 0;
    
    return 0;
}

/*
 * VFS: Read directory entries
 */
static int
isofs_readdir(struct vnode *vp, struct dirent *de, uint off)
{
    struct isofs_inode *ip = vp->data;
    struct isofs_mount *mp = ip->mp;
    struct iso_directory_record dr;
    
    while (off < ip->size) {
        int len = isofs_read_dir_record(mp, ip->extent, off, &dr);
        if (len <= 0)
            return 0;  /* End of directory */
        
        off += len;
        
        /* Skip . and .. */
        if (dr.name_len == 1 && dr.name[0] == 0) {
            de->inum = ip->extent;
            strncpy(de->name, ".", DIRSIZ);
            return off;
        }
        if (dr.name_len == 1 && dr.name[0] == 1) {
            de->inum = dr.extent_le;  /* Parent directory */
            strncpy(de->name, "..", DIRSIZ);
            return off;
        }
        
        /* Convert filename */
        int nlen = dr.name_len;
        if (nlen > DIRSIZ - 1)
            nlen = DIRSIZ - 1;
        
        memmove(de->name, dr.name, nlen);
        de->name[nlen] = '\0';
        
        /* Strip version number and trailing dots */
        for (int i = nlen - 1; i >= 0; i--) {
            if (de->name[i] == ';') {
                de->name[i] = '\0';
                break;
            }
        }
        
        de->inum = dr.extent_le;
        return off;
    }
    
    return 0;  /* End of directory */
}

/*
 * VFS: Close vnode (release inode)
 */
static void
isofs_close(struct vnode *vp)
{
    if (vp->data) {
        kfree(vp->data);
        vp->data = 0;
    }
}

/*
 * VFS operations structure
 */
static struct vfs_ops isofs_ops = {
    .lookup  = isofs_lookup,
    .read    = isofs_read,
    .write   = isofs_write,
    .stat    = isofs_stat,
    .readdir = isofs_readdir,
    .close   = isofs_close,
    /* Other ops are NULL (not supported for read-only fs) */
};

/*
 * Mount an ISO 9660 filesystem
 */
int
isofs_mount(char *dev, char *mountpoint)
{
    struct isofs_mount *mp;
    struct iso_primary_descriptor pvd;
    struct iso_directory_record *root;
    
    /* Allocate mount structure */
    mp = kalloc();
    if (!mp)
        return -1;
    memset(mp, 0, sizeof(*mp));
    initlock(&mp->lock, "isofs");
    
    /* TODO: Get device number from dev path */
    mp->dev = 0;  /* Placeholder */
    
    /* Read primary volume descriptor at sector 16 */
    if (isofs_read_sectors(mp, ISO_VOL_DESC_START, &pvd, 1) < 0) {
        kfree(mp);
        return -1;
    }
    
    /* Verify signature */
    if (pvd.type != ISO_VD_PRIMARY ||
        strncmp(pvd.id, "CD001", 5) != 0) {
        cprintf("isofs: invalid volume descriptor\n");
        kfree(mp);
        return -1;
    }
    
    mp->block_size = pvd.logical_block_size_le;
    
    /* Extract root directory info */
    root = (struct iso_directory_record *)pvd.root_directory_record;
    mp->root_extent = root->extent_le;
    mp->root_size = root->size_le;
    
    cprintf("isofs: mounted volume '%.*s'\n", 32, pvd.volume_id);
    cprintf("isofs: block size=%d root at LBA %d\n",
            mp->block_size, mp->root_extent);
    
    /* TODO: Register with VFS mount table */
    
    return 0;
}

/*
 * Initialize ISO 9660 filesystem driver
 */
void
isofs_init(void)
{
    cprintf("isofs: ISO 9660 filesystem driver initialized\n");
    /* Register with VFS */
    vfs_register_type(ISOFS, &isofs_ops);
}
