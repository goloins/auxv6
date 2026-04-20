#ifndef AUXV6_EXT_COMMON_H
#define AUXV6_EXT_COMMON_H

#include "types.h"

#define EXT_SUPER_MAGIC 0xEF53
#define EXT_SB_OFFSET 1024
#define EXT_SB_SIZE 1024

#define EXT_GOOD_OLD_REV 0
#define EXT_DYNAMIC_REV 1
#define EXT_GOOD_OLD_INODE_SIZE 128
#define EXT_GOOD_OLD_FIRST_INO 11

#define EXT2_FEATURE_COMPAT_DIR_PREALLOC 0x0001
#define EXT2_FEATURE_COMPAT_IMAGIC_INODES 0x0002
#define EXT3_FEATURE_COMPAT_HAS_JOURNAL 0x0004
#define EXT2_FEATURE_COMPAT_EXT_ATTR 0x0008
#define EXT2_FEATURE_COMPAT_RESIZE_INODE 0x0010
#define EXT2_FEATURE_COMPAT_DIR_INDEX 0x0020

#define EXT2_FEATURE_INCOMPAT_COMPRESSION 0x0001
#define EXT2_FEATURE_INCOMPAT_FILETYPE 0x0002
#define EXT3_FEATURE_INCOMPAT_RECOVER 0x0004
#define EXT3_FEATURE_INCOMPAT_JOURNAL_DEV 0x0008
#define EXT2_FEATURE_INCOMPAT_META_BG 0x0010

#define EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER 0x0001
#define EXT2_FEATURE_RO_COMPAT_LARGE_FILE 0x0002
#define EXT2_FEATURE_RO_COMPAT_BTREE_DIR 0x0004

struct ext_superblock {
  uint s_inodes_count;
  uint s_blocks_count;
  uint s_r_blocks_count;
  uint s_free_blocks_count;
  uint s_free_inodes_count;
  uint s_first_data_block;
  uint s_log_block_size;
  uint s_log_frag_size;
  uint s_blocks_per_group;
  uint s_frags_per_group;
  uint s_inodes_per_group;
  uint s_mtime;
  uint s_wtime;
  ushort s_mnt_count;
  short s_max_mnt_count;
  ushort s_magic;
  ushort s_state;
  ushort s_errors;
  ushort s_minor_rev_level;
  uint s_lastcheck;
  uint s_checkinterval;
  uint s_creator_os;
  uint s_rev_level;
  ushort s_def_resuid;
  ushort s_def_resgid;
  uint s_first_ino;
  ushort s_inode_size;
  ushort s_block_group_nr;
  uint s_feature_compat;
  uint s_feature_incompat;
  uint s_feature_ro_compat;
  uchar s_uuid[16];
  char s_volume_name[16];
  char s_last_mounted[64];
  uint s_algorithm_usage_bitmap;
  uchar s_prealloc_blocks;
  uchar s_prealloc_dir_blocks;
  ushort s_padding1;
  uchar s_journal_uuid[16];
  uint s_journal_inum;
  uint s_journal_dev;
  uint s_last_orphan;
};

enum ext_mount_target {
  EXT_MOUNT_TARGET_EXT2 = 1,
  EXT_MOUNT_TARGET_EXT3 = 2,
};

enum ext_mount_reject_reason {
  EXT_MOUNT_REJECT_NONE = 0,
  EXT_MOUNT_REJECT_BAD_MAGIC,
  EXT_MOUNT_REJECT_BAD_GEOMETRY,
  EXT_MOUNT_REJECT_BAD_BLOCK_SIZE,
  EXT_MOUNT_REJECT_BAD_INODE_SIZE,
  EXT_MOUNT_REJECT_MISSING_DYNAMIC_REV,
  EXT_MOUNT_REJECT_EXT2_ON_JOURNALED,
  EXT_MOUNT_REJECT_EXT3_NO_JOURNAL,
  EXT_MOUNT_REJECT_UNSUPPORTED_COMPAT,
  EXT_MOUNT_REJECT_UNSUPPORTED_INCOMPAT,
  EXT_MOUNT_REJECT_UNSUPPORTED_RO_COMPAT,
  EXT_MOUNT_REJECT_EXTERNAL_JOURNAL,
  EXT_MOUNT_REJECT_NEEDS_RECOVERY,
};

struct ext_mount_probe {
  uint block_size;
  uint inode_size;
  uint compat;
  uint incompat;
  uint ro_compat;
  int reason;
};

int ext_mount_probe_superblock(const struct ext_superblock *sb,
                               int target,
                               int readonly,
                               struct ext_mount_probe *out);
const char *ext_mount_reject_reason_name(int reason);

#endif