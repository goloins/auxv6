#include "types.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "vfs.h"
#include "fcntl.h"
#include "ext_common.h"
#include "vfs_ext2_shared.h"

static int
ext3_mount_init(struct mount *m)
{
  struct ext2_mount_data *data;

  if(m == 0)
    return -1;
  if((m->flags & MNT_RDONLY) == 0){
    MOUNTDBG("ext3: mount requires readonly flags=%x dev=%d path=%s\n",
             m->flags, m->dev, m->path);
    return -1;
  }

  data = 0;
  if(ext2_mount_setup(m, EXT_MOUNT_TARGET_EXT3, &data) < 0)
    return -1;

  m->fs_data = data;
  MOUNTDBG("ext3: mount ok dev=%d block=%d groups=%d readonly-probe\n",
           data->dev, data->block_size, data->group_count);
  return 0;
}

static void
ext3_mount_destroy(struct vfs *fs)
{
  (void)fs;
}

void
vfs_ext3_init(struct vfs *fs)
{
  if(fs == 0)
    return;

  safestrcpy(fs->name, "ext3", sizeof(fs->name));
  fs->caps = VFS_CAP_READ;
  fs->fs_data = 0;
  fs->fs_destroy = ext3_mount_destroy;
  fs->mount_init = ext3_mount_init;
  fs->ops.root_inode = ext2_root_inode;
  fs->ops.namei = ext2_namei;
  fs->ops.nameiparent = ext2_nameiparent;
  fs->ops.inode_put = ext2_inode_put;
  fs->vnode_ops.read = ext2_read;
  fs->vnode_ops.stat = ext2_stat;
  fs->vnode_ops.access = ext2_access;
  fs->vnode_ops.dirlookup = ext2_dirlookup;
  fs->vnode_ops.readlink = ext2_readlink;
}