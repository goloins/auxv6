#ifndef XV6_VFS_H
#define XV6_VFS_H

struct inode;

#define VFS_NAME_MAX 8
#define VFS_MOUNT_PATH_MAX 32
#define VFS_MOUNTS_MAX 8

struct vfs_ops {
  struct inode* (*namei)(char *path);
  struct inode* (*nameiparent)(char *path, char *name);
  void (*inode_put)(struct inode *ip);
};

struct vfs {
  char name[VFS_NAME_MAX];
  struct vfs_ops ops;
};

struct mount {
  int used;
  int dev;
  int flags;
  char path[VFS_MOUNT_PATH_MAX];
  struct inode *mountpoint;
  struct vfs *fs;
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
void vfs_xv6fs_init(struct vfs *fs);
void vfs_procfs_init(struct vfs *fs);

#endif
