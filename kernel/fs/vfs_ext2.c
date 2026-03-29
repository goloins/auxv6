//
// ext2 filesystem support for VFS
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "vfs.h"
#include "file.h"
#include "stat.h"
#include "fcntl.h"

// ext2 superblock structure (starts at byte offset 1024)
struct ext2_superblock {
  uint s_inodes_count;      // Total inode count
  uint s_blocks_count;      // Total block count
  uint s_r_blocks_count;    // Reserved block count
  uint s_free_blocks_count; // Free block count
  uint s_free_inodes_count; // Free inode count
  uint s_first_data_block;  // First Data Block (usually 1)
  uint s_log_block_size;    // Block size = 1024 << s_log_block_size
  uint s_log_frag_size;     // Fragment size = 1024 << s_log_frag_size
  uint s_blocks_per_group;   // Blocks per group
  uint s_frags_per_group;    // Fragments per group
  uint s_inodes_per_group;   // Inodes per group
  uint s_mtime;             // Mounting time
  uint s_wtime;             // Last write time
  ushort s_mnt_count;       // Mount count
  short s_max_mnt_count;    // Max mount count
  ushort s_magic;           // 0xEF53 - ext2 signature
  ushort s_state;           // Filesystem state
  ushort s_errors;          // Error handling
  ushort s_minor_rev_level; // Minor revision level
  uint s_lastcheck;         // Last check time
  uint s_checkinterval;     // Max time between checks
  uint s_creator_os;        // Creator OS
  uint s_rev_level;         // Revision level
  ushort s_def_resuid;      // Default reserved UID
  ushort s_def_resgid;      // Default reserved GID
  uint s_first_ino;
  ushort s_inode_size;
  ushort s_block_group_nr;
};

#define EXT2_SUPER_MAGIC 0xEF53
#define EXT2_SB_OFFSET 1024
#define EXT2_SB_SIZE 1024

// ext2 block group descriptor
struct ext2_group_desc {
  uint bg_block_bitmap;     // Block bitmap block
  uint bg_inode_bitmap;     // Inode bitmap block
  uint bg_inode_table;      // Inode table start block
  ushort bg_free_blocks_count;
  ushort bg_free_inodes_count;
  ushort bg_used_dirs_count;
  ushort bg_pad;
  uint bg_reserved[3];
};

#define EXT2_BLOCK_GROUP_DESC_SIZE 32

// ext2 inode structure (on-disk)
struct ext2_inode {
  ushort i_mode;            // Type and permissions
  ushort i_uid;             // Owner UID
  uint i_size;              // File size in bytes
  uint i_atime;             // Access time
  uint i_ctime;             // Creation time
  uint i_mtime;             // Modification time
  uint i_dtime;             // Deletion time
  ushort i_gid;             // Owner GID
  ushort i_links_count;     // Link count
  uint i_blocks;            // Number of 512-byte blocks
  uint i_flags;             // File flags
  uint i_osd1;              // OS-specific value
  uint i_block[15];         // Block pointers (12 direct, 3 indirect)
  uint i_generation;        // Generation number
  uint i_file_acl;          // File ACL location
  uint i_dir_acl;           // Directory ACL location
  uint i_faddr;             // Fragment address
  uint i_osd2[3];           // OS-specific value
};

#define EXT2_MIN_INODE_SIZE 128
#define EXT2_ROOT_INO 2
#define EXT2_NAME_MAX 255

#define EXT2_S_IFMT  0xF000
#define EXT2_S_IFREG 0x8000
#define EXT2_S_IFDIR 0x4000
#define EXT2_S_IFCHR 0x2000
#define EXT2_S_IFBLK 0x6000

// ext2 directory entry
struct ext2_dirent {
  uint inode;               // Inode number
  ushort rec_len;           // Record length (must be a multiple of 4)
  uchar name_len;           // Name length (without null terminator)
  uchar file_type;          // File type indicator
  char name[0];             // Name (variable length, followed by padding)
};

