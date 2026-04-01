#include "types.h"
#include "stat.h"
#include "fs.h"
#include "user.h"
#include "fcntl.h"

#define VBLKTEST_MAX_DISKS 4
#define VBLKTEST_BUF_SIZE 512

static int passed = 0;
static int failed = 0;
static int skipped = 0;

enum {
  VBLKTEST_OK = 0,
  VBLKTEST_ERR_ENSURE_DIR,
  VBLKTEST_ERR_MOUNT,
  VBLKTEST_ERR_OPEN,
  VBLKTEST_ERR_WRITE,
  VBLKTEST_ERR_SEEK,
  VBLKTEST_ERR_READ,
  VBLKTEST_ERR_UMOUNT,
  VBLKTEST_ERR_VERIFY,
};

struct vblk_counters {
  int ok[VD_DISK_UNITS];
  int flush_ok[VD_DISK_UNITS];
};

static void
record_result(char *name, int ok)
{
  if(ok){
    printf(1, "[PASS] %s\n", name);
    passed++;
  } else {
    printf(1, "[FAIL] %s\n", name);
    failed++;
  }
}

static void
record_skip(char *name, char *reason)
{
  printf(1, "[SKIP] %s (%s)\n", name, reason);
  skipped++;
}

static int
parse_uint(char **ps)
{
  int v;
  char *s;

  s = *ps;
  v = 0;
  while(*s >= '0' && *s <= '9'){
    v = v * 10 + (*s - '0');
    s++;
  }
  *ps = s;
  return v;
}

static char*
find_field(char *s, char *key)
{
  int keylen;

  keylen = strlen(key);
  while(*s){
    if(strncmp(s, key, keylen) == 0)
      return s + keylen;
    while(*s && *s != ' ' && *s != '\n')
      s++;
    while(*s == ' ')
      s++;
    if(*s == '\n')
      s++;
  }
  return 0;
}

static int
read_vblk_counters(struct vblk_counters *out)
{
  char buf[1024];
  char *line;
  int fd;
  int n;
  int i;

  if(out == 0)
    return -1;

  for(i = 0; i < VD_DISK_UNITS; i++){
    out->ok[i] = -1;
    out->flush_ok[i] = -1;
  }

  fd = open("/proc/vblk_flush", O_RDONLY);
  if(fd < 0)
    return -1;

  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(n < 0)
    return -1;
  buf[n] = 0;

  line = buf;
  while(*line){
    char *next;
    char *p;
    int dev;
    int unit;

    next = line;
    while(*next && *next != '\n')
      next++;
    if(*next == '\n')
      *next++ = 0;

    if(strncmp(line, "dev=", 4) == 0){
      p = line + 4;
      dev = parse_uint(&p);
      if(dev >= VD_DISK_BASE && dev < VD_DISK_BASE + VD_DISK_UNITS){
        unit = dev - VD_DISK_BASE;
        p = find_field(line, "ok=");
        if(p)
          out->ok[unit] = parse_uint(&p);
        p = find_field(line, "flush_ok=");
        if(p)
          out->flush_ok[unit] = parse_uint(&p);
      }
    }

    line = next;
  }

  return 0;
}

static int
ensure_dir(char *path)
{
  struct stat st;

  if(stat(path, &st) == 0){
    if(st.st_type == T_DIR)
      return 0;
    return -1;
  }
  return mkdir(path);
}

static int
fill_pattern(char *buf, int n, int seed)
{
  int i;

  for(i = 0; i < n; i++)
    buf[i] = (char)((seed + i * 17) & 0xff);
  return 0;
}

static int
buffer_equal(char *a, char *b, int n)
{
  int i;

  for(i = 0; i < n; i++){
    if(a[i] != b[i])
      return 0;
  }
  return 1;
}

