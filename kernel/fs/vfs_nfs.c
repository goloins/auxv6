#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "vfs.h"
#include "fs.h"
#include "file.h"
#include "stat.h"
#include "socket.h"
#include "mount.h"
#include "nfs.h"

#define NFS_FH_CACHE_MAX 128
#define NFS_PATH_MAX 256

struct nfs_fh_cache_entry {
  int used;
  uint inum;
  fhandle3 fh;
};

struct nfs_mount_data {
  struct spinlock lock;
  int dev;
  struct in_addr server;
  char export_path[256];
  nfs_mount nm;
  uint root_inum;
  struct nfs_fh_cache_entry cache[NFS_FH_CACHE_MAX];
};

static int nfs_parse_ipv4(const char *s, struct in_addr *out);
static int nfs_split_source(const char *src, struct in_addr *server, char *export_path, int max);
static int nfs_path_next(char **pp, char *elem);
static int nfs_attr_from_fh(struct nfs_mount_data *md, fhandle3 *fh, fattr3 *out);
static int nfs_cache_store_fh(struct nfs_mount_data *md, uint inum, fhandle3 *fh);
static int nfs_cache_get_fh(struct nfs_mount_data *md, uint inum, fhandle3 *fh);
static uint nfs_inum_from_attr(fattr3 *attr);
static short nfs_type_from_attr(fattr3 *attr);
static ushort nfs_mode_from_attr(fattr3 *attr);
static struct inode *nfs_make_inode(struct nfs_mount_data *md, uint inum, fattr3 *attr, fhandle3 *fh);
static struct inode *nfs_lookup_internal(struct nfs_mount_data *md, fhandle3 *dirfh, const char *name);

static int
nfs_parse_ipv4(const char *s, struct in_addr *out)
{
  uint part;
  uint acc;
  int i;

  if(s == 0 || out == 0)
    return -1;

  acc = 0;
  for(i = 0; i < 4; i++){
    part = 0;
    if(*s < '0' || *s > '9')
      return -1;
    while(*s >= '0' && *s <= '9'){
      part = part * 10 + (uint)(*s - '0');
      if(part > 255)
        return -1;
      s++;
    }
    acc = (acc << 8) | part;
    if(i < 3){
      if(*s != '.')
        return -1;
      s++;
    }
  }

  if(*s != 0)
    return -1;

  out->s_addr = acc;
  return 0;
}

static int
nfs_split_source(const char *src, struct in_addr *server, char *export_path, int max)
{
  int i;
  int colon;
  char host[64];

  if(src == 0 || server == 0 || export_path == 0 || max <= 1)
    return -1;

  colon = -1;
  for(i = 0; src[i]; i++){
    if(src[i] == ':'){
      colon = i;
      break;
    }
  }
  if(colon <= 0)
    return -1;

  if(colon >= (int)sizeof(host))
    return -1;

  memmove(host, src, colon);
  host[colon] = 0;

  if(nfs_parse_ipv4(host, server) < 0)
    return -1;

  if(src[colon + 1] != '/')
    return -1;

  safestrcpy(export_path, (char *)&src[colon + 1], max);
  return 0;
}

static int
nfs_path_next(char **pp, char *elem)
{
  char *p;
  int n;

  if(pp == 0 || *pp == 0 || elem == 0)
    return 0;

  p = *pp;
  while(*p == '/')
    p++;

  if(*p == 0){
    *pp = p;
    return 0;
  }

  n = 0;
  while(*p && *p != '/'){
    if(n < DIRSIZ)
      elem[n++] = *p;
    p++;
  }
  elem[n] = 0;

  while(*p == '/')
    p++;
  *pp = p;
  return 1;
}

static int
nfs_attr_from_fh(struct nfs_mount_data *md, fhandle3 *fh, fattr3 *out)
{
  if(md == 0 || fh == 0 || out == 0)
    return -1;
  return nfs_getattr(&md->nm, fh, out);
}

