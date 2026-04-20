#include "types.h"
#include "defs.h"
#include "ext_common.h"

static void
ext_mount_probe_init(struct ext_mount_probe *out)
{
  if(out == 0)
    return;
  memset(out, 0, sizeof(*out));
}

const char *
ext_mount_reject_reason_name(int reason)
{
  switch(reason){
  case EXT_MOUNT_REJECT_NONE:
    return "none";
  case EXT_MOUNT_REJECT_BAD_MAGIC:
    return "bad-magic";
  case EXT_MOUNT_REJECT_BAD_GEOMETRY:
    return "bad-geometry";
  case EXT_MOUNT_REJECT_BAD_BLOCK_SIZE:
    return "bad-block-size";
  case EXT_MOUNT_REJECT_BAD_INODE_SIZE:
    return "bad-inode-size";
  case EXT_MOUNT_REJECT_MISSING_DYNAMIC_REV:
    return "missing-dynamic-rev";
  case EXT_MOUNT_REJECT_EXT2_ON_JOURNALED:
    return "ext2-on-journaled-image";
  case EXT_MOUNT_REJECT_EXT3_NO_JOURNAL:
    return "ext3-without-internal-journal";
  case EXT_MOUNT_REJECT_UNSUPPORTED_COMPAT:
    return "unsupported-compat";
  case EXT_MOUNT_REJECT_UNSUPPORTED_INCOMPAT:
    return "unsupported-incompat";
  case EXT_MOUNT_REJECT_UNSUPPORTED_RO_COMPAT:
    return "unsupported-ro-compat";
  case EXT_MOUNT_REJECT_EXTERNAL_JOURNAL:
    return "external-journal";
  case EXT_MOUNT_REJECT_NEEDS_RECOVERY:
    return "needs-recovery";
  default:
    return "unknown";
  }
}

int
ext_mount_probe_superblock(const struct ext_superblock *sb,
                           int target,
                           int readonly,
                           struct ext_mount_probe *out)
{
  uint block_size;
  uint inode_size;
  uint compat_allowed;
  uint incompat_allowed;
  uint ro_compat_allowed;

  (void)readonly;

  ext_mount_probe_init(out);
  if(sb == 0 || out == 0)
    return -1;

  out->compat = sb->s_feature_compat;
  out->incompat = sb->s_feature_incompat;
  out->ro_compat = sb->s_feature_ro_compat;

  if(sb->s_magic != EXT_SUPER_MAGIC){
    out->reason = EXT_MOUNT_REJECT_BAD_MAGIC;
    return -1;
  }

  if(sb->s_blocks_count <= sb->s_first_data_block ||
     sb->s_blocks_per_group == 0 ||
     sb->s_inodes_per_group == 0){
    out->reason = EXT_MOUNT_REJECT_BAD_GEOMETRY;
    return -1;
  }

  block_size = 1024U << sb->s_log_block_size;
  if(block_size < 1024 || block_size > 4096){
    out->reason = EXT_MOUNT_REJECT_BAD_BLOCK_SIZE;
    return -1;
  }

  inode_size = sb->s_inode_size;
  if(inode_size == 0)
    inode_size = EXT_GOOD_OLD_INODE_SIZE;
  if(inode_size < EXT_GOOD_OLD_INODE_SIZE || inode_size > block_size ||
     (inode_size & 3) != 0){
    out->reason = EXT_MOUNT_REJECT_BAD_INODE_SIZE;
    return -1;
  }

  if(target == EXT_MOUNT_TARGET_EXT3 && sb->s_rev_level < EXT_DYNAMIC_REV){
    out->reason = EXT_MOUNT_REJECT_MISSING_DYNAMIC_REV;
    return -1;
  }

  compat_allowed = EXT2_FEATURE_COMPAT_DIR_PREALLOC |
                   EXT2_FEATURE_COMPAT_IMAGIC_INODES |
                   EXT2_FEATURE_COMPAT_EXT_ATTR |
                   EXT2_FEATURE_COMPAT_RESIZE_INODE |
                   EXT2_FEATURE_COMPAT_DIR_INDEX;
  incompat_allowed = EXT2_FEATURE_INCOMPAT_FILETYPE;
  ro_compat_allowed = EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER |
                      EXT2_FEATURE_RO_COMPAT_LARGE_FILE;

  if(target == EXT_MOUNT_TARGET_EXT3)
    compat_allowed |= EXT3_FEATURE_COMPAT_HAS_JOURNAL;

  if((sb->s_feature_compat & EXT3_FEATURE_COMPAT_HAS_JOURNAL) != 0 &&
     target == EXT_MOUNT_TARGET_EXT2){
    out->reason = EXT_MOUNT_REJECT_EXT2_ON_JOURNALED;
    return -1;
  }

  if(target == EXT_MOUNT_TARGET_EXT3 &&
     (sb->s_feature_compat & EXT3_FEATURE_COMPAT_HAS_JOURNAL) == 0){
    out->reason = EXT_MOUNT_REJECT_EXT3_NO_JOURNAL;
    return -1;
  }

  if((sb->s_feature_incompat & EXT3_FEATURE_INCOMPAT_JOURNAL_DEV) != 0 ||
     sb->s_journal_dev != 0){
    out->reason = EXT_MOUNT_REJECT_EXTERNAL_JOURNAL;
    return -1;
  }

  if(target == EXT_MOUNT_TARGET_EXT3 && sb->s_journal_inum == 0){
    out->reason = EXT_MOUNT_REJECT_EXT3_NO_JOURNAL;
    return -1;
  }

  if((sb->s_feature_incompat & EXT3_FEATURE_INCOMPAT_RECOVER) != 0){
    out->reason = EXT_MOUNT_REJECT_NEEDS_RECOVERY;
    return -1;
  }

  if((sb->s_feature_compat & ~compat_allowed) != 0){
    out->reason = EXT_MOUNT_REJECT_UNSUPPORTED_COMPAT;
    return -1;
  }

  if((sb->s_feature_incompat & ~incompat_allowed) != 0){
    out->reason = EXT_MOUNT_REJECT_UNSUPPORTED_INCOMPAT;
    return -1;
  }

  if((sb->s_feature_ro_compat & ~ro_compat_allowed) != 0){
    out->reason = EXT_MOUNT_REJECT_UNSUPPORTED_RO_COMPAT;
    return -1;
  }

  out->block_size = block_size;
  out->inode_size = inode_size;
  out->reason = EXT_MOUNT_REJECT_NONE;
  return 0;
}