static int
run_roundtrip(int unit, int iteration)
{
  char mountpoint[32];
  char filepath[64];
  char wbuf[VBLKTEST_BUF_SIZE];
  char rbuf[VBLKTEST_BUF_SIZE];
  int fd;
  int dev;
  int flags;
  int n;
  int i;

  dev = VD_DISK_DEV(unit);

  strcpy(mountpoint, "/mnt/vblk");
  mountpoint[9] = '0' + unit;
  mountpoint[10] = 0;

  if(ensure_dir(mountpoint) < 0)
    return VBLKTEST_ERR_ENSURE_DIR;

  flags = MNT_MAKEDEV(dev);
  if(mount(mountpoint, "ext2", flags) < 0)
    return VBLKTEST_ERR_MOUNT;

  strcpy(filepath, mountpoint);
  i = strlen(filepath);
  filepath[i++] = '/';
  filepath[i++] = 'i';
  filepath[i++] = 'o';
  filepath[i++] = '.';
  filepath[i++] = '0' + iteration;
  filepath[i] = 0;

  fill_pattern(wbuf, sizeof(wbuf), unit * 31 + iteration);
  memset(rbuf, 0, sizeof(rbuf));

  unlink(filepath);
  fd = open(filepath, O_CREATE | O_RDWR | O_TRUNC);
  if(fd < 0){
    umount(mountpoint);
    return VBLKTEST_ERR_OPEN;
  }

  n = write(fd, wbuf, sizeof(wbuf));
  if(n != sizeof(wbuf)){
    close(fd);
    unlink(filepath);
    umount(mountpoint);
    return VBLKTEST_ERR_WRITE;
  }

  if(lseek(fd, 0, SEEK_SET) < 0){
    close(fd);
    unlink(filepath);
    umount(mountpoint);
    return VBLKTEST_ERR_SEEK;
  }

  n = read(fd, rbuf, sizeof(rbuf));
  close(fd);
  unlink(filepath);

  if(umount(mountpoint) < 0)
    return VBLKTEST_ERR_UMOUNT;
  if(n != sizeof(rbuf))
    return VBLKTEST_ERR_READ;
  return buffer_equal(wbuf, rbuf, sizeof(wbuf)) ? VBLKTEST_OK : VBLKTEST_ERR_VERIFY;
}

static char*
roundtrip_error_name(int rc)
{
  switch(rc){
  case VBLKTEST_OK:
    return "ok";
  case VBLKTEST_ERR_ENSURE_DIR:
    return "ensure-dir";
  case VBLKTEST_ERR_MOUNT:
    return "mount";
  case VBLKTEST_ERR_OPEN:
    return "open";
  case VBLKTEST_ERR_WRITE:
    return "write";
  case VBLKTEST_ERR_SEEK:
    return "seek";
  case VBLKTEST_ERR_READ:
    return "read";
  case VBLKTEST_ERR_UMOUNT:
    return "umount";
  case VBLKTEST_ERR_VERIFY:
    return "verify";
  }
  return "unknown";
}

static void
test_enumeration(int expected_min, int *units, int *nunits)
{
  int unit;
  int count;

  count = 0;
  for(unit = 0; unit < VD_DISK_UNITS; unit++){
    if(devblocks(VD_DISK_DEV(unit)) > 0)
      units[count++] = unit;
  }

  *nunits = count;
  printf(1, "Detected %d virtio-blk disks\n", count);
  record_result("virtio-blk enumeration", count >= expected_min);
}

static void
test_procfs_visible(void)
{
  struct vblk_counters counters;

  record_result("/proc/vblk_flush readable", read_vblk_counters(&counters) == 0);
}

static void
test_mount_io_cycles(int *units, int nunits)
{
  struct vblk_counters before;
  struct vblk_counters after;
  int i;
  int unit;
  int progressed;

  if(nunits == 0){
    record_skip("mount/write/read/umount cycles", "no virtio-blk disks detected");
    return;
  }

  if(read_vblk_counters(&before) < 0){
    record_skip("counter progression", "/proc/vblk_flush unavailable");
    return;
  }

  for(i = 0; i < nunits; i++){
    int rc;

    unit = units[i];
    printf(1, "Testing vd%c cycle 0\n", 'a' + unit);
    rc = run_roundtrip(unit, 0);
    if(rc != VBLKTEST_OK)
      printf(1, "  failure stage: %s\n", roundtrip_error_name(rc));
    record_result("mount/write/read/umount cycle", rc == VBLKTEST_OK);

    printf(1, "Testing vd%c cycle 1\n", 'a' + unit);
    rc = run_roundtrip(unit, 1);
    if(rc != VBLKTEST_OK)
      printf(1, "  failure stage: %s\n", roundtrip_error_name(rc));
    record_result("repeat mount/write/read/umount cycle", rc == VBLKTEST_OK);
  }

  if(read_vblk_counters(&after) < 0){
    record_result("counter progression", 0);
    return;
  }

  progressed = 0;
  for(i = 0; i < nunits; i++){
    unit = units[i];
    if(before.ok[unit] >= 0 && after.ok[unit] > before.ok[unit])
      progressed = 1;
  }

  record_result("virtio-blk ok counter increased", progressed);
}

int
main(int argc, char *argv[])
{
  int units[VBLKTEST_MAX_DISKS];
  int nunits;
  int expected_min;

  expected_min = 2;
  if(argc > 2){
    printf(2, "usage: vblktest [expected-min-disks]\n");
    exit();
  }
  if(argc == 2)
    expected_min = atoi(argv[1]);
  if(expected_min < 0)
    expected_min = 0;

  printf(1, "Virtio Block Regression Suite\n");
  printf(1, "============================\n");

  test_enumeration(expected_min, units, &nunits);
  test_procfs_visible();
  test_mount_io_cycles(units, nunits);

  printf(1, "\n============================\n");
  printf(1, "Results: %d passed, %d failed, %d skipped\n", passed, failed, skipped);
  exit();
}