#include "types.h"
#include "defs.h"
#include "param.h"
#include "ext_journal.h"
#include "vfs_ext2_shared.h"

#define EXT3_JBD_MAGIC 0xC03B3998U

#define EXT3_JBD_DESCRIPTOR_BLOCK 1U
#define EXT3_JBD_COMMIT_BLOCK 2U
#define EXT3_JBD_SUPERBLOCK_V1 3U
#define EXT3_JBD_SUPERBLOCK_V2 4U
#define EXT3_JBD_REVOKE_BLOCK 5U

#define EXT3_JBD_TAG_ESCAPE   0x00000001U
#define EXT3_JBD_TAG_SAME_UUID 0x00000002U
#define EXT3_JBD_TAG_DELETED  0x00000004U
#define EXT3_JBD_TAG_LAST_TAG 0x00000008U

static uint
ext3_be32(const uchar *src)
{
  return ((uint)src[0] << 24) |
         ((uint)src[1] << 16) |
         ((uint)src[2] << 8) |
         (uint)src[3];
}

static uint
ext3_journal_advance(const struct ext2_mount_data *data, uint cur, uint step)
{
  uint span;
  uint rel;

  span = data->journal.maxlen - data->journal.first;
  if(span == 0)
    return 0;

  rel = cur - data->journal.first;
  rel = (rel + step) % span;
  return data->journal.first + rel;
}

static int
ext3_journal_read_block(struct ext2_mount_data *data,
                        struct ext2_inode *journal_inode,
                        uint jblk,
                        char *buf)
{
  uint disk_block;

  if(data == 0 || journal_inode == 0 || buf == 0)
    return -1;
  if(ext2_inode_blockno(data, journal_inode, jblk, &disk_block) < 0 || disk_block == 0)
    return -1;
  return ext2_dev_read(data->dev, disk_block * data->block_size, buf, data->block_size);
}

static int
ext3_journal_descriptor_tags(const char *buf, uint block_size)
{
  uint off;
  int tags;

  off = 12;
  tags = 0;
  while(off + 8 <= block_size){
    uint flags;

    flags = ext3_be32((const uchar*)buf + off + 4);
    off += 8;
    tags++;

    if((flags & EXT3_JBD_TAG_SAME_UUID) == 0){
      if(off + 16 > block_size)
        return -1;
      off += 16;
    }

    if((flags & EXT3_JBD_TAG_LAST_TAG) != 0)
      return tags;
  }

  return -1;
}

static int
ext3_journal_revoke_valid(const char *buf, uint block_size)
{
  uint count;

  count = ext3_be32((const uchar*)buf + 12);
  if(count < 16 || count > block_size)
    return 0;
  if((count & 3) != 0)
    return 0;
  return 1;
}

