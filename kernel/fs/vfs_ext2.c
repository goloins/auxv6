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
#define EXT2_S_IFLNK 0xA000

#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2
#define EXT2_FT_CHRDEV   3
#define EXT2_FT_BLKDEV   4
#define EXT2_FT_SYMLINK  7

// Fast symlink threshold - symlinks <= 60 bytes are stored in i_block
#define EXT2_FAST_SYMLINK_MAX 60

#define EXT2_DIRENT_MIN_SIZE 8

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
static struct ext2_mount_data *ext2_bootstrap_data;
static int ext2_fail_alloc_after = -1;
static uint ext2_alloc_calls;
static int ext2_fail_inode_write_after = -1;
static uint ext2_inode_write_calls;

int
ext2_fault_inject_set(int which, int value)
{
  if(which == 0){
    ext2_fail_alloc_after = -1;
    ext2_alloc_calls = 0;
    ext2_fail_inode_write_after = -1;
    ext2_inode_write_calls = 0;
    return 0;
  }

  // which=1: fail ext2_alloc_block() after `value` successful alloc calls.
  if(which == 1){
    if(value < 0)
      return -1;
    ext2_fail_alloc_after = value;
    ext2_alloc_calls = 0;
    return 0;
  }

  // which=2: fail ext2_write_disk_inode() after `value` successful inode writes.
  if(which == 2){
    if(value < 0)
      return -1;
    ext2_fail_inode_write_after = value;
    ext2_inode_write_calls = 0;
    return 0;
  }

  return -1;
}

static int ext2_inode_blockno(struct ext2_mount_data *data, struct ext2_inode *dip,
                              uint lbn, uint *out);
static int ext2_read_data(struct ext2_mount_data *data, struct ext2_inode *dip,
                          char *dst, uint off, uint n);
static int ext2_name_equal(char *want, char *got, uint gotlen);
static struct inode* ext2_dirlookup(struct inode *dp, char *name, uint *poff);
static int ext2_read_disk_inode(struct ext2_mount_data *data, uint inum,
                                struct ext2_inode *out);
static int ext2_write_disk_inode(struct ext2_mount_data *data, uint inum,
                                 struct ext2_inode *in);
static struct inode* ext2_make_inode(uint dev, uint inum);
static int ext2_readlink(struct inode *ip, char *buf, uint size);
static uint ext2_encode_dev(short major, short minor);
static void ext2_decode_dev(uint raw, short *major, short *minor);

static uint
ext2_min_u32(uint a, uint b)
{
  return (a < b) ? a : b;
}

static uint
ext2_align4(uint n)
{
  return (n + 3) & ~3;
}

static struct ext2_mount_data*
ext2_data_for_dev(uint dev)
{
  struct ext2_mount_data *data;

  data = (struct ext2_mount_data*)vfs_dev_fs_data(dev);
  if(data == 0 && ext2_bootstrap_data && ext2_bootstrap_data->dev == (int)dev)
    data = ext2_bootstrap_data;
  return data;
}

int
ext2_block_usage(uint dev, uint *total_blocks, uint *free_blocks, uint *block_size)
{
  struct ext2_mount_data *data;

  data = ext2_data_for_dev(dev);
  if(data == 0)
    return -1;

  if(total_blocks)
    *total_blocks = data->sb.s_blocks_count;
  if(free_blocks)
    *free_blocks = data->sb.s_free_blocks_count;
  if(block_size)
    *block_size = data->block_size;

  return 0;
}

static int
ext2_inode_disk_offset(struct ext2_mount_data *data, uint inum, uint *off)
{
  uint group;
  uint index;

  if(data == 0 || off == 0)
    return -1;
  if(inum == 0 || inum > data->sb.s_inodes_count)
    return -1;
  if(data->inode_size < EXT2_MIN_INODE_SIZE)
    return -1;

  group = (inum - 1) / data->sb.s_inodes_per_group;
  index = (inum - 1) % data->sb.s_inodes_per_group;
  if(group >= data->group_count)
    return -1;

  *off = data->group_descs[group].bg_inode_table * data->block_size;
  *off += index * data->inode_size;
  return 0;
}

static int
ext2_dirent_valid(struct ext2_dirent_hdr *hdr, uint remain)
{
  if(hdr == 0)
    return 0;
  if(hdr->rec_len < sizeof(struct ext2_dirent_hdr))
    return 0;
  if((hdr->rec_len & 3) != 0)
    return 0;
  if(hdr->rec_len > remain)
    return 0;
  if(hdr->name_len > hdr->rec_len - sizeof(struct ext2_dirent_hdr))
    return 0;
  if(hdr->name_len > EXT2_NAME_MAX)
    return 0;
  return 1;
}

static uint
ext2_encode_dev(short major, short minor)
{
  return ((uint)(major & 0xFFFF) << 16) | (uint)(minor & 0xFFFF);
}

static void
ext2_decode_dev(uint raw, short *major, short *minor)
{
  if(major)
    *major = (short)((raw >> 16) & 0xFFFF);
  if(minor)
    *minor = (short)(raw & 0xFFFF);
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
ext2_dev_write(uint dev, uint off, char *src, uint n)
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
    memmove((char*)b->data + boff, src + done, take);
    bwrite(b);
    brelse(b);

    done += take;
  }

  return 0;
}

static int
ext2_write_group_descs(struct ext2_mount_data *data)
{
  uint gd_off;
  uint gd_bytes;

  if(data == 0)
    return -1;
  gd_off = (data->sb.s_first_data_block + 1) * data->block_size;
  gd_bytes = data->group_count * sizeof(struct ext2_group_desc);
  return ext2_dev_write(data->dev, gd_off, (char*)data->group_descs, gd_bytes);
}

static int
ext2_write_super(struct ext2_mount_data *data)
{
  if(data == 0)
    return -1;
  return ext2_dev_write(data->dev, EXT2_SB_OFFSET, (char*)&data->sb, sizeof(data->sb));
}

static int
ext2_alloc_inode(struct ext2_mount_data *data, uint *out_inum)
{
  uint g;
  uchar *bitmap;
  uint first_ino;

  if(data == 0 || out_inum == 0)
    return -1;
  if(data->block_size > PGSIZE)
    return -1;

  bitmap = (uchar*)kalloc();
  if(bitmap == 0)
    return -1;

  first_ino = data->sb.s_first_ino;
  if(first_ino == 0)
    first_ino = 11;

  for(g = 0; g < data->group_count; g++){
    uint bitmax;
    uint b;
    uint bitmap_off;

    if(data->group_descs[g].bg_free_inodes_count == 0)
      continue;

    bitmap_off = data->group_descs[g].bg_inode_bitmap * data->block_size;
    if(ext2_dev_read(data->dev, bitmap_off, (char*)bitmap, data->block_size) < 0)
      goto fail;

    bitmax = data->sb.s_inodes_per_group;
    for(b = 0; b < bitmax; b++){
      uint inum;
      uint byte_index;
      uchar bit_mask;

      inum = g * data->sb.s_inodes_per_group + b + 1;
      if(inum < first_ino || inum > data->sb.s_inodes_count)
        continue;

      byte_index = b / 8;
      bit_mask = 1 << (b % 8);
      if(bitmap[byte_index] & bit_mask)
        continue;

      bitmap[byte_index] |= bit_mask;
      if(ext2_dev_write(data->dev, bitmap_off, (char*)bitmap, data->block_size) < 0)
        goto fail;

      if(data->group_descs[g].bg_free_inodes_count > 0)
        data->group_descs[g].bg_free_inodes_count--;
      if(data->sb.s_free_inodes_count > 0)
        data->sb.s_free_inodes_count--;

      if(ext2_write_group_descs(data) < 0)
        goto fail;
      if(ext2_write_super(data) < 0)
        goto fail;

      *out_inum = inum;
      kfree((char*)bitmap);
      return 0;
    }
  }

fail:
  kfree((char*)bitmap);
  return -1;
}

