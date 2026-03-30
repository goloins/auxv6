#include "types.h"
#include "defs.h"
#include "fs.h"
#include "stat.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
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
xv6fs_truncate(struct inode *ip)
{
  if(ip == 0)
    return -1;
  itruncate(ip);
  return 0;
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

static int
xv6fs_link(struct inode *ip, struct inode *dp, char *name)
{
  if(ip == 0 || dp == 0 || name == 0)
    return -1;
  if(dp->dev != ip->dev)
    return -1;

  ip->nlink++;
  iupdate(ip);
  if(dirlink(dp, name, ip->inum) < 0){
    ip->nlink--;
    iupdate(ip);
    return -1;
  }
  return 0;
}

static int
xv6fs_rename(struct inode *olddp, char *oldname, struct inode *newdp, char *newname)
{
  struct inode *ip;
  struct inode *exist;
  struct dirent de;
  uint off;
  uint newoff;

  if(olddp == 0 || newdp == 0 || oldname == 0 || newname == 0)
    return -1;
  if(olddp->dev != newdp->dev)
    return -1;
  if(olddp == newdp && namecmp(oldname, newname) == 0)
    return 0;

  ip = dirlookup(olddp, oldname, &off);
  if(ip == 0)
    return -1;
  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    return -1;
  }

  exist = dirlookup(newdp, newname, &newoff);
  if(exist != 0){
    ilock(exist);
    if(exist->type == T_DIR){
      iunlockput(exist);
      iunlockput(ip);
      return -1;
    }
    if(exist->inum == ip->inum){
      iunlockput(exist);
      if(olddp != newdp || namecmp(oldname, newname) != 0){
        memset(&de, 0, sizeof(de));
        if(writei(olddp, (char*)&de, off, sizeof(de)) != sizeof(de)){
          iunlockput(ip);
          return -1;
        }
      }
      iunlockput(ip);
      return 0;
    }

    memset(&de, 0, sizeof(de));
    if(writei(newdp, (char*)&de, newoff, sizeof(de)) != sizeof(de)){
      iunlockput(exist);
      iunlockput(ip);
      return -1;
    }
    if(exist->nlink < 1)
      panic("xv6fs_rename: nlink < 1");
    exist->nlink--;
    iupdate(exist);
    iunlockput(exist);
  }

  ip->nlink++;
  iupdate(ip);
  if(dirlink(newdp, newname, ip->inum) < 0){
    ip->nlink--;
    iupdate(ip);
    iunlockput(ip);
    return -1;
  }

  memset(&de, 0, sizeof(de));
  if(writei(olddp, (char*)&de, off, sizeof(de)) != sizeof(de)){
    iunlockput(ip);
    return -1;
  }

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  return 0;
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
             VFS_CAP_REMOVE | VFS_CAP_LINK | VFS_CAP_MKDIR | VFS_CAP_RENAME;
  fs->fs_data = 0;
  fs->fs_destroy = 0;
  fs->mount_init = 0;
  fs->ops.namei = xv6fs_namei;
  fs->ops.nameiparent = xv6fs_nameiparent;
  fs->ops.inode_put = xv6fs_inode_put;
  fs->vnode_ops.read = xv6fs_read;
  fs->vnode_ops.write = xv6fs_write;
  fs->vnode_ops.truncate = xv6fs_truncate;
  fs->vnode_ops.stat = xv6fs_stat;
  fs->vnode_ops.access = xv6fs_access;
  fs->vnode_ops.dirlookup = xv6fs_dirlookup;
  fs->vnode_ops.dirlink = xv6fs_dirlink;
  fs->vnode_ops.link = xv6fs_link;
  fs->vnode_ops.rename = xv6fs_rename;
}
