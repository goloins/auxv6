#ifndef XV6_VFS_H
#define XV6_VFS_H

#include "types.h"

struct inode;
struct stat;

#define VFS_NAME_MAX 8
#define VFS_MOUNT_PATH_MAX 32
#define VFS_MOUNTS_MAX 8

#define VFS_CAP_READ     0x0001
#define VFS_CAP_WRITE    0x0002
#define VFS_CAP_CREATE   0x0004
#define VFS_CAP_REMOVE   0x0008
#define VFS_CAP_LINK     0x0010
#define VFS_CAP_MKDIR    0x0020

struct vnode_ops {
  int (*read)(struct inode *ip, char *dst, uint off, uint n);
  int (*write)(struct inode *ip, char *src, uint off, uint n);
  int (*stat)(struct inode *ip, struct stat *st);
  int (*access)(struct inode *ip, int mode);
  struct inode* (*dirlookup)(struct inode *dp, char *name, uint *poff);
  int (*dirlink)(struct inode *dp, char *name, uint inum);
};

struct vfs_ops {
  struct inode* (*namei)(char *path);
  struct inode* (*nameiparent)(char *path, char *name);
  void (*inode_put)(struct inode *ip);
};

struct vfs {
  char name[VFS_NAME_MAX];
  uint caps;
  void *fs_data;
  void (*fs_destroy)(struct vfs *fs);
  int (*mount_init)(struct mount *m);
  struct vfs_ops ops;
  struct vnode_ops vnode_ops;
};

struct mount {
  int used;
  int dev;
  int flags;
  uint caps;
  char path[VFS_MOUNT_PATH_MAX];
  struct inode *mountpoint;
  struct vfs *fs;
  void *fs_data;
};

struct vfs_mount_info {
  int dev;
  int flags;
  char fstype[VFS_NAME_MAX];
  char path[VFS_MOUNT_PATH_MAX];
};

struct vnode {
  struct inode *ip;
  struct mount *mnt;
};

void vfs_init(void);
struct inode* vfs_namei(char *path);
struct inode* vfs_nameiparent(char *path, char *name);
int vfs_lookup(char *path, struct vnode *vn);
int vfs_lookup_parent(char *path, char *name, struct vnode *vn);
void vfs_vnode_drop(struct vnode *vn);
int vfs_register_mount(struct vfs *fs, int dev, int flags, char *path);
int vfs_unmount(char *path);
int vfs_mount_count(void);
int vfs_get_mounts(struct vfs_mount_info *out, int max);
int vfs_dev_has_cap(uint dev, uint cap);
const struct vnode_ops* vfs_dev_vops(uint dev);
void* vfs_dev_fs_data(uint dev);
void vfs_xv6fs_init(struct vfs *fs);
void vfs_ext2_init(struct vfs *fs);
void vfs_procfs_init(struct vfs *fs);

#endif
