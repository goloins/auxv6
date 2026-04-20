#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
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
#define PROCFS_FDLIMITS_INO   22   /* /proc/fdlimits — per-proc fd limits */
#define PROCFS_AUDIO_INO      23   /* /proc/audio — audio core summary */
#define PROCFS_AUDIO_STATS_INO 24  /* /proc/audio_stats — audio counters */
#define PROCFS_SERIAL_TTY_INO 25   /* /proc/serial_tty — ttyS line capabilities/state */
#define PROCFS_AUDIO_CLIENTS_INO 26 /* /proc/audio_clients — active audio stream table */
#define PROCFS_MODEMS_INO     27   /* /proc/modems — discovered modem devices */
#define PROCFS_BCACHE_HEALTH_INO 28  /* /proc/bcache_health — buffer-cache integrity check */
#define PROCFS_FIREWIRE_INO   29   /* /proc/firewire — discovered IEEE1394 controllers */
#define PROCFS_USB_INO        30   /* /proc/usb — discovered USB host controllers */
#define PROCFS_NFORCE_INO     31   /* /proc/nforce — nForce driver counters */
#define PROCFS_WIFI_INO       32   /* /proc/wifi — discovered 802.11 controllers */
#define PROCFS_WPAN_INO       33   /* /proc/wpan — discovered 802.15.4 coordinators */
#define PROCFS_R815X_INO      34   /* /proc/r815x — discovered RTL8152/RTL8153 USB NICs */
#define PROCFS_MLFQ_TUNE_INO  35   /* /proc/mlfq_tune — runtime MLFQ tuning knobs */
#define PROCFS_THUNDERBOLT_INO 36  /* /proc/thunderbolt — discovered Thunderbolt/USB4 host routers */
#define PROCFS_LIGHTNING_INO   37  /* /proc/lightning — Apple Lightning/iAP2 scaffold state */
#define PROCFS_USB_MSC_INO     38  /* /proc/usb_msc — USB mass-storage attach/runtime state */
#define PROCFS_TIMER_INO       39  /* /proc/timer — timer and HPET diagnostics */
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
  { PROCFS_SCHEDSTAT_INO, "schedstat", 640 },
  { PROCFS_VMSTAT_INO, "vmstat", 4096 },
  { PROCFS_FDLIMITS_INO, "fdlimits", 2048 },
  { PROCFS_AUDIO_INO, "audio", 512 },
  { PROCFS_AUDIO_STATS_INO, "audio_stats", 512 },
  { PROCFS_AUDIO_CLIENTS_INO, "audio_clients", 2048 },
  { PROCFS_SERIAL_TTY_INO, "serial_tty", 512 },
  { PROCFS_MODEMS_INO, "modems", 2048 },
  { PROCFS_BCACHE_HEALTH_INO, "bcache_health", 512 },
  { PROCFS_FIREWIRE_INO, "firewire", 2048 },
  { PROCFS_USB_INO, "usb", 4096 },
  { PROCFS_NFORCE_INO, "nforce", 2048 },
  { PROCFS_WIFI_INO, "wifi", 2048 },
  { PROCFS_WPAN_INO, "wpan", 1024 },
  { PROCFS_R815X_INO, "r815x", 2048 },
  { PROCFS_MLFQ_TUNE_INO, "mlfq_tune", 192 },
  { PROCFS_THUNDERBOLT_INO, "thunderbolt", 2048 },
  { PROCFS_LIGHTNING_INO, "lightning", 512 },
  { PROCFS_USB_MSC_INO, "usb_msc", 1024 },
  { PROCFS_TIMER_INO, "timer", 512 },
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
procfs_buf_putnsp(char *buf, uint max, uint *len, uint n)
{
  uint i;

  for(i = 0; i < n; i++){
    if(procfs_buf_putc(buf, max, len, ' ') < 0)
      return -1;
  }
  return 0;
}

static int
procfs_buf_putu_rjust(char *buf, uint max, uint *len, uint v, uint width)
{
  char tmp[16];
  uint n;
  uint i;

  n = procfs_write_uint(tmp, v);
  if(width > n){
    if(procfs_buf_putnsp(buf, max, len, width - n) < 0)
      return -1;
  }
  for(i = 0; i < n; i++){
    if(procfs_buf_putc(buf, max, len, tmp[i]) < 0)
      return -1;
  }
  return 0;
}

