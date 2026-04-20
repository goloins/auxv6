#ifndef AUXV6_VFS_EXT2_SHARED_H
#define AUXV6_VFS_EXT2_SHARED_H

#include "types.h"
#include "stdint.h"
#include "ext_common.h"

struct inode;
struct mount;
struct stat;
struct vfs;

struct ext2_group_desc {
  uint bg_block_bitmap;
  uint bg_inode_bitmap;
  uint bg_inode_table;
  ushort bg_free_blocks_count;
  ushort bg_free_inodes_count;
  ushort bg_used_dirs_count;
  ushort bg_pad;
  uint bg_reserved[3];
};

struct ext2_mount_data {
  struct ext_superblock sb;
  struct ext2_group_desc *group_descs;
  uint group_count;
  uint block_size;
  uint inode_size;
  int dev;
};

#define EXT2_ROOT_INO 2

int ext2_mount_setup(struct mount *m, int target, struct ext2_mount_data **out_data);
struct inode* ext2_root_inode(struct vfs *fs);
struct inode* ext2_namei(struct vfs *fs, char *path);
struct inode* ext2_nameiparent(struct vfs *fs, char *path, char *name);
void ext2_inode_put(struct inode *ip);
struct inode* ext2_dirlookup(struct inode *dp, char *name, uint *poff);
int ext2_read(struct inode *ip, char *dst, uint64_t off, uint n);
int ext2_stat(struct inode *ip, struct stat *st);
int ext2_access(struct inode *ip, int mode);
int ext2_readlink(struct inode *ip, char *buf, uint size);

#endif