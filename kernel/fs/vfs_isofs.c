/*
 * ISO 9660 Filesystem (CD-ROM/DVD) for auxv6
 *
 * Read-only filesystem for CD-ROM, DVD, and ISO images.
 * Supports mounting via loop device.
 *
 * Supports:
 * - ISO 9660 Level 1, 2, 3
 * - Basic directory traversal
 * - File reading
 *
 * Reference: ECMA-119 Volume and File Structure of CDROM
 */

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "proc.h"
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
#define ISO_SECTORS_PER_BLOCK (ISO_SECTOR_SIZE / BSIZE)  /* 4 x 512-byte blocks per ISO sector */

/* Volume descriptor types */
#define ISO_VD_BOOT         0
#define ISO_VD_PRIMARY      1
#define ISO_VD_SUPP         2
#define ISO_VD_PARTITION    3
#define ISO_VD_END          255

/* Directory record flags */
#define ISO_FLAG_HIDDEN     0x01
#define ISO_FLAG_DIRECTORY  0x02
#define ISO_FLAG_ASSOC      0x04
#define ISO_FLAG_RECORD     0x08
#define ISO_FLAG_PROTECTION 0x10
#define ISO_FLAG_MULTI      0x80    /* Multi-extent */

/* Volume descriptor header */
struct iso_volume_descriptor {
    uchar  type;
    char     id[5];             /* "CD001" */
    uchar  version;
    uchar  data[2041];
} __attribute__((packed));

/* Primary Volume Descriptor */
struct iso_primary_descriptor {
    uchar  type;              /* 1 */
    char     id[5];             /* "CD001" */
    uchar  version;           /* 1 */
    uchar  unused1;
    char     system_id[32];
    char     volume_id[32];
    uchar  unused2[8];
    uint   volume_space_size_le;
    uint   volume_space_size_be;
    uchar  unused3[32];
    ushort volume_set_size_le;
    ushort volume_set_size_be;
    ushort volume_sequence_number_le;
    ushort volume_sequence_number_be;
    ushort logical_block_size_le;
    ushort logical_block_size_be;
    uint   path_table_size_le;
    uint   path_table_size_be;
    uint   type_l_path_table;
    uint   opt_type_l_path_table;
    uint   type_m_path_table;
    uint   opt_type_m_path_table;
    uchar  root_directory_record[34];
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
    uchar  file_structure_version;
    uchar  reserved1;
    uchar  application_use[512];
    uchar  reserved2[653];
} __attribute__((packed));

/* Directory Record */
struct iso_directory_record {
    uchar  length;            /* Length of this record */
    uchar  ext_attr_length;   /* Extended attribute length */
    uint   extent_le;         /* Location of extent (LBA) */
    uint   extent_be;
    uint   size_le;           /* Data length */
    uint   size_be;
    uchar  date[7];           /* Recording date and time */
    uchar  flags;             /* File flags */
    uchar  file_unit_size;    /* Interleave unit size */
    uchar  interleave;        /* Interleave gap size */
    ushort volume_sequence_le;
    ushort volume_sequence_be;
    uchar  name_len;          /* File identifier length */
    char     name[1];           /* File identifier (variable length) */
} __attribute__((packed));

/* In-memory ISO 9660 mount info */
struct isofs_mount_data {
    int dev;                   /* Device number */
    uint block_size;            /* Logical block size (usually 2048) */
    uint root_extent;           /* Root directory extent (LBA) */
    uint root_size;             /* Root directory size */
    uint volume_space_size;     /* Total sectors in volume */
    struct spinlock lock;
};

/* Pseudo-inode data stored in inode->addrs for isofs inodes */
/* We store: addrs[0] = extent LBA, addrs[1] = file size, addrs[2] = flags */
#define ISOFS_INO_EXTENT  0
#define ISOFS_INO_SIZE    1
#define ISOFS_INO_FLAGS   2

static struct isofs_mount_data *isofs_mount_data_ptr;

/* Forward declarations */
static uint isofs_gen_inum(uint extent, uint offset);

/*
 * Read ISO sectors into a buffer.
 * lba is ISO sector number (2048-byte sectors)
 * buf must be able to hold count * ISO_SECTOR_SIZE bytes
 */
