#ifndef AUXV6_VFS_EXT2_SHARED_H
#define AUXV6_VFS_EXT2_SHARED_H

#include "types.h"
#include "stdint.h"
#include "ext_common.h"

struct inode;
struct mount;
struct stat;
struct vfs;

struct ext2_inode {
  ushort i_mode;
  ushort i_uid;
  uint i_size;
  uint i_atime;
  uint i_ctime;
  uint i_mtime;
  uint i_dtime;
  ushort i_gid;
  ushort i_links_count;
  uint i_blocks;
  uint i_flags;
  uint i_osd1;
  uint i_block[15];
  uint i_generation;
  uint i_file_acl;
  uint i_dir_acl;
  uint i_faddr;
  uint i_osd2[3];
};

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
  struct {
    uint inode_num;
    uint superblock_block;
    uint block_size;
    uint maxlen;
    uint first;
    uint sequence;
    uint start;
    uint feature_compat;
    uint feature_incompat;
    uint feature_ro_compat;
    uint descriptor_blocks;
    uint data_blocks;
    uint revoke_blocks;
    uint commit_blocks;
    uint transaction_count;
    uint committed_data_blocks;
    uint committed_revoke_blocks;
    uint open_data_blocks;
    uint open_revoke_blocks;
    uint last_commit_sequence;
    uint last_descriptor_block;
    uint last_commit_block;
    uint last_data_blocks;
    uint last_revoke_blocks;
    int replay_seed_valid;
    uint end_sequence;
    int valid;
  } journal;
  int dev;
};

#define EXT2_ROOT_INO 2

int ext2_mount_setup(struct mount *m, int target, struct ext2_mount_data **out_data);
int ext2_dev_read(uint dev, uint off, char *dst, uint n);
int ext2_read_disk_inode(struct ext2_mount_data *data, uint inum, struct ext2_inode *out);
int ext2_inode_blockno(struct ext2_mount_data *data, struct ext2_inode *dip,
                       uint lbn, uint *out);
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