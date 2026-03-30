#include "types.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "vfs.h"

static struct spinlock vfslock;
static struct vfs rootvfs;
static struct mount mounts[VFS_MOUNTS_MAX];
static int vfs_ready;
static uint vfs_rootdev = ROOTFS_DEV;
static char vfs_rootrel[] = "/";

static void
vfs_root_configure(struct vfs *fs)
{
#if ROOTFS_TYPE == ROOTFS_TYPE_EXT2
  vfs_ext2_init(fs);
#elif ROOTFS_TYPE == ROOTFS_TYPE_XV6FS
  vfs_xv6fs_init(fs);
#else
#error Unsupported ROOTFS_TYPE
#endif
}

static struct mount*
select_mount_for_dev_locked(uint dev)
{
  int i;

  for(i = 0; i < VFS_MOUNTS_MAX; i++){
    if(mounts[i].used == 0 || mounts[i].fs == 0)
      continue;
    if((uint)mounts[i].dev == dev)
      return &mounts[i];
  }
  return 0;
}

static int
is_path_separator(char c)
{
  return c == 0 || c == '/';
}

static int
path_effective_len(char *path)
{
  int n;

  if(path == 0)
    return 0;

  for(n = 0; path[n]; n++)
    ;

  // Keep root path as "/".
  while(n > 1 && path[n - 1] == '/')
    n--;

  return n;
}

static int
mount_path_match_len(char *mount_path, char *path)
{
  int i;
  int mlen;
  int plen;

  if(mount_path == 0 || path == 0)
    return -1;

  mlen = path_effective_len(mount_path);
  plen = path_effective_len(path);

  if(mlen == 1 && mount_path[0] == '/')
    return 1;

  if(plen < mlen)
    return -1;

  for(i = 0; i < mlen; i++){
    if(mount_path[i] != path[i])
      return -1;
  }

  if(plen == mlen)
    return mlen;

  if(!is_path_separator(path[mlen]))
    return -1;
  return mlen;
}

static struct mount*
select_mount_locked(char *path)
{
  int i;
  int best_len;
  struct mount *best;

  best = 0;
  best_len = -1;
  for(i = 0; i < VFS_MOUNTS_MAX; i++){
    int mlen;

    if(mounts[i].used == 0 || mounts[i].fs == 0)
      continue;
    mlen = mount_path_match_len(mounts[i].path, path);
    if(mlen < 0)
      continue;
    if(mlen > best_len){
      best = &mounts[i];
      best_len = mlen;
    }
  }
  return best;
}

static int
mount_allocate_locked(void)
{
  int i;

  for(i = 0; i < VFS_MOUNTS_MAX; i++){
    if(mounts[i].used == 0)
      return i;
  }
  return -1;
}

static int
vfs_attach_mount_locked(struct vfs *fs, int dev, int flags, char *path,
                        struct inode *mountpoint, void *fs_data)
{
  int slot;

  slot = mount_allocate_locked();
  if(slot < 0)
    return -1;

  mounts[slot].used = 1;
  mounts[slot].dev = dev;
  mounts[slot].flags = flags;
  mounts[slot].caps = fs->caps;
  safestrcpy(mounts[slot].path, path, sizeof(mounts[slot].path));
  mounts[slot].mountpoint = mountpoint;
  mounts[slot].fs_data = fs_data;
  mounts[slot].fs = fs;
  return 0;
}

static int
vfs_mount_register_inode(struct vfs *fs, int dev, int flags, char *path, struct inode *mountpoint)
{
  int slot;
  void *fs_data;

  if(fs == 0 || path == 0 || mountpoint == 0)
    return -1;

  fs_data = 0;

  acquire(&vfslock);
  slot = mount_allocate_locked();
  if(slot < 0){
    release(&vfslock);
    return -1;
  }

  mounts[slot].used = 1;
  mounts[slot].dev = dev;
  mounts[slot].flags = flags;
  mounts[slot].caps = fs->caps;
  safestrcpy(mounts[slot].path, path, sizeof(mounts[slot].path));
  // Keep fs null until mount_init finishes so lookup paths skip this slot.
  mounts[slot].fs = 0;
  mounts[slot].fs_data = 0;
  mounts[slot].mountpoint = mountpoint;

  release(&vfslock);

  // mount_init may block on disk I/O; never call it with vfslock held.
  if(fs->mount_init){
    struct mount mctx;

    memset(&mctx, 0, sizeof(mctx));
    mctx.dev = dev;
    mctx.flags = flags;
    mctx.caps = fs->caps;
    safestrcpy(mctx.path, path, sizeof(mctx.path));
    mctx.mountpoint = mountpoint;
    mctx.fs = fs;
    if(fs->mount_init(&mctx) < 0){
      acquire(&vfslock);
      mounts[slot].used = 0;
      mounts[slot].dev = 0;
      mounts[slot].flags = 0;
      mounts[slot].caps = 0;
      mounts[slot].path[0] = 0;
      mounts[slot].mountpoint = 0;
      mounts[slot].fs = 0;
      mounts[slot].fs_data = 0;
      release(&vfslock);
      return -1;
    }
    fs_data = mctx.fs_data;
  }

  acquire(&vfslock);
  mounts[slot].fs_data = fs_data;
  mounts[slot].fs = fs;
  release(&vfslock);
  return 0;
}