static int
nfs_cache_store_fh(struct nfs_mount_data *md, uint inum, fhandle3 *fh)
{
  int i;
  int free_i;

  if(md == 0 || fh == 0)
    return -1;

  free_i = -1;
  acquire(&md->lock);
  for(i = 0; i < NFS_FH_CACHE_MAX; i++){
    if(md->cache[i].used && md->cache[i].inum == inum){
      md->cache[i].fh = *fh;
      release(&md->lock);
      return 0;
    }
    if(free_i < 0 && md->cache[i].used == 0)
      free_i = i;
  }

  if(free_i >= 0){
    md->cache[free_i].used = 1;
    md->cache[free_i].inum = inum;
    md->cache[free_i].fh = *fh;
    release(&md->lock);
    return 0;
  }

  /* Simple replacement policy for now. */
  md->cache[0].used = 1;
  md->cache[0].inum = inum;
  md->cache[0].fh = *fh;
  release(&md->lock);
  return 0;
}

static int
nfs_cache_get_fh(struct nfs_mount_data *md, uint inum, fhandle3 *fh)
{
  int i;

  if(md == 0 || fh == 0)
    return -1;

  acquire(&md->lock);
  for(i = 0; i < NFS_FH_CACHE_MAX; i++){
    if(md->cache[i].used && md->cache[i].inum == inum){
      *fh = md->cache[i].fh;
      release(&md->lock);
      return 0;
    }
  }
  release(&md->lock);

  return -1;
}

static uint
nfs_inum_from_attr(fattr3 *attr)
{
  if(attr == 0)
    return ROOTINO;
  if((uint)attr->fileid == 0)
    return ROOTINO;
  return (uint)attr->fileid;
}

static short
nfs_type_from_attr(fattr3 *attr)
{
  if(attr == 0)
    return T_FILE;

  switch(attr->type){
  case NF3DIR:
    return T_DIR;
  case NF3LNK:
    return T_SYMLINK;
  case NF3BLK:
  case NF3CHR:
    return T_DEV;
  default:
    return T_FILE;
  }
}

static ushort
nfs_mode_from_attr(fattr3 *attr)
{
  ushort mode;
  short t;

  if(attr == 0)
    return M_IFREG | 0444;

  t = nfs_type_from_attr(attr);
  if(t == T_DIR)
    mode = M_IFDIR;
  else if(t == T_SYMLINK)
    mode = M_IFLNK;
  else if(t == T_DEV)
    mode = M_IFCHR;
  else
    mode = M_IFREG;

  mode |= (ushort)(attr->mode & 07777);
  return mode;
}

static struct inode *
nfs_make_inode(struct nfs_mount_data *md, uint inum, fattr3 *attr, fhandle3 *fh)
{
  struct inode *ip;

  if(md == 0 || attr == 0)
    return 0;

  ip = iget((uint)md->dev, inum);
  if(ip == 0)
    return 0;

  acquiresleep(&ip->lock);
  ip->dev = (uint)md->dev;
  ip->inum = inum;
  ip->valid = 1;
  ip->type = nfs_type_from_attr(attr);
  ip->major = 0;
  ip->minor = 0;
  ip->nlink = (short)attr->nlink;
  ip->uid = (short)attr->uid;
  ip->gid = (short)attr->gid;
  ip->mode = (short)nfs_mode_from_attr(attr);
  ip->size = (uint)attr->size;
  memset(ip->addrs, 0, sizeof(ip->addrs));
  releasesleep(&ip->lock);

  if(fh)
    nfs_cache_store_fh(md, inum, fh);

  return ip;
}

static struct inode *
nfs_lookup_internal(struct nfs_mount_data *md, fhandle3 *dirfh, const char *name)
{
  fhandle3 childfh;
  fattr3 childattr;
  uint inum;

  if(md == 0 || dirfh == 0 || name == 0)
    return 0;

  if(nfs_lookup(&md->nm, dirfh, name, &childfh) < 0)
    return 0;
  if(nfs_attr_from_fh(md, &childfh, &childattr) < 0)
    return 0;

  inum = nfs_inum_from_attr(&childattr);
  return nfs_make_inode(md, inum, &childattr, &childfh);
}

static struct inode*
nfs_root_inode(struct vfs *fs)
{
  struct nfs_mount_data *md;
  fattr3 attr;

  if(fs == 0)
    return 0;

  md = (struct nfs_mount_data *)fs->fs_data;
  if(md == 0)
    return 0;

  if(nfs_attr_from_fh(md, &md->nm.rootfh, &attr) < 0)
    return 0;

  md->root_inum = nfs_inum_from_attr(&attr);
  return nfs_make_inode(md, md->root_inum, &attr, &md->nm.rootfh);
}

