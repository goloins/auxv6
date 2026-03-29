#include "types.h"
#include "defs.h"
#include "vfs.h"

static int
xv6fs_read(struct inode *ip, char *dst, uint off, uint n)
{
  return readi(ip, dst, off, n);
}

static int
xv6fs_write(struct inode *ip, char *src, uint off, uint n)
{
  return writei(ip, src, off, n);
}

static int
xv6fs_stat(struct inode *ip, struct stat *st)
{
  if(ip == 0 || st == 0)
    return -1;
  stati(ip, st);
  return 0;
}

static int
xv6fs_access(struct inode *ip, int mode)
{
  return iaccess(ip, mode);
}

static struct inode*
xv6fs_dirlookup(struct inode *dp, char *name, uint *poff)
{
  return dirlookup(dp, name, poff);
}

static int
xv6fs_dirlink(struct inode *dp, char *name, uint inum)
{
  return dirlink(dp, name, inum);
}

static struct inode*
xv6fs_namei(char *path)
{
  return namei(path);
}

static struct inode*
xv6fs_nameiparent(char *path, char *name)
{
  return nameiparent(path, name);
}

static void
xv6fs_inode_put(struct inode *ip)
{
  iput(ip);
}

void
vfs_xv6fs_init(struct vfs *fs)
{
  if(fs == 0)
    return;

  safestrcpy(fs->name, "xv6fs", sizeof(fs->name));
  fs->caps = VFS_CAP_READ | VFS_CAP_WRITE | VFS_CAP_CREATE |
             VFS_CAP_REMOVE | VFS_CAP_LINK | VFS_CAP_MKDIR;
  fs->fs_data = 0;
  fs->fs_destroy = 0;
  fs->mount_init = 0;
  fs->ops.namei = xv6fs_namei;
  fs->ops.nameiparent = xv6fs_nameiparent;
  fs->ops.inode_put = xv6fs_inode_put;
  fs->vnode_ops.read = xv6fs_read;
  fs->vnode_ops.write = xv6fs_write;
  fs->vnode_ops.stat = xv6fs_stat;
  fs->vnode_ops.access = xv6fs_access;
  fs->vnode_ops.dirlookup = xv6fs_dirlookup;
  fs->vnode_ops.dirlink = xv6fs_dirlink;
}