static int
ext2_alloc_block(struct ext2_mount_data *data, uint *out_blockno)
{
  uint g;
  uchar *bitmap;

  if(data == 0 || out_blockno == 0)
    return -1;
  if(data->block_size > PGSIZE)
    return -1;

  if(ext2_fail_alloc_after >= 0){
    if(ext2_alloc_calls >= (uint)ext2_fail_alloc_after)
      return -1;
    ext2_alloc_calls++;
  }

  bitmap = (uchar*)kalloc();
  if(bitmap == 0)
    return -1;

  for(g = 0; g < data->group_count; g++){
    uint bitmax;
    uint b;
    uint bitmap_off;

    if(data->group_descs[g].bg_free_blocks_count == 0)
      continue;

    bitmap_off = data->group_descs[g].bg_block_bitmap * data->block_size;
    if(ext2_dev_read(data->dev, bitmap_off, (char*)bitmap, data->block_size) < 0)
      goto fail;

    bitmax = data->sb.s_blocks_per_group;
    for(b = 0; b < bitmax; b++){
      uint blockno;
      uint byte_index;
      uchar bit_mask;

      blockno = data->sb.s_first_data_block + g * data->sb.s_blocks_per_group + b;
      if(blockno >= data->sb.s_blocks_count)
        break;

      byte_index = b / 8;
      bit_mask = 1 << (b % 8);
      if(bitmap[byte_index] & bit_mask)
        continue;

      bitmap[byte_index] |= bit_mask;
      if(ext2_dev_write(data->dev, bitmap_off, (char*)bitmap, data->block_size) < 0)
        goto fail;

      if(data->group_descs[g].bg_free_blocks_count > 0)
        data->group_descs[g].bg_free_blocks_count--;
      if(data->sb.s_free_blocks_count > 0)
        data->sb.s_free_blocks_count--;

      if(ext2_write_group_descs(data) < 0)
        goto fail;
      if(ext2_write_super(data) < 0)
        goto fail;

      *out_blockno = blockno;
      kfree((char*)bitmap);
      return 0;
    }
  }

fail:
  kfree((char*)bitmap);
  return -1;
}

static int
ext2_free_block(struct ext2_mount_data *data, uint blockno)
{
  uint rel;
  uint g;
  uint b;
  uint bitmap_off;
  uint byte_index;
  uchar bit_mask;
  uchar *bitmap;

  if(data == 0)
    return -1;
  if(blockno < data->sb.s_first_data_block || blockno >= data->sb.s_blocks_count)
    return -1;
  if(data->block_size > PGSIZE)
    return -1;

  rel = blockno - data->sb.s_first_data_block;
  g = rel / data->sb.s_blocks_per_group;
  b = rel % data->sb.s_blocks_per_group;
  if(g >= data->group_count)
    return -1;

  bitmap = (uchar*)kalloc();
  if(bitmap == 0)
    return -1;

  bitmap_off = data->group_descs[g].bg_block_bitmap * data->block_size;
  if(ext2_dev_read(data->dev, bitmap_off, (char*)bitmap, data->block_size) < 0)
    goto fail;

  byte_index = b / 8;
  bit_mask = 1 << (b % 8);
  if((bitmap[byte_index] & bit_mask) == 0){
    kfree((char*)bitmap);
    return 0;
  }

  bitmap[byte_index] &= ~bit_mask;
  if(ext2_dev_write(data->dev, bitmap_off, (char*)bitmap, data->block_size) < 0)
    goto fail;

  data->group_descs[g].bg_free_blocks_count++;
  data->sb.s_free_blocks_count++;

  if(ext2_write_group_descs(data) < 0)
    goto fail;
  if(ext2_write_super(data) < 0)
    goto fail;

  kfree((char*)bitmap);
  return 0;

fail:
  kfree((char*)bitmap);
  return -1;
}

static int
ext2_free_inode(struct ext2_mount_data *data, uint inum, int was_dir)
{
  uint g;
  uint b;
  uint bitmap_off;
  uint byte_index;
  uchar bit_mask;
  uchar *bitmap;

  if(data == 0)
    return -1;
  if(inum == 0 || inum > data->sb.s_inodes_count)
    return -1;
  if(data->block_size > PGSIZE)
    return -1;

  g = (inum - 1) / data->sb.s_inodes_per_group;
  b = (inum - 1) % data->sb.s_inodes_per_group;
  if(g >= data->group_count)
    return -1;

  bitmap = (uchar*)kalloc();
  if(bitmap == 0)
    return -1;

  bitmap_off = data->group_descs[g].bg_inode_bitmap * data->block_size;
  if(ext2_dev_read(data->dev, bitmap_off, (char*)bitmap, data->block_size) < 0)
    goto fail;

  byte_index = b / 8;
  bit_mask = 1 << (b % 8);
  if((bitmap[byte_index] & bit_mask) == 0){
    kfree((char*)bitmap);
    return 0;
  }

  bitmap[byte_index] &= ~bit_mask;
  if(ext2_dev_write(data->dev, bitmap_off, (char*)bitmap, data->block_size) < 0)
    goto fail;

  data->group_descs[g].bg_free_inodes_count++;
  if(was_dir && data->group_descs[g].bg_used_dirs_count > 0)
    data->group_descs[g].bg_used_dirs_count--;
  data->sb.s_free_inodes_count++;

  if(ext2_write_group_descs(data) < 0)
    goto fail;
  if(ext2_write_super(data) < 0)
    goto fail;

  kfree((char*)bitmap);
  return 0;

fail:
  kfree((char*)bitmap);
  return -1;
}

static int
ext2_dir_dev_off(struct ext2_mount_data *data, struct ext2_inode *dip,
                 uint dir_off, uint *out)
{
  uint lbn;
  uint boff;
  uint blockno;

  if(data == 0 || dip == 0 || out == 0)
    return -1;

  lbn = dir_off / data->block_size;
  boff = dir_off % data->block_size;
  if(ext2_inode_blockno(data, dip, lbn, &blockno) < 0 || blockno == 0)
    return -1;
  *out = blockno * data->block_size + boff;
  return 0;
}

static int
ext2_dir_add_entry(struct ext2_mount_data *data, struct ext2_inode *dip,
                   char *name, uint inum, uchar file_type)
{
  uint off;
  uint need;
  uint namelen;

  if(data == 0 || dip == 0 || name == 0 || inum == 0)
    return -1;

  namelen = strlen(name);
  if(namelen == 0 || namelen > EXT2_NAME_MAX)
    return -1;
  need = ext2_align4(EXT2_DIRENT_MIN_SIZE + namelen);

  off = 0;
  while(off + sizeof(struct ext2_dirent_hdr) <= dip->i_size){
    struct ext2_dirent_hdr hdr;
    uint remain;
    int got;

    got = ext2_read_data(data, dip, (char*)&hdr, off, sizeof(hdr));
    if(got != sizeof(hdr))
      return -1;
    remain = dip->i_size - off;
    if(!ext2_dirent_valid(&hdr, remain))
      return -1;

    if(hdr.inode == 0 && hdr.rec_len >= need){
      struct ext2_dirent_hdr nhdr;
      uint dev_off;

      nhdr.inode = inum;
      nhdr.rec_len = hdr.rec_len;
      nhdr.name_len = (uchar)namelen;
      nhdr.file_type = file_type;

      if(ext2_dir_dev_off(data, dip, off, &dev_off) < 0)
        return -1;
      if(ext2_dev_write(data->dev, dev_off, (char*)&nhdr, sizeof(nhdr)) < 0)
        return -1;
      if(ext2_dev_write(data->dev, dev_off + EXT2_DIRENT_MIN_SIZE, name, namelen) < 0)
        return -1;
      return 0;
    }

    if(hdr.inode != 0){
      uint ideal;

      ideal = ext2_align4(EXT2_DIRENT_MIN_SIZE + hdr.name_len);
      if(hdr.rec_len >= ideal + need){
        struct ext2_dirent_hdr left;
        struct ext2_dirent_hdr nhdr;
        uint split_off;
        uint left_dev_off;
        uint new_dev_off;

        left = hdr;
        left.rec_len = ideal;

        split_off = off + ideal;
        nhdr.inode = inum;
        nhdr.rec_len = hdr.rec_len - ideal;
        nhdr.name_len = (uchar)namelen;
        nhdr.file_type = file_type;

        if(ext2_dir_dev_off(data, dip, off, &left_dev_off) < 0)
          return -1;
        if(ext2_dir_dev_off(data, dip, split_off, &new_dev_off) < 0)
          return -1;

        if(ext2_dev_write(data->dev, left_dev_off, (char*)&left, sizeof(left)) < 0)
          return -1;
        if(ext2_dev_write(data->dev, new_dev_off, (char*)&nhdr, sizeof(nhdr)) < 0)
          return -1;
        if(ext2_dev_write(data->dev, new_dev_off + EXT2_DIRENT_MIN_SIZE, name, namelen) < 0)
          return -1;
        return 0;
      }
    }

    off += hdr.rec_len;
  }

  // No free slot in existing records: grow directory by one direct block.
  if(need > data->block_size)
    return -1;
  if((dip->i_size % data->block_size) != 0)
    return -1;
  {
    uint lbn;
    uint new_block;
    uint dev_off;
    char *zbuf;
    struct ext2_dirent_hdr nhdr;

    if(data->block_size > PGSIZE)
      return -1;

    lbn = dip->i_size / data->block_size;
    if(lbn >= 12)
      return -1;

    if(ext2_alloc_block(data, &new_block) < 0)
      return -1;

    zbuf = kalloc();
    if(zbuf == 0)
      return -1;
    memset(zbuf, 0, PGSIZE);
    dev_off = new_block * data->block_size;
    if(ext2_dev_write(data->dev, dev_off, zbuf, data->block_size) < 0){
      kfree(zbuf);
      return -1;
    }
    kfree(zbuf);

    nhdr.inode = inum;
    nhdr.rec_len = data->block_size;
    nhdr.name_len = (uchar)namelen;
    nhdr.file_type = file_type;
    if(ext2_dev_write(data->dev, dev_off, (char*)&nhdr, sizeof(nhdr)) < 0)
      return -1;
    if(ext2_dev_write(data->dev, dev_off + EXT2_DIRENT_MIN_SIZE, name, namelen) < 0)
      return -1;

    dip->i_block[lbn] = new_block;
    dip->i_size += data->block_size;
    dip->i_blocks += data->block_size / 512;
    return 0;
  }

  return -1;
}

