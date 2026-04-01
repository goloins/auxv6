#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
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
#define PROCFS_VBLK_FLUSH_INO 5
#define PROCFS_AHCI_TUNE_INO 6
#define PROCFS_MEMINFO_INO  7
#define PROCFS_PS_INO       8
#define PROCFS_MOUNTSTATS_INO 9
#define PROCFS_LOGO_INO     10
#define PROCFS_GFXSTATS_INO 11
#define PROCFS_LSOF_INO     12
#define PROCFS_NVME_TUNE_INO 13
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
  { PROCFS_VBLK_FLUSH_INO, "vblk_flush", 16 },
  { PROCFS_AHCI_TUNE_INO, "ahci_tune", 2048 },
  { PROCFS_MEMINFO_INO, "meminfo", 128 },
  { PROCFS_PS_INO, "ps", 2048 },
  { PROCFS_MOUNTSTATS_INO, "mountstats", 1024 },
  { PROCFS_LOGO_INO, "logo", 16 },
  { PROCFS_GFXSTATS_INO, "gfxstats", 256 },
  { PROCFS_LSOF_INO, "lsof", 2048 },
  { PROCFS_NVME_TUNE_INO, "nvme_tune", 2048 },
  { 0, 0, 0 }
};

static int procfs_writei(struct inode *ip, char *src, uint off, uint n);
static uint procfs_write_uint(char *buf, uint value);

static uint
procfs_root_dir_size(void)
{
  return 14 * sizeof(struct dirent);
}

static const char*
procfs_fd_type_name(int type)
{
  switch(type){
  case FD_PIPE:
    return "pipe";
  case FD_INODE:
    return "inode";
  case FD_SOCKET:
    return "socket";
  default:
    return "none";
  }
}

static int
procfs_buf_putc(char *buf, uint max, uint *len, char c)
{
  if(*len >= max)
    return -1;
  buf[*len] = c;
  (*len)++;
  return 0;
}

static int
procfs_buf_puts(char *buf, uint max, uint *len, const char *s)
{
  uint i;

  for(i = 0; s[i]; i++){
    if(procfs_buf_putc(buf, max, len, s[i]) < 0)
      return -1;
  }
  return 0;
}

static int
procfs_buf_putu(char *buf, uint max, uint *len, uint v)
{
  char tmp[16];
  uint n;
  uint i;

  n = procfs_write_uint(tmp, v);
  for(i = 0; i < n; i++){
    if(procfs_buf_putc(buf, max, len, tmp[i]) < 0)
      return -1;
  }
  return 0;
}