int
vfs_register_mount(struct vfs *fs, int dev, int flags, char *path)
{
  struct vnode vn;

  if(fs == 0 || path == 0 || path[0] != '/'){
    return -1;
  }
  if(fs->ops.namei == 0 || fs->ops.nameiparent == 0 || fs->ops.inode_put == 0){
    return -1;
  }

    VFSDBG("vfs: register mount path=%s fs=%s dev=%d flags=%x\n",
      path, fs->name, dev, flags);

  if(vfs_lookup(path, &vn) < 0)
    return -1;

  if(vfs_mount_register_inode(fs, dev, flags, path, vn.ip) < 0){
    VFSDBG("vfs: mount register failed path=%s fs=%s dev=%d\n", path, fs->name, dev);
    vfs_vnode_drop(&vn);
    return -1;
  }

  VFSDBG("vfs: mount register ok path=%s fs=%s dev=%d\n", path, fs->name, dev);

  vn.ip = 0;
  vn.mnt = 0;
  return 0;
}

int
vfs_unmount(char *path)
{
  struct inode *mountpoint;
  struct vfs *fs;
  void *mount_fs_data;
  uint dev;
  struct mount *m;
  int i;

  if(path == 0 || path[0] != '/')
    return -1;
  if(path[0] == '/' && path[1] == 0)
    return -1;

  mountpoint = 0;
  fs = 0;
  mount_fs_data = 0;
  dev = 0;

  acquire(&vfslock);
  for(i = 0; i < VFS_MOUNTS_MAX; i++){
    m = &mounts[i];
    if(m->used && m->path[0]){
      int j;

      for(j = 0; m->path[j] && path[j]; j++){
        if(m->path[j] != path[j])
          break;
      }
      if(m->path[j] == 0 && (path[j] == 0 || (path[j] == '/' && path[j + 1] == 0))){
        dev = m->dev;
        if((dev != 0 && file_has_refs_on_dev(dev)) ||
           (dev != 0 && proc_has_cwd_on_dev(dev))){
          release(&vfslock);
          return -1;
        }
        mountpoint = m->mountpoint;
        fs = m->fs;
        mount_fs_data = m->fs_data;
        m->used = 0;
        m->dev = 0;
        m->flags = 0;
        m->caps = 0;
        m->path[0] = 0;
        m->mountpoint = 0;
        m->fs = 0;
        m->fs_data = 0;
        break;
      }
    }
  }
  release(&vfslock);

  if(mountpoint == 0)
    return -1;

  // Drop the held mountpoint vnode ref outside the spinlock.
  if(fs && fs->ops.inode_put)
    fs->ops.inode_put(mountpoint);
  else
    iput(mountpoint);

  if(fs && fs->fs_destroy)
    fs->fs_destroy(fs);

  // Per-mount private data is owned by the mount slot.
  if(mount_fs_data)
    kfree((char*)mount_fs_data);

  // Dynamically allocated filesystems are owned by mount(2).
  if(fs && fs != &rootvfs)
    kfree((char*)fs);

  return 0;
}

static char*
mount_relative_path(struct mount *m, char *path)
{
  int i;
  int mlen;
  int plen;

  if(m == 0 || path == 0)
    return 0;

  mlen = path_effective_len(m->path);
  plen = path_effective_len(path);

  if(mlen == 1 && m->path[0] == '/')
    return path;

  if(plen < mlen)
    return 0;

  for(i = 0; i < mlen; i++){
    if(m->path[i] != path[i])
      return 0;
  }

  if(plen == mlen)
    return vfs_rootrel;

  if(path[mlen] != '/')
    return 0;

  return &path[mlen];
}

