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
#include "../../include/socket.h"
#include "../../include/net.h"

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
#define PROCFS_SERVER7_INO  14
#define PROCFS_LOADAVG_INO  15
#define PROCFS_BDEV_TABLE_INO 16
#define PROCFS_NET_TCP_INO    17   /* /proc/net_tcp  — TCP socket table */
#define PROCFS_NET_UDP_INO    18   /* /proc/net_udp  — UDP socket table */
#define PROCFS_NET_DEV_INO    19   /* /proc/net_dev  — interface counters */
#define PROCFS_SCHEDSTAT_INO  20   /* /proc/schedstat — scheduler counters */
#define PROCFS_VMSTAT_INO     21   /* /proc/vmstat — allocator/vm counters */
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
  { PROCFS_GFXSTATS_INO, "gfxstats", 1024 },
  { PROCFS_LSOF_INO, "lsof", 2048 },
  { PROCFS_NVME_TUNE_INO, "nvme_tune", 2048 },
  { PROCFS_SERVER7_INO, "server7", 256 },
  { PROCFS_LOADAVG_INO, "loadavg", 64 },
  { PROCFS_BDEV_TABLE_INO, "bdev_table", 4096 },
  { PROCFS_NET_TCP_INO, "net_tcp", 4096 },
  { PROCFS_NET_UDP_INO, "net_udp", 4096 },
  { PROCFS_NET_DEV_INO, "net_dev", 1024 },
  { PROCFS_SCHEDSTAT_INO, "schedstat", 256 },
  { PROCFS_VMSTAT_INO, "vmstat", 512 },
  { 0, 0, 0 }
};

// Large scratch buffers for procfs_readi. Keeping large metadata arrays off
// the kernel stack avoids stack overflow in read-heavy paths.
static struct procinfo_k procfs_read_pinfo[NPROC];
static struct vfs_mount_info procfs_read_mounts[VFS_MOUNTS_MAX];
// Net node snapshot + formatted output buffers.  Protected by the inode lock
// held across procfs_readi calls, which serialises concurrent readers.
static struct socket_info_k procfs_net_sockets[NSOCKET];
static char procfs_net_outbuf[4096];

static int procfs_writei(struct inode *ip, char *src, uint off, uint n);
static uint procfs_write_uint(char *buf, uint value);