static struct inode*
nfs_namei(struct vfs *fs, char *path)
{
  struct nfs_mount_data *md;
  fhandle3 curfh;
  fattr3 curattr;
  char elem[DIRSIZ + 1];
  char *p;
  struct inode *ip;

  if(fs == 0 || path == 0)
    return 0;

  md = (struct nfs_mount_data *)fs->fs_data;
  if(md == 0)
    return 0;

  curfh = md->nm.rootfh;
  if(nfs_attr_from_fh(md, &curfh, &curattr) < 0)
    return 0;

  p = path;
  if(path[0] == 0 || (path[0] == '/' && path[1] == 0) ||
     (path[0] == '.' && path[1] == 0)){
    md->root_inum = nfs_inum_from_attr(&curattr);
    return nfs_make_inode(md, md->root_inum, &curattr, &curfh);
  }

  while(nfs_path_next(&p, elem)){
    if(elem[0] == 0 || (elem[0] == '.' && elem[1] == 0))
      continue;

    if(elem[0] == '.' && elem[1] == '.' && elem[2] == 0){
      /* For now, keep '..' at current directory to avoid crossing mounts. */
      continue;
    }

    ip = nfs_lookup_internal(md, &curfh, elem);
    if(ip == 0)
      return 0;

    if(nfs_cache_get_fh(md, ip->inum, &curfh) < 0){
      iput(ip);
      return 0;
    }

    if(nfs_attr_from_fh(md, &curfh, &curattr) < 0){
      iput(ip);
      return 0;
    }

    iput(ip);
  }

  return nfs_make_inode(md, nfs_inum_from_attr(&curattr), &curattr, &curfh);
}

static struct inode*
nfs_nameiparent(struct vfs *fs, char *path, char *name)
{
  char tmp[NFS_PATH_MAX];
  int len;
  int i;

  if(fs == 0 || path == 0 || name == 0)
    return 0;

  safestrcpy(tmp, path, sizeof(tmp));
  len = strlen(tmp);

  while(len > 1 && tmp[len - 1] == '/')
    tmp[--len] = 0;

  if(len <= 0)
    return 0;

  i = len - 1;
  while(i >= 0 && tmp[i] != '/')
    i--;

  if(i < 0){
    safestrcpy(name, tmp, DIRSIZ + 1);
    return nfs_namei(fs, ".");
  }

  safestrcpy(name, &tmp[i + 1], DIRSIZ + 1);
  if(i == 0)
    tmp[1] = 0;
  else
    tmp[i] = 0;

  return nfs_namei(fs, tmp);
}

static void
nfs_inode_put(struct inode *ip)
{
  iput(ip);
}

static int
nfs_read_vop(struct inode *ip, char *dst, uint off, uint n)
{
  struct nfs_mount_data *md;
  fhandle3 fh;
  uint nread;

  if(ip == 0 || dst == 0)
    return -1;

  md = (struct nfs_mount_data *)vfs_dev_fs_data(ip->dev);
  if(md == 0)
    return -1;

  if(n == 0)
    return 0;

  if(nfs_cache_get_fh(md, ip->inum, &fh) < 0)
    return -1;

  if(nfs_read(&md->nm, &fh, (ulong)off, n, dst, &nread) < 0)
    return -1;

  return (int)nread;
}

static int
nfs_stat_vop(struct inode *ip, struct stat *st)
{
  struct nfs_mount_data *md;
  fhandle3 fh;
  fattr3 attr;

  if(ip == 0 || st == 0)
    return -1;

  md = (struct nfs_mount_data *)vfs_dev_fs_data(ip->dev);
  if(md == 0)
    return -1;

  if(nfs_cache_get_fh(md, ip->inum, &fh) < 0)
    return -1;

  if(nfs_attr_from_fh(md, &fh, &attr) < 0)
    return -1;

  st->st_type = nfs_type_from_attr(&attr);
  st->st_dev = ip->dev;
  st->st_ino = nfs_inum_from_attr(&attr);
  st->st_major = 0;
  st->st_minor = 0;
  st->st_nlink = (short)attr.nlink;
  st->st_uid = (short)attr.uid;
  st->st_gid = (short)attr.gid;
  st->st_mode = nfs_mode_from_attr(&attr);
  st->st_size = (uint)attr.size;
  st->st_atime = (int)attr.atime_sec;
  st->st_mtime = (int)attr.mtime_sec;
  st->st_ctime = (int)attr.ctime_sec;

  return 0;
}

static int
nfs_access_vop(struct inode *ip, int mode)
{
  return iaccess(ip, mode);
}