void
vfs_init(void)
{
  struct mount mctx;
  void *root_fs_data;
  struct inode *rootip;

  initlock(&vfslock, "vfs");

  memset(&rootvfs, 0, sizeof(rootvfs));
  vfs_root_configure(&rootvfs);
  if(rootvfs.ops.root_inode == 0 && rootvfs.ops.namei == 0)
    panic("vfs_init: root lookup");

  root_fs_data = 0;
  if(rootvfs.mount_init){
    memset(&mctx, 0, sizeof(mctx));
    mctx.dev = vfs_rootdev;
    mctx.flags = 0;
    mctx.caps = rootvfs.caps;
    safestrcpy(mctx.path, "/", sizeof(mctx.path));
    mctx.fs = &rootvfs;
    if(rootvfs.mount_init(&mctx) < 0)
      panic("vfs_init: root mount_init");
    root_fs_data = mctx.fs_data;
  }

  // Hold a stable mountpoint reference for '/'.
  if(rootvfs.ops.root_inode)
    rootip = rootvfs.ops.root_inode();
  else
    rootip = rootvfs.ops.namei("/");
  if(rootip == 0)
    panic("vfs_init: root mountpoint");

  acquire(&vfslock);
  if(vfs_attach_mount_locked(&rootvfs, vfs_rootdev, 0, "/", rootip, root_fs_data) < 0){
    release(&vfslock);
    panic("vfs_init: mount root");
  }

  vfs_ready = 1;
  release(&vfslock);
}

uint
vfs_root_dev(void)
{
  uint dev;

  acquire(&vfslock);
  dev = vfs_rootdev;
  release(&vfslock);
  return dev;
}

int
vfs_is_root_inode(struct inode *ip)
{
  struct mount *m;
  int is_root;

  if(ip == 0)
    return 0;

  acquire(&vfslock);
  m = select_mount_locked("/");
  is_root = (m && m->mountpoint && m->mountpoint->dev == ip->dev &&
             m->mountpoint->inum == ip->inum);
  release(&vfslock);
  return is_root;
}

struct inode*
vfs_namei(char *path)
{
  struct vnode vn;

  if(vfs_lookup(path, &vn) < 0)
    return 0;
  return vn.ip;
}

struct inode*
vfs_nameiparent(char *path, char *name)
{
  struct vnode vn;

  if(vfs_lookup_parent(path, name, &vn) < 0)
    return 0;
  return vn.ip;
}

int
vfs_lookup(char *path, struct vnode *vn)
{
  struct inode *ip;
  struct inode* (*lookup)(char *path);
  uint cwddev;
  struct mount *m;
  char *relpath;

  if(path == 0 || vn == 0)
    return -1;

  vn->ip = 0;
  vn->mnt = 0;

  acquire(&vfslock);
  if(vfs_ready == 0){
    release(&vfslock);
    return -1;
  }
  if(path[0] == '/'){
    m = select_mount_locked(path);
    if(m == 0){
      release(&vfslock);
      return -1;
    }
    relpath = mount_relative_path(m, path);
    if(relpath == 0){
      release(&vfslock);
      return -1;
    }
  } else {
    cwddev = proc_cwd_dev();
    if(cwddev == 0)
      cwddev = vfs_rootdev;
    m = select_mount_for_dev_locked(cwddev);
    if(m == 0 && cwddev == vfs_rootdev)
      m = select_mount_locked("/");
    if(m == 0){
      release(&vfslock);
      return -1;
    }
    relpath = path;
  }
  if(m->fs->ops.namei == 0){
    release(&vfslock);
    return -1;
  }
  lookup = m->fs->ops.namei;
  vn->mnt = m;
  release(&vfslock);

  ip = lookup(relpath);
  if(ip == 0){
    vn->mnt = 0;
    return -1;
  }
  vn->ip = ip;
  return 0;
}