static int
ext2_dir_init_block(struct ext2_mount_data *data, uint blockno, uint self_inum, uint parent_inum)
{
  char *buf;
  struct ext2_dirent_hdr dot;
  struct ext2_dirent_hdr dotdot;
  int r;

  if(data == 0 || blockno == 0 || self_inum == 0 || parent_inum == 0)
    return -1;
  if(data->block_size > PGSIZE)
    return -1;
  if(data->block_size < 24)
    return -1;

  buf = kalloc();
  if(buf == 0)
    return -1;
  memset(buf, 0, PGSIZE);

  dot.inode = self_inum;
  dot.rec_len = 12;
  dot.name_len = 1;
  dot.file_type = EXT2_FT_DIR;
  memmove(buf, &dot, sizeof(dot));
  buf[EXT2_DIRENT_MIN_SIZE] = '.';

  dotdot.inode = parent_inum;
  dotdot.rec_len = data->block_size - dot.rec_len;
  dotdot.name_len = 2;
  dotdot.file_type = EXT2_FT_DIR;
  memmove(buf + dot.rec_len, &dotdot, sizeof(dotdot));
  buf[dot.rec_len + EXT2_DIRENT_MIN_SIZE] = '.';
  buf[dot.rec_len + EXT2_DIRENT_MIN_SIZE + 1] = '.';

  r = ext2_dev_write(data->dev, blockno * data->block_size, buf, data->block_size);
  kfree(buf);
  return r;
}

static int
ext2_inode_truncate_blocks(struct ext2_mount_data *data, struct ext2_inode *dip)
{
  uint i;

  if(data == 0 || dip == 0)
    return -1;

  for(i = 0; i < 12; i++){
    if(dip->i_block[i] == 0)
      continue;
    if(ext2_free_block(data, dip->i_block[i]) < 0)
      return -1;
    dip->i_block[i] = 0;
  }

  if(dip->i_block[12]){
    uint ind_block;
    uint *tbl;
    uint ptrs;

    ind_block = dip->i_block[12];
    if(data->block_size > PGSIZE)
      return -1;
    tbl = (uint*)kalloc();
    if(tbl == 0)
      return -1;
    if(ext2_dev_read(data->dev, ind_block * data->block_size, (char*)tbl, data->block_size) < 0){
      kfree((char*)tbl);
      return -1;
    }
    ptrs = data->block_size / sizeof(uint);
    for(i = 0; i < ptrs; i++){
      if(tbl[i] == 0)
        continue;
      if(ext2_free_block(data, tbl[i]) < 0){
        kfree((char*)tbl);
        return -1;
      }
    }
    kfree((char*)tbl);
    if(ext2_free_block(data, ind_block) < 0)
      return -1;
    dip->i_block[12] = 0;
  }

  dip->i_block[13] = 0;
  dip->i_block[14] = 0;
  dip->i_size = 0;
  dip->i_blocks = 0;
  return 0;
}

static int
ext2_dir_is_empty(struct ext2_mount_data *data, struct ext2_inode *dip)
{
  uint off;

  if(data == 0 || dip == 0)
    return 0;

  off = 0;
  while(off + sizeof(struct ext2_dirent_hdr) <= dip->i_size){
    struct ext2_dirent_hdr hdr;
    uint remain;
    int got;

    got = ext2_read_data(data, dip, (char*)&hdr, off, sizeof(hdr));
    if(got != sizeof(hdr))
      return 0;
    remain = dip->i_size - off;
    if(!ext2_dirent_valid(&hdr, remain))
      return 0;

    if(hdr.inode != 0 && hdr.name_len > 0){
      char nm[EXT2_NAME_MAX + 1];

      got = ext2_read_data(data, dip, nm, off + 8, hdr.name_len);
      if(got != hdr.name_len)
        return 0;
      nm[hdr.name_len] = 0;
      if(!(hdr.name_len == 1 && nm[0] == '.') &&
         !(hdr.name_len == 2 && nm[0] == '.' && nm[1] == '.'))
        return 0;
    }

    off += hdr.rec_len;
  }

  return 1;
}

static int
ext2_dir_rewrite_inum(struct ext2_mount_data *data, struct ext2_inode *dip,
                      char *name, uint new_inum)
{
  uint off;

  if(data == 0 || dip == 0 || name == 0)
    return -1;

  off = 0;
  while(off + sizeof(struct ext2_dirent_hdr) <= dip->i_size){
    struct ext2_dirent_hdr hdr;
    uint remain;
    int got;

    got = ext2_read_data(data, dip, (char*)&hdr, off, sizeof(hdr));
    if(got != sizeof(hdr))
      return -1;
    remain = dip->i_size - off;
    if(!ext2_dirent_valid(&hdr, remain))
      return -1;

    if(hdr.inode != 0 && hdr.name_len > 0){
      char nm[EXT2_NAME_MAX + 1];
      uint dev_off;

      got = ext2_read_data(data, dip, nm, off + 8, hdr.name_len);
      if(got != hdr.name_len)
        return -1;
      nm[hdr.name_len] = 0;

      if(ext2_name_equal(name, nm, hdr.name_len)){
        hdr.inode = new_inum;
        if(ext2_dir_dev_off(data, dip, off, &dev_off) < 0)
          return -1;
        if(ext2_dev_write(data->dev, dev_off, (char*)&hdr, sizeof(hdr)) < 0)
          return -1;
        return 0;
      }
    }

    off += hdr.rec_len;
  }

  return -1;
}

static int
ext2_dir_clear_entry(struct ext2_mount_data *data, struct ext2_inode *dip, char *name)
{
  return ext2_dir_rewrite_inum(data, dip, name, 0);
}