static int
isofs_read_sectors(struct isofs_mount_data *mp, uint lba, void *buf, uint count)
{
    uint i;
    char *dst = (char*)buf;

    for(i = 0; i < count; i++){
        uint block;
    int j;

    /* Each ISO sector spans 4 x 512-byte blocks */
    block = (lba + i) * ISO_SECTORS_PER_BLOCK;

    for(j = 0; j < ISO_SECTORS_PER_BLOCK; j++){
        struct buf *bp;
        if(bread_ok(mp->dev, block + j, &bp) < 0)
            return -1;
        memmove(dst + j * BSIZE, bp->data, BSIZE);
        brelse(bp);
    }
    dst += ISO_SECTOR_SIZE;
    }

    return 0;
}

/*
 * Read data at a specific ISO byte offset.
 */
static int
isofs_read_bytes(struct isofs_mount_data *mp, uint lba, uint offset, void *buf, uint count)
{
    uint start_sector;
    uint byte_offset;
    char *sector_buf;
    uint done;
    struct proc *p;
    int user_dst;
    
    /* Calculate absolute offset from LBA */
    uint abs_offset = lba * ISO_SECTOR_SIZE + offset;
    start_sector = abs_offset / ISO_SECTOR_SIZE;
    byte_offset = abs_offset % ISO_SECTOR_SIZE;
    
    sector_buf = kalloc();
    if(sector_buf == 0)
        return -1;

    user_dst = ((uint)buf < KERNBASE);
    p = user_dst ? myproc() : 0;
    if(user_dst && (p == 0 || p->pgdir == 0)){
        kfree(sector_buf);
        return -1;
    }
    
    /* Read sectors, handling partial reads */
    done = 0;
    while(done < count){
        uint to_read = count - done;
        uint avail;
        uint copy_start;
        
        /* Read one sector at a time */
        if(isofs_read_sectors(mp, start_sector, sector_buf, 1) < 0){
            kfree(sector_buf);
            return -1;
        }
        
        copy_start = (done == 0) ? byte_offset : 0;
        avail = ISO_SECTOR_SIZE - copy_start;
        if(to_read > avail)
            to_read = avail;
        
        if(user_dst){
            if(copyout(p->pgdir, (uint)((char*)buf + done), sector_buf + copy_start, to_read) < 0){
                kfree(sector_buf);
                return -1;
            }
        } else {
            memmove((char*)buf + done, sector_buf + copy_start, to_read);
        }
        done += to_read;
        start_sector++;
        byte_offset = 0;
    }
    
    kfree(sector_buf);
    return 0;
}

/*
 * Make an inode for an isofs file/directory.
 * ISO9660 inodes are synthetic, so do not call ilock() here: ilock() may
 * attempt xv6 dinode reads based on inum and issue bogus block I/O.
 */
static struct inode*
isofs_make_inode(uint dev, uint inum, uint extent, uint size, uint flags)
{
    struct inode *ip;

    ip = iget(dev, inum);
    if(ip == 0)
        return 0;

    acquiresleep(&ip->lock);

    if(flags & ISO_FLAG_DIRECTORY){
        ip->type = T_DIR;
        ip->mode = M_IFDIR | 0555;
    } else {
        ip->type = T_FILE;
        ip->mode = M_IFREG | 0444;
    }

    ip->major = 0;
    ip->minor = 0;
    ip->nlink = 1;
    ip->uid = 0;
    ip->gid = 0;
    ip->size = size;

    /* Store extent info in addrs array */
    ip->addrs[ISOFS_INO_EXTENT] = extent;
    ip->addrs[ISOFS_INO_SIZE] = size;
    ip->addrs[ISOFS_INO_FLAGS] = flags;

    ip->valid = 1;

    releasesleep(&ip->lock);
    return ip;
}

/*
 * Parse a directory record and create an inode.
 */
static struct inode*
isofs_parse_dirent(struct isofs_mount_data *mp, struct iso_directory_record *dr, uint inum)
{
    return isofs_make_inode(mp->dev, inum, dr->extent_le, dr->size_le, dr->flags);
}

/*
 * VFS: Get the root inode.
 */
static struct inode*
isofs_root_inode(struct vfs *fs)
{
    struct isofs_mount_data *mp = fs ? (struct isofs_mount_data*)fs->fs_data : 0;
    if(mp == 0)
        mp = isofs_mount_data_ptr;
    
    if(mp == 0)
        return 0;
    
    /* Use ROOTINO so VFS mount-root crossover logic can synthesize . and .. */
    return isofs_make_inode(mp->dev, ROOTINO, mp->root_extent, mp->root_size,
                            ISO_FLAG_DIRECTORY);
}

