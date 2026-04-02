#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
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

static int
vfs_path_split_leading_dotdot(char *path, char **tail)
{
  char *p;

  if(path == 0 || tail == 0)
    return 0;

  p = path;
  while(*p == '/')
    p++;
  if(!(p[0] == '.' && p[1] == '.' && (p[2] == 0 || p[2] == '/')))
    return 0;

  p += 2;
  while(*p == '/')
    p++;
  *tail = p;
  return 1;
}

static int
vfs_mount_parent_path_locked(struct mount *m, char *out, int outsz)
{
  int len;

  if(m == 0 || out == 0 || outsz < 2)
    return -1;

  safestrcpy(out, m->path, outsz);
  len = strlen(out);
  while(len > 1 && out[len - 1] == '/')
    out[--len] = 0;

  while(len > 1 && out[len - 1] != '/')
    len--;

  if(len <= 1){
    out[0] = '/';
    out[1] = 0;
  } else {
    out[len - 1] = 0;
  }

  return 0;
}

static int
vfs_rewrite_mount_root_dotdot_locked(struct mount *m, char *path,
                                     char *out, int outsz)
{
  char *tail;
  char parent[VFS_MOUNT_PATH_MAX];
  int plen;
  int tlen;

  if(m == 0 || path == 0 || out == 0 || outsz <= 0)
    return -1;

  if(vfs_path_split_leading_dotdot(path, &tail) == 0)
    return -1;

  if(vfs_mount_parent_path_locked(m, parent, sizeof(parent)) < 0)
    return -1;

  plen = strlen(parent);
  tlen = strlen(tail);

  if(tlen == 0){
    if(plen + 1 > outsz)
      return -1;
    safestrcpy(out, parent, outsz);
    return 0;
  }

  if(parent[0] == '/' && parent[1] == 0){
    if(1 + tlen + 1 > outsz)
      return -1;
    out[0] = '/';
    memmove(out + 1, tail, tlen);
    out[1 + tlen] = 0;
    return 0;
  }

  if(plen + 1 + tlen + 1 > outsz)
    return -1;
  memmove(out, parent, plen);
  out[plen] = '/';
  memmove(out + plen + 1, tail, tlen);
  out[plen + 1 + tlen] = 0;
  return 0;
}

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
  mounts[slot].root_inum = mountpoint ? mountpoint->inum : ROOTINO;
  safestrcpy(mounts[slot].path, path, sizeof(mounts[slot].path));
  mounts[slot].mountpoint = mountpoint;
  mounts[slot].fs_data = fs_data;
  mounts[slot].fs = fs;
  mounts[slot].data = 0;
  mounts[slot].datalen = 0;
  return 0;
}

static int
vfs_mount_register_inode(struct vfs *fs, int dev, int flags, char *path,
                         struct inode *mountpoint, const void *data, int datalen)
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
  mounts[slot].root_inum = ROOTINO;
  safestrcpy(mounts[slot].path, path, sizeof(mounts[slot].path));
  // Keep fs null until mount_init finishes so lookup paths skip this slot.
  mounts[slot].fs = 0;
  mounts[slot].fs_data = 0;
  mounts[slot].mountpoint = mountpoint;
  mounts[slot].data = data;
  mounts[slot].datalen = datalen;

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
    mctx.data = data;
    mctx.datalen = datalen;
    if(fs->mount_init(&mctx) < 0){
      acquire(&vfslock);
      mounts[slot].used = 0;
      mounts[slot].dev = 0;
      mounts[slot].flags = 0;
      mounts[slot].caps = 0;
      mounts[slot].root_inum = 0;
      mounts[slot].path[0] = 0;
      mounts[slot].mountpoint = 0;
      mounts[slot].fs = 0;
      mounts[slot].fs_data = 0;
      mounts[slot].data = 0;
      mounts[slot].datalen = 0;
      release(&vfslock);
      return -1;
    }
    fs_data = mctx.fs_data;
    fs->fs_data = fs_data;
  }

  {
    struct inode *rootip;
    rootip = 0;
    if(fs->ops.root_inode)
      rootip = fs->ops.root_inode(fs);
    else if(fs->ops.namei)
      rootip = fs->ops.namei(fs, "/");
    if(rootip){
      acquire(&vfslock);
      mounts[slot].root_inum = rootip->inum;
      release(&vfslock);
      if(fs->ops.inode_put)
        fs->ops.inode_put(rootip);
      else
        iput(rootip);
    }
  }

  acquire(&vfslock);
  mounts[slot].fs_data = fs_data;
  mounts[slot].fs = fs;
  release(&vfslock);
  return 0;
}