int
vfs_lookup_parent(char *path, char *name, struct vnode *vn)
{
  struct inode *ip;
  struct inode* (*lookup_parent)(char *path, char *name);
  uint cwddev;
  struct mount *m;
  char *relpath;

  if(path == 0 || name == 0 || vn == 0)
    return -1;

  vn->ip = 0;
  vn->mnt = 0;

  acquire(&vfslock);
  if(vfs_ready == 0){
    release(&vfslock);
    return -1;
  }
  if(path[0] == '/'){
    m = select_mount_locked(path);
    if(m == 0){
      release(&vfslock);
      return -1;
    }
    relpath = mount_relative_path(m, path);
    if(relpath == 0){
      release(&vfslock);
      return -1;
    }
  } else {
    cwddev = proc_cwd_dev();
    if(cwddev == 0)
      cwddev = vfs_rootdev;
    m = select_mount_for_dev_locked(cwddev);
    if(m == 0 && cwddev == vfs_rootdev)
      m = select_mount_locked("/");
    if(m == 0){
      release(&vfslock);
      return -1;
    }
    relpath = path;
  }
  if(m->fs->ops.nameiparent == 0){
    release(&vfslock);
    return -1;
  }
  lookup_parent = m->fs->ops.nameiparent;
  vn->mnt = m;
  release(&vfslock);

  ip = lookup_parent(relpath, name);
  if(ip == 0){
    vn->mnt = 0;
    return -1;
  }
  vn->ip = ip;
  return 0;
}

void
vfs_vnode_drop(struct vnode *vn)
{
  void (*drop)(struct inode *ip);

  if(vn == 0 || vn->ip == 0)
    return;

  drop = iput;
  if(vn->mnt && vn->mnt->fs && vn->mnt->fs->ops.inode_put)
    drop = vn->mnt->fs->ops.inode_put;

  drop(vn->ip);
  vn->ip = 0;
  vn->mnt = 0;
}

int
vfs_mount_count(void)
{
  int i;
  int n;

  acquire(&vfslock);
  n = 0;
  for(i = 0; i < VFS_MOUNTS_MAX; i++){
    if(mounts[i].used)
      n++;
  }
  release(&vfslock);
  return n;
}

int
vfs_dev_has_cap(uint dev, uint cap)
{
  struct mount *m;
  int ok;

  acquire(&vfslock);
  m = select_mount_for_dev_locked(dev);
  if(m == 0 && dev == vfs_rootdev)
    m = select_mount_locked("/");
  ok = (m && (m->caps & cap) == cap);
  release(&vfslock);

  return ok;
}

const struct vnode_ops*
vfs_dev_vops(uint dev)
{
  struct mount *m;
  const struct vnode_ops *vops;

  vops = 0;
  acquire(&vfslock);
  m = select_mount_for_dev_locked(dev);
  if(m == 0 && dev == vfs_rootdev)
    m = select_mount_locked("/");
  if(m && m->fs)
    vops = &m->fs->vnode_ops;
  release(&vfslock);

  return vops;
}

int
vfs_dev_is_xv6fs(uint dev)
{
  struct mount *m;
  int is_xv6;

  is_xv6 = 0;
  acquire(&vfslock);
  m = select_mount_for_dev_locked(dev);
  if(m == 0 && dev == vfs_rootdev)
    m = select_mount_locked("/");
  if(m && m->fs && strncmp(m->fs->name, "xv6fs", VFS_NAME_MAX) == 0)
    is_xv6 = 1;
  release(&vfslock);

  return is_xv6;
}

int
vfs_dev_faultctl(uint dev, int which, int value)
{
  const struct vnode_ops *ops;

  ops = vfs_dev_vops(dev);
  if(ops == 0 || ops->faultctl == 0)
    return -1;
  return ops->faultctl(which, value);
}

void*
vfs_dev_fs_data(uint dev)
{
  struct mount *m;
  void *fs_data;

  fs_data = 0;
  acquire(&vfslock);
  m = select_mount_for_dev_locked(dev);
  if(m == 0 && dev == vfs_rootdev)
    m = select_mount_locked("/");
  if(m)
    fs_data = m->fs_data;
  release(&vfslock);

  return fs_data;
}

int
vfs_get_mounts(struct vfs_mount_info *out, int max)
{
  int i;
  int n;

  if(out == 0 || max <= 0)
    return -1;

  acquire(&vfslock);
  n = 0;
  for(i = 0; i < VFS_MOUNTS_MAX && n < max; i++){
    if(mounts[i].used == 0 || mounts[i].fs == 0)
      continue;
    out[n].dev = mounts[i].dev;
    out[n].flags = mounts[i].flags;
    safestrcpy(out[n].fstype, mounts[i].fs->name, sizeof(out[n].fstype));
    safestrcpy(out[n].path, mounts[i].path, sizeof(out[n].path));
    n++;
  }
  release(&vfslock);
  return n;
}