#define EXT2_FT_UNKNOWN 0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR 2
#define EXT2_FT_CHRDEV 3
#define EXT2_FT_BLKDEV 4
#define EXT2_FT_FIFO 5
#define EXT2_FT_SOCK 6
#define EXT2_FT_SYMLINK 7

// ext2 filesystem mountpoint data
struct ext2_mount_data {
  struct ext2_superblock sb;
  struct ext2_group_desc *group_descs;
  uint group_count;
  uint block_size;
  uint inode_size;
  int dev;
};

struct ext2_dirent_hdr {
  uint inode;
  ushort rec_len;
  uchar name_len;
  uchar file_type;
};

static uint ext2_active_dev = EXT2DEV;

static uint
ext2_min_u32(uint a, uint b)
{
  return (a < b) ? a : b;
}

static struct ext2_mount_data*
ext2_data_for_dev(uint dev)
{
  return (struct ext2_mount_data*)vfs_dev_fs_data(dev);
}

static int
ext2_dev_read(uint dev, uint off, char *dst, uint n)
{
  uint done;

  done = 0;
  while(done < n){
    uint cur;
    uint blockno;
    uint boff;
    uint take;
    struct buf *b;

    cur = off + done;
    blockno = cur / BSIZE;
    boff = cur % BSIZE;
    take = ext2_min_u32(BSIZE - boff, n - done);

    b = bread(dev, blockno);
    if(b == 0)
      return -1;
    memmove(dst + done, (char*)b->data + boff, take);
    brelse(b);

    done += take;
  }

  return 0;
}

static int
ext2_read_disk_inode(struct ext2_mount_data *data, uint inum, struct ext2_inode *out)
{
  uint group;
  uint index;
  uint inode_off;
  uint inode_bytes;
  char raw[256];

  if(data == 0 || out == 0)
    return -1;
  if(inum == 0 || inum > data->sb.s_inodes_count)
    return -1;
  if(data->inode_size < EXT2_MIN_INODE_SIZE || data->inode_size > sizeof(raw))
    return -1;

  group = (inum - 1) / data->sb.s_inodes_per_group;
  index = (inum - 1) % data->sb.s_inodes_per_group;
  if(group >= data->group_count)
    return -1;

  inode_off = data->group_descs[group].bg_inode_table * data->block_size;
  inode_off += index * data->inode_size;
  inode_bytes = data->inode_size;

  if(ext2_dev_read(data->dev, inode_off, raw, inode_bytes) < 0)
    return -1;

  memset(out, 0, sizeof(*out));
  memmove(out, raw, EXT2_MIN_INODE_SIZE);
  return 0;
}

static short
ext2_mode_to_type(ushort mode)
{
  switch(mode & EXT2_S_IFMT){
  case EXT2_S_IFDIR:
    return T_DIR;
  case EXT2_S_IFCHR:
  case EXT2_S_IFBLK:
    return T_DEV;
  case EXT2_S_IFREG:
  default:
    return T_FILE;
  }
}

static struct inode*
ext2_make_inode(uint dev, uint inum)
{
  struct ext2_mount_data *data;
  struct ext2_inode dip;
  struct inode *ip;

  data = ext2_data_for_dev(dev);
  if(data == 0)
    return 0;
  if(ext2_read_disk_inode(data, inum, &dip) < 0)
    return 0;

  ip = iget(dev, inum);
  if(ip == 0)
    return 0;

  acquiresleep(&ip->lock);
  ip->dev = dev;
  ip->inum = inum;
  ip->valid = 1;
  ip->type = ext2_mode_to_type(dip.i_mode);
  ip->major = 0;
  ip->minor = 0;
  ip->nlink = dip.i_links_count;
  ip->uid = dip.i_uid;
  ip->gid = dip.i_gid;
  ip->mode = dip.i_mode & 0x0FFF;
  ip->size = dip.i_size;
  memset(ip->addrs, 0, sizeof(ip->addrs));
  releasesleep(&ip->lock);

  return ip;
}

