#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "vfs.h"
#include "fs.h"
#include "fcntl.h"
#include "file.h"

// Simple procfs implementation for testing mount system.

#define PROCFS_ROOT_INO     1
#define PROCFS_UPTIME_INO   2
#define PROCFS_VERSION_INO  3
#define PROCFS_PCI_INO      4
#define PROCFS_VERSION_STR  "a/ux86 aux86 i686\n"

struct procfs_inode {
  uint inum;
  char *name;
  uint size;
};

static struct procfs_inode procfs_inodes[] = {
  { PROCFS_UPTIME_INO,  "uptime",  16 },
  { PROCFS_VERSION_INO, "version", 32 },
  { PROCFS_PCI_INO,     "pci",     2048 },
  { 0, 0, 0 }
};

static uint
procfs_root_dir_size(void)
{
  return 5 * sizeof(struct dirent);
}

static uint
procfs_write_uint(char *buf, uint value)
{
  char tmp[16];
  uint len;
  uint i;

  len = 0;
  do {
    tmp[len++] = '0' + (value % 10);
    value /= 10;
  } while(value > 0);

  for(i = 0; i < len; i++)
    buf[i] = tmp[len - i - 1];
  return len;
}

static int
procfs_copy_data(char *dst, uint off, uint n, char *src, uint len)
{
  if(off >= len)
    return 0;
  if(off + n > len)
    n = len - off;
  memmove(dst, src + off, n);
  return n;
}

static void
procfs_fill_inode(struct inode *ip, uint inum)
{
  acquiresleep(&ip->lock);
  ip->dev = PROCFSDEV;
  ip->inum = inum;
  ip->valid = 1;
  ip->major = 0;
  ip->minor = 0;
  ip->nlink = 1;
  ip->uid = 0;
  ip->gid = 0;
  memset(ip->addrs, 0, sizeof(ip->addrs));

  if(inum == PROCFS_ROOT_INO){
    ip->type = T_DIR;
    ip->mode = M_IRUSR | M_IWUSR | M_IXUSR | M_IRGRP | M_IXGRP | M_IROTH | M_IXOTH;
    ip->size = procfs_root_dir_size();
  } else if(inum == PROCFS_UPTIME_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 16;
  } else if(inum == PROCFS_PCI_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 2048;  /* Dynamic content */
  } else {
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = sizeof(PROCFS_VERSION_STR) - 1;
  }

  releasesleep(&ip->lock);
}

static struct inode*
procfs_make_inode(uint inum)
{
  struct inode *ip;

  ip = iget(PROCFSDEV, inum);
  if(ip == 0)
    return 0;

  procfs_fill_inode(ip, inum);
  return ip;
}

static struct inode*
procfs_namei(char *path)
{
  char filename[64];
  char *start;
  int i;
  int j;

  if(path == 0)
    return 0;

  if((path[0] == '/' && path[1] == 0) ||
     (path[0] == '.' && path[1] == 0) ||
     path[0] == 0)
    return procfs_make_inode(PROCFS_ROOT_INO);

  start = path;
  if(start[0] == '/')
    start++;
  while(start[0] == '.' && start[1] == '/')
    start += 2;

  if(start[0] == 0)
    return procfs_make_inode(PROCFS_ROOT_INO);
  if(start[0] == '.' && start[1] == 0)
    return procfs_make_inode(PROCFS_ROOT_INO);
  if(start[0] == '.' && start[1] == '.' && start[2] == 0)
    return procfs_make_inode(PROCFS_ROOT_INO);

  for(i = 0; i < sizeof(filename) - 1 && start[i]; i++){
    if(start[i] == '/')
      return 0;
    filename[i] = start[i];
  }
  filename[i] = 0;

  for(j = 0; procfs_inodes[j].name; j++) {
    int namelen;
    int filelen;
    char *n;

    namelen = 0;
    n = procfs_inodes[j].name;
    while(n[namelen])
      namelen++;

    filelen = 0;
    while(filename[filelen])
      filelen++;

    if(filelen == namelen && memcmp(filename, procfs_inodes[j].name, namelen) == 0)
      return procfs_make_inode(procfs_inodes[j].inum);
  }

  return 0;
}

