#ifndef XV6_VFS_H
#define XV6_VFS_H

#include "types.h"

struct inode;
struct dirent;
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
#define VFS_CAP_RENAME   0x0040
#define VFS_CAP_SYMLINK  0x0080

#define SYMLOOP_MAX      8   // Maximum symlink depth during path resolution

struct vnode_ops {
  int (*read)(struct inode *ip, char *dst, uint off, uint n);
  int (*write)(struct inode *ip, char *src, uint off, uint n);
  int (*truncate)(struct inode *ip);
  int (*drop)(struct inode *ip);
  int (*stat)(struct inode *ip, struct stat *st);
  int (*setattr)(struct inode *ip,
                 int set_mode, int mode,
                 int set_uid, int uid,
                 int set_gid, int gid);
  int (*access)(struct inode *ip, int mode);
  struct inode* (*dirlookup)(struct inode *dp, char *name, uint *poff);
  int (*dirlink)(struct inode *dp, char *name, uint inum);
  int (*link)(struct inode *ip, struct inode *dp, char *name);
  int (*remove)(struct inode *dp, char *name);
  int (*rename)(struct inode *olddp, char *oldname,
                struct inode *newdp, char *newname);
  int (*faultctl)(int which, int value);
  struct inode* (*create)(struct inode *dp, char *name, short type,
                          short major, short minor, int mode,
                          int uid, int gid);
  // Symlink operations
  int (*readlink)(struct inode *ip, char *buf, uint size);
  int (*symlink)(struct inode *dp, char *name, char *target);
};

struct vfs_ops {
  struct inode* (*root_inode)(struct vfs *fs);
  struct inode* (*namei)(struct vfs *fs, char *path);
  struct inode* (*nameiparent)(struct vfs *fs, char *path, char *name);
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
  uint root_inum;
  char path[VFS_MOUNT_PATH_MAX];
  struct inode *mountpoint;
  struct vfs *fs;
  void *fs_data;
  const void *data;
  int datalen;
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
int vfs_lookup_follow(char *path, struct vnode *vn);
int vfs_lookup_parent(char *path, char *name, struct vnode *vn);
void vfs_vnode_drop(struct vnode *vn);
int vfs_register_mount(struct vfs *fs, int dev, int flags, char *path,
                       const void *data, int datalen);
int vfs_remount(char *path, int flags);
int vfs_unmount(char *path);
uint vfs_root_dev(void);
int vfs_is_system_root_inode(struct inode *ip);
int vfs_dirent_visible(struct inode *dir, struct dirent *de);
int vfs_mount_count(void);
int vfs_get_mounts(struct vfs_mount_info *out, int max);
int vfs_dev_is_mounted(uint dev);
int vfs_dev_has_cap(uint dev, uint cap);
const struct vnode_ops* vfs_dev_vops(uint dev);
int vfs_dev_is_xv6fs(uint dev);
int vfs_dev_faultctl(uint dev, int which, int value);
void* vfs_dev_fs_data(uint dev);
void vfs_xv6fs_init(struct vfs *fs);
void vfs_ext2_init(struct vfs *fs);
void vfs_msdosfs_init(struct vfs *fs);
void vfs_exfat_init(struct vfs *fs);
void vfs_btrfs_init(struct vfs *fs);
void vfs_ufs2_init(struct vfs *fs);
void vfs_procfs_init(struct vfs *fs);
void vfs_isofs_init(struct vfs *fs);
void vfs_tmpfs_init(struct vfs *fs);
void vfs_nfs_init(struct vfs *fs);
#endif
