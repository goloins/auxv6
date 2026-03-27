#ifndef XV6_VFS_H
#define XV6_VFS_H

struct inode;

struct vfs_ops {
  struct inode* (*namei)(char *path);
  struct inode* (*nameiparent)(char *path, char *name);
};

struct vfs {
  char name[8];
  struct vfs_ops ops;
};

struct mount {
  int used;
  int dev;
  int flags;
  struct inode *mountpoint;
  struct vfs *fs;
};

void vfs_init(void);
struct inode* vfs_namei(char *path);
struct inode* vfs_nameiparent(char *path, char *name);

#endif