static int
procfs_buf_puts_ljust(char *buf, uint max, uint *len, const char *s, uint width)
{
  uint n;

  if(procfs_buf_puts(buf, max, len, s) < 0)
    return -1;

  n = 0;
  while(s[n])
    n++;
  if(width > n){
    if(procfs_buf_putnsp(buf, max, len, width - n) < 0)
      return -1;
  }
  return 0;
}

static int
procfs_buf_puts_rjust(char *buf, uint max, uint *len, const char *s, uint width)
{
  uint n;

  n = 0;
  while(s[n])
    n++;
  if(width > n){
    if(procfs_buf_putnsp(buf, max, len, width - n) < 0)
      return -1;
  }
  if(procfs_buf_puts(buf, max, len, s) < 0)
    return -1;
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

static const char*
procfs_if_link_state_name(uint state)
{
  switch(state){
  case LINK_STATE_UP:
    return "up";
  case LINK_STATE_DOWN:
    return "down";
  default:
    return "unknown";
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
  struct proc *p;
  pde_t *pgdir;

  if(off >= len)
    return 0;
  if(off + n > len)
    n = len - off;

  if((uint)dst < KERNBASE){
    p = myproc();
    pgdir = p ? proc_pgdir(p) : 0;
    if(pgdir == 0)
      return -1;
    if(copyout(pgdir, (uint)dst, src + off, n) < 0)
      return -1;
  } else {
    memmove(dst, src + off, n);
  }
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
  } else if(inum == PROCFS_FDLIMITS_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 2048;
  } else if(inum == PROCFS_AUDIO_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 512;
  } else if(inum == PROCFS_AUDIO_STATS_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 512;
  } else if(inum == PROCFS_AUDIO_CLIENTS_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 2048;
  } else if(inum == PROCFS_SERIAL_TTY_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 512;
  } else if(inum == PROCFS_MODEMS_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 2048;
  } else if(inum == PROCFS_BCACHE_HEALTH_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 512;
  } else if(inum == PROCFS_FIREWIRE_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 2048;
  } else if(inum == PROCFS_USB_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 2048;
  } else if(inum == PROCFS_TIMER_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 512;
  } else if(inum == PROCFS_NFORCE_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 2048;
  } else if(inum == PROCFS_THUNDERBOLT_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 2048;
  } else if(inum == PROCFS_LIGHTNING_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 512;
  } else if(inum == PROCFS_USB_MSC_INO){
    ip->type = T_FILE;
    ip->mode = M_IRUSR | M_IRGRP | M_IROTH;
    ip->size = 1024;
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
procfs_vread(struct inode *ip, char *dst, uint64_t off, uint n)
{
  return procfs_readi(ip, dst, off, n);
}

static int
procfs_vwrite(struct inode *ip, char *src, uint64_t off, uint n)
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
procfs_readi(struct inode *ip, char *dst, uint64_t off, uint n)
{
  char buf[4096];
  struct procinfo_k *pinfo;
  struct vfs_mount_info *mins;
  struct kalloc_stats_k kstats;
  uint total_pages;
  uint free_pages;
  uint pg_desc_pages;
  uint pg_desc_bytes;
  uint pg_desc_backing_pages;
  uint pg_desc_managed;
  uint pg_desc_free;
  uint pg_desc_reserved;
  uint pg_desc_pinned;
  int pg_desc_ready;
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
    struct dirent more_entries[32];
    int entc;

    memset(more_entries, 0, sizeof(more_entries));
    entc = 0;
    for(i = 0; procfs_inodes[i].name && entc < 32; i++) {
      more_entries[entc].inum = procfs_inodes[i].inum;
      safestrcpy(more_entries[entc].name, procfs_inodes[i].name, DIRSIZ);
      entc++;
    }
    return procfs_copy_data(dst, off, n, (char*)more_entries,
                            (uint)(entc * sizeof(struct dirent)));
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
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "flush_blocked_tickslock ", gfx.flush_blocked_tickslock) < 0)
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
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "csi_unknown_total ", gfx.csi_unknown_total) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "csi_unsupported_intermediate_total ", gfx.csi_unsupported_intermediate_total) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "csi_unknown_bare_total ", gfx.csi_unknown_bare_total) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "csi_unknown_question_total ", gfx.csi_unknown_question_total) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "csi_unknown_greater_total ", gfx.csi_unknown_greater_total) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "csi_unknown_dollar_total ", gfx.csi_unknown_dollar_total) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "csi_unknown_bang_total ", gfx.csi_unknown_bang_total) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "csi_unknown_squote_total ", gfx.csi_unknown_squote_total) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "csi_unknown_quote_total ", gfx.csi_unknown_quote_total) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "csi_unknown_space_total ", gfx.csi_unknown_space_total) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "csi_cancelled_total ", gfx.csi_cancelled_total) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "csi_param_overflow_total ", gfx.csi_param_overflow_total) < 0)
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
    pagedb_stats(&pg_desc_pages, &pg_desc_bytes, &pg_desc_backing_pages,
           &pg_desc_ready);
    pagedb_flag_counts(&pg_desc_managed, &pg_desc_free,
               &pg_desc_reserved, &pg_desc_pinned);

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
    if(procfs_buf_puts(buf, sizeof(buf), &len, "PgDescReady: ") < 0)
      return -1;
    if(procfs_buf_putu(buf, sizeof(buf), &len, (uint)pg_desc_ready) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, '\n') < 0)
      return -1;
    if(procfs_buf_puts(buf, sizeof(buf), &len, "PgDescPages: ") < 0)
      return -1;
    if(procfs_buf_putu(buf, sizeof(buf), &len, pg_desc_pages) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, '\n') < 0)
      return -1;
    if(procfs_buf_puts(buf, sizeof(buf), &len, "PgDescBytes: ") < 0)
      return -1;
    if(procfs_buf_putu(buf, sizeof(buf), &len, pg_desc_bytes) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, '\n') < 0)
      return -1;
    if(procfs_buf_puts(buf, sizeof(buf), &len, "PgDescManaged: ") < 0)
      return -1;
    if(procfs_buf_putu(buf, sizeof(buf), &len, pg_desc_managed) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, '\n') < 0)
      return -1;
    if(procfs_buf_puts(buf, sizeof(buf), &len, "PgDescFree: ") < 0)
      return -1;
    if(procfs_buf_putu(buf, sizeof(buf), &len, pg_desc_free) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, '\n') < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, len);
  }
  if(ip->inum == PROCFS_VMSTAT_INO){
    uint vm_sync_calls;
    uint vm_sync_full_calls;
    uint vm_sync_entries;
    uint vm_pde_repairs;
    uint vm_master_repairs;
    uint vm_bad_pte_drops;
    uint cow_mappings;
    uint pipe_read_sleeps;
    uint pipe_write_sleeps;
    uint pipe_wake_readers;
    uint pipe_wake_writers;
    uint buddy_alloc_order0;
    uint buddy_free_order0;
    uint buddy_bad_order;
    uint buddy_alloc_order[8];
    uint buddy_free_order[8];
    uint buddy_est_free_order[8];
    uint buddy_alloc_fail_order[8];
    uint buddy_invariant_ok;
    uint buddy_bad_free_not_managed;
    uint buddy_bad_free_refcount_nonzero;
    uint buddy_bad_reserved_free;
    uint buddy_free_invalid_order;
    uint buddy_free_invalid_desc;
    uint buddy_free_double_free;
    uint cache_hits;
    uint cache_misses;
    uint cache_total;
    uint cache_hit_permille;
    uint as_guard_checks;
    uint as_guard_allows;
    uint as_guard_denies;
    uint as_guard_bypass_no_as;
    uint as_guard_bypass_vm_size;
    uint vm_fault_dispatches;
    uint vm_fault_cow_resolved;
    uint vm_fault_stack_growth;
    uint vm_fault_demand_zero;
    uint vm_fault_sigsegv;

    kalloc_stats(&kstats);
    pagedb_stats(&pg_desc_pages, &pg_desc_bytes, &pg_desc_backing_pages,
           &pg_desc_ready);
    pagedb_flag_counts(&pg_desc_managed, &pg_desc_free,
               &pg_desc_reserved, &pg_desc_pinned);
    vm_get_sync_stats(&vm_sync_calls, &vm_sync_full_calls, &vm_sync_entries,
                      &vm_pde_repairs, &vm_master_repairs, &vm_bad_pte_drops);
    vm_get_cow_stats(&cow_mappings);
    vm_get_fault_stats(&vm_fault_dispatches, &vm_fault_cow_resolved,
          &vm_fault_stack_growth,
          &vm_fault_demand_zero,
           &vm_fault_sigsegv);
    vm_get_addrspace_guard_stats(&as_guard_checks, &as_guard_allows,
                   &as_guard_denies, &as_guard_bypass_no_as,
                   &as_guard_bypass_vm_size);
    pipe_get_stats(&pipe_read_sleeps, &pipe_write_sleeps,
                   &pipe_wake_readers, &pipe_wake_writers);
    buddy_stats_all(buddy_alloc_order, buddy_free_order,
            buddy_est_free_order, &buddy_bad_order);
    buddy_invariant_stats(&buddy_invariant_ok,
                &buddy_bad_free_not_managed,
                &buddy_bad_free_refcount_nonzero,
                &buddy_bad_reserved_free);
    buddy_error_stats(buddy_alloc_fail_order,
              &buddy_free_invalid_order,
              &buddy_free_invalid_desc,
              &buddy_free_double_free);
    cache_hits = kstats.cache_alloc_hits;
    cache_misses = kstats.cache_alloc_misses;
    cache_total = cache_hits + cache_misses;
    cache_hit_permille = (cache_total > 0) ? (cache_hits * 1000U) / cache_total : 0;
    buddy_alloc_order0 = buddy_alloc_order[0];
    buddy_free_order0 = buddy_free_order[0];

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
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "cache_hit_permille ", cache_hit_permille) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "kalloc_pcpu_low_water ", KALLOC_PCPU_LOW_WATER) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "kalloc_pcpu_high_water ", KALLOC_PCPU_HIGH_WATER) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "kalloc_pcpu_refill_trigger ", KALLOC_PCPU_REFILL_TRIGGER) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "kalloc_refill_batch ", KALLOC_REFILL_BATCH) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "kalloc_drain_batch ", KALLOC_DRAIN_BATCH) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "ref_increments ", kstats.ref_increments) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "deferred_frees ", kstats.deferred_frees) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "pagedb_ready ", (uint)pg_desc_ready) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "pagedb_desc_pages ", pg_desc_pages) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "pagedb_desc_bytes ", pg_desc_bytes) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "pagedb_backing_pages ", pg_desc_backing_pages) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "pagedb_managed ", pg_desc_managed) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "pagedb_free ", pg_desc_free) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "pagedb_reserved ", pg_desc_reserved) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "pagedb_pinned ", pg_desc_pinned) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vm_sync_calls ", vm_sync_calls) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vm_sync_full_calls ", vm_sync_full_calls) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vm_sync_entries ", vm_sync_entries) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vm_pde_repairs ", vm_pde_repairs) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vm_master_repairs ", vm_master_repairs) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vm_bad_pte_drops ", vm_bad_pte_drops) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "cow_mappings ", cow_mappings) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vm_fault_dispatches ", vm_fault_dispatches) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vm_fault_cow_resolved ", vm_fault_cow_resolved) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vm_fault_stack_growth ", vm_fault_stack_growth) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vm_fault_demand_zero ", vm_fault_demand_zero) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vm_fault_sigsegv ", vm_fault_sigsegv) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vm_as_guard_checks ", as_guard_checks) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vm_as_guard_allows ", as_guard_allows) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vm_as_guard_denies ", as_guard_denies) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vm_as_guard_bypass_no_as ", as_guard_bypass_no_as) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "vm_as_guard_bypass_vm_size ", as_guard_bypass_vm_size) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "pipe_read_sleeps ", pipe_read_sleeps) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "pipe_write_sleeps ", pipe_write_sleeps) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "pipe_wake_readers ", pipe_wake_readers) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "pipe_wake_writers ", pipe_wake_writers) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_alloc_order0 ", buddy_alloc_order0) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_alloc_order1 ", buddy_alloc_order[1]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_alloc_order2 ", buddy_alloc_order[2]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_alloc_order3 ", buddy_alloc_order[3]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_alloc_order4 ", buddy_alloc_order[4]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_alloc_order5 ", buddy_alloc_order[5]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_alloc_order6 ", buddy_alloc_order[6]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_alloc_order7 ", buddy_alloc_order[7]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_free_order0 ", buddy_free_order0) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_free_order1 ", buddy_free_order[1]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_free_order2 ", buddy_free_order[2]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_free_order3 ", buddy_free_order[3]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_free_order4 ", buddy_free_order[4]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_free_order5 ", buddy_free_order[5]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_free_order6 ", buddy_free_order[6]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_free_order7 ", buddy_free_order[7]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_bad_order_requests ", buddy_bad_order) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_invariant_ok ", buddy_invariant_ok) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_bad_free_not_managed ", buddy_bad_free_not_managed) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_bad_free_refcount_nonzero ", buddy_bad_free_refcount_nonzero) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_bad_reserved_free ", buddy_bad_reserved_free) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_alloc_fail_order0 ", buddy_alloc_fail_order[0]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_alloc_fail_order1 ", buddy_alloc_fail_order[1]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_alloc_fail_order2 ", buddy_alloc_fail_order[2]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_alloc_fail_order3 ", buddy_alloc_fail_order[3]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_alloc_fail_order4 ", buddy_alloc_fail_order[4]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_alloc_fail_order5 ", buddy_alloc_fail_order[5]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_alloc_fail_order6 ", buddy_alloc_fail_order[6]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_alloc_fail_order7 ", buddy_alloc_fail_order[7]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_free_invalid_order ", buddy_free_invalid_order) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_free_invalid_desc ", buddy_free_invalid_desc) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_free_double_free ", buddy_free_double_free) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_est_free_order0 ", buddy_est_free_order[0]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_est_free_order1 ", buddy_est_free_order[1]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_est_free_order2 ", buddy_est_free_order[2]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_est_free_order3 ", buddy_est_free_order[3]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_est_free_order4 ", buddy_est_free_order[4]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_est_free_order5 ", buddy_est_free_order[5]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_est_free_order6 ", buddy_est_free_order[6]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "buddy_est_free_order7 ", buddy_est_free_order[7]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "alloc_order_0 ", buddy_alloc_order[0]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "alloc_order_1 ", buddy_alloc_order[1]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "alloc_order_2 ", buddy_alloc_order[2]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "alloc_order_3 ", buddy_alloc_order[3]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "alloc_order_4 ", buddy_alloc_order[4]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "alloc_order_5 ", buddy_alloc_order[5]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "alloc_order_6 ", buddy_alloc_order[6]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "alloc_order_7 ", buddy_alloc_order[7]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "free_order_0 ", buddy_free_order[0]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "free_order_1 ", buddy_free_order[1]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "free_order_2 ", buddy_free_order[2]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "free_order_3 ", buddy_free_order[3]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "free_order_4 ", buddy_free_order[4]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "free_order_5 ", buddy_free_order[5]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "free_order_6 ", buddy_free_order[6]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "free_order_7 ", buddy_free_order[7]) < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, len);
  }
  if(ip->inum == PROCFS_PS_INO){
    enum {
      PS_PID_W = 4,
      PS_PPID_W = 4,
      PS_PGID_W = 4,
      PS_SID_W = 4,
      PS_TTY_W = 10,
      PS_UID_W = 5,
      PS_GID_W = 5,
      PS_STAT_W = 8,
      PS_SZ_W = 10,
      PS_CTICKS_W = 8
    };

    pm = proc_snapshot(pinfo, NPROC);
    if(pm < 0)
      return -1;

    len = 0;
    if(procfs_buf_puts_rjust(buf, sizeof(buf), &len, "PID", PS_PID_W) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
      return -1;
    if(procfs_buf_puts_rjust(buf, sizeof(buf), &len, "PPID", PS_PPID_W) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
      return -1;
    if(procfs_buf_puts_rjust(buf, sizeof(buf), &len, "PGID", PS_PGID_W) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
      return -1;
    if(procfs_buf_puts_rjust(buf, sizeof(buf), &len, "SID", PS_SID_W) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
      return -1;
    if(procfs_buf_puts_rjust(buf, sizeof(buf), &len, "TTY", PS_TTY_W) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
      return -1;
    if(procfs_buf_puts_rjust(buf, sizeof(buf), &len, "UID", PS_UID_W) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
      return -1;
    if(procfs_buf_puts_rjust(buf, sizeof(buf), &len, "GID", PS_GID_W) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
      return -1;
    if(procfs_buf_puts_ljust(buf, sizeof(buf), &len, "STAT", PS_STAT_W) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
      return -1;
    if(procfs_buf_puts_rjust(buf, sizeof(buf), &len, "SZ", PS_SZ_W) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
      return -1;
    if(procfs_buf_puts_rjust(buf, sizeof(buf), &len, "CTICKS", PS_CTICKS_W) < 0)
      return -1;
    if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
      return -1;
    if(procfs_buf_puts(buf, sizeof(buf), &len, "NAME\n") < 0)
      return -1;

    for(i = 0; i < pm; i++){
      if(procfs_buf_putu_rjust(buf, sizeof(buf), &len, (uint)pinfo[i].pid, PS_PID_W) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_putu_rjust(buf, sizeof(buf), &len, (uint)pinfo[i].ppid, PS_PPID_W) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_putu_rjust(buf, sizeof(buf), &len, (uint)pinfo[i].pgid, PS_PGID_W) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_putu_rjust(buf, sizeof(buf), &len, (uint)pinfo[i].sid, PS_SID_W) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(pinfo[i].tty < 0){
        if(procfs_buf_puts_rjust(buf, sizeof(buf), &len, "-", PS_TTY_W) < 0)
          break;
      } else {
        if(procfs_buf_putu_rjust(buf, sizeof(buf), &len, (uint)pinfo[i].tty, PS_TTY_W) < 0)
          break;
      }
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_putu_rjust(buf, sizeof(buf), &len, (uint)pinfo[i].uid, PS_UID_W) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_putu_rjust(buf, sizeof(buf), &len, (uint)pinfo[i].gid, PS_GID_W) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_puts_ljust(buf, sizeof(buf), &len, procfs_state_name(pinfo[i].state), PS_STAT_W) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_putu_rjust(buf, sizeof(buf), &len, pinfo[i].sz, PS_SZ_W) < 0)
        break;
      if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
        break;
      if(procfs_buf_putu_rjust(buf, sizeof(buf), &len, pinfo[i].cticks, PS_CTICKS_W) < 0)
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
  if(ip->inum == PROCFS_FDLIMITS_INO){
    struct procfdlimitinfo_k flinfo[16];
    int fm;
    int skip;

    len = 0;
    if(procfs_buf_puts(buf, sizeof(buf), &len,
                       "PID SOFT HARD USED HIGHWATER NAME\n") < 0)
      return -1;

    skip = 0;
    for(;;){
      fm = proc_fd_limits_snapshot(flinfo, 16, skip);
      if(fm <= 0)
        break;

      for(i = 0; i < fm; i++){
        if(procfs_buf_putu(buf, sizeof(buf), &len, (uint)flinfo[i].pid) < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putu(buf, sizeof(buf), &len, flinfo[i].soft) < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putu(buf, sizeof(buf), &len, flinfo[i].hard) < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putu(buf, sizeof(buf), &len, flinfo[i].used) < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putu(buf, sizeof(buf), &len, flinfo[i].highwater) < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_putc(buf, sizeof(buf), &len, ' ') < 0)
          return procfs_copy_data(dst, off, n, buf, len);
        if(procfs_buf_puts(buf, sizeof(buf), &len, flinfo[i].name) < 0)
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
  if(ip->inum == PROCFS_AUDIO_INO){
    int r = audio_procfs_summary(buf, sizeof(buf));
    if(r < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, (uint)r);
  }
  if(ip->inum == PROCFS_AUDIO_STATS_INO){
    int r = audio_procfs_stats(buf, sizeof(buf));
    if(r < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, (uint)r);
  }
  if(ip->inum == PROCFS_AUDIO_CLIENTS_INO){
    int r = audio_procfs_clients(buf, sizeof(buf));
    if(r < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, (uint)r);
  }
  if(ip->inum == PROCFS_SERIAL_TTY_INO){
    int r = serial_procfs_dump(buf, sizeof(buf));
    if(r < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, (uint)r);
  }
  if(ip->inum == PROCFS_MODEMS_INO){
    int r = modem_procfs_dump(buf, sizeof(buf));
    if(r < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, (uint)r);
  }
  if(ip->inum == PROCFS_BCACHE_HEALTH_INO){
    int r = bcache_health_check(buf, sizeof(buf));
    if(r < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, (uint)r);
  }
  if(ip->inum == PROCFS_FIREWIRE_INO){
    int r = firewire_procfs_dump(buf, sizeof(buf));
    if(r < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, (uint)r);
  }
  if(ip->inum == PROCFS_USB_INO){
    int r = usb_procfs_dump(buf, sizeof(buf));
    if(r < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, (uint)r);
  }
  if(ip->inum == PROCFS_WIFI_INO){
    int r = wifi_procfs_dump(buf, sizeof(buf));
    if(r < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, (uint)r);
  }
  if(ip->inum == PROCFS_WPAN_INO){
    int r = ieee802154_procfs_dump(buf, sizeof(buf));
    if(r < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, (uint)r);
  }
  if(ip->inum == PROCFS_R815X_INO){
    int r = rtl815x_procfs_dump(buf, sizeof(buf));
    if(r < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, (uint)r);
  }
  if(ip->inum == PROCFS_NFORCE_INO){
    int r = nforce_procfs_dump(buf, sizeof(buf));
    if(r < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, (uint)r);
  }
  if(ip->inum == PROCFS_THUNDERBOLT_INO){
    int r = thunderbolt_procfs_dump(buf, sizeof(buf));
    if(r < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, (uint)r);
  }
  if(ip->inum == PROCFS_LIGHTNING_INO){
    int r = lightning_procfs_dump(buf, sizeof(buf));
    if(r < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, (uint)r);
  }
  if(ip->inum == PROCFS_USB_MSC_INO){
    int r = usb_msc_procfs_dump(buf, sizeof(buf));
    if(r < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, (uint)r);
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
    const char *lstate;
    uint olen;

    olen = 0;
    if(procfs_buf_puts(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen,
                       "Iface    Link     RxPkts   RxBytes  RxErr  TxPkts   TxBytes  TxErr\n") < 0)
      return -1;

    for(ifp_iter = if_first(); ifp_iter != 0; ifp_iter = if_next(ifp_iter)){
      lstate = procfs_if_link_state_name(ifp_iter->if_link_state);
      if(procfs_buf_puts(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ifp_iter->if_xname) < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      if(procfs_buf_putc(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, ' ') < 0)
        return procfs_copy_data(dst, off, n, procfs_net_outbuf, olen);
      if(procfs_buf_puts(procfs_net_outbuf, sizeof(procfs_net_outbuf), &olen, lstate) < 0)
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
    uint wake_calls;
    uint wake_scanned;
    uint wake_matched;
    uint wait_loops;
    uint wait_scanned;
    uint wake_ticks_calls;
    uint wake_proc_calls;
    uint wake_other_calls;
    uint mlfq_promotions;
    uint mlfq_demotions;
    uint mlfq_boosts;
    uint mlfq_budget_expired;
    uint mlfq_boost_interval;
    uint mlfq_q_lens[5];
    uint mlfq_q_dispatch[5];

    proc_get_sched_stats(&passes, &idle_halts, &picks);
    proc_get_sched_latency_stats(&wake_calls, &wake_scanned, &wake_matched,
                                 &wait_loops, &wait_scanned);
    proc_get_wakeup_class_stats(&wake_ticks_calls, &wake_proc_calls,
                  &wake_other_calls);
    proc_get_mlfq_stats(&mlfq_promotions, &mlfq_demotions, &mlfq_boosts,
                        &mlfq_budget_expired, mlfq_q_lens, mlfq_q_dispatch);
    mlfq_boost_interval = mlfq_get_boost_interval();

    len = 0;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "passes ", passes) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "idle_halts ", idle_halts) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "picks ", picks) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "wake_calls ", wake_calls) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "wake_scanned ", wake_scanned) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "wake_matched ", wake_matched) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "waitpid_loops ", wait_loops) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "waitpid_scanned ", wait_scanned) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "wake_ticks_calls ", wake_ticks_calls) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "wake_proc_calls ", wake_proc_calls) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "wake_other_calls ", wake_other_calls) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "mlfq_q0_len ", mlfq_q_lens[0]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "mlfq_q1_len ", mlfq_q_lens[1]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "mlfq_q2_len ", mlfq_q_lens[2]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "mlfq_q3_len ", mlfq_q_lens[3]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "mlfq_q4_len ", mlfq_q_lens[4]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "mlfq_dispatch_q0 ", mlfq_q_dispatch[0]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "mlfq_dispatch_q1 ", mlfq_q_dispatch[1]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "mlfq_dispatch_q2 ", mlfq_q_dispatch[2]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "mlfq_dispatch_q3 ", mlfq_q_dispatch[3]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "mlfq_dispatch_q4 ", mlfq_q_dispatch[4]) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "mlfq_promotions ", mlfq_promotions) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "mlfq_demotions ", mlfq_demotions) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "mlfq_boosts ", mlfq_boosts) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "mlfq_boost_interval_ticks ",
                          mlfq_boost_interval) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "mlfq_budget_expired ", mlfq_budget_expired) < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, len);
  }
  if(ip->inum == PROCFS_MLFQ_TUNE_INO){
    uint interval_ticks;

    interval_ticks = mlfq_get_boost_interval();
    len = 0;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "boost_interval_ticks ",
                          interval_ticks) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "boost_interval_ms ",
                          interval_ticks * 10) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "boost_interval_min_ticks ",
                          MLFQ_BOOST_INTERVAL_MIN) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "boost_interval_max_ticks ",
                          MLFQ_BOOST_INTERVAL_MAX) < 0)
      return -1;
    if(procfs_buf_puts(buf, sizeof(buf), &len,
                       "write_usage echo <ticks> > /proc/mlfq_tune\n") < 0)
      return -1;
    return procfs_copy_data(dst, off, n, buf, len);
  }
  if(ip->inum == PROCFS_TIMER_INO){
    unsigned long long ctr;
    uint ctr_hi;
    uint ctr_lo;
    int irq_line;
    int timer_idx;

    acquire(&tickslock);
    now = ticks;
    release(&tickslock);

    ctr = hpet_read_counter();
    ctr_hi = (uint)(ctr >> 32);
    ctr_lo = (uint)ctr;
    irq_line = hpet_irq_line();
    timer_idx = hpet_test_timer_index();

    len = 0;
    if(procfs_buf_puts(buf, sizeof(buf), &len, "backend lapic\n") < 0)
      return -1;
    if(procfs_buf_puts(buf, sizeof(buf), &len,
                       ktime_uses_hpet() ? "clocksource hpet\n" : "clocksource tsc\n") < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "ticks ", now) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "tick_hz ", 100) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "tick_sleepers ", (uint)proc_has_tick_sleepers()) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "lapic_calibrated ", (uint)lapic_timer_is_calibrated()) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "lapic_ticr ", lapic_timer_initial_count()) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "hpet_available ", (uint)hpet_available()) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "hpet_test_enabled ", (uint)hpet_test_enabled()) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "hpet_timer_index ", timer_idx >= 0 ? (uint)timer_idx : 0U) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "hpet_irq_line ", irq_line >= 0 ? (uint)irq_line : 0U) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "hpet_irq_count ", hpet_irq_count()) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "hpet_route_cap ", hpet_test_route_cap()) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "hpet_period_fs ", hpet_period_fs()) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "hpet_num_timers ", hpet_num_timers()) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "hpet_counter_64bit ", (uint)hpet_counter_is_64bit()) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "hpet_counter_hi ", ctr_hi) < 0)
      return -1;
    if(procfs_buf_putkv_u(buf, sizeof(buf), &len, "hpet_counter_lo ", ctr_lo) < 0)
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
      ip->inum != PROCFS_SERVER7_INO &&
      ip->inum != PROCFS_MLFQ_TUNE_INO)
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

  if(ip->inum == PROCFS_MLFQ_TUNE_INO){
    if(mlfq_set_boost_interval(val) < 0)
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