/*
 * VFS: Read file data (for regular files) or synthesize dirents (for directories).
 */
static int
isofs_read(struct inode *ip, char *dst, uint64_t off, uint n)
{
    struct isofs_mount_data *mp = isofs_mount_data_ptr;
    uint extent;
    uint size;
    
    if(mp == 0 || ip == 0 || dst == 0)
        return -1;
    
    extent = ip->addrs[ISOFS_INO_EXTENT];
    size = ip->size;
    
    /* For regular files, read raw data */
    if(ip->type != T_DIR){
        if(off >= size)
            return 0;
        if(off + n > size)
            n = size - off;
        
        if(n == 0)
            return 0;
        
        if(isofs_read_bytes(mp, extent, off, dst, n) < 0)
            return -1;
        
        return n;
    }
    
    /* For directories, synthesize struct dirent entries from ISO records */
    {
        char *sector_buf;
        uint dir_offset;   /* Offset into ISO directory data */
        uint dirent_idx;   /* Which struct dirent are we at (0-based) */
        uint target_idx;   /* Which dirent does caller want (based on off) */
        uint written;
        struct dirent de;
        
        if(n < sizeof(struct dirent))
            return 0;
        
        /* Calculate which dirent entry the caller wants based on offset */
        target_idx = off / sizeof(struct dirent);
        
        sector_buf = kalloc();
        if(sector_buf == 0)
            return -1;
        
        dir_offset = 0;
        dirent_idx = 0;
        written = 0;
        
        while(dir_offset < size && written + sizeof(struct dirent) <= n){
            struct iso_directory_record *dr;
            uint sector_num;
            uint sector_offset;
            int i;
            
            sector_num = extent + (dir_offset / ISO_SECTOR_SIZE);
            sector_offset = dir_offset % ISO_SECTOR_SIZE;
            
            /* Read this sector */
            if(isofs_read_sectors(mp, sector_num, sector_buf, 1) < 0){
                kfree(sector_buf);
                return (written > 0) ? written : -1;
            }
            
            dr = (struct iso_directory_record*)(sector_buf + sector_offset);
            
            /* Check for end of directory or sector */
            if(dr->length == 0){
                /* Skip to next sector */
                dir_offset = ((dir_offset / ISO_SECTOR_SIZE) + 1) * ISO_SECTOR_SIZE;
                continue;
            }
            
            /* Skip entries that cross sector boundaries */
            if(sector_offset + dr->length > ISO_SECTOR_SIZE){
                dir_offset = ((dir_offset / ISO_SECTOR_SIZE) + 1) * ISO_SECTOR_SIZE;
                continue;
            }
            
            /* Skip . and .. entries (name_len==1 with name[0]==0 or 1) */
            if(dr->name_len == 1 && (dr->name[0] == 0 || dr->name[0] == 1)){
                dir_offset += dr->length;
                continue;
            }
            
            /* This is a real entry - check if caller wants it */
            if(dirent_idx >= target_idx){
                uint inum = isofs_gen_inum(dr->extent_le, dir_offset);
                
                memset(&de, 0, sizeof(de));
                de.inum = (ushort)(inum & 0xFFFF);
                if(de.inum == 0)
                    de.inum = 1;  /* Avoid inum 0 which means "deleted" */
                
                /* Copy name, converting from ISO format */
                for(i = 0; i < dr->name_len && i < DIRSIZ - 1; i++){
                    char c = dr->name[i];
                    if(c == ';')  /* Strip version number */
                        break;
                    if(c >= 'A' && c <= 'Z')
                        c = c - 'A' + 'a';  /* Lowercase */
                    de.name[i] = c;
                }
                de.name[i] = 0;
                
                /* Remove trailing dot if present (ISO puts . for files without extension) */
                if(i > 0 && de.name[i-1] == '.')
                    de.name[i-1] = 0;
                
                if((uint)(dst + written) < KERNBASE){
                    struct proc *p = myproc();
                    if(p == 0 || p->pgdir == 0 || copyout(p->pgdir, (uint)(dst + written), &de, sizeof(de)) < 0){
                        kfree(sector_buf);
                        return (written > 0) ? (int)written : -1;
                    }
                } else {
                    memmove(dst + written, &de, sizeof(de));
                }
                written += sizeof(de);
            }
            
            dirent_idx++;
            dir_offset += dr->length;
        }
        
        kfree(sector_buf);
        return written;
    }
}

