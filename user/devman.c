/*
 * devman - Device Node Manager for auxv6
 *
 * Scans kernel device inventory and creates device nodes based on
 * configuration file (/etc/devman.conf).
 *
 * Inspired by BusyBox mdev and standard udev, with a simplified
 * configuration format suitable for auxv6.
 *
 * Usage: devman [-s|--scan]
 *   -s, --scan    Scan system and create all device nodes
 */

#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fcntl.h"

#define MAX_DEVICES 256
#define MAX_LINE 256
#define MAX_PATH 64
#define CREATED_LINE_MAX 512

struct devman_device {
  char path[MAX_PATH];
  int major;
  int minor;
  int type;  /* M_IFBLK or M_IFCHR */
};

static struct devman_device devices[MAX_DEVICES];
static int ndevices = 0;
static int debug_mode = 0;

static int
is_space(char c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char *
trim_left(char *s)
{
  while(*s && is_space(*s))
    s++;
  return s;
}

static void
trim_right(char *s)
{
  int n;

  n = strlen(s);
  while(n > 0 && is_space(s[n - 1])) {
    s[n - 1] = 0;
    n--;
  }
}

static void
maybe_parse_config_line(char *line)
{
  char *p;

  p = trim_left(line);
  trim_right(p);
  if(*p == 0 || *p == '#')
    return;

  if(strncmp(p, "debug=", 6) == 0) {
    debug_mode = atoi(p + 6) ? 1 : 0;
    return;
  }
  if(strncmp(p, "debug ", 6) == 0) {
    debug_mode = atoi(p + 6) ? 1 : 0;
    return;
  }
}

static void
devman_load_config(void)
{
  int fd;
  int n;
  int i;
  char buf[128];
  char line[MAX_LINE];
  int llen;

  fd = open("/etc/devman.conf", O_RDONLY);
  if(fd < 0)
    return;

  llen = 0;
  while((n = read(fd, buf, sizeof(buf))) > 0) {
    for(i = 0; i < n; i++) {
      char c = buf[i];
      if(c == '\n') {
        line[llen] = 0;
        maybe_parse_config_line(line);
        llen = 0;
      } else if(llen < MAX_LINE - 1) {
        line[llen++] = c;
      }
    }
  }

  if(llen > 0) {
    line[llen] = 0;
    maybe_parse_config_line(line);
  }

  close(fd);
}

static int
append_token(char *dst, int dstsz, const char *tok)
{
  int i;
  int n;

  n = strlen(dst);
  if(n >= dstsz - 1)
    return n;

  if(n > 0 && n < dstsz - 1)
    dst[n++] = ' ';

  for(i = 0; tok[i] && n < dstsz - 1; i++)
    dst[n++] = tok[i];

  dst[n] = 0;
  return n;
}

static void
devman_enumerate_block_devices(void)
{
  int unit;
  int dev;
  char path[MAX_PATH];

  ndevices = 0;

  /* IDE/ATA block devices */
  for(unit = 0; unit < 4; unit++) {
    dev = HD_DISK_DEV(unit);
    if(devblocks(dev) > 0) {
      strcpy(path, "/dev/hd");
      path[7] = 'a' + unit;
      path[8] = 0;
      strcpy(devices[ndevices].path, path);
      devices[ndevices].major = 2;
      devices[ndevices].minor = dev;
      devices[ndevices].type = M_IFBLK;
      if(ndevices < MAX_DEVICES - 1)
        ndevices++;
    }
  }

  /* Virtio block devices */
  for(unit = 0; unit < 4; unit++) {
    dev = VD_DISK_DEV(unit);
    if(devblocks(dev) > 0) {
      strcpy(path, "/dev/vd");
      path[7] = 'a' + unit;
      path[8] = 0;
      strcpy(devices[ndevices].path, path);
      devices[ndevices].major = 2;
      devices[ndevices].minor = dev;
      devices[ndevices].type = M_IFBLK;
      if(ndevices < MAX_DEVICES - 1)
        ndevices++;
    }
  }

  /* NVMe block devices */
  for(unit = 0; unit < 4; unit++) {
    dev = ND_DISK_DEV(unit);
    if(devblocks(dev) > 0) {
      strcpy(path, "/dev/nd");
      path[7] = 'a' + unit;
      path[8] = 0;
      strcpy(devices[ndevices].path, path);
      devices[ndevices].major = 2;
      devices[ndevices].minor = dev;
      devices[ndevices].type = M_IFBLK;
      if(ndevices < MAX_DEVICES - 1)
        ndevices++;
    }
  }

  /* Loop devices */
  for(unit = 0; unit < 8; unit++) {
    dev = 40 + unit;
    if(devblocks(dev) > 0) {
      strcpy(path, "/dev/loop0");
      path[9] = '0' + unit;
      strcpy(devices[ndevices].path, path);
      devices[ndevices].major = 2;
      devices[ndevices].minor = dev;
      devices[ndevices].type = M_IFBLK;
      if(ndevices < MAX_DEVICES - 1)
        ndevices++;
    }
  }
}

static void
devman_enumerate_pty_devices(void)
{
  int i;
  char path[MAX_PATH];

  /* PTY slave devices - create pts/0..15 for now */
  for(i = 0; i < 16; i++) {
    strcpy(path, "/dev/pts/00");
    if(i < 10) {
      path[9] = '0' + i;
      path[10] = 0;
    } else {
      path[9] = '1';
      path[10] = '0' + (i - 10);
      path[11] = 0;
    }
    strcpy(devices[ndevices].path, path);
    devices[ndevices].major = 3;
    devices[ndevices].minor = i + 1;  /* PTY_MINOR_SLAVE_BASE + i */
    devices[ndevices].type = M_IFCHR;
    if(ndevices < MAX_DEVICES - 1)
      ndevices++;
  }

  /* PTM device - /dev/ptmx */
  if(ndevices < MAX_DEVICES) {
    strcpy(devices[ndevices].path, "/dev/ptmx");
    devices[ndevices].major = 3;
    devices[ndevices].minor = 0;  /* PTY_MINOR_PTMX */
    devices[ndevices].type = M_IFCHR;
    ndevices++;
  }

  /* TTY devices */
  for(i = 0; i < 4; i++) {
    if(ndevices >= MAX_DEVICES)
      break;
    strcpy(path, "/dev/tty0");
    path[8] = '0' + i;
    strcpy(devices[ndevices].path, path);
    devices[ndevices].major = 1;
    devices[ndevices].minor = i + 1;
    devices[ndevices].type = M_IFCHR;
    ndevices++;
  }

  /* Console and standard char devices */
  if(ndevices < MAX_DEVICES) {
    strcpy(devices[ndevices].path, "/dev/console");
    devices[ndevices].major = 1;
    devices[ndevices].minor = 1;
    devices[ndevices].type = M_IFCHR;
    ndevices++;
  }

  if(ndevices < MAX_DEVICES) {
    strcpy(devices[ndevices].path, "/dev/null");
    devices[ndevices].major = 1;
    devices[ndevices].minor = 3;
    devices[ndevices].type = M_IFCHR;
    ndevices++;
  }

  if(ndevices < MAX_DEVICES) {
    strcpy(devices[ndevices].path, "/dev/zero");
    devices[ndevices].major = 1;
    devices[ndevices].minor = 5;
    devices[ndevices].type = M_IFCHR;
    ndevices++;
  }
}

static int
devman_create_node(const char *path, int type, short major, short minor, uint mode)
{
  struct stat st;

  /* Check if node exists and is correct */
  if(stat((char*)path, &st) == 0) {
    if(st.st_type == T_DEV && st.st_major == major && st.st_minor == minor) {
      /* Node exists and is correct */
      return 0;
    }
    /* Wrong node - replace it */
    unlink((char*)path);
  }

  /* Create parent directory if needed */
  char dirpath[MAX_PATH];
  int i, lastslash = -1;
  
  for(i = 0; path[i]; i++) {
    if(path[i] == '/')
      lastslash = i;
  }
  
  if(lastslash > 0) {
    for(i = 0; i < lastslash && i < MAX_PATH - 1; i++)
      dirpath[i] = path[i];
    dirpath[i] = 0;
    mkdir(dirpath);
  }

  /* Create the device node */
  if(mknod((char *)path, type | (mode & 0777), major, minor) < 0) {
    printf(2, "devman: failed to create %s\n", path);
    return -1;
  }

  if(debug_mode) {
    printf(1, "devman: created %s (major=%d, minor=%d, mode=%03o)\n",
           path, major, minor, mode & 0777);
  }

  return 1;
}

void
devman_scan_and_create(void)
{
  int i;
  int rc;
  int created;
  int changed;
  struct devman_device *dev;
  char created_line[CREATED_LINE_MAX];
  int created_line_len;

  if(debug_mode)
    printf(1, "devman: scanning devices\n");
  
  devman_enumerate_block_devices();
  if(debug_mode)
    printf(1, "devman: found %d block/loop devices\n", ndevices);
  
  changed = ndevices;
  devman_enumerate_pty_devices();
  if(debug_mode)
    printf(1, "devman: found %d pty/tty devices\n", ndevices - changed);

  created = 0;
  created_line[0] = 0;
  created_line_len = 0;
  printf(1, "devman: creating devices");

  /* Create all devices with sensible defaults */
  for(i = 0; i < ndevices; i++) {
    dev = &devices[i];
    uint mode = 0660;  /* Default mode */
    
    /* Adjust mode based on device type */
    if(dev->type == M_IFCHR) {
      if(dev->major == 3) {  /* PTY devices */
        if(dev->minor == 0) {
          mode = 0666;  /* /dev/ptmx */
        } else {
          mode = 0620;  /* /dev/pts/N */
        }
      } else if(dev->major == 1) {  /* Console and char devices */
        if(dev->minor == 1) {
          mode = 0600;  /* /dev/console */
        } else if(dev->minor == 3 || dev->minor == 5) {
          mode = 0666;  /* /dev/null, /dev/zero */
        } else {
          mode = 0620;  /* /dev/tty* */
        }
      }
    }
    
    rc = devman_create_node(dev->path, dev->type, dev->major, dev->minor, mode);
    if(rc == 1) {
      int j;
      int last;
      char *name;

      created++;
      last = -1;
      for(j = 0; dev->path[j]; j++) {
        if(dev->path[j] == '/')
          last = j;
      }
      name = (last >= 0) ? &dev->path[last + 1] : dev->path;
      if(strncmp(dev->path, "/dev/pts/", 9) == 0)
        name = &dev->path[5]; /* "pts/N" */

      if(created_line_len < (int)sizeof(created_line) - 1)
        created_line_len = append_token(created_line, sizeof(created_line), name);

      if(created_line_len >= (int)sizeof(created_line) - 4) {
        created_line[sizeof(created_line) - 4] = '.';
        created_line[sizeof(created_line) - 3] = '.';
        created_line[sizeof(created_line) - 2] = '.';
        created_line[sizeof(created_line) - 1] = 0;
      }

      /* Append to the same logical line without terminal redraw tricks. */
      printf(1, " %s", name);
    }
  }

  if(created == 0)
    printf(1, " (none)\n");
  else
    printf(1, "\n");

  if(debug_mode)
    printf(1, "devman: done creating %d/%d device nodes\n", created, ndevices);
}

int
main(int argc, char *argv[])
{
  int scan_mode = 0;

  if(argc > 1) {
    if(strcmp(argv[1], "-s") == 0 || strcmp(argv[1], "--scan") == 0) {
      scan_mode = 1;
    } else {
      printf(2, "usage: %s [-s|--scan]\n", argv[0]);
      printf(2, "  -s, --scan    Scan and create all device nodes\n");
      exit();
    }
  }

  if(scan_mode) {
    devman_load_config();
    if(debug_mode)
      printf(1, "devman: device node manager (scan mode, debug=%d)\n", debug_mode);
    devman_scan_and_create();
  } else {
    printf(2, "devman: no mode specified (try -s for scan mode)\n");
    exit();
  }

  exit();
}