static const char*
procfs_state_name(int state)
{
  switch(state){
  case UNUSED:   return "unused";
  case EMBRYO:   return "embryo";
  case SLEEPING: return "sleep";
  case RUNNABLE: return "runnable";
  case RUNNING:  return "running";
  case STOPPED:  return "stopped";
  case ZOMBIE:   return "zombie";
  default:       return "?";
  }
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
  } else if(inum == PROCFS_VBLK_FLUSH_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IWUSR | M_IRGRP | M_IROTH;
    ip->size = 16;
  } else if(inum == PROCFS_AHCI_TUNE_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IWUSR | M_IRGRP | M_IROTH;
    ip->size = 2048;
  } else if(inum == PROCFS_LOGO_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IWUSR | M_IRGRP | M_IROTH;
    ip->size = 16;
  } else if(inum == PROCFS_GFXSTATS_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 256;
  } else if(inum == PROCFS_LSOF_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 2048;
  } else if(inum == PROCFS_NVME_TUNE_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IWUSR | M_IRGRP | M_IROTH;
    ip->size = 2048;
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
  return procfs_writei(ip, src, off, n);
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
  struct procinfo_k pinfo[NPROC];
  struct vfs_mount_info mins[VFS_MOUNTS_MAX];
  uint total_pages;
  uint free_pages;
  uint total_blocks;
  uint free_blocks;
  uint block_size;
  int pm;
  int mm;
  int i;
  uint len;
  uint now;

  if(ip == 0 || dst == 0)
    return -1;
  if(ip->inum == PROCFS_ROOT_INO){
    // Note: . and .. are synthesized by VFS for mount roots
    struct dirent more_entries[12];
    memset(more_entries, 0, sizeof(more_entries));
    more_entries[0].inum = PROCFS_UPTIME_INO;
    safestrcpy(more_entries[0].name, "uptime", DIRSIZ);
    more_entries[1].inum = PROCFS_VERSION_INO;
    safestrcpy(more_entries[1].name, "version", DIRSIZ);
    more_entries[2].inum = PROCFS_PCI_INO;
    safestrcpy(more_entries[2].name, "pci", DIRSIZ);
    more_entries[3].inum = PROCFS_VBLK_FLUSH_INO;
    safestrcpy(more_entries[3].name, "vblk_flush", DIRSIZ);
    more_entries[4].inum = PROCFS_AHCI_TUNE_INO;
    safestrcpy(more_entries[4].name, "ahci_tune", DIRSIZ);
    more_entries[5].inum = PROCFS_MEMINFO_INO;
    safestrcpy(more_entries[5].name, "meminfo", DIRSIZ);
    more_entries[6].inum = PROCFS_PS_INO;
    safestrcpy(more_entries[6].name, "ps", DIRSIZ);
    more_entries[7].inum = PROCFS_MOUNTSTATS_INO;
    safestrcpy(more_entries[7].name, "mountstats", DIRSIZ);
    more_entries[8].inum = PROCFS_LOGO_INO;
    safestrcpy(more_entries[8].name, "logo", DIRSIZ);
    more_entries[9].inum = PROCFS_GFXSTATS_INO;
    safestrcpy(more_entries[9].name, "gfxstats", DIRSIZ);
    more_entries[10].inum = PROCFS_LSOF_INO;
    safestrcpy(more_entries[10].name, "lsof", DIRSIZ);
    more_entries[11].inum = PROCFS_NVME_TUNE_INO;
    safestrcpy(more_entries[11].name, "nvme_tune", DIRSIZ);
    return procfs_copy_data(dst, off, n, (char*)more_entries, sizeof(more_entries));
  }
  if(ip->inum == PROCFS_VERSION_INO)
    return procfs_copy_data(dst, off, n, PROCFS_VERSION_STR,
                            sizeof(PROCFS_VERSION_STR) - 1);
  if(ip->inum == PROCFS_PCI_INO){
    len = pci_format_devices(buf, sizeof(buf));
    return procfs_copy_data(dst, off, n, buf, len);
  }
  if(ip->inum == PROCFS_VBLK_FLUSH_INO){
    int r = virtio_blk_get_tune(buf, sizeof(buf));
    if(r < 0)
      return -1;
    len = (uint)r;
    return procfs_copy_data(dst, off, n, buf, len);
  }
  if(ip->inum == PROCFS_LOGO_INO){
    len = procfs_write_uint(buf, (uint)console_logo_get_enabled());
    buf[len++] = '\n';
    return procfs_copy_data(dst, off, n, buf, len);
  }
  if(ip->inum == PROCFS_GFXSTATS_INO){
    len = 0;
    if(procfs_buf_puts(buf, sizeof(buf), &len, "sync_calls ") < 0)
      return -1;
    if(procfs_buf_putu(buf, sizeof(buf), &len, console_gfx_stats_sync_calls()) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, '\n') < 0)
      return -1;

    if(procfs_buf_puts(buf, sizeof(buf), &len, "cells_changed ") < 0)
      return -1;
    if(procfs_buf_putu(buf, sizeof(buf), &len, console_gfx_stats_cells_changed()) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, '\n') < 0)
      return -1;

    if(procfs_buf_puts(buf, sizeof(buf), &len, "cells_rendered ") < 0)
      return -1;
    if(procfs_buf_putu(buf, sizeof(buf), &len, console_gfx_stats_cells_rendered()) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, '\n') < 0)
      return -1;

    if(procfs_buf_puts(buf, sizeof(buf), &len, "flush_calls ") < 0)
      return -1;
    if(procfs_buf_putu(buf, sizeof(buf), &len, console_gfx_stats_flush_calls()) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, '\n') < 0)
      return -1;

    if(procfs_buf_puts(buf, sizeof(buf), &len, "flush_pixels ") < 0)
      return -1;
    if(procfs_buf_putu(buf, sizeof(buf), &len, console_gfx_stats_flush_pixels()) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, '\n') < 0)
      return -1;

    return procfs_copy_data(dst, off, n, buf, len);
  }
  if(ip->inum == PROCFS_AHCI_TUNE_INO){
    int r = ahci_get_tune(buf, sizeof(buf));
    if(r < 0)
      return -1;
    len = (uint)r;
    return procfs_copy_data(dst, off, n, buf, len);
  }
  if(ip->inum == PROCFS_NVME_TUNE_INO){
    int r = nvme_get_tune(buf, sizeof(buf));
    if(r < 0)
      return -1;
    len = (uint)r;
    return procfs_copy_data(dst, off, n, buf, len);
  }
  if(ip->inum == PROCFS_MEMINFO_INO){
    total_pages = 0;
    free_pages = 0;
    kalloc_meminfo(&total_pages, &free_pages);

    len = 0;
    if(procfs_buf_puts(buf, sizeof(buf), &len, "MemTotal: ") < 0)
      return -1;
    if(procfs_buf_putu(buf, sizeof(buf), &len, total_pages * (PGSIZE / 1024)) < 0)
      return -1;
    if(procfs_buf_puts(buf, sizeof(buf), &len, " kB\n") < 0)
      return -1;
    if(procfs_buf_puts(buf, sizeof(buf), &len, "MemFree: ") < 0)
      return -1;
    if(procfs_buf_putu(buf, sizeof(buf), &len, free_pages * (PGSIZE / 1024)) < 0)
      return -1;
    if(procfs_buf_puts(buf, sizeof(buf), &len, " kB\n") < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, len);
  }
  if(ip->inum == PROCFS_PS_INO){
    pm = proc_snapshot(pinfo, NPROC);
    if(pm < 0)
      return -1;

    len = 0;
    if(procfs_buf_puts(buf, sizeof(buf), &len,
                       "PID PPID PGID SID TTY UID GID STAT SZ NAME\n") < 0)
      return -1;

    for(i = 0; i < pm; i++){
      if(procfs_buf_putu(buf, sizeof(buf), &len, (uint)pinfo[i].pid) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_putu(buf, sizeof(buf), &len, (uint)pinfo[i].ppid) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_putu(buf, sizeof(buf), &len, (uint)pinfo[i].pgid) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_putu(buf, sizeof(buf), &len, (uint)pinfo[i].sid) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_putu(buf, sizeof(buf), &len, (uint)pinfo[i].tty) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_putu(buf, sizeof(buf), &len, (uint)pinfo[i].uid) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_putu(buf, sizeof(buf), &len, (uint)pinfo[i].gid) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_puts(buf, sizeof(buf), &len, procfs_state_name(pinfo[i].state)) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_putu(buf, sizeof(buf), &len, pinfo[i].sz) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_puts(buf, sizeof(buf), &len, pinfo[i].name) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, '\n') < 0)
        break;
    }
    return procfs_copy_data(dst, off, n, buf, len);
  }
  if(ip->inum == PROCFS_MOUNTSTATS_INO){
    mm = vfs_get_mounts(mins, VFS_MOUNTS_MAX);
    if(mm < 0)
      return -1;

    len = 0;
    if(procfs_buf_puts(buf, sizeof(buf), &len, "dev path type total free bsize\n") < 0)
      return -1;

    for(i = 0; i < mm; i++){
      total_blocks = bdev_nblocks((uint)mins[i].dev);
      free_blocks = 0;
      block_size = BSIZE;
      if(ext2_block_usage((uint)mins[i].dev, &total_blocks, &free_blocks, &block_size) < 0)
        free_blocks = 0;

      if(procfs_buf_putu(buf, sizeof(buf), &len, (uint)mins[i].dev) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_puts(buf, sizeof(buf), &len, mins[i].path) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_puts(buf, sizeof(buf), &len, mins[i].fstype) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_putu(buf, sizeof(buf), &len, total_blocks) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_putu(buf, sizeof(buf), &len, free_blocks) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_putu(buf, sizeof(buf), &len, block_size) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, '\n') < 0)
        break;
    }
    return procfs_copy_data(dst, off, n, buf, len);
  }
  if(ip->inum == PROCFS_LSOF_INO){
    struct procfdinfo_k finfo[16];
    int fm;
    int skip;

    len = 0;
    if(procfs_buf_puts(buf, sizeof(buf), &len,
                       "PID FD TYPE RW DEV INO OFF NAME\n") < 0)
      return -1;

    skip = 0;
    for(;;){
      fm = proc_fd_snapshot(finfo, 16, skip);
      if(fm <= 0)
        break;

      for(i = 0; i < fm; i++){
        const char *t;

        t = procfs_fd_type_name(finfo[i].type);
        if(procfs_buf_putu(buf, sizeof(buf), &len, (uint)finfo[i].pid) < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putu(buf, sizeof(buf), &len, (uint)finfo[i].fd) < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_puts(buf, sizeof(buf), &len, t) < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putc(buf, sizeof(buf), &len,
                           finfo[i].readable ? 'r' : '-') < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putc(buf, sizeof(buf), &len,
                           finfo[i].writable ? 'w' : '-') < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putu(buf, sizeof(buf), &len, finfo[i].dev) < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putu(buf, sizeof(buf), &len, finfo[i].inum) < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putu(buf, sizeof(buf), &len, finfo[i].off) < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_puts(buf, sizeof(buf), &len, finfo[i].name) < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putc(buf, sizeof(buf), &len, '\n') < 0)
          return procfs_copy_data(dst, off, n, buf, len);
      }

      skip += fm;
    }
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

static int
procfs_writei(struct inode *ip, char *src, uint off, uint n)
{
  char kbuf[128];
  uint i;
  uint val;

  if(ip == 0 || src == 0)
    return -1;
  if(ip->inum != PROCFS_VBLK_FLUSH_INO &&
      ip->inum != PROCFS_AHCI_TUNE_INO &&
      ip->inum != PROCFS_NVME_TUNE_INO &&
      ip->inum != PROCFS_LOGO_INO)
    return -1;
  if(off != 0)
    return -1;
  if(n == 0)
    return 0;
  if(n >= sizeof(kbuf))
    return -1;

  memmove(kbuf, src, n);
  kbuf[n] = 0;

  if(ip->inum == PROCFS_VBLK_FLUSH_INO){
    if(virtio_blk_set_tune(kbuf, n) < 0)
      return -1;
    return n;
  }

  if(ip->inum == PROCFS_AHCI_TUNE_INO){
    if(ahci_set_tune(kbuf, n) < 0)
      return -1;
    return n;
  }

  if(ip->inum == PROCFS_NVME_TUNE_INO){
    if(nvme_set_tune(kbuf, n) < 0)
      return -1;
    return n;
  }

  val = 0;
  i = 0;
  while(kbuf[i] == ' ' || kbuf[i] == '\t')
    i++;
  if(kbuf[i] < '0' || kbuf[i] > '9')
    return -1;

  for(; kbuf[i]; i++){
    if(kbuf[i] >= '0' && kbuf[i] <= '9'){
      uint digit = (uint)(kbuf[i] - '0');
      if(val > 1000000)
        return -1;
      val = val * 10 + digit;
      if(val > 1000000)
        return -1;
      continue;
    }

    if(kbuf[i] == '\n' || kbuf[i] == '\r' || kbuf[i] == ' ' || kbuf[i] == '\t')
      break;

    return -1;
  }

  if(ip->inum == PROCFS_LOGO_INO){
    if(val > 1)
      return -1;
    if(console_logo_set_enabled((int)val) < 0)
      return -1;
    return n;
  }

  return -1;
}

void
vfs_procfs_init(struct vfs *fs)
{
  if(fs == 0)
    return;

  safestrcpy(fs->name, "procfs", sizeof(fs->name));
  fs->caps = VFS_CAP_READ | VFS_CAP_WRITE;
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