/*
 * VFS: Write - not supported (read-only filesystem).
 */
static int
isofs_write(struct inode *ip, char *src, uint64_t off, uint n)
{
    return -1;  /* Read-only filesystem */
}

/*
 * VFS: Truncate - not supported.
 */
static int
isofs_truncate(struct inode *ip)
{
    return -1;  /* Read-only filesystem */
}

/*
 * VFS: Stat.
 */
static int
isofs_stat(struct inode *ip, struct stat *st)
{
    if(ip == 0 || st == 0)
        return -1;
    
    st->st_type = ip->type;
    st->st_dev = ip->dev;
    st->st_ino = ip->inum;
    st->st_major = 0;
    st->st_minor = 0;
    st->st_nlink = ip->nlink;
    st->st_uid = ip->uid;
    st->st_gid = ip->gid;
    st->st_mode = ip->mode;
    st->st_size = ip->size;
    st->st_atime = 0;
    st->st_mtime = 0;
    st->st_ctime = 0;
    
    return 0;
}

/*
 * Compare ISO filename (potentially with version suffix) to search name.
 * ISO names end with ";1" version suffix and may have trailing spaces.
 */
static int
isofs_name_match(char *iso_name, uint iso_len, char *search)
{
    uint search_len = strlen(search);
    uint i;
    
    /* Handle "." and ".." special cases */
    if(iso_len == 1 && iso_name[0] == 0){
        return (search_len == 1 && search[0] == '.');
    }
    if(iso_len == 1 && iso_name[0] == 1){
        return (search_len == 2 && search[0] == '.' && search[1] == '.');
    }
    
    /* Strip trailing ";N" version */
    for(i = 0; i < iso_len; i++){
        if(iso_name[i] == ';'){
            iso_len = i;
            break;
        }
    }
    
    /* Strip trailing '.' if it's the last character and search doesn't have it */
    if(iso_len > 0 && iso_name[iso_len - 1] == '.' && 
       (search_len == 0 || search[search_len - 1] != '.')){
        iso_len--;
    }
    
    if(iso_len != search_len)
        return 0;
    
    /* Case-insensitive comparison */
    for(i = 0; i < iso_len; i++){
        char c1 = iso_name[i];
        char c2 = search[i];
        
        /* Convert to uppercase for comparison */
        if(c1 >= 'a' && c1 <= 'z')
            c1 -= 32;
        if(c2 >= 'a' && c2 <= 'z')
            c2 -= 32;
        
        if(c1 != c2)
            return 0;
    }
    
    return 1;
}

/*
 * Generate a unique inode number from extent + offset.
 * This ensures each directory entry gets a unique inum.
 */
static uint
isofs_gen_inum(uint extent, uint offset)
{
    /* Use a simple hash to generate unique inode numbers */
    return (extent << 12) | (offset & 0xFFF);
}

/*
 * VFS: Directory lookup.
 */
static struct inode*
isofs_dirlookup(struct inode *dp, char *name, uint *poff)
{
    struct isofs_mount_data *mp = isofs_mount_data_ptr;
    uint extent;
    uint dir_size;
    uint offset;
    char *sector_buf;
    
    if(mp == 0 || dp == 0 || name == 0)
        return 0;
    
    if(dp->type != T_DIR)
        return 0;
    
    extent = dp->addrs[ISOFS_INO_EXTENT];
    dir_size = dp->size;
    
    sector_buf = kalloc();
    if(sector_buf == 0)
        return 0;
    
    offset = 0;
    while(offset < dir_size){
        struct iso_directory_record *dr;
        uint sector_offset;
        uint sector_num;
        
        sector_num = extent + (offset / ISO_SECTOR_SIZE);
        sector_offset = offset % ISO_SECTOR_SIZE;
        
        /* Read this sector if needed */
        if(sector_offset == 0 || sector_offset + sizeof(struct iso_directory_record) > ISO_SECTOR_SIZE){
            if(isofs_read_sectors(mp, sector_num, sector_buf, 1) < 0){
                kfree(sector_buf);
                return 0;
            }
        }
        
        dr = (struct iso_directory_record*)(sector_buf + sector_offset);
        
        /* Check for end of directory or sector */
        if(dr->length == 0){
            /* Skip to next sector */
            offset = ((offset / ISO_SECTOR_SIZE) + 1) * ISO_SECTOR_SIZE;
            continue;
        }
        
        /* Skip entries that cross sector boundaries */
        if(sector_offset + dr->length > ISO_SECTOR_SIZE){
            offset = ((offset / ISO_SECTOR_SIZE) + 1) * ISO_SECTOR_SIZE;
            continue;
        }
        
        /* Check name match */
        if(isofs_name_match(dr->name, dr->name_len, name)){
            uint inum = isofs_gen_inum(dr->extent_le, offset);
            struct inode *ip;
            
            if(poff)
                *poff = offset;
            
            ip = isofs_parse_dirent(mp, dr, inum);
            kfree(sector_buf);
            return ip;
        }
        
        offset += dr->length;
    }
    
    kfree(sector_buf);
    return 0;
}