static struct inode*
procfs_nameiparent(char *path, char *name)
{
  char parent[256];
  char *start;
  int pathlen;
  int i;

  if(path == 0)
    return 0;

  start = path;
  if(start[0] == '/')
    start++;
  while(start[0] == '.' && start[1] == '/')
    start += 2;

  pathlen = strlen(start);
  if(pathlen == 0)
    return 0;

  i = pathlen - 1;
  while(i > 0 && start[i] != '/')
    i--;

  if(i == 0 && start[0] != '/') {
    if(start[0] == '/' && start[1] == 0)
      return 0;
    safestrcpy(name, start, 64);
    return procfs_make_inode(PROCFS_ROOT_INO);
  }

  if(i == 0)
    return 0;

  memmove(parent, start, i);
  parent[i] = 0;

  safestrcpy(name, &start[i+1], 64);

  return procfs_namei(parent);
}

static void
procfs_inode_put(struct inode *ip)
{
  if(ip && ip->dev == PROCFSDEV)
    iput(ip);
}

static int
procfs_vread(struct inode *ip, char *dst, uint off, uint n)
{
  return procfs_readi(ip, dst, off, n);
}

static int
procfs_vwrite(struct inode *ip, char *src, uint off, uint n)
{
  (void)ip;
  (void)src;
  (void)off;
  (void)n;
  return -1;
}

static int
procfs_vstat(struct inode *ip, struct stat *st)
{
  if(ip == 0 || st == 0)
    return -1;
  stati(ip, st);
  return 0;
}

static int
procfs_vaccess(struct inode *ip, int mode)
{
  return iaccess(ip, mode);
}

int
procfs_readi(struct inode *ip, char *dst, uint off, uint n)
{
  char buf[2048];
  struct dirent entries[3];
  uint len;
  uint now;

  if(ip == 0 || dst == 0)
    return -1;
  if(ip->inum == PROCFS_ROOT_INO){
    // Note: . and .. are synthesized by VFS for mount roots
    memset(entries, 0, sizeof(entries));
    entries[0].inum = PROCFS_UPTIME_INO;
    safestrcpy(entries[0].name, "uptime", DIRSIZ);
    entries[1].inum = PROCFS_VERSION_INO;
    safestrcpy(entries[1].name, "version", DIRSIZ);
    entries[2].inum = PROCFS_PCI_INO;
    safestrcpy(entries[2].name, "pci", DIRSIZ);
    return procfs_copy_data(dst, off, n, (char*)entries, sizeof(entries));
  }
  if(ip->inum == PROCFS_VERSION_INO)
    return procfs_copy_data(dst, off, n, PROCFS_VERSION_STR,
                            sizeof(PROCFS_VERSION_STR) - 1);
  if(ip->inum == PROCFS_PCI_INO){
    len = pci_format_devices(buf, sizeof(buf));
    return procfs_copy_data(dst, off, n, buf, len);
  }
  if(ip->inum != PROCFS_UPTIME_INO)
    return -1;

  acquire(&tickslock);
  now = ticks;
  release(&tickslock);

  len = procfs_write_uint(buf, now);
  buf[len++] = '\n';
  return procfs_copy_data(dst, off, n, buf, len);
}

void
vfs_procfs_init(struct vfs *fs)
{
  if(fs == 0)
    return;

  safestrcpy(fs->name, "procfs", sizeof(fs->name));
  fs->caps = VFS_CAP_READ;
  fs->fs_data = 0;
  fs->fs_destroy = 0;
  fs->mount_init = 0;
  fs->ops.namei = procfs_namei;
  fs->ops.nameiparent = procfs_nameiparent;
  fs->ops.inode_put = procfs_inode_put;
  fs->vnode_ops.read = procfs_vread;
  fs->vnode_ops.write = procfs_vwrite;
  fs->vnode_ops.stat = procfs_vstat;
  fs->vnode_ops.access = procfs_vaccess;
  fs->vnode_ops.dirlookup = 0;
  fs->vnode_ops.dirlink = 0;
}