static uint
procfs_root_dir_size(void)
{
  int n;

  n = 0;
  while(procfs_inodes[n].name)
    n++;
  return (uint)(n + 2) * sizeof(struct dirent);
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

static int
procfs_buf_putkv_u(char *buf, uint max, uint *len, const char *key, uint v)
{
  if(procfs_buf_puts(buf, max, len, key) < 0)
    return -1;
  if(procfs_buf_putu(buf, max, len, v) < 0)
    return -1;
  if(procfs_buf_putc(buf, max, len, '\n') < 0)
    return -1;
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

static const char*
procfs_tcp_state_name(uint state)
{
  switch(state){
  case TCPS_CLOSED:       return "CLOSED";
  case TCPS_LISTEN:       return "LISTEN";
  case TCPS_SYN_SENT:     return "SYN_SENT";
  case TCPS_SYN_RECEIVED: return "SYN_RCVD";
  case TCPS_ESTABLISHED:  return "ESTABLISHED";
  case TCPS_FIN_WAIT_1:   return "FIN_WAIT_1";
  case TCPS_FIN_WAIT_2:   return "FIN_WAIT_2";
  case TCPS_CLOSE_WAIT:   return "CLOSE_WAIT";
  case TCPS_LAST_ACK:     return "LAST_ACK";
  case TCPS_TIME_WAIT:    return "TIME_WAIT";
  default:                return "UNKNOWN";
  }
}

/* Format a host-order IPv4 uint as dotted-quad into procfs output buf. */
static int
procfs_buf_put_ipv4(char *buf, uint max, uint *len, uint ip)
{
  if(procfs_buf_putu(buf, max, len, (ip >> 24) & 0xff) < 0) return -1;
  if(procfs_buf_putc(buf, max, len, '.') < 0) return -1;
  if(procfs_buf_putu(buf, max, len, (ip >> 16) & 0xff) < 0) return -1;
  if(procfs_buf_putc(buf, max, len, '.') < 0) return -1;
  if(procfs_buf_putu(buf, max, len, (ip >> 8) & 0xff) < 0) return -1;
  if(procfs_buf_putc(buf, max, len, '.') < 0) return -1;
  if(procfs_buf_putu(buf, max, len, ip & 0xff) < 0) return -1;
  return 0;
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
  } else if(inum == PROCFS_MEMINFO_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 256;
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
  } else if(inum == PROCFS_SERVER7_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IWUSR | M_IRGRP | M_IROTH;
    ip->size = 256;
  } else if(inum == PROCFS_LOADAVG_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 64;
  } else if(inum == PROCFS_BDEV_TABLE_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 4096;
  } else if(inum == PROCFS_SCHEDSTAT_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 256;
  } else if(inum == PROCFS_VMSTAT_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 512;
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
procfs_namei(struct vfs *fs, char *path)
{
  (void)fs;
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
procfs_nameiparent(struct vfs *fs, char *path, char *name)
{
  (void)fs;
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

  return procfs_namei(fs, parent);
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
  struct procinfo_k *pinfo;
  struct vfs_mount_info *mins;
  struct kalloc_stats_k kstats;
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

  pinfo = procfs_read_pinfo;
  mins = procfs_read_mounts;

  if(ip == 0 || dst == 0)
    return -1;
  if(ip->inum == PROCFS_ROOT_INO){
    // Note: . and .. are synthesized by VFS for mount roots
    struct dirent more_entries[20];
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
    more_entries[12].inum = PROCFS_SERVER7_INO;
    safestrcpy(more_entries[12].name, "server7", DIRSIZ);
    more_entries[13].inum = PROCFS_LOADAVG_INO;
    safestrcpy(more_entries[13].name, "loadavg", DIRSIZ);
    more_entries[14].inum = PROCFS_BDEV_TABLE_INO;
    safestrcpy(more_entries[14].name, "bdev_table", DIRSIZ);
    more_entries[15].inum = PROCFS_NET_TCP_INO;
    safestrcpy(more_entries[15].name, "net_tcp", DIRSIZ);
    more_entries[16].inum = PROCFS_NET_UDP_INO;
    safestrcpy(more_entries[16].name, "net_udp", DIRSIZ);
    more_entries[17].inum = PROCFS_NET_DEV_INO;
    safestrcpy(more_entries[17].name, "net_dev", DIRSIZ);
    more_entries[18].inum = PROCFS_SCHEDSTAT_INO;
    safestrcpy(more_entries[18].name, "schedstat", DIRSIZ);
    more_entries[19].inum = PROCFS_VMSTAT_INO;
    safestrcpy(more_entries[19].name, "vmstat", DIRSIZ);
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
    struct console_gfx_debug_info gfx;

    len = 0;
    if(console_gfx_debug_snapshot(&gfx) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "sync_calls ", gfx.sync_calls) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "cells_changed ", gfx.cells_changed) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "cells_rendered ", gfx.cells_rendered) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "flush_calls ", gfx.flush_calls) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "flush_pixels ", gfx.flush_pixels) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "boot_ready ", gfx.boot_ready) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "has_dev ", gfx.has_dev) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "has_fb ", gfx.has_fb) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "has_ctx ", gfx.has_ctx) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "has_vt ", gfx.has_vt) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "active_tty ", gfx.active_tty) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "mode_width ", gfx.mode_width) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "mode_height ", gfx.mode_height) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "fb_width ", gfx.fb_width) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "fb_height ", gfx.fb_height) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "fb_stride ", gfx.fb_stride) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "fb_bpp ", gfx.fb_bpp) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "cell_width ", gfx.cell_width) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "cell_height ", gfx.cell_height) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "tty_cols ", gfx.tty_cols) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "tty_rows ", gfx.tty_rows) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "tty_cursor ", gfx.tty_cursor) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "tty_cursor_row ", gfx.tty_cursor_row) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "tty_cursor_col ", gfx.tty_cursor_col) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "tty_nonblank_cells ", gfx.tty_nonblank_cells) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "hw_view_row0 ", gfx.hw_view_row0) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "hw_view_col0 ", gfx.hw_view_col0) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vt_cols ", gfx.vt_cols) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vt_rows ", gfx.vt_rows) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vt_origin_x ", gfx.vt_origin_x) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vt_origin_y ", gfx.vt_origin_y) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vt_cursor_x ", gfx.vt_cursor_x) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vt_cursor_y ", gfx.vt_cursor_y) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vt_nonblank_cells ", gfx.vt_nonblank_cells) < 0)
      return -1;
    if(procfs_buf_puts(buf, sizeof(buf), &len, "gfx_owner_pid ") < 0)
      return -1;
    if(gfx.gfx_owner_pid < 0) {
      if(procfs_buf_puts(buf, sizeof(buf), &len, "none\n") < 0)
        return -1;
    } else {
      if(procfs_buf_putu(buf, sizeof(buf), &len, (uint)gfx.gfx_owner_pid) < 0)
        return -1;
      if(procfs_buf_putc(buf, sizeof(buf), &len, '\n') < 0)
        return -1;
    }
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "input_events ", gfx.input_events) < 0)
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
    kalloc_stats(&kstats);

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
    if(procfs_buf_puts(buf, sizeof(buf), &len, "PagesTotal: ") < 0)
      return -1;
    if(procfs_buf_putu(buf, sizeof(buf), &len, kstats.total_pages) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, '\n') < 0)
      return -1;
    if(procfs_buf_puts(buf, sizeof(buf), &len, "PagesFree: ") < 0)
      return -1;
    if(procfs_buf_putu(buf, sizeof(buf), &len, kstats.free_pages) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, '\n') < 0)
      return -1;
    if(procfs_buf_puts(buf, sizeof(buf), &len, "PagesAlloc: ") < 0)
      return -1;
    if(procfs_buf_putu(buf, sizeof(buf), &len, kstats.allocated_pages) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, '\n') < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, len);
  }
  if(ip->inum == PROCFS_VMSTAT_INO){
    kalloc_stats(&kstats);

    len = 0;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "pages_total ", kstats.total_pages) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "pages_free ", kstats.free_pages) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "pages_allocated ", kstats.allocated_pages) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "pages_shared ", kstats.shared_pages) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "alloc_calls ", kstats.alloc_calls) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "free_calls ", kstats.free_calls) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "cache_alloc_hits ", kstats.cache_alloc_hits) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "cache_alloc_misses ", kstats.cache_alloc_misses) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "cache_free_inserts ", kstats.cache_free_inserts) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "global_refill_batches ", kstats.global_refill_batches) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "global_refill_pages ", kstats.global_refill_pages) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "global_drain_batches ", kstats.global_drain_batches) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "global_drain_pages ", kstats.global_drain_pages) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "ref_increments ", kstats.ref_increments) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "deferred_frees ", kstats.deferred_frees) < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, len);
  }
  if(ip->inum == PROCFS_PS_INO){
    pm = proc_snapshot(pinfo, NPROC);
    if(pm < 0)
      return -1;

    len = 0;
    if(procfs_buf_puts(buf, sizeof(buf), &len,
                       "PID PPID PGID SID TTY UID GID STAT SZ CTICKS NAME\n") < 0)
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
      if(procfs_buf_putu(buf, sizeof(buf), &len, pinfo[i].cticks) < 0)
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
      if(strncmp(mins[i].fstype, "ext2", sizeof(mins[i].fstype)) == 0){
        if(ext2_block_usage((uint)mins[i].dev, &total_blocks, &free_blocks, &block_size) < 0)
          free_blocks = 0;
      } else if(strncmp(mins[i].fstype, "tmpfs", sizeof(mins[i].fstype)) == 0){
        if(tmpfs_block_usage((uint)mins[i].dev, &total_blocks, &free_blocks, &block_size) < 0)
          free_blocks = 0;
      }

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
  if(ip->inum == PROCFS_SERVER7_INO){
    int owner;

    len = 0;
    owner = console_gfx_server_owner();

    if(procfs_buf_puts(buf, sizeof(buf), &len, "owner_pid ") < 0)
      return -1;
    if(owner < 0) {
      if(procfs_buf_puts(buf, sizeof(buf), &len, "none\n") < 0)
        return -1;
    } else {
      if(procfs_buf_putu(buf, sizeof(buf), &len, (uint)owner) < 0)
        return -1;
      if(procfs_buf_putc(buf, sizeof(buf), &len, '\n') < 0)
        return -1;
    }

    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "claimed ", owner > 0 ? 1U : 0U) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "input_events ", console_input_events()) < 0)
      return -1;
    if(procfs_buf_puts(buf, sizeof(buf), &len, "commands claim|release\n") < 0)
      return -1;

    return procfs_copy_data(dst, off, n, buf, len);
  }
  if(ip->inum == PROCFS_LOADAVG_INO){
    uint la1, la5, la15;
    uint whole, frac;
    int rn, rtot;

    proc_get_loadavg(&la1, &la5, &la15);
    proc_count_active(&rn, &rtot);

    /*
     * Format: "1.23 4.56 7.89 running/total lastpid\n"
     * Fixed-point divisor is 2048 (LAVG_FSHIFT=11).
     * Fraction expressed as two decimal digits.
     */
#define LAVG_DIV 2048
    len = 0;
    /* 1-min */
    whole = la1 / LAVG_DIV; frac = (la1 % LAVG_DIV) * 100 / LAVG_DIV;
    if(procfs_buf_putu(buf, sizeof(buf), &len, whole) < 0) return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, '.') < 0) return -1;
    if(frac < 10 && procfs_buf_putc(buf, sizeof(buf), &len, '0') < 0) return -1;
    if(procfs_buf_putu(buf, sizeof(buf), &len, frac) < 0) return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0) return -1;
    /* 5-min */
    whole = la5 / LAVG_DIV; frac = (la5 % LAVG_DIV) * 100 / LAVG_DIV;
    if(procfs_buf_putu(buf, sizeof(buf), &len, whole) < 0) return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, '.') < 0) return -1;
    if(frac < 10 && procfs_buf_putc(buf, sizeof(buf), &len, '0') < 0) return -1;
    if(procfs_buf_putu(buf, sizeof(buf), &len, frac) < 0) return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0) return -1;
    /* 15-min */
    whole = la15 / LAVG_DIV; frac = (la15 % LAVG_DIV) * 100 / LAVG_DIV;
    if(procfs_buf_putu(buf, sizeof(buf), &len, whole) < 0) return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, '.') < 0) return -1;
    if(frac < 10 && procfs_buf_putc(buf, sizeof(buf), &len, '0') < 0) return -1;
    if(procfs_buf_putu(buf, sizeof(buf), &len, frac) < 0) return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0) return -1;
    /* running/total */
    if(procfs_buf_putu(buf, sizeof(buf), &len, (uint)rn) < 0) return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, '/') < 0) return -1;
    if(procfs_buf_putu(buf, sizeof(buf), &len, (uint)rtot) < 0) return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, '\n') < 0) return -1;