/*
 * VFS: Create - not supported.
 */
static struct inode*
isofs_create(struct inode *dp, char *name, short type,
             short major, short minor, int mode, int uid, int gid)
{
    return 0;  /* Read-only filesystem */
}

/*
 * VFS: Link operations - not supported.
 */
static int isofs_dirlink(struct inode *dp, char *name, uint inum) { return -1; }
static int isofs_link(struct inode *ip, struct inode *dp, char *name) { return -1; }
static int isofs_remove(struct inode *dp, char *name) { return -1; }
static int isofs_rename(struct inode *olddp, char *oldname, struct inode *newdp, char *newname) { return -1; }
static int isofs_setattr(struct inode *ip, int set_mode, int mode, int set_uid, int uid, int set_gid, int gid) { return -1; }
static int isofs_access(struct inode *ip, int mode) { return 0; }  /* Always allow read access */

/*
 * Skip leading slashes and extract the next path component.
 */
static char*
isofs_skipelem(char *path, char *name)
{
    char *s;
    int len;
    
    while(*path == '/')
        path++;
    if(*path == 0)
        return 0;
    
    s = path;
    while(*path != '/' && *path != 0)
        path++;
    len = path - s;
    if(len >= DIRSIZ)
        len = DIRSIZ - 1;
    memmove(name, s, len);
    name[len] = 0;
    
    while(*path == '/')
        path++;
    return path;
}

/*
 * Walk a path in the isofs filesystem.
 */
static struct inode*
isofs_walk(struct vfs *fs, char *path, int nameiparent, char *name)
{
    struct inode *ip;
    struct inode *next;
    char elem[DIRSIZ];
    char *p;
    int i;
    
    if(path == 0)
        return 0;
    
    if(path[0] == '/'){
        ip = isofs_root_inode(fs);
        if(ip == 0)
            return 0;
    } else {
        /* Relative path - not supported for isofs, fall back to root */
        ip = isofs_root_inode(fs);
        if(ip == 0)
            return 0;
    }
    
    p = path;
    while((p = isofs_skipelem(p, elem)) != 0){
        if(elem[0] == 0 || (elem[0] == '.' && elem[1] == 0))
            continue;

        if(nameiparent && *p == 0){
            if(name){
                for(i = 0; i < DIRSIZ - 1 && elem[i]; i++)
                    name[i] = elem[i];
                name[i] = 0;
            }
            return ip;
        }
        
        ilock(ip);
        next = isofs_dirlookup(ip, elem, 0);
        iunlock(ip);
        
        if(next == 0){
            iput(ip);
            return 0;
        }
        iput(ip);
        ip = next;
    }
    
    if(nameiparent){
        iput(ip);
        return 0;
    }
    
    return ip;
}

/*
 * VFS: Resolve path to inode.
 */
static struct inode*
isofs_namei(struct vfs *fs, char *path)
{
    return isofs_walk(fs, path, 0, 0);
}

/*
 * VFS: Resolve path to parent inode and get filename.
 */
static struct inode*
isofs_nameiparent(struct vfs *fs, char *path, char *name)
{
    return isofs_walk(fs, path, 1, name);
}

/*
 * VFS: Release inode reference.
 */
static void
isofs_inode_put(struct inode *ip)
{
    iput(ip);
}

/*
 * Initialize isofs mount.
 */