int
vfs_register_mount(struct vfs *fs, int dev, int flags, char *path,
                   const void *data, int datalen)
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

  if(vfs_mount_register_inode(fs, dev, flags, path, vn.ip, data, datalen) < 0){
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
        m->root_inum = 0;
        m->path[0] = 0;
        m->mountpoint = 0;
        m->fs = 0;
        m->fs_data = 0;
        m->data = 0;
        m->datalen = 0;
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
    rootvfs.fs_data = root_fs_data;
  }

  // Hold a stable mountpoint reference for '/'.
  if(rootvfs.ops.root_inode)
    rootip = rootvfs.ops.root_inode(&rootvfs);
  else
    rootip = rootvfs.ops.namei(&rootvfs, "/");
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

// If ip is a mountpoint (underlying directory), return the root inode
// of the mounted filesystem (with idup). Otherwise return 0.
// This enables crossing INTO a mounted filesystem during path resolution.
static struct inode*
vfs_cross_into_mount(struct inode *ip)
{
  int i;
  struct mount *m;
  struct inode *root;

  if(ip == 0)
    return 0;

  root = 0;
  acquire(&vfslock);
  for(i = 0; i < VFS_MOUNTS_MAX; i++){
    m = &mounts[i];
    if(m->used == 0 || m->fs == 0)
      continue;
    // Skip root mount - it has no mountpoint to cross from
    if(m->path[0] == '/' && m->path[1] == 0)
      continue;
    // Check if this mount's mountpoint matches ip
    if(m->mountpoint && m->mountpoint->dev == ip->dev &&
       m->mountpoint->inum == ip->inum){
      // Found it - get the mounted filesystem's root inode
      if(m->fs->ops.root_inode){
        release(&vfslock);
        root = m->fs->ops.root_inode(m->fs);
        return root;
      }
      break;
    }
  }
  release(&vfslock);
  return 0;
}

// If ip is the root inode of a non-root mount, return the mountpoint
// inode from the underlying filesystem (with idup) and copy the mount
// basename into name. Otherwise return 0.
struct inode*
vfs_mount_crossover(struct inode *ip, char *name)
{
  int i;
  struct mount *m;
  struct inode *mp;
  char *p;
  int len;

  if(ip == 0)
    return 0;

  mp = 0;
  acquire(&vfslock);
  for(i = 0; i < VFS_MOUNTS_MAX; i++){
    m = &mounts[i];
    if(m->used == 0 || m->fs == 0)
      continue;
    // Skip root mount
    if(m->path[0] == '/' && m->path[1] == 0)
      continue;
    // Check if this mount's device matches and ip is this fs mount root.
    if((uint)m->dev == ip->dev && ip->inum == m->root_inum){
      // Found the mount - get the mountpoint from underlying fs
      mp = m->mountpoint;
      if(mp)
        mp = idup(mp);
      // Extract basename from mount path for name
      if(name){
        p = m->path;
        len = strlen(p);
        // Find last component
        while(len > 0 && p[len-1] == '/')
          len--;
        while(len > 0 && p[len-1] != '/')
          len--;
        safestrcpy(name, p + len, DIRSIZ + 1);
        // Strip trailing slashes from name
        len = strlen(name);
        while(len > 0 && name[len-1] == '/')
          name[--len] = 0;
      }
      break;
    }
  }
  release(&vfslock);
  return mp;
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
  struct inode *cwdip;
  struct inode* (*lookup)(struct vfs *fs, char *path);
  uint cwddev;
  struct mount *m;
  char *relpath;
  char rewritten[128];

  if(path == 0 || vn == 0)
    return -1;

  vn->ip = 0;
  vn->mnt = 0;

  cwdip = 0;
  rewritten[0] = 0;
  if(path[0] != '/')
    cwdip = proc_cwd_idup();

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
      if(cwdip)
        iput(cwdip);
      return -1;
    }
    relpath = path;

    // Centralized mount-root '..' handling for relative lookups.
    if(cwdip && m->path[0] == '/' && !(m->path[1] == 0) &&
       cwdip->dev == (uint)m->dev && cwdip->inum == m->root_inum){
      if(vfs_rewrite_mount_root_dotdot_locked(m, path, rewritten,
                                              sizeof(rewritten)) == 0){
        m = select_mount_locked(rewritten);
        if(m == 0){
          release(&vfslock);
          iput(cwdip);
          return -1;
        }
        relpath = mount_relative_path(m, rewritten);
        if(relpath == 0){
          release(&vfslock);
          iput(cwdip);
          return -1;
        }
      }
    }
  }
  if(m->fs->ops.namei == 0){
    release(&vfslock);
    if(cwdip)
      iput(cwdip);
    return -1;
  }
  lookup = m->fs->ops.namei;
  vn->mnt = m;
  release(&vfslock);
  if(cwdip)
    iput(cwdip);

  ip = lookup(m->fs, relpath);
  if(ip == 0){
    vn->mnt = 0;
    return -1;
  }

  // Check if the resolved inode is a mount point - if so, cross into
  // the mounted filesystem and return its root instead.
  {
    struct inode *mounted_root;
    mounted_root = vfs_cross_into_mount(ip);
    if(mounted_root){
      iput(ip);
      ip = mounted_root;
    }
  }

  vn->ip = ip;
  return 0;
}