static int
ext2_inode_blockno(struct ext2_mount_data *data, struct ext2_inode *dip, uint lbn, uint *out)
{
  uint ptrs;
  uint ind_index;
  uint ind_block;
  uint off;

  if(data == 0 || dip == 0 || out == 0)
    return -1;

  if(lbn < 12){
    *out = dip->i_block[lbn];
    return 0;
  }

  ptrs = data->block_size / sizeof(uint);
  ind_index = lbn - 12;
  if(ind_index >= ptrs)
    return -1;

  ind_block = dip->i_block[12];
  if(ind_block == 0){
    *out = 0;
    return 0;
  }

  off = ind_block * data->block_size + ind_index * sizeof(uint);
  if(ext2_dev_read(data->dev, off, (char*)out, sizeof(uint)) < 0)
    return -1;
  return 0;
}

static int
ext2_read_data(struct ext2_mount_data *data, struct ext2_inode *dip,
               char *dst, uint off, uint n)
{
  uint done;

  if(data == 0 || dip == 0 || dst == 0)
    return -1;
  if(off >= dip->i_size)
    return 0;
  if(off + n > dip->i_size)
    n = dip->i_size - off;

  done = 0;
  while(done < n){
    uint pos;
    uint lbn;
    uint boff;
    uint chunk;
    uint blockno;
    uint byte_off;

    pos = off + done;
    lbn = pos / data->block_size;
    boff = pos % data->block_size;
    chunk = ext2_min_u32(data->block_size - boff, n - done);

    if(ext2_inode_blockno(data, dip, lbn, &blockno) < 0)
      return (done == 0) ? -1 : (int)done;
    if(blockno == 0)
      break;

    byte_off = blockno * data->block_size + boff;
    if(ext2_dev_read(data->dev, byte_off, dst + done, chunk) < 0)
      return (done == 0) ? -1 : (int)done;

    done += chunk;
  }

  return done;
}

static int
ext2_read_dirents(struct ext2_mount_data *data, struct ext2_inode *dip,
                  char *dst, uint off, uint n)
{
  uint want_index;
  uint cur_index;
  uint doff;
  uint produced;

  if(data == 0 || dip == 0 || dst == 0)
    return -1;

  if((off % sizeof(struct dirent)) != 0)
    return -1;

  want_index = off / sizeof(struct dirent);
  cur_index = 0;
  doff = 0;
  produced = 0;

  while(doff + sizeof(struct ext2_dirent_hdr) <= dip->i_size){
    struct ext2_dirent_hdr hdr;
    int got;

    got = ext2_read_data(data, dip, (char*)&hdr, doff, sizeof(hdr));
    if(got < 0)
      return (produced == 0) ? -1 : (int)produced;
    if(got != sizeof(hdr) || hdr.rec_len < 8)
      break;

    if(hdr.inode != 0 && hdr.name_len > 0 && hdr.name_len <= EXT2_NAME_MAX){
      if(cur_index >= want_index){
        struct dirent de;
        char nm[EXT2_NAME_MAX + 1];
        uint cpy;

        if(produced + sizeof(de) > n)
          break;

        memset(&de, 0, sizeof(de));
        de.inum = (ushort)hdr.inode;
        got = ext2_read_data(data, dip, nm, doff + 8, hdr.name_len);
        if(got < 0)
          return (produced == 0) ? -1 : (int)produced;
        if(got != hdr.name_len)
          break;
        nm[hdr.name_len] = 0;

        cpy = hdr.name_len;
        if(cpy > DIRSIZ)
          cpy = DIRSIZ;
        memmove(de.name, nm, cpy);
        memmove(dst + produced, &de, sizeof(de));
        produced += sizeof(de);
      }
      cur_index++;
    }

    doff += hdr.rec_len;
  }

  return produced;
}

static int
ext2_name_equal(char *want, char *got, uint gotlen)
{
  uint i;

  if(strlen(want) != gotlen)
    return 0;
  for(i = 0; i < gotlen; i++){
    if(want[i] != got[i])
      return 0;
  }
  return 1;
}