static struct inode*
nfs_dirlookup_vop(struct inode *dp, char *name, uint *poff)
{
  struct nfs_mount_data *md;
  fhandle3 dirfh;

  if(dp == 0 || name == 0)
    return 0;

  md = (struct nfs_mount_data *)vfs_dev_fs_data(dp->dev);
  if(md == 0)
    return 0;

  if(name[0] == '.' && name[1] == 0)
    return idup(dp);

  if(name[0] == '.' && name[1] == '.' && name[2] == 0){
    if(dp->inum == md->root_inum)
      return idup(dp);
  }

  if(nfs_cache_get_fh(md, dp->inum, &dirfh) < 0)
    return 0;

  if(poff)
    *poff = 0;
  return nfs_lookup_internal(md, &dirfh, name);
}

static int
nfs_mount_init(struct mount *m)
{
  struct nfs_mount_data *md;
  char src[NFS_PATH_MAX];
  fhandle3 rootfh;
  fattr3 rootattr;
  uint auth_flavor;
  ushort nfs_port;

  if(m == 0)
    return -1;
  if(m->data == 0 || m->datalen <= 0)
    return -1;
  if(m->datalen >= NFS_PATH_MAX)
    return -1;

  memmove(src, m->data, m->datalen);
  src[m->datalen] = 0;

  md = (struct nfs_mount_data *)kalloc();
  if(md == 0)
    return -1;
  memset(md, 0, sizeof(*md));
  initlock(&md->lock, "nfs_mount");
  lockdep_set_rank(&md->lock, LOCK_RANK_DEFAULT, "nfs_mount");

  if(nfs_split_source(src, &md->server, md->export_path, sizeof(md->export_path)) < 0)
    goto fail;

  auth_flavor = 0;
  if(mount_nfs(md->server, md->export_path, &rootfh, &auth_flavor) < 0)
    goto fail;

  nfs_port = pmap_getport(md->server, NFS_PROGRAM, NFS_VERSION, IPPROTO_UDP);
  if(nfs_port == 0)
    nfs_port = 2049;

  memset(&md->nm, 0, sizeof(md->nm));
  md->nm.server = md->server;
  safestrcpy(md->nm.export, md->export_path, sizeof(md->nm.export));
  md->nm.rootfh = rootfh;
  md->nm.nfs_port = nfs_port;
  md->nm.timeout_ms = 5000;
  md->dev = m->dev;

  if(nfs_attr_from_fh(md, &md->nm.rootfh, &rootattr) < 0)
    goto fail_umount;

  md->root_inum = nfs_inum_from_attr(&rootattr);
  nfs_cache_store_fh(md, md->root_inum, &md->nm.rootfh);

  m->fs_data = md;
  return 0;

fail_umount:
  umount_nfs(md->server, md->export_path);
fail:
  kfree((char *)md);
  return -1;
}

static void
nfs_fs_destroy(struct vfs *fs)
{
  struct nfs_mount_data *md;

  if(fs == 0)
    return;

  md = (struct nfs_mount_data *)fs->fs_data;
  if(md == 0)
    return;

  umount_nfs(md->server, md->export_path);
  kfree((char *)md);
  fs->fs_data = 0;
}

static struct vfs_ops nfs_vfs_ops = {
  .root_inode = nfs_root_inode,
  .namei = nfs_namei,
  .nameiparent = nfs_nameiparent,
  .inode_put = nfs_inode_put,
};

static struct vnode_ops nfs_vnode_ops = {
  .read = nfs_read_vop,
  .write = 0,
  .truncate = 0,
  .drop = 0,
  .stat = nfs_stat_vop,
  .setattr = 0,
  .access = nfs_access_vop,
  .dirlookup = nfs_dirlookup_vop,
  .dirlink = 0,
  .link = 0,
  .remove = 0,
  .rename = 0,
  .faultctl = 0,
  .create = 0,
  .readlink = 0,
  .symlink = 0,
};

void
vfs_nfs_init(struct vfs *fs)
{
  if(fs == 0)
    return;

  safestrcpy(fs->name, "nfs", sizeof(fs->name));
  fs->caps = VFS_CAP_READ;
  fs->fs_data = 0;
  fs->fs_destroy = nfs_fs_destroy;
  fs->mount_init = nfs_mount_init;
  fs->ops = nfs_vfs_ops;
  fs->vnode_ops = nfs_vnode_ops;
}