// vfs_lookup_follow: like vfs_lookup, but follows symlinks at the VFS layer.
// The filesystem vnode_ops.readlink op is used to read each symlink target;
// the filesystem itself never needs to know about symlink following.
// Returns -1 on ELOOP (> SYMLOOP_MAX redirections), broken link, or missing op.
int
vfs_lookup_follow(char *path, struct vnode *vn)
{
  char cur[256];
  char linktgt[256];
  struct inode *ip;
  const struct vnode_ops *vops;
  int depth;
  int n;

  if(path == 0 || vn == 0)
    return -1;

  safestrcpy(cur, path, sizeof(cur));

  for(depth = 0; depth <= SYMLOOP_MAX; depth++){
    if(vfs_lookup(cur, vn) < 0)
      return -1;

    ip = vn->ip;
    if(ip->type != T_SYMLINK)
      return 0;  // Resolved to a non-symlink inode; done.

    if(depth == SYMLOOP_MAX){
      // Too many levels of symbolic links.
      iput(ip);
      vn->ip = 0;
      vn->mnt = 0;
      return -1;
    }

    vops = vfs_dev_vops(ip->dev);
    if(vops == 0 || vops->readlink == 0){
      iput(ip);
      vn->ip = 0;
      vn->mnt = 0;
      return -1;
    }

    ilock(ip);
    n = vops->readlink(ip, linktgt, sizeof(linktgt) - 1);
    iunlock(ip);
    iput(ip);
    vn->ip = 0;
    vn->mnt = 0;

    if(n <= 0)
      return -1;
    linktgt[n] = 0;

    if(linktgt[0] == '/'){
      // Absolute target: replace entire current path.
      safestrcpy(cur, linktgt, sizeof(cur));
    } else {
      // Relative target: compose with the directory part of cur.
      char base[256];
      char *p;
      char *slash;
      int blen;
      int llen;

      safestrcpy(base, cur, sizeof(base));
      slash = 0;
      for(p = base; *p; p++)
        if(*p == '/')
          slash = p;
      if(slash)
        *(slash + 1) = 0;
      else
        base[0] = 0;

      blen = strlen(base);
      llen = strlen(linktgt);
      if(blen + llen >= (int)sizeof(cur) - 1)
        return -1;
      memmove(cur, base, blen);
      memmove(cur + blen, linktgt, llen + 1);
    }
  }

  return -1;
}

int
vfs_lookup_parent(char *path, char *name, struct vnode *vn)
{
  struct inode *ip;
  struct inode *cwdip;
  struct inode* (*lookup_parent)(struct vfs *fs, char *path, char *name);
  uint cwddev;
  struct mount *m;
  char *relpath;
  char rewritten[128];

  if(path == 0 || name == 0 || vn == 0)
    return -1;

  vn->ip = 0;
  vn->mnt = 0;

  cwdip = 0;
  rewritten[0] = 0;
  if(path[0] != '/')
    cwdip = proc_cwd_idup();

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
      if(cwdip)
        iput(cwdip);
      return -1;
    }
    relpath = path;

    // Centralized mount-root '..' handling for relative parent lookups.
    if(cwdip && m->path[0] == '/' && !(m->path[1] == 0) &&
       cwdip->dev == (uint)m->dev && cwdip->inum == m->root_inum){
      if(vfs_rewrite_mount_root_dotdot_locked(m, path, rewritten,
                                              sizeof(rewritten)) == 0){
        m = select_mount_locked(rewritten);
        if(m == 0){
          release(&vfslock);
          iput(cwdip);
          return -1;
        }
        relpath = mount_relative_path(m, rewritten);
        if(relpath == 0){
          release(&vfslock);
          iput(cwdip);
          return -1;
        }
      }
    }
  }
  if(m->fs->ops.nameiparent == 0){
    release(&vfslock);
    if(cwdip)
      iput(cwdip);
    return -1;
  }
  lookup_parent = m->fs->ops.nameiparent;
  vn->mnt = m;
  release(&vfslock);
  if(cwdip)
    iput(cwdip);

  ip = lookup_parent(m->fs, relpath, name);
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
vfs_dev_is_mounted(uint dev)
{
  int i;
  int mounted;

  mounted = 0;
  acquire(&vfslock);
  for(i = 0; i < VFS_MOUNTS_MAX; i++){
    if(mounts[i].used == 0 || mounts[i].fs == 0)
      continue;
    if((uint)mounts[i].dev == dev){
      mounted = 1;
      break;
    }
  }
  release(&vfslock);
  return mounted;
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