static struct inode*
ext2_dirlookup(struct inode *dp, char *name, uint *poff)
{
  struct ext2_mount_data *data;
  struct ext2_inode dip;
  uint off;

  if(dp == 0 || name == 0)
    return 0;

  data = ext2_data_for_dev(dp->dev);
  if(data == 0)
    return 0;
  if(ext2_read_disk_inode(data, dp->inum, &dip) < 0)
    return 0;
  if((dip.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
    return 0;

  off = 0;
  while(off + sizeof(struct ext2_dirent_hdr) <= dip.i_size){
    struct ext2_dirent_hdr hdr;
    int got;

    got = ext2_read_data(data, &dip, (char*)&hdr, off, sizeof(hdr));
    if(got < 0)
      return 0;
    if(got != sizeof(hdr))
      break;

    if(hdr.rec_len < 8)
      break;

    if(hdr.inode != 0 && hdr.name_len > 0 && hdr.name_len <= EXT2_NAME_MAX){
      char nm[EXT2_NAME_MAX + 1];

      got = ext2_read_data(data, &dip, nm, off + 8, hdr.name_len);
      if(got < 0)
        return 0;
      if(got != hdr.name_len)
        break;
      nm[hdr.name_len] = 0;

      if(ext2_name_equal(name, nm, hdr.name_len)){
        if(poff)
          *poff = off;
        return ext2_make_inode(dp->dev, hdr.inode);
      }
    }

    off += hdr.rec_len;
  }

  return 0;
}

static char*
ext2_skipelem(char *path, char *name)
{
  char *s;
  int len;
  int i;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;

  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len > EXT2_NAME_MAX)
    len = EXT2_NAME_MAX;

  for(i = 0; i < len; i++)
    name[i] = s[i];
  name[len] = 0;

  while(*path == '/')
    path++;
  return path;
}

static struct inode*
ext2_walk(char *path, int nameiparent, char *name)
{
  struct inode *ip;
  struct inode *next;
  char elem[EXT2_NAME_MAX + 1];
  char *p;
  int i;

  if(path == 0)
    return 0;

  ip = ext2_make_inode(ext2_active_dev, EXT2_ROOT_INO);
  if(ip == 0)
    return 0;

  p = path;
  while((p = ext2_skipelem(p, elem)) != 0){
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

    next = ext2_dirlookup(ip, elem, 0);
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

static int
ext2_read(struct inode *ip, char *dst, uint off, uint n)
{
  struct ext2_mount_data *data;
  struct ext2_inode dip;

  if(ip == 0 || dst == 0)
    return -1;

  data = ext2_data_for_dev(ip->dev);
  if(data == 0)
    return -1;
  if(ext2_read_disk_inode(data, ip->inum, &dip) < 0)
    return -1;

  if((dip.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR)
    return ext2_read_dirents(data, &dip, dst, off, n);

  return ext2_read_data(data, &dip, dst, off, n);
}

static int
ext2_write(struct inode *ip, char *src, uint off, uint n)
{
  (void)ip;
  (void)src;
  (void)off;
  (void)n;
  return -1;
}

static int
ext2_stat(struct inode *ip, struct stat *st)
{
  struct ext2_mount_data *data;
  struct ext2_inode dip;

  if(ip == 0 || st == 0)
    return -1;

  data = ext2_data_for_dev(ip->dev);
  if(data == 0)
    return -1;
  if(ext2_read_disk_inode(data, ip->inum, &dip) < 0)
    return -1;

  st->type = ext2_mode_to_type(dip.i_mode);
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->nlink = dip.i_links_count;
  st->uid = dip.i_uid;
  st->gid = dip.i_gid;
  st->mode = dip.i_mode & 0x0FFF;
  st->size = dip.i_size;
  return 0;
}

static int
ext2_access(struct inode *ip, int mode)
{
  return iaccess(ip, mode);
}

static int
ext2_dirlink(struct inode *dp, char *name, uint inum)
{
  (void)dp;
  (void)name;
  (void)inum;
  return -1;
}

static struct inode*
ext2_namei(char *path)
{
  return ext2_walk(path, 0, 0);
}

static struct inode*
ext2_nameiparent(char *path, char *name)
{
  return ext2_walk(path, 1, name);
}

static void
ext2_inode_put(struct inode *ip)
{
  iput(ip);
}

static int
ext2_mount_init(struct mount *m)
{
  struct ext2_mount_data *data;
  uint gd_off;
  uint gd_bytes;
  uint gd_capacity;
  int stage;

  stage = 0;
  if(m == 0)
    return -1;
  cprintf("ext2: mount_init dev=%d path=%s flags=%x\n", m->dev, m->path, m->flags);
  stage = 1;
  if(bdev_nblocks(m->dev) == 0)
    return -1;

  data = (struct ext2_mount_data *)kalloc();
  if(data == 0)
    return -1;
  memset(data, 0, sizeof(*data));

  data->dev = m->dev;
  stage = 2;
  if(ext2_dev_read(data->dev, EXT2_SB_OFFSET, (char*)&data->sb, sizeof(data->sb)) < 0)
    goto fail;

  stage = 3;
  if(data->sb.s_magic != EXT2_SUPER_MAGIC){
    cprintf("ext2: bad magic dev=%d got=%x want=%x\n",
            data->dev, data->sb.s_magic, EXT2_SUPER_MAGIC);
    goto fail;
  }

  stage = 4;
  data->block_size = 1024U << data->sb.s_log_block_size;
  if(data->block_size < 1024 || data->block_size > 4096)
    goto fail;

  stage = 5;
  data->inode_size = data->sb.s_inode_size;
  if(data->inode_size == 0)
    data->inode_size = EXT2_MIN_INODE_SIZE;

  stage = 6;
  data->group_count = (data->sb.s_blocks_count - data->sb.s_first_data_block +
                       data->sb.s_blocks_per_group - 1) /
                      data->sb.s_blocks_per_group;
  if(data->group_count == 0)
    goto fail;

  stage = 7;
  gd_bytes = data->group_count * sizeof(struct ext2_group_desc);
  gd_capacity = PGSIZE - sizeof(*data);
  if(gd_bytes > gd_capacity)
    goto fail;

  data->group_descs = (struct ext2_group_desc*)((char*)data + sizeof(*data));
  memset((char*)data->group_descs, 0, gd_capacity);

  stage = 8;
  gd_off = (data->sb.s_first_data_block + 1) * data->block_size;
  if(ext2_dev_read(data->dev, gd_off, (char*)data->group_descs, gd_bytes) < 0)
    goto fail;

  m->fs_data = (void *)data;
  ext2_active_dev = data->dev;
  cprintf("ext2: mount ok dev=%d block=%d groups=%d\n", data->dev, data->block_size, data->group_count);
  return 0;

fail:
  cprintf("ext2: mount failed dev=%d stage=%d\n", m ? m->dev : -1, stage);
  kfree((char*)data);
  return -1;
}

void
ext2_mount_destroy(struct vfs *fs)
{
  (void)fs;
}

void
vfs_ext2_init(struct vfs *fs)
{
  if(fs == 0)
    return;

  safestrcpy(fs->name, "ext2", sizeof(fs->name));
  // Start read-only; write-side ops come after allocator/bitmap support.
  fs->caps = VFS_CAP_READ;
  fs->fs_data = 0;
  fs->fs_destroy = ext2_mount_destroy;
  fs->mount_init = ext2_mount_init;
  fs->ops.namei = ext2_namei;
  fs->ops.nameiparent = ext2_nameiparent;
  fs->ops.inode_put = ext2_inode_put;
  fs->vnode_ops.read = ext2_read;
  fs->vnode_ops.write = ext2_write;
  fs->vnode_ops.stat = ext2_stat;
  fs->vnode_ops.access = ext2_access;
  fs->vnode_ops.dirlookup = ext2_dirlookup;
  fs->vnode_ops.dirlink = ext2_dirlink;
}