static int
isofs_mount_init(struct mount *m)
{
    struct iso_primary_descriptor *pvd;
    struct iso_directory_record *root_dr;
    struct isofs_mount_data *mp;
    char *buf;
    int sector;
    
    buf = kalloc();
    if(buf == 0)
        return -1;
    
    mp = (struct isofs_mount_data*)kalloc();
    if(mp == 0){
        kfree(buf);
        return -1;
    }
    
    memset(mp, 0, sizeof(*mp));
    initlock(&mp->lock, "isofs");
    lockdep_set_rank(&mp->lock, LOCK_RANK_DEFAULT, "isofs");
    mp->dev = m->dev;
    
    MOUNTDBG("isofs: mount_init dev=%d\n", m->dev);
    
    /* Search for primary volume descriptor */
    for(sector = ISO_VOL_DESC_START; sector < ISO_VOL_DESC_START + 16; sector++){
        /* Read volume descriptor */
        uint blk = sector * ISO_SECTORS_PER_BLOCK;
        int i;
        
        MOUNTDBG("isofs: reading sector %d (blk %d)\n", sector, blk);
        
        for(i = 0; i < ISO_SECTORS_PER_BLOCK; i++){
            struct buf *bp = bread(mp->dev, blk + i);
            if(bp == 0){
                cprintf("isofs: bread failed dev=%d blk=%d\n", mp->dev, blk + i);
                kfree((char*)mp);
                kfree(buf);
                return -1;
            }
            memmove(buf + i * BSIZE, bp->data, BSIZE);
            brelse(bp);
        }
        
        MOUNTDBG("isofs: sector %d magic=%c%c%c%c%c type=%d\n",
                sector, buf[1], buf[2], buf[3], buf[4], buf[5], buf[0]);
        
        /* Check magic */
        if(memcmp(buf + 1, "CD001", 5) != 0)
            continue;
        
        /* Check type */
        if(buf[0] == ISO_VD_END)
            break;
        
        if(buf[0] == ISO_VD_PRIMARY){
            pvd = (struct iso_primary_descriptor*)buf;
            
            mp->block_size = pvd->logical_block_size_le;
            mp->volume_space_size = pvd->volume_space_size_le;
            
            /* Parse root directory record */
            root_dr = (struct iso_directory_record*)pvd->root_directory_record;
            mp->root_extent = root_dr->extent_le;
            mp->root_size = root_dr->size_le;
            
            MOUNTDBG("isofs: mounted volume '%.*s'\n", 32, pvd->volume_id);
            MOUNTDBG("isofs: root extent=%d size=%d block_size=%d\n",
                    mp->root_extent, mp->root_size, mp->block_size);
            
            m->fs_data = mp;
            isofs_mount_data_ptr = mp;
            
            kfree(buf);
            return 0;
        }
    }
    
    cprintf("isofs: no primary volume descriptor found\n");
    kfree((char*)mp);
    kfree(buf);
    return -1;
}

/*
 * Destroy isofs mount.
 */
static void
isofs_fs_destroy(struct vfs *fs)
{
    if(fs->fs_data){
        kfree(fs->fs_data);
        fs->fs_data = 0;
    }
    isofs_mount_data_ptr = 0;
}

/*
 * VFS operations
 */
static struct vfs_ops isofs_vfs_ops = {
    .root_inode = isofs_root_inode,
    .namei = isofs_namei,
    .nameiparent = isofs_nameiparent,
    .inode_put = isofs_inode_put,
};

static struct vnode_ops isofs_vnode_ops = {
    .read = isofs_read,
    .write = isofs_write,
    .truncate = isofs_truncate,
    .stat = isofs_stat,
    .setattr = isofs_setattr,
    .access = isofs_access,
    .dirlookup = isofs_dirlookup,
    .dirlink = isofs_dirlink,
    .link = isofs_link,
    .remove = isofs_remove,
    .rename = isofs_rename,
    .faultctl = 0,
    .create = isofs_create,
    .readlink = 0,
    .symlink = 0,
};

/*
 * Initialize isofs VFS registration.
 */
void
vfs_isofs_init(struct vfs *fs)
{
    safestrcpy(fs->name, "isofs", VFS_NAME_MAX);
    fs->caps = VFS_CAP_READ;  /* Read-only */
    fs->fs_data = 0;
    fs->fs_destroy = isofs_fs_destroy;
    fs->mount_init = isofs_mount_init;
    fs->ops = isofs_vfs_ops;
    fs->vnode_ops = isofs_vnode_ops;
}