static int
ext3_journal_scan(struct ext2_mount_data *data, struct ext2_inode *journal_inode)
{
  char *buf;
  uint cur;
  uint seq;
  uint scanned;
  uint open_data_blocks;
  uint open_revoke_blocks;
  uint open_descriptor_block;
  int transaction_open;

  if(data == 0 || journal_inode == 0)
    return -1;

  data->journal.descriptor_blocks = 0;
  data->journal.data_blocks = 0;
  data->journal.revoke_blocks = 0;
  data->journal.commit_blocks = 0;
  data->journal.transaction_count = 0;
  data->journal.committed_data_blocks = 0;
  data->journal.committed_revoke_blocks = 0;
  data->journal.open_data_blocks = 0;
  data->journal.open_revoke_blocks = 0;
  data->journal.last_commit_sequence = 0;
  data->journal.last_descriptor_block = 0;
  data->journal.last_commit_block = 0;
  data->journal.last_data_blocks = 0;
  data->journal.last_revoke_blocks = 0;
  data->journal.last_data_start_block = 0;
  data->journal.last_data_end_block = 0;
  data->journal.replay_seed_valid = 0;
  data->journal.end_sequence = data->journal.sequence;

  if(data->journal.start == 0)
    return 0;

  buf = kalloc();
  if(buf == 0)
    return -1;

  cur = data->journal.start;
  seq = data->journal.sequence;
  scanned = 0;
  open_data_blocks = 0;
  open_revoke_blocks = 0;
  open_descriptor_block = 0;
  transaction_open = 0;

  while(scanned < data->journal.maxlen){
    uint magic;
    uint blocktype;
    uint blockseq;

    if(cur < data->journal.first || cur >= data->journal.maxlen){
      kfree(buf);
      return -1;
    }
    if(ext3_journal_read_block(data, journal_inode, cur, buf) < 0){
      kfree(buf);
      return -1;
    }

    magic = ext3_be32((uchar*)buf);
    if(magic != EXT3_JBD_MAGIC)
      break;

    blocktype = ext3_be32((uchar*)buf + 4);
    blockseq = ext3_be32((uchar*)buf + 8);
    if(blockseq != seq)
      break;

    if(blocktype == EXT3_JBD_DESCRIPTOR_BLOCK){
      int tags;
      uint step;

      if(transaction_open){
        kfree(buf);
        return -1;
      }
      tags = ext3_journal_descriptor_tags(buf, data->block_size);
      if(tags <= 0){
        kfree(buf);
        return -1;
      }
      step = 1 + (uint)tags;
      if(step >= data->journal.maxlen){
        kfree(buf);
        return -1;
      }
      data->journal.descriptor_blocks++;
      data->journal.data_blocks += (uint)tags;
      open_descriptor_block = cur;
      open_data_blocks = (uint)tags;
      open_revoke_blocks = 0;
      transaction_open = 1;
      cur = ext3_journal_advance(data, cur, step);
      scanned += step;
      continue;
    }

    if(blocktype == EXT3_JBD_REVOKE_BLOCK){
      if(!transaction_open){
        kfree(buf);
        return -1;
      }
      if(!ext3_journal_revoke_valid(buf, data->block_size)){
        kfree(buf);
        return -1;
      }
      data->journal.revoke_blocks++;
      open_revoke_blocks++;
      cur = ext3_journal_advance(data, cur, 1);
      scanned++;
      continue;
    }

    if(blocktype == EXT3_JBD_COMMIT_BLOCK){
      if(!transaction_open){
        kfree(buf);
        return -1;
      }
      data->journal.commit_blocks++;
      data->journal.transaction_count++;
      data->journal.committed_data_blocks += open_data_blocks;
      data->journal.committed_revoke_blocks += open_revoke_blocks;
      data->journal.last_commit_sequence = seq;
      data->journal.last_descriptor_block = open_descriptor_block;
      data->journal.last_commit_block = cur;
      data->journal.last_data_blocks = open_data_blocks;
      data->journal.last_revoke_blocks = open_revoke_blocks;
      data->journal.last_data_start_block =
        ext3_journal_advance(data, open_descriptor_block, 1);
      data->journal.last_data_end_block =
        ext3_journal_advance(data, open_descriptor_block, open_data_blocks);
      data->journal.replay_seed_valid = 1;
      open_descriptor_block = 0;
      open_data_blocks = 0;
      open_revoke_blocks = 0;
      transaction_open = 0;
      seq++;
      cur = ext3_journal_advance(data, cur, 1);
      scanned++;
      continue;
    }

    kfree(buf);
    return -1;
  }

  data->journal.open_data_blocks = open_data_blocks;
  data->journal.open_revoke_blocks = open_revoke_blocks;
  data->journal.end_sequence = seq;
  kfree(buf);
  return 0;
}

int
ext3_journal_discover(struct ext2_mount_data *data)
{
  struct ext2_inode journal_inode;
  char *buf;
  uint blockno;
  uint magic;
  uint blocktype;
  uint block_size;
  uint maxlen;
  uint first;

  if(data == 0)
    return -1;

  memset(&data->journal, 0, sizeof(data->journal));
  if(data->sb.s_journal_inum == 0)
    return -1;
  if(ext2_read_disk_inode(data, data->sb.s_journal_inum, &journal_inode) < 0)
    return -1;
  if(journal_inode.i_size < data->block_size || journal_inode.i_blocks == 0)
    return -1;
  if(ext2_inode_blockno(data, &journal_inode, 0, &blockno) < 0 || blockno == 0)
    return -1;

  buf = kalloc();
  if(buf == 0)
    return -1;
  if(ext2_dev_read(data->dev, blockno * data->block_size, buf, data->block_size) < 0){
    kfree(buf);
    return -1;
  }

  magic = ext3_be32((uchar*)buf);
  blocktype = ext3_be32((uchar*)buf + 4);
  block_size = ext3_be32((uchar*)buf + 12);
  maxlen = ext3_be32((uchar*)buf + 16);
  first = ext3_be32((uchar*)buf + 20);

  if(magic != EXT3_JBD_MAGIC ||
     (blocktype != EXT3_JBD_SUPERBLOCK_V1 &&
      blocktype != EXT3_JBD_SUPERBLOCK_V2) ||
     block_size != data->block_size ||
     maxlen == 0 ||
     first == 0 ||
     first >= maxlen){
    kfree(buf);
    return -1;
  }

  data->journal.inode_num = data->sb.s_journal_inum;
  data->journal.superblock_block = blockno;
  data->journal.block_size = block_size;
  data->journal.maxlen = maxlen;
  data->journal.first = first;
  data->journal.sequence = ext3_be32((uchar*)buf + 24);
  data->journal.start = ext3_be32((uchar*)buf + 28);
  data->journal.feature_compat = ext3_be32((uchar*)buf + 36);
  data->journal.feature_incompat = ext3_be32((uchar*)buf + 40);
  data->journal.feature_ro_compat = ext3_be32((uchar*)buf + 44);
  data->journal.valid = 1;
  kfree(buf);

  return ext3_journal_scan(data, &journal_inode);
}