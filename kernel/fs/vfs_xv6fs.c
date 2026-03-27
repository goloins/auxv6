#include "types.h"
#include "defs.h"
#include "vfs.h"

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
  fs->ops.namei = xv6fs_namei;
  fs->ops.nameiparent = xv6fs_nameiparent;
  fs->ops.inode_put = xv6fs_inode_put;
}