#undef LAVG_DIV
    return procfs_copy_data(dst, off, n, buf, len);
  }
  if(ip->inum == PROCFS_NET_TCP_INO || ip->inum == PROCFS_NET_UDP_INO){
    /* Snapshot all sockets, then format matching ones into procfs_net_outbuf. */
    int ns;
    int si;
    uint olen;
    int want_stream;
    const char *hdr;

    want_stream = (ip->inum == PROCFS_NET_TCP_INO);
    hdr = want_stream
      ? "Local Address           Remote Address          State          RxBuf TxBuf\n"
      : "Local Address           Remote Address          RxBuf TxBuf\n";

    ns = socket_get_table(procfs_net_sockets, NSOCKET);
    if(ns < 0)
      ns = 0;

    olen = 0;
    if(procfs_buf_puts(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, hdr) < 0)
      return -1;

    for(si = 0; si < ns; si++){
      struct socket_info_k *sk = &procfs_net_sockets[si];
      int match;

      if(want_stream)
        match = (sk->type == SOCK_STREAM);
      else
        match = (sk->type == SOCK_DGRAM);

      if(!match)
        continue;

      /* local ip:port */
      if(procfs_buf_put_ipv4(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, sk->local_ip) < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      if(procfs_buf_putc(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ':') < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      if(procfs_buf_putu(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, sk->local_port) < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);

      /* pad to column 24 */
      {
        uint col = olen;
        while(col < 24){ /* approximate; good enough for alignment */
          if(procfs_buf_putc(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ' ') < 0)
            return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
          col++;
        }
      }

      /* remote ip:port */
      if(sk->remote_ip == 0 && sk->remote_port == 0){
        if(procfs_buf_puts(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, "*:*") < 0)
          return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      } else {
        if(procfs_buf_put_ipv4(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, sk->remote_ip) < 0)
          return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
        if(procfs_buf_putc(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ':') < 0)
          return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
        if(procfs_buf_putu(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, sk->remote_port) < 0)
          return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      }

      if(procfs_buf_putc(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ' ') < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);

      /* state (TCP only) */
      if(want_stream){
        const char *st = procfs_tcp_state_name(sk->tcp_state);
        /* pad remote+state block to column 24+24=48 then print state */
        if(procfs_buf_puts(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, st) < 0)
          return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
        if(procfs_buf_putc(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ' ') < 0)
          return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      }

      /* rxbuf txbuf */
      if(procfs_buf_putu(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, sk->recv_len) < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      if(procfs_buf_putc(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ' ') < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      if(procfs_buf_putu(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, sk->send_len) < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      if(procfs_buf_putc(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, '\n') < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
    }
    return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
  }
  if(ip->inum == PROCFS_NET_DEV_INO){
    struct ifnet *ifp_iter;
    uint olen;

    olen = 0;
    if(procfs_buf_puts(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen,
                       "Iface    RxPkts   RxBytes  RxErr  TxPkts   TxBytes  TxErr\n") < 0)
      return -1;

    for(ifp_iter = if_first(); ifp_iter != 0; ifp_iter = if_next(ifp_iter)){
      if(procfs_buf_puts(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ifp_iter->if_xname) < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      if(procfs_buf_putc(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ' ') < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      if(procfs_buf_putu(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ifp_iter->if_ipackets) < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      if(procfs_buf_putc(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ' ') < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      if(procfs_buf_putu(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ifp_iter->if_ibytes) < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      if(procfs_buf_putc(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ' ') < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      if(procfs_buf_putu(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ifp_iter->if_ierrors) < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      if(procfs_buf_putc(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ' ') < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      if(procfs_buf_putu(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ifp_iter->if_opackets) < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      if(procfs_buf_putc(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ' ') < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      if(procfs_buf_putu(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ifp_iter->if_obytes) < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      if(procfs_buf_putc(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ' ') < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      if(procfs_buf_putu(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ifp_iter->if_oerrors) < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      if(procfs_buf_putc(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, '\n') < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
    }
    return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
  }
  if(ip->inum == PROCFS_BDEV_TABLE_INO){
    int r;

    r = bdev_format_table(buf, sizeof(buf));
    if(r < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, (uint)r);
  }
  if(ip->inum == PROCFS_SCHEDSTAT_INO){
    uint passes;
    uint idle_halts;
    uint picks;

    proc_get_sched_stats(&passes, &idle_halts, &picks);

    len = 0;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "passes ", passes) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "idle_halts ", idle_halts) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "picks ", picks) < 0)
      return -1;
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
      ip->inum != PROCFS_LOGO_INO &&
      ip->inum != PROCFS_SERVER7_INO)
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

  if(ip->inum == PROCFS_SERVER7_INO){
    int pid;

    pid = myproc() ? myproc()->pid : -1;
    i = 0;
    while(kbuf[i] == ' ' || kbuf[i] == '\t' || kbuf[i] == '\n' || kbuf[i] == '\r')
      i++;

    if(strncmp(kbuf + i, "claim", 5) == 0) {
      i += 5;
      while(kbuf[i] == ' ' || kbuf[i] == '\t' || kbuf[i] == '\n' || kbuf[i] == '\r')
        i++;
      if(kbuf[i] != 0)
        return -1;
      if(console_gfx_server_claim(pid) < 0)
        return -1;
      return n;
    }

    if(strncmp(kbuf + i, "release", 7) == 0) {
      i += 7;
      while(kbuf[i] == ' ' || kbuf[i] == '\t' || kbuf[i] == '\n' || kbuf[i] == '\r')
        i++;
      if(kbuf[i] != 0)
        return -1;
      if(console_gfx_server_release(pid) < 0)
        return -1;
      return n;
    }

    return -1;
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
