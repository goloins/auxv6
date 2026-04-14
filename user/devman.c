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
#include "sys/stat.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "string.h"
#include "dirent.h"
#include "audio.h"
#include "audio_ioctl.h"

#define CONSOLE 1
#define CONSOLE_MINOR_FB0 100
#define CONSOLE_MINOR_MOUSE0 101
#define CONSOLE_MINOR_KBD0 102

#define MAX_DEVICES 256
#define MAX_LINE 256
#define MAX_PATH 64
#define CREATED_LINE_MAX 512
#define MAX_RULES 32
#define SERIALDEV 4
#define AUDIODEV 5
#define TUNTAPDEV 6
#define RNGDEV 7

struct devman_device {
  char path[MAX_PATH];
  int major;
  int minor;
  int type;  /* M_IFBLK or M_IFCHR */
};

struct devman_rule {
  char pattern[64];
  uint mode;
};

static struct devman_device devices[MAX_DEVICES];
static int ndevices = 0;
static struct devman_rule rules[MAX_RULES];
static int nrules = 0;
static int debug_mode = 0;

static int
parse_uint(const char *s, int *out)
{
  int v;

  if(!s || !*s)
    return -1;
  v = 0;
  while(*s >= '0' && *s <= '9'){
    v = v * 10 + (*s - '0');
    s++;
  }
  if(out)
    *out = v;
  return 0;
}

static char*
find_field(char *line, const char *key)
{
  int keylen;
  char *p;

  if(!line || !key)
    return 0;
  keylen = strlen(key);
  p = line;
  while(*p){
    if(strncmp(p, key, keylen) == 0)
      return p + keylen;
    while(*p && *p != ' ' && *p != '\n')
      p++;
    while(*p == ' ')
      p++;
    if(*p == '\n')
      p++;
  }
  return 0;
}

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

static uint
parse_octal(const char *s)
{
  uint v;

  v = 0;
  if(*s == '0')
    s++;
  while(*s >= '0' && *s <= '7'){
    v = v * 8 + (uint)(*s - '0');
    s++;
  }
  return v;
}

/* Simple glob: only '*' wildcard supported */
static int
glob_match(const char *pat, const char *str)
{
  while(*pat){
    if(*pat == '*'){
      pat++;
      if(!*pat)
        return 1;
      while(*str){
        if(glob_match(pat, str))
          return 1;
        str++;
      }
      return 0;
    }
    if(*pat != *str)
      return 0;
    pat++;
    str++;
  }
  return *str == 0;
}

static int
devman_match_rule(const char *path, uint *mode_out)
{
  int i;

  for(i = 0; i < nrules; i++){
    if(glob_match(rules[i].pattern, path)){
      if(mode_out)
        *mode_out = rules[i].mode;
      return 1;
    }
  }
  return 0;
}