static int
ext2_dir_is_ancestor(struct inode *ancestor, struct inode *start)
{
  struct ext2_mount_data *data;
  uint cur_inum;
  uint anc_inum;
  int guard;

  if(ancestor == 0 || start == 0)
    return 0;

  data = ext2_data_for_dev(start->dev);
  if(data == 0)
    return -1;

  anc_inum = ancestor->inum;
  cur_inum = start->inum;
  guard = 0;
  while(cur_inum){
    struct ext2_inode dip;
    uint off;
    int found;

    if(cur_inum == anc_inum)
      return 1;
    if(cur_inum == EXT2_ROOT_INO)
      return 0;

    if(ext2_read_disk_inode(data, cur_inum, &dip) < 0)
      return -1;
    if((dip.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
      return -1;

    found = 0;
    off = 0;
    while(off + sizeof(struct ext2_dirent_hdr) <= dip.i_size){
      struct ext2_dirent_hdr hdr;
      uint remain;
      int got;

      got = ext2_read_data(data, &dip, (char*)&hdr, off, sizeof(hdr));
      if(got != sizeof(hdr))
        return -1;
      remain = dip.i_size - off;
      if(!ext2_dirent_valid(&hdr, remain))
        return -1;

      if(hdr.inode != 0 && hdr.name_len == 2){
        char nm[3];

        got = ext2_read_data(data, &dip, nm, off + EXT2_DIRENT_MIN_SIZE, 2);
        if(got != 2)
          return -1;
        if(nm[0] == '.' && nm[1] == '.'){
          cur_inum = hdr.inode;
          found = 1;
          break;
        }
      }

      off += hdr.rec_len;
    }

    if(!found || cur_inum == 0)
      return -1;
    guard++;
    if(guard > 128)
      return -1;
  }

  return 0;
}

static struct inode*
ext2_create(struct inode *dp, char *name, short type,
            short major, short minor, int mode, int uid, int gid)
{
  struct ext2_mount_data *data;
  struct ext2_inode dip;
  struct ext2_inode ni;
  struct inode *ip;
  uint inum;
  uint inode_mode;
  uchar file_type;
  uint dir_block;
  int have_inode;
  int have_block;
  int have_entry;

  if(dp == 0 || name == 0)
    return 0;
  if(type != T_FILE && type != T_DIR && type != T_DEV)
    return 0;

  inum = 0;
  dir_block = 0;
  have_inode = 0;
  have_block = 0;
  have_entry = 0;

  data = ext2_data_for_dev(dp->dev);
  if(data == 0)
    return 0;
  if(ext2_read_disk_inode(data, dp->inum, &dip) < 0)
    return 0;
  if((dip.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
    return 0;
  if(iaccess(dp, IACC_WRITE | IACC_EXEC) < 0)
    return 0;

  if(ext2_alloc_inode(data, &inum) < 0)
    return 0;
  have_inode = 1;

  if(type == T_DIR){
    inode_mode = EXT2_S_IFDIR | (M_IRUSR | M_IWUSR | M_IXUSR |
                                 M_IRGRP | M_IXGRP |
                                 M_IROTH | M_IXOTH);
    file_type = EXT2_FT_DIR;
  } else if(type == T_DEV) {
    int kind;
    int perms;

    kind = mode & M_IFMT;
    if(kind != M_IFBLK)
      kind = M_IFCHR;
    perms = mode & 07777;
    if(perms == 0)
      perms = M_IRUSR | M_IWUSR |
              M_IRGRP | M_IWGRP |
              M_IROTH | M_IWOTH;
    inode_mode = kind | perms;
    file_type = (kind == M_IFBLK) ? EXT2_FT_BLKDEV : EXT2_FT_CHRDEV;
  } else {
    inode_mode = EXT2_S_IFREG | (M_IRUSR | M_IWUSR | M_IRGRP | M_IROTH);
    file_type = EXT2_FT_REG_FILE;
  }

  memset(&ni, 0, sizeof(ni));
  ni.i_mode = inode_mode;
  ni.i_uid = uid;
  ni.i_gid = gid;
  ni.i_links_count = (type == T_DIR) ? 2 : 1;
  ni.i_size = 0;
  ni.i_blocks = 0;
  if(type == T_DEV)
    ni.i_block[0] = ext2_encode_dev(major, minor);

  if(type == T_DIR){
    if(ext2_alloc_block(data, &dir_block) < 0)
      goto fail;
    have_block = 1;
    ni.i_block[0] = dir_block;
    ni.i_size = data->block_size;
    ni.i_blocks = data->block_size / 512;
    if(ext2_dir_init_block(data, dir_block, inum, dp->inum) < 0)
      goto fail;
  }

  acquire(&tickslock);
  ni.i_ctime = ticks;
  ni.i_mtime = ticks;
  ni.i_atime = ticks;
  release(&tickslock);

  if(ext2_write_disk_inode(data, inum, &ni) < 0)
    goto fail;
  if(ext2_dir_add_entry(data, &dip, name, inum, file_type) < 0)
    goto fail;
  have_entry = 1;

  if(type == T_DIR)
    dip.i_links_count++;
  acquire(&tickslock);
  dip.i_mtime = ticks;
  dip.i_ctime = ticks;
  release(&tickslock);
  if(ext2_write_disk_inode(data, dp->inum, &dip) < 0)
    goto fail;

  ip = ext2_make_inode(dp->dev, inum);
  if(ip == 0)
    goto fail;
  ilock(ip);
  return ip;

fail:
  if(have_entry)
    ext2_dir_clear_entry(data, &dip, name);
  if(have_block)
    ext2_free_block(data, dir_block);
  if(have_inode)
    ext2_free_inode(data, inum, type == T_DIR);
  return 0;
}

static int
ext2_read_disk_inode(struct ext2_mount_data *data, uint inum, struct ext2_inode *out)
{
  uint inode_off;
  uint inode_bytes;
  char raw[256];

  if(data == 0 || out == 0)
    return -1;
  if(data->inode_size < EXT2_MIN_INODE_SIZE || data->inode_size > sizeof(raw))
    return -1;

  if(ext2_inode_disk_offset(data, inum, &inode_off) < 0)
    return -1;
  inode_bytes = data->inode_size;

  if(ext2_dev_read(data->dev, inode_off, raw, inode_bytes) < 0)
    return -1;

  memset(out, 0, sizeof(*out));
  memmove(out, raw, EXT2_MIN_INODE_SIZE);
  return 0;
}

static int
ext2_write_disk_inode(struct ext2_mount_data *data, uint inum, struct ext2_inode *in)
{
  uint inode_off;

  if(data == 0 || in == 0)
    return -1;

  if(ext2_fail_inode_write_after >= 0){
    if(ext2_inode_write_calls >= (uint)ext2_fail_inode_write_after)
      return -1;
    ext2_inode_write_calls++;
  }

  if(ext2_inode_disk_offset(data, inum, &inode_off) < 0)
    return -1;
  if(ext2_dev_write(data->dev, inode_off, (char*)in, EXT2_MIN_INODE_SIZE) < 0)
    return -1;
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
  case EXT2_S_IFLNK:
    return T_SYMLINK;
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
  if(ip->type == T_DEV)
    ext2_decode_dev(dip.i_block[0], &ip->major, &ip->minor);
  else {
    ip->major = 0;
    ip->minor = 0;
  }
  ip->nlink = dip.i_links_count;
  ip->uid = dip.i_uid;
  ip->gid = dip.i_gid;
  if(ip->type == T_DEV)
    ip->mode = (dip.i_mode & M_IFMT) | (dip.i_mode & 07777);
  else
    ip->mode = dip.i_mode & 07777;
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
    uint remain;
    int got;

    got = ext2_read_data(data, dip, (char*)&hdr, doff, sizeof(hdr));
    if(got < 0)
      return (produced == 0) ? -1 : (int)produced;
    remain = dip->i_size - doff;
    if(got != sizeof(hdr) || !ext2_dirent_valid(&hdr, remain))
      break;

    if(hdr.inode != 0 && hdr.name_len > 0){
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

static int
ext2_block_in_list(uint *list, uint n, uint bno)
{
  uint i;

  if(list == 0 || bno == 0)
    return 0;
  for(i = 0; i < n; i++){
    if(list[i] == bno)
      return 1;
  }
  return 0;
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
    uint remain;
    int got;

    got = ext2_read_data(data, &dip, (char*)&hdr, off, sizeof(hdr));
    if(got < 0)
      return 0;
    if(got != sizeof(hdr))
      break;
    remain = dip.i_size - off;
    if(!ext2_dirent_valid(&hdr, remain))
      break;

    if(hdr.inode != 0 && hdr.name_len > 0){
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
ext2_walk(struct vfs *fs, char *path, int nameiparent, char *name)
{
  struct inode *ip;
  struct inode *next;
  struct ext2_mount_data *md;
  char elem[EXT2_NAME_MAX + 1];
  char curpath[256];
  char newpath[256];
  char linkbuf[256];
  char *p;
  char *start;
  int depth;
  int n;
  int prefix_len;
  int link_len;
  int rest_len;
  int pos;
  int need_slash;
  int i;

  if(path == 0)
    return 0;

  md = 0;
  if(fs)
    md = (struct ext2_mount_data*)fs->fs_data;
  if(md == 0)
    md = ext2_bootstrap_data;
  if(md == 0)
    md = ext2_data_for_dev(ext2_active_dev);
  if(md == 0)
    return 0;

  safestrcpy(curpath, path, sizeof(curpath));
  depth = 0;

restart:

  if(curpath[0] == '/'){
    ip = ext2_make_inode(md->dev, EXT2_ROOT_INO);
    if(ip == 0)
      return 0;
  } else {
    // Relative path - start from current working directory
    ip = proc_cwd_idup();
    if(ip == 0 || ext2_data_for_dev(ip->dev) == 0){
      // cwd is not on this ext2 mount, fall back to root
      if(ip)
        iput(ip);
      ip = ext2_make_inode(md->dev, EXT2_ROOT_INO);
      if(ip == 0)
        return 0;
    }
  }

  p = curpath;
  while(1){
    start = p;
    p = ext2_skipelem(p, elem);
    if(p == 0)
      break;

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

    // Follow symlinks in non-final components (e.g. /a/linkdir/file).
    // Final component symlink following is handled by vfs_lookup_follow().
    if(next->type == T_SYMLINK && *p != 0){
      if(depth >= SYMLOOP_MAX){
        iput(next);
        iput(ip);
        return 0;
      }

      n = ext2_readlink(next, linkbuf, sizeof(linkbuf) - 1);
      if(n <= 0){
        iput(next);
        iput(ip);
        return 0;
      }
      linkbuf[n] = 0;

      if(linkbuf[0] == '/'){
        prefix_len = 0;
      } else {
        while(*start == '/')
          start++;
        prefix_len = start - curpath;
      }

      if(prefix_len < 0 || prefix_len >= (int)sizeof(newpath)){
        iput(next);
        iput(ip);
        return 0;
      }

      memmove(newpath, curpath, prefix_len);
      pos = prefix_len;

      link_len = strlen(linkbuf);
      if(pos + link_len >= (int)sizeof(newpath)){
        iput(next);
        iput(ip);
        return 0;
      }
      memmove(newpath + pos, linkbuf, link_len);
      pos += link_len;

      rest_len = strlen(p);
      if(rest_len > 0){
        need_slash = (pos == 0 || newpath[pos - 1] != '/');
        if(need_slash){
          if(pos + 1 >= (int)sizeof(newpath)){
            iput(next);
            iput(ip);
            return 0;
          }
          newpath[pos++] = '/';
        }
        if(pos + rest_len >= (int)sizeof(newpath)){
          iput(next);
          iput(ip);
          return 0;
        }
        memmove(newpath + pos, p, rest_len);
        pos += rest_len;
      }
      newpath[pos] = 0;

      iput(next);
      iput(ip);

      safestrcpy(curpath, newpath, sizeof(curpath));
      depth++;
      goto restart;
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

  if((dip.i_mode & EXT2_S_IFMT) == EXT2_S_IFLNK)
    // Symlink data (fast: stored in i_block; slow: in data blocks) must never
    // be read as file data.  Callers should follow the link via vfs_lookup_follow
    // before opening; if we reach here the VFS wiring has a bug.
    return -1;

  if((dip.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR)
    // Directory bytes are exposed through getdents()-style fixed dirent reads.
    // Reject generic bulk reads (e.g. cat on a directory) to keep semantics safe.
    if(n != sizeof(struct dirent))
      return -1;

  if((dip.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR)
    return ext2_read_dirents(data, &dip, dst, off, n);

  return ext2_read_data(data, &dip, dst, off, n);
}

static int
ext2_write(struct inode *ip, char *src, uint off, uint n)
{
  struct ext2_mount_data *data;
  struct ext2_inode dip;
  uint done;
  uint *ind_tbl;
  uint *alloc_list;
  char *zbuf;
  int ind_loaded;
  int ind_dirty;
  int ind_flushed;
  int new_ind_block;
  uint alloc_n;
  uint ptrs;
  uint blocks_per_sec;
  uint i;

  if(ip == 0 || src == 0)
    return -1;
  if(n == 0)
    return 0;

  data = ext2_data_for_dev(ip->dev);
  if(data == 0)
    return -1;
  if(ext2_read_disk_inode(data, ip->inum, &dip) < 0)
    return -1;

  // Initial write support is conservative: regular files only, no growth/allocation.
  if((dip.i_mode & EXT2_S_IFMT) != EXT2_S_IFREG)
    return -1;
  // No sparse writes: caller may append/extend only from current EOF.
  if(off > dip.i_size)
    return -1;

  blocks_per_sec = data->block_size / 512;
  if(blocks_per_sec == 0)
    return -1;

  ind_tbl = 0;
  alloc_list = 0;
  zbuf = 0;
  ind_loaded = 0;
  ind_dirty = 0;
  ind_flushed = 0;
  new_ind_block = 0;
  alloc_n = 0;
  ptrs = data->block_size / sizeof(uint);

  alloc_list = (uint*)kalloc();
  if(alloc_list == 0)
    goto fail;
  memset((char*)alloc_list, 0, PGSIZE);

  zbuf = kalloc();
  if(zbuf == 0)
    goto fail;
  memset(zbuf, 0, PGSIZE);

  // Ensure all touched logical blocks are mapped; allocate direct/single-indirect blocks as needed.
  done = 0;
  while(done < n){
    uint pos;
    uint lbn;
    uint boff;
    uint chunk;
    uint blockno;

    pos = off + done;
    lbn = pos / data->block_size;
    boff = pos % data->block_size;
    chunk = ext2_min_u32(data->block_size - boff, n - done);

    if(ext2_inode_blockno(data, &dip, lbn, &blockno) < 0)
      goto fail;
    if(blockno == 0){
      uint new_block;

      if(lbn < 12){
        if(ext2_alloc_block(data, &new_block) < 0)
          goto fail;
        if(alloc_n >= PGSIZE / sizeof(uint))
          goto fail;
        if(ext2_dev_write(data->dev, new_block * data->block_size, zbuf, data->block_size) < 0)
          goto fail;
        alloc_list[alloc_n++] = new_block;
        dip.i_block[lbn] = new_block;
      } else {
        uint ind_index;

        ind_index = lbn - 12;
        if(ind_index >= ptrs)
          goto fail;

        if(!ind_loaded){
          ind_tbl = (uint*)kalloc();
          if(ind_tbl == 0)
            goto fail;
          memset((char*)ind_tbl, 0, PGSIZE);

          if(dip.i_block[12] == 0){
            if(ext2_alloc_block(data, &dip.i_block[12]) < 0)
              goto fail;
            if(alloc_n >= PGSIZE / sizeof(uint))
              goto fail;
            alloc_list[alloc_n++] = dip.i_block[12];
            if(ext2_dev_write(data->dev, dip.i_block[12] * data->block_size,
                             zbuf, data->block_size) < 0)
              goto fail;
            new_ind_block = 1;
            ind_dirty = 1;
          } else {
            if(ext2_dev_read(data->dev, dip.i_block[12] * data->block_size,
                             (char*)ind_tbl, data->block_size) < 0)
              goto fail;
          }
          ind_loaded = 1;
        }

        if(ind_tbl[ind_index] == 0){
          if(ext2_alloc_block(data, &new_block) < 0)
            goto fail;
          if(alloc_n >= PGSIZE / sizeof(uint))
            goto fail;
          if(ext2_dev_write(data->dev, new_block * data->block_size, zbuf, data->block_size) < 0)
            goto fail;
          alloc_list[alloc_n++] = new_block;
          ind_tbl[ind_index] = new_block;
          ind_dirty = 1;
        }
      }
    }
    done += chunk;
  }

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

    if(lbn < 12){
      blockno = dip.i_block[lbn];
    } else {
      uint ind_index;

      ind_index = lbn - 12;
      if(ind_tbl == 0 || ind_index >= ptrs)
        goto fail;
      blockno = ind_tbl[ind_index];
    }
    if(blockno == 0)
      goto fail;

    byte_off = blockno * data->block_size + boff;
    if(ext2_dev_write(data->dev, byte_off, src + done, chunk) < 0)
      goto fail;
    done += chunk;
  }

  if(ind_dirty){
    if(ext2_dev_write(data->dev, dip.i_block[12] * data->block_size,
                      (char*)ind_tbl, data->block_size) < 0)
      goto fail;
    ind_flushed = 1;
  }

  if(alloc_n > 0)
    dip.i_blocks += alloc_n * blocks_per_sec;

  acquire(&tickslock);
  if(off + n > dip.i_size)
    dip.i_size = off + n;
  dip.i_mtime = ticks;
  dip.i_ctime = ticks;
  release(&tickslock);
  if(ext2_write_disk_inode(data, ip->inum, &dip) < 0)
    goto fail;

  if(ip->size < dip.i_size)
    ip->size = dip.i_size;

  if(alloc_list)
    kfree((char*)alloc_list);
  if(zbuf)
    kfree(zbuf);
  if(ind_tbl)
    kfree((char*)ind_tbl);

  return n;

fail:
  if(ind_tbl){
    for(i = 0; i < 12; i++){
      if(ext2_block_in_list(alloc_list, alloc_n, dip.i_block[i]))
        dip.i_block[i] = 0;
    }
    for(i = 0; i < ptrs; i++){
      if(ext2_block_in_list(alloc_list, alloc_n, ind_tbl[i]))
        ind_tbl[i] = 0;
    }
    if(new_ind_block)
      dip.i_block[12] = 0;
  }

  for(i = 0; i < alloc_n; i++){
    if(alloc_list[i])
      ext2_free_block(data, alloc_list[i]);
  }

  if(ind_tbl && ind_flushed && !new_ind_block && dip.i_block[12])
    ext2_dev_write(data->dev, dip.i_block[12] * data->block_size,
                   (char*)ind_tbl, data->block_size);

  if(alloc_list)
    kfree((char*)alloc_list);
  if(zbuf)
    kfree(zbuf);
  if(ind_tbl)
    kfree((char*)ind_tbl);
  return -1;
}

static int
ext2_stat(struct inode *ip, struct stat *st)
{
  struct ext2_mount_data *data;
  struct ext2_inode dip;
  short major;
  short minor;

  if(ip == 0 || st == 0)
    return -1;

  data = ext2_data_for_dev(ip->dev);
  if(data == 0)
    return -1;
  if(ext2_read_disk_inode(data, ip->inum, &dip) < 0)
    return -1;

  st->st_type = ext2_mode_to_type(dip.i_mode);
  st->st_dev = ip->dev;
  st->st_ino = ip->inum;
  major = 0;
  minor = 0;
  if(st->st_type == T_DEV)
    ext2_decode_dev(dip.i_block[0], &major, &minor);
  st->st_major = major;
  st->st_minor = minor;
  st->st_nlink = dip.i_links_count;
  st->st_uid = dip.i_uid;
  st->st_gid = dip.i_gid;
  // Set mode with proper file type bits
  switch(dip.i_mode & EXT2_S_IFMT){
  case EXT2_S_IFDIR:
    st->st_mode = M_IFDIR | (dip.i_mode & 07777);
    break;
  case EXT2_S_IFCHR:
    st->st_mode = M_IFCHR | (dip.i_mode & 07777);
    break;
  case EXT2_S_IFBLK:
    st->st_mode = M_IFBLK | (dip.i_mode & 07777);
    break;
  case EXT2_S_IFLNK:
    st->st_mode = M_IFLNK | (dip.i_mode & 07777);
    break;
  case EXT2_S_IFREG:
  default:
    st->st_mode = M_IFREG | (dip.i_mode & 07777);
    break;
  }
  st->st_size = dip.i_size;
  st->st_atime = dip.i_atime;
  st->st_mtime = dip.i_mtime;
  st->st_ctime = dip.i_ctime;
  return 0;
}

static int
ext2_setattr(struct inode *ip,
             int set_mode, int mode,
             int set_uid, int uid,
             int set_gid, int gid)
{
  struct ext2_mount_data *data;
  struct ext2_inode dip;

  if(ip == 0)
    return -1;

  data = ext2_data_for_dev(ip->dev);
  if(data == 0)
    return -1;
  if(ext2_read_disk_inode(data, ip->inum, &dip) < 0)
    return -1;

  if(set_mode){
    dip.i_mode = (dip.i_mode & EXT2_S_IFMT) | (mode & 07777);
    ip->mode = (ip->mode & M_IFMT) | (mode & 07777);
  }
  if(set_uid && uid >= 0){
    dip.i_uid = uid;
    ip->uid = uid;
  }
  if(set_gid && gid >= 0){
    dip.i_gid = gid;
    ip->gid = gid;
  }

  acquire(&tickslock);
  dip.i_ctime = ticks;
  release(&tickslock);

  if(ext2_write_disk_inode(data, ip->inum, &dip) < 0)
    return -1;

  return 0;
}

// Read the target of a symbolic link
// Returns number of bytes read, or -1 on error
static int
ext2_readlink(struct inode *ip, char *buf, uint size)
{
  struct ext2_mount_data *data;
  struct ext2_inode dip;
  uint len;

  if(ip == 0 || buf == 0 || size == 0)
    return -1;

  data = ext2_data_for_dev(ip->dev);
  if(data == 0)
    return -1;
  if(ext2_read_disk_inode(data, ip->inum, &dip) < 0)
    return -1;

  // Must be a symlink
  if((dip.i_mode & EXT2_S_IFMT) != EXT2_S_IFLNK)
    return -1;

  len = dip.i_size;
  if(len > size)
    len = size;

  // Fast symlink: target stored directly in i_block array
  // This is used when size <= 60 bytes (size of i_block)
  if(dip.i_size <= EXT2_FAST_SYMLINK_MAX){
    memmove(buf, (char*)dip.i_block, len);
    return len;
  }

  // Slow symlink: target stored in data blocks
  return ext2_read_data(data, &dip, buf, 0, len);
}

// Create a symbolic link
// dp is the parent directory, name is the link name, target is what it points to
static int
ext2_symlink(struct inode *dp, char *name, char *target)
{
  struct ext2_mount_data *data;
  struct ext2_inode parent_dip;
  struct ext2_inode link_dip;
  uint link_inum;
  uint target_len;
  struct inode *existing;

  if(dp == 0 || name == 0 || target == 0)
    return -1;

  target_len = strlen(target);
  if(target_len == 0 || target_len > 1023)  // Reasonable limit
    return -1;

  data = ext2_data_for_dev(dp->dev);
  if(data == 0)
    return -1;

  // Read parent directory inode
  if(ext2_read_disk_inode(data, dp->inum, &parent_dip) < 0)
    return -1;
  if((parent_dip.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
    return -1;

  // Check if name already exists using ext2_dirlookup
  existing = ext2_dirlookup(dp, name, 0);
  if(existing != 0){
    iput(existing);
    return -1;  // Name exists
  }

  // Allocate a new inode for the symlink
  if(ext2_alloc_inode(data, &link_inum) < 0)
    return -1;

  // Initialize the symlink inode
  memset(&link_dip, 0, sizeof(link_dip));
  link_dip.i_mode = EXT2_S_IFLNK | 0777;  // Symlinks typically have 0777 perms
  // Use dp's uid/gid as fallback (inherits from parent)
  link_dip.i_uid = dp->uid;
  link_dip.i_gid = dp->gid;
  link_dip.i_links_count = 1;
  link_dip.i_size = target_len;
  acquire(&tickslock);
  link_dip.i_atime = ticks;
  link_dip.i_ctime = ticks;
  link_dip.i_mtime = ticks;
  release(&tickslock);

  // Store the target
  if(target_len <= EXT2_FAST_SYMLINK_MAX){
    // Fast symlink: store directly in i_block
    memmove((char*)link_dip.i_block, target, target_len);
    link_dip.i_blocks = 0;  // No data blocks used
  } else {
    // Slow symlink: need to allocate a data block
    // For simplicity, only support fast symlinks initially
    ext2_free_inode(data, link_inum, 0);
    return -1;  // TODO: implement slow symlinks
  }

  // Write the symlink inode
  if(ext2_write_disk_inode(data, link_inum, &link_dip) < 0){
    ext2_free_inode(data, link_inum, 0);
    return -1;
  }

  // Add directory entry for the symlink
  if(ext2_dir_add_entry(data, &parent_dip, name, link_inum, EXT2_FT_SYMLINK) < 0){
    ext2_free_inode(data, link_inum, 0);
    return -1;
  }

  return 0;
}

static int
ext2_access(struct inode *ip, int mode)
{
  return iaccess(ip, mode);
}

static int
ext2_truncate(struct inode *ip)
{
  struct ext2_mount_data *data;
  struct ext2_inode dip;

  if(ip == 0)
    return -1;

  data = ext2_data_for_dev(ip->dev);
  if(data == 0)
    return -1;
  if(ext2_read_disk_inode(data, ip->inum, &dip) < 0)
    return -1;
  if((dip.i_mode & EXT2_S_IFMT) != EXT2_S_IFREG)
    return -1;

  if(ext2_inode_truncate_blocks(data, &dip) < 0)
    return -1;
  acquire(&tickslock);
  dip.i_mtime = ticks;
  dip.i_ctime = ticks;
  release(&tickslock);

  if(ext2_write_disk_inode(data, ip->inum, &dip) < 0)
    return -1;

  ip->size = 0;
  return 0;
}

static int
ext2_remove(struct inode *dp, char *name)
{
  struct ext2_mount_data *data;
  struct ext2_inode dip;
  struct ext2_inode tip;
  struct inode *ip;
  uint off;
  struct ext2_dirent_hdr hdr;
  struct ext2_dirent_hdr oldhdr;
  uint dev_off;
  int was_dir;
  ushort tip_kind;

  if(dp == 0 || name == 0)
    return -1;

  data = ext2_data_for_dev(dp->dev);
  if(data == 0)
    return -1;
  if(ext2_read_disk_inode(data, dp->inum, &dip) < 0)
    return -1;
  if((dip.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
    return -1;

  ip = ext2_dirlookup(dp, name, &off);
  if(ip == 0)
    return -1;
  ilock(ip);

  if(ext2_read_disk_inode(data, ip->inum, &tip) < 0){
    iunlockput(ip);
    return -1;
  }

  tip_kind = tip.i_mode & EXT2_S_IFMT;
  was_dir = (tip_kind == EXT2_S_IFDIR);
  if(was_dir && !ext2_dir_is_empty(data, &tip)){
    iunlockput(ip);
    return -1;
  }

  if(ext2_read_data(data, &dip, (char*)&hdr, off, sizeof(hdr)) != sizeof(hdr)){
    iunlockput(ip);
    return -1;
  }
  if(hdr.inode == 0){
    iunlockput(ip);
    return -1;
  }
  oldhdr = hdr;
  hdr.inode = 0;
  if(ext2_dir_dev_off(data, &dip, off, &dev_off) < 0){
    iunlockput(ip);
    return -1;
  }
  if(ext2_dev_write(data->dev, dev_off, (char*)&hdr, sizeof(hdr)) < 0){
    iunlockput(ip);
    return -1;
  }

  if(was_dir && dip.i_links_count > 0)
    dip.i_links_count--;
  acquire(&tickslock);
  dip.i_mtime = ticks;
  dip.i_ctime = ticks;
  release(&tickslock);
  if(ext2_write_disk_inode(data, dp->inum, &dip) < 0){
    ext2_dev_write(data->dev, dev_off, (char*)&oldhdr, sizeof(oldhdr));
    iunlockput(ip);
    return -1;
  }

  if(tip.i_links_count > 0)
    tip.i_links_count--;
  if(tip.i_links_count == 0){
    if(tip_kind == EXT2_S_IFREG || tip_kind == EXT2_S_IFDIR)
      if(ext2_inode_truncate_blocks(data, &tip) < 0){
        iunlockput(ip);
        return -1;
      }
    acquire(&tickslock);
    tip.i_dtime = ticks;
    tip.i_ctime = ticks;
    tip.i_mtime = ticks;
    release(&tickslock);
    if(ext2_write_disk_inode(data, ip->inum, &tip) < 0){
      iunlockput(ip);
      return -1;
    }
    if(ext2_free_inode(data, ip->inum, was_dir) < 0){
      iunlockput(ip);
      return -1;
    }

    // The ext2 backend already truncated and freed the on-disk inode.
    // Clear the cached inode so generic iput() won't try xv6 inode teardown.
    ip->nlink = 0;
    ip->size = 0;
    ip->valid = 0;
  } else {
    acquire(&tickslock);
    tip.i_ctime = ticks;
    tip.i_mtime = ticks;
    release(&tickslock);
    if(ext2_write_disk_inode(data, ip->inum, &tip) < 0){
      iunlockput(ip);
      return -1;
    }

    ip->nlink = tip.i_links_count;
    ip->size = tip.i_size;
  }

  dp->nlink = dip.i_links_count;
  dp->size = dip.i_size;

  iunlockput(ip);
  return 0;
}

static int
ext2_dirlink(struct inode *dp, char *name, uint inum)
{
  (void)dp;
  (void)name;
  (void)inum;
  return -1;
}

static int
ext2_link(struct inode *ip, struct inode *dp, char *name)
{
  struct ext2_mount_data *data;
  struct ext2_inode dip;
  struct ext2_inode tip;
  uchar file_type;
  struct inode *existing;

  if(ip == 0 || dp == 0 || name == 0)
    return -1;
  if(dp->dev != ip->dev)
    return -1;

  data = ext2_data_for_dev(dp->dev);
  if(data == 0)
    return -1;
  if(ext2_read_disk_inode(data, dp->inum, &dip) < 0)
    return -1;
  if(ext2_read_disk_inode(data, ip->inum, &tip) < 0)
    return -1;
  if((dip.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
    return -1;

  existing = ext2_dirlookup(dp, name, 0);
  if(existing != 0){
    iput(existing);
    return -1;
  }

  if((tip.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR)
    return -1;
  if((tip.i_mode & EXT2_S_IFMT) == EXT2_S_IFCHR)
    file_type = EXT2_FT_CHRDEV;
  else if((tip.i_mode & EXT2_S_IFMT) == EXT2_S_IFBLK)
    file_type = EXT2_FT_BLKDEV;
  else
    file_type = EXT2_FT_REG_FILE;

  tip.i_links_count++;
  acquire(&tickslock);
  tip.i_ctime = ticks;
  release(&tickslock);
  if(ext2_write_disk_inode(data, ip->inum, &tip) < 0)
    return -1;

  if(ext2_dir_add_entry(data, &dip, name, ip->inum, file_type) < 0){
    if(tip.i_links_count > 0)
      tip.i_links_count--;
    acquire(&tickslock);
    tip.i_ctime = ticks;
    release(&tickslock);
    ext2_write_disk_inode(data, ip->inum, &tip);
    return -1;
  }

  acquire(&tickslock);
  dip.i_mtime = ticks;
  dip.i_ctime = ticks;
  release(&tickslock);
  if(ext2_write_disk_inode(data, dp->inum, &dip) < 0)
    return -1;

  ip->nlink = tip.i_links_count;
  return 0;
}

static int
ext2_rename(struct inode *olddp, char *oldname, struct inode *newdp, char *newname)
{
  struct ext2_mount_data *data;
  struct ext2_inode odip;
  struct ext2_inode ndip;
  struct ext2_inode tip;
  struct inode *ip;
  struct inode *exist;
  uchar file_type;
  int is_dir;
  int rel;
  int moved_entry;
  int removed_old;
  int moved_dotdot;
  int moved_links;

  if(olddp == 0 || newdp == 0 || oldname == 0 || newname == 0)
    return -1;
  if(olddp->dev != newdp->dev)
    return -1;
  if(olddp == newdp && namecmp(oldname, newname) == 0)
    return 0;

  data = ext2_data_for_dev(olddp->dev);
  if(data == 0)
    return -1;
  if(ext2_read_disk_inode(data, olddp->inum, &odip) < 0)
    return -1;
  if(ext2_read_disk_inode(data, newdp->inum, &ndip) < 0)
    return -1;

  moved_entry = 0;
  removed_old = 0;
  moved_dotdot = 0;
  moved_links = 0;

  ip = ext2_dirlookup(olddp, oldname, 0);
  if(ip == 0)
    return -1;
  ilock(ip);
  if(ext2_read_disk_inode(data, ip->inum, &tip) < 0){
    iunlockput(ip);
    return -1;
  }
  is_dir = ((tip.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR);
  file_type = is_dir ? EXT2_FT_DIR : EXT2_FT_REG_FILE;

  if(is_dir && olddp != newdp){
    rel = ext2_dir_is_ancestor(ip, newdp);
    if(rel != 0){
      iunlockput(ip);
      return -1;
    }
  }

  iunlock(ip);

  exist = ext2_dirlookup(newdp, newname, 0);
  if(exist != 0){
    ilock(exist);
    if(exist->type == T_DIR || is_dir){
      iunlockput(exist);
      iput(ip);
      return -1;
    }
    if(exist->inum == ip->inum){
      iunlockput(exist);
      if(olddp != newdp || namecmp(oldname, newname) != 0){
        if(ext2_remove(olddp, oldname) < 0){
          iput(ip);
          return -1;
        }
      }
      iput(ip);
      return 0;
    }
    iunlock(exist);
    if(ext2_remove(newdp, newname) < 0){
      iput(exist);
      iput(ip);
      return -1;
    }
    iput(exist);
    if(ext2_read_disk_inode(data, newdp->inum, &ndip) < 0){
      iput(ip);
      return -1;
    }
  }

  if(ext2_dir_add_entry(data, &ndip, newname, ip->inum, file_type) < 0){
    iput(ip);
    return -1;
  }
  moved_entry = 1;

  if(ext2_dir_clear_entry(data, &odip, oldname) < 0){
    ext2_dir_clear_entry(data, &ndip, newname);
    iput(ip);
    return -1;
  }
  removed_old = 1;

  acquire(&tickslock);
  odip.i_mtime = ticks;
  odip.i_ctime = ticks;
  ndip.i_mtime = ticks;
  ndip.i_ctime = ticks;
  release(&tickslock);

  if(is_dir && olddp != newdp){
    if(ext2_dir_rewrite_inum(data, &tip, "..", newdp->inum) < 0){
      ext2_dir_clear_entry(data, &ndip, newname);
      ext2_dir_add_entry(data, &odip, oldname, ip->inum, file_type);
      iput(ip);
      return -1;
    }
    moved_dotdot = 1;
    if(odip.i_links_count > 0)
      odip.i_links_count--;
    ndip.i_links_count++;
    moved_links = 1;
    acquire(&tickslock);
    tip.i_ctime = ticks;
    tip.i_mtime = ticks;
    release(&tickslock);
    if(ext2_write_disk_inode(data, ip->inum, &tip) < 0){
      ext2_dir_rewrite_inum(data, &tip, "..", olddp->inum);
      ext2_dir_clear_entry(data, &ndip, newname);
      ext2_dir_add_entry(data, &odip, oldname, ip->inum, file_type);
      if(moved_links){
        odip.i_links_count++;
        if(ndip.i_links_count > 0)
          ndip.i_links_count--;
      }
      iput(ip);
      return -1;
    }
  }

  if(ext2_write_disk_inode(data, olddp->inum, &odip) < 0){
    if(moved_dotdot)
      ext2_dir_rewrite_inum(data, &tip, "..", olddp->inum);
    if(removed_old)
      ext2_dir_add_entry(data, &odip, oldname, ip->inum, file_type);
    if(moved_entry)
      ext2_dir_clear_entry(data, &ndip, newname);
    if(moved_links){
      odip.i_links_count++;
      if(ndip.i_links_count > 0)
        ndip.i_links_count--;
    }
    ext2_write_disk_inode(data, olddp->inum, &odip);
    ext2_write_disk_inode(data, newdp->inum, &ndip);
    iput(ip);
    return -1;
  }
  if(ext2_write_disk_inode(data, newdp->inum, &ndip) < 0){
    if(moved_dotdot)
      ext2_dir_rewrite_inum(data, &tip, "..", olddp->inum);
    if(removed_old)
      ext2_dir_add_entry(data, &odip, oldname, ip->inum, file_type);
    if(moved_entry)
      ext2_dir_clear_entry(data, &ndip, newname);
    if(moved_links){
      odip.i_links_count++;
      if(ndip.i_links_count > 0)
        ndip.i_links_count--;
    }
    ext2_write_disk_inode(data, olddp->inum, &odip);
    ext2_write_disk_inode(data, newdp->inum, &ndip);
    iput(ip);
    return -1;
  }

  olddp->nlink = odip.i_links_count;
  olddp->size = odip.i_size;
  newdp->nlink = ndip.i_links_count;
  newdp->size = ndip.i_size;

  ip->nlink = tip.i_links_count;
  ip->size = tip.i_size;

  iput(ip);
  return 0;
}

static struct inode*
ext2_root_inode(struct vfs *fs)
{
  struct ext2_mount_data *md;

  md = 0;
  if(fs)
    md = (struct ext2_mount_data*)fs->fs_data;
  if(md == 0)
    md = ext2_bootstrap_data;
  if(md == 0)
    md = ext2_data_for_dev(ext2_active_dev);
  if(md == 0)
    return 0;

  return ext2_make_inode(md->dev, EXT2_ROOT_INO);
}

static struct inode*
ext2_namei(struct vfs *fs, char *path)
{
  return ext2_walk(fs, path, 0, 0);
}

static struct inode*
ext2_nameiparent(struct vfs *fs, char *path, char *name)
{
  return ext2_walk(fs, path, 1, name);
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
  EXT2DBG("ext2: mount_init dev=%d path=%s flags=%x\n", m->dev, m->path, m->flags);
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
        EXT2DBG("ext2: bad magic dev=%d got=%x want=%x\n",
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
  if(m->path[0] == '/' && m->path[1] == 0)
    ext2_bootstrap_data = data;
  EXT2DBG("ext2: mount ok dev=%d block=%d groups=%d\n", data->dev, data->block_size, data->group_count);
  return 0;

fail:
  EXT2DBG("ext2: mount failed dev=%d stage=%d\n", m ? m->dev : -1, stage);
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
  // ext2 supports read/write and staged file/directory creation.
  fs->caps = VFS_CAP_READ | VFS_CAP_WRITE | VFS_CAP_CREATE |
             VFS_CAP_REMOVE | VFS_CAP_LINK | VFS_CAP_MKDIR | VFS_CAP_RENAME |
             VFS_CAP_SYMLINK;
  fs->fs_data = 0;
  fs->fs_destroy = ext2_mount_destroy;
  fs->mount_init = ext2_mount_init;
  fs->ops.root_inode = ext2_root_inode;
  fs->ops.namei = ext2_namei;
  fs->ops.nameiparent = ext2_nameiparent;
  fs->ops.inode_put = ext2_inode_put;
  fs->vnode_ops.read = ext2_read;
  fs->vnode_ops.write = ext2_write;
  fs->vnode_ops.truncate = ext2_truncate;
  fs->vnode_ops.stat = ext2_stat;
  fs->vnode_ops.setattr = ext2_setattr;
  fs->vnode_ops.access = ext2_access;
  fs->vnode_ops.dirlookup = ext2_dirlookup;
  fs->vnode_ops.dirlink = ext2_dirlink;
  fs->vnode_ops.link = ext2_link;
  fs->vnode_ops.rename = ext2_rename;
  fs->vnode_ops.faultctl = ext2_fault_inject_set;
  fs->vnode_ops.remove = ext2_remove;
  fs->vnode_ops.create = ext2_create;
  fs->vnode_ops.readlink = ext2_readlink;
  fs->vnode_ops.symlink = ext2_symlink;
}