static void
maybe_parse_config_line(char *line)
{
  char *p;
  char pat[64];
  int plen;

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

  /* Try to parse a policy rule: <pattern> <octal-mode>
   * e.g. "/dev/pts/0"  0620 or "/dev/null" 0666
   */
  plen = 0;
  while(*p && !is_space(*p) && plen < (int)sizeof(pat) - 1)
    pat[plen++] = *p++;
  pat[plen] = 0;
  while(*p && is_space(*p))
    p++;

  if(plen > 0 && *p >= '0' && *p <= '7' && nrules < MAX_RULES){
    rules[nrules].mode = parse_octal(p);
    strncpy(rules[nrules].pattern, pat, sizeof(rules[nrules].pattern) - 1);
    rules[nrules].pattern[sizeof(rules[nrules].pattern) - 1] = 0;
    nrules++;
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
  int atapi_devs[16];
  int atapi_count;

  ndevices = 0;

  atapi_count = 0;

  /* Collect ATAPI devices from /proc/ahci_tune so we can skip /dev/hd* */
  {
    char buf[1024];
    int fd;
    int n;
    char *line;

    fd = open("/proc/ahci_tune", O_RDONLY);
    if(fd >= 0){
      n = read(fd, buf, sizeof(buf) - 1);
      close(fd);
      if(n > 0){
        buf[n] = 0;
        line = buf;
        while(*line){
          char *next;
          char *p;
          int v;

          next = line;
          while(*next && *next != '\n')
            next++;
          if(*next == '\n')
            *next++ = 0;

          if(strncmp(line, "hba=", 4) == 0){
            p = find_field(line, "type=");
            if(p && strncmp(p, "atapi", 5) == 0){
              p = find_field(line, "dev=");
              if(p && parse_uint(p, &v) == 0 && atapi_count < (int)(sizeof(atapi_devs) / sizeof(atapi_devs[0])))
                atapi_devs[atapi_count++] = v;
            }
          }

          line = next;
        }
      }
    }
  }

  /* IDE/ATA block devices (skip ATAPI-backed dev ids) */
  for(unit = 0; unit < 4; unit++) {
    dev = HD_DISK_DEV(unit);
    if(devblocks(dev) > 0) {
      int i;
      int is_atapi;

      is_atapi = 0;
      for(i = 0; i < atapi_count; i++) {
        if(atapi_devs[i] == dev) {
          is_atapi = 1;
          break;
        }
      }
      if(is_atapi)
        continue;
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
    dev = 44 + unit;
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

  /* ATAPI devices: also expose as /dev/cdrom, /dev/cdrom1, ... */
  if(atapi_count > 0) {
    int cdidx;
    int i;

    cdidx = 0;
    for(i = 0; i < atapi_count && ndevices < MAX_DEVICES - 1; i++) {
      dev = atapi_devs[i];
      if(devblocks(dev) <= 0)
        continue;
      if(cdidx == 0){
        strcpy(devices[ndevices].path, "/dev/cdrom");
      } else {
        strcpy(devices[ndevices].path, "/dev/cdrom0");
        devices[ndevices].path[10] = '0' + cdidx;
        devices[ndevices].path[11] = 0;
      }
      devices[ndevices].major = 2;
      devices[ndevices].minor = dev;
      devices[ndevices].type = M_IFBLK;
      ndevices++;
      cdidx++;
    }
  }
}

/*
 * Returns 1 if the kernel audio subsystem has at least one real (non-null)
 * hardware device registered, 0 otherwise.  Creates /dev/audioctl temporarily
 * if it does not already exist so the ioctl can be issued.
 */
static int
devman_has_real_audio_hw(void)
{
  struct audio_enum_devices req;
  int fd;
  int created;
  int result;

  created = 0;
  if(mknod("/dev/audioctl", M_IFCHR | 0600, AUDIODEV, 0) == 0)
    created = 1;

  fd = open("/dev/audioctl", O_RDONLY);
  if(fd < 0){
    if(created)
      unlink("/dev/audioctl");
    return 0;
  }

  memset(&req, 0, sizeof(req));
  req.abi_version = AUDIO_ABI_VERSION;
  req.struct_size = sizeof(req);
  req.max_entries = 0;
  req.entries_ptr = 0;

  result = 0;
  if(ioctl(fd, AUDIO_IOC_ENUM_DEVICES, &req) == 0)
    result = (req.num_entries >= 2) ? 1 : 0;

  close(fd);
  /* Leave the node in place — the main enumeration pass will own it. */
  return result;
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

  /* Serial TTY devices (/dev/ttyS0..3) */
  for(i = 0; i < 4; i++) {
    if(ndevices >= MAX_DEVICES)
      break;
    strcpy(path, "/dev/ttyS0");
    path[9] = '0' + i;
    strcpy(devices[ndevices].path, path);
    devices[ndevices].major = SERIALDEV;
    devices[ndevices].minor = i + 1;
    devices[ndevices].type = M_IFCHR;
    ndevices++;
  }

  /* Audio control node — always present (null device is always registered). */
  if(ndevices < MAX_DEVICES) {
    strcpy(devices[ndevices].path, "/dev/audioctl");
    devices[ndevices].major = AUDIODEV;
    devices[ndevices].minor = 0;
    devices[ndevices].type = M_IFCHR;
    ndevices++;
  }
  /* Playback endpoint — only if real hardware was detected by the kernel. */
  if(ndevices < MAX_DEVICES && devman_has_real_audio_hw()) {
    strcpy(devices[ndevices].path, "/dev/pcmC0D0p");
    devices[ndevices].major = AUDIODEV;
    devices[ndevices].minor = 1;
    devices[ndevices].type = M_IFCHR;
    ndevices++;
  }

  /* TUN/TAP control endpoint */
  if(ndevices < MAX_DEVICES) {
    strcpy(devices[ndevices].path, "/dev/net/tun");
    devices[ndevices].major = TUNTAPDEV;
    devices[ndevices].minor = 0;
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
    strcpy(devices[ndevices].path, "/dev/fb0");
    devices[ndevices].major = CONSOLE;
    devices[ndevices].minor = CONSOLE_MINOR_FB0;
    devices[ndevices].type = M_IFCHR;
    ndevices++;
  }

  if(ndevices < MAX_DEVICES) {
    strcpy(devices[ndevices].path, "/dev/mouse0");
    devices[ndevices].major = CONSOLE;
    devices[ndevices].minor = CONSOLE_MINOR_MOUSE0;
    devices[ndevices].type = M_IFCHR;
    ndevices++;
  }

  if(ndevices < MAX_DEVICES) {
    strcpy(devices[ndevices].path, "/dev/kbd0");
    devices[ndevices].major = CONSOLE;
    devices[ndevices].minor = CONSOLE_MINOR_KBD0;
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

  if(ndevices < MAX_DEVICES) {
    strcpy(devices[ndevices].path, "/dev/urandom");
    devices[ndevices].major = RNGDEV;
    devices[ndevices].minor = 0;
    devices[ndevices].type = M_IFCHR;
    ndevices++;
  }

  if(ndevices < MAX_DEVICES) {
    strcpy(devices[ndevices].path, "/dev/random");
    devices[ndevices].major = RNGDEV;
    devices[ndevices].minor = 1;
    devices[ndevices].type = M_IFCHR;
    ndevices++;
  }
}

static int
devman_remove_node(const char *path)
{
  struct stat st;

  if(stat((char*)path, &st) < 0)
    return 0;
  if(unlink((char*)path) < 0) {
    if(debug_mode)
      dprintf(2, "devman: failed to remove %s\n", path);
    return -1;
  }
  return 1;
}

static int
devman_create_node(const char *path, int type, short major, short minor, uint mode)
{
  struct stat st;

  /* Check if node exists and is correct */
  if(stat((char*)path, &st) == 0) {
    if((S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) && major(st.st_rdev) == major && minor(st.st_rdev) == minor) {
      /* Node exists and is correct */
      return 0;
    }
    /* Wrong node - replace it */
    devman_remove_node(path);
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
    dprintf(2, "devman: failed to create %s\n", path);
    return -1;
  }

  if(debug_mode) {
    dprintf(1, "devman: created %s (major=%d, minor=%d, mode=%03o)\n",
           path, major, minor, mode & 0777);
  }

  return 1;
}

static void
devman_remove_managed_nodes(void)
{
  int i;
  char path[MAX_PATH];

  /* Block devices: hd[a-d], vd[a-d], nd[a-d] */
  for(i = 0; i < 4; i++) {
    strcpy(path, "/dev/hd");
    path[7] = 'a' + i;
    path[8] = 0;
    devman_remove_node(path);

    strcpy(path, "/dev/vd");
    path[7] = 'a' + i;
    path[8] = 0;
    devman_remove_node(path);

    strcpy(path, "/dev/nd");
    path[7] = 'a' + i;
    path[8] = 0;
    devman_remove_node(path);
  }

  /* Loop devices */
  for(i = 0; i < 8; i++) {
    strcpy(path, "/dev/loop0");
    path[9] = '0' + i;
    path[10] = 0;
    devman_remove_node(path);
  }

  /* ATAPI aliases */
  for(i = 0; i < 8; i++) {
    if(i == 0) {
      strcpy(path, "/dev/cdrom");
    } else {
      strcpy(path, "/dev/cdrom0");
      path[10] = '0' + i;
      path[11] = 0;
    }
    devman_remove_node(path);
  }

  /* PTY/TTY devices */
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
    devman_remove_node(path);
  }

  devman_remove_node("/dev/ptmx");

  for(i = 0; i < 4; i++) {
    strcpy(path, "/dev/tty0");
    path[8] = '0' + i;
    path[9] = 0;
    devman_remove_node(path);
  }

  devman_remove_node("/dev/audioctl");
  devman_remove_node("/dev/pcmC0D0p");
  devman_remove_node("/dev/net/tun");

  devman_remove_node("/dev/console");
  devman_remove_node("/dev/fb0");
  devman_remove_node("/dev/mouse0");
  devman_remove_node("/dev/kbd0");
  devman_remove_node("/dev/null");
  devman_remove_node("/dev/zero");
  devman_remove_node("/dev/urandom");
  devman_remove_node("/dev/random");
}

/*
 * devman_cleanup_stale - remove /dev nodes not in the current device inventory.
 *
 * Enumerates /dev and /dev/pts, stat()s each device node, and unlinks any
 * that are not present in the devices[] array populated by the enumerate
 * functions.  Must be called after devman_enumerate_*() has been run.
 */
static void
devman_cleanup_stale(void)
{
  DIR *dp;
  struct dirent *ent;
  struct stat st;
  char path[MAX_PATH];
  int i;
  int found;

  /* Scan /dev (flat entries only — /dev/pts handled separately) */
  dp = opendir("/dev");
  if(!dp)
    return;
  while((ent = readdir(dp)) != 0){
    if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
      continue;
    if(strcmp(ent->d_name, "pts") == 0)
      continue;

    /* Build full path safely */
    strncpy(path, "/dev/", MAX_PATH - 1);
    path[MAX_PATH - 1] = 0;
    strncat(path, ent->d_name, MAX_PATH - 1 - 5);

    if(stat(path, &st) < 0)
      continue;
    if(!(S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)))
      continue;

    found = 0;
    for(i = 0; i < ndevices; i++){
      if(strcmp(devices[i].path, path) == 0){
        found = 1;
        break;
      }
    }
    if(!found){
      if(debug_mode)
        dprintf(1, "devman: removing stale node %s\n", path);
      devman_remove_node(path);
    }
  }
  closedir(dp);

  /* Scan /dev/pts */
  dp = opendir("/dev/pts");
  if(!dp)
    return;
  while((ent = readdir(dp)) != 0){
    if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
      continue;

    strncpy(path, "/dev/pts/", MAX_PATH - 1);
    path[MAX_PATH - 1] = 0;
    strncat(path, ent->d_name, MAX_PATH - 1 - 9);

    if(stat(path, &st) < 0)
      continue;
    if(!(S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)))
      continue;

    found = 0;
    for(i = 0; i < ndevices; i++){
      if(strcmp(devices[i].path, path) == 0){
        found = 1;
        break;
      }
    }
    if(!found){
      if(debug_mode)
        dprintf(1, "devman: removing stale node %s\n", path);
      devman_remove_node(path);
    }
  }
  closedir(dp);
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
    dprintf(1, "devman: scanning devices\n");
  
  devman_enumerate_block_devices();
  if(debug_mode)
    dprintf(1, "devman: found %d block/loop devices\n", ndevices);
  
  changed = ndevices;
  devman_enumerate_pty_devices();
  if(debug_mode)
    dprintf(1, "devman: found %d pty/tty devices\n", ndevices - changed);

  created = 0;
  created_line[0] = 0;
  created_line_len = 0;
  dprintf(1, "devman: creating devices");

  /* Create all devices with sensible defaults */
  for(i = 0; i < ndevices; i++) {
    dev = &devices[i];
    uint mode = 0660;  /* Default mode */

    /* Config rules take priority; fall back to built-in defaults */
    if(!devman_match_rule(dev->path, &mode)){
      if(dev->type == M_IFCHR){
        if(dev->major == 3){         /* PTY devices */
          mode = (dev->minor == 0) ? 0666 : 0620;
        } else if(dev->major == 1){  /* Console and char devices */
          if(dev->minor == 1)
            mode = 0600;             /* /dev/console */
          else if(dev->minor == 3 || dev->minor == 5)
            mode = 0666;             /* /dev/null, /dev/zero */
          else
            mode = 0620;             /* /dev/tty* */
        } else if(dev->major == RNGDEV) {
          mode = 0666;               /* /dev/random, /dev/urandom */
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
      dprintf(1, " %s", name);
    }
  }

  if(created == 0)
    dprintf(1, " (none)\n");
  else
    dprintf(1, "\n");

  if(debug_mode)
    dprintf(1, "devman: done creating %d/%d device nodes\n", created, ndevices);
}

int
main(int argc, char *argv[])
{
  int scan_mode = 0;
  int replace_mode = 0;
  int cleanup_mode = 0;
  int daemon_mode = 0;

  if(argc > 1) {
    if(strcmp(argv[1], "-s") == 0 || strcmp(argv[1], "--scan") == 0) {
      scan_mode = 1;
    } else if(strcmp(argv[1], "-rr") == 0) {
      scan_mode = 1;
      replace_mode = 1;
    } else if(strcmp(argv[1], "-c") == 0) {
      cleanup_mode = 1;
    } else if(strcmp(argv[1], "-d") == 0) {
      daemon_mode = 1;
    } else {
      dprintf(2, "usage: %s [-s|--scan|-rr|-c|-d]\n", argv[0]);
      dprintf(2, "  -s, --scan    Scan and create all device nodes\n");
      dprintf(2, "  -rr          Rescan and replace managed device nodes\n");
      dprintf(2, "  -c           Remove stale /dev nodes not in kernel inventory\n");
      dprintf(2, "  -d           Daemonize: scan, then loop cleaning stale nodes\n");
      exit(0);
    }
  }

  if(scan_mode) {
    devman_load_config();
    if(debug_mode)
      dprintf(1, "devman: device node manager (scan mode, debug=%d)\n", debug_mode);
    if(replace_mode) {
      if(debug_mode)
        dprintf(1, "devman: removing managed nodes before rescan\n");
      devman_remove_managed_nodes();
    }
    devman_scan_and_create();
  } else if(cleanup_mode) {
    devman_load_config();
    if(debug_mode)
      dprintf(1, "devman: cleanup mode (debug=%d)\n", debug_mode);
    devman_enumerate_block_devices();
    devman_enumerate_pty_devices();
    devman_cleanup_stale();
  } else if(daemon_mode) {
    int pid;

    /* Double-fork to fully detach from the calling session */
    pid = fork();
    if(pid < 0){
      dprintf(2, "devman: fork failed\n");
      exit(1);
    }
    if(pid > 0)
      exit(0);  /* parent: done */

    setsid();

    pid = fork();
    if(pid < 0)
      exit(1);
    if(pid > 0)
      exit(0);  /* first child: done */

    /* Second child: redirect stdin/stdout/stderr to /dev/null */
    {
      int nullfd;

      nullfd = open("/dev/null", O_RDONLY);
      if(nullfd >= 0){
        close(0);
        dup(nullfd);
        close(nullfd);
      }
      nullfd = open("/dev/null", O_WRONLY);
      if(nullfd >= 0){
        close(1);
        dup(nullfd);
        close(2);
        dup(nullfd);
        close(nullfd);
      }
    }

    devman_load_config();
    devman_scan_and_create();

    /* Periodic cleanup loop: re-enumerate and clean stale nodes every 30s.
     * This is a lightweight stand-in until a kernel hotplug event fd exists.
     */
    for(;;){
      sleep(30);
      devman_enumerate_block_devices();
      devman_enumerate_pty_devices();
      devman_cleanup_stale();
    }
  } else {
    dprintf(2, "devman: no mode specified (try -s for scan mode)\n");
    exit(0);
  }

  exit(0);
}
