#include "types.h"
#include "stat.h"
#include "fs.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "string.h"

#define AHCI_UNIT 3
#define AHCI_MOUNT "/mnt/ahcitest"
#define AHCI_INJ_MOUNT "/mnt/ahciinj"

static int passed = 0;
static int failed = 0;
static int skipped = 0;

static void
record_result(char *name, int ok)
{
  if(ok){
    dprintf(1, "[PASS] %s\n", name);
    passed++;
  } else {
    dprintf(1, "[FAIL] %s\n", name);
    failed++;
  }
}

static void
record_skip(char *name, char *reason)
{
  dprintf(1, "[SKIP] %s (%s)\n", name, reason);
  skipped++;
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
contains(const char *hay, const char *needle)
{
  int n;
  int i;

  if(hay == 0 || needle == 0)
    return 0;

  n = strlen(needle);
  if(n == 0)
    return 1;

  for(i = 0; hay[i]; i++){
    if(strncmp(hay + i, needle, n) == 0)
      return 1;
  }
  return 0;
}

static int
read_file(char *path, char *buf, int max)
{
  int fd;
  int n;

  fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;
  n = read(fd, buf, max - 1);
  close(fd);
  if(n < 0)
    return -1;
  buf[n] = 0;
  return n;
}

static int
write_tune(char *line)
{
  int fd;
  int n;

  fd = open("/proc/ahci_tune", O_WRONLY);
  if(fd < 0)
    return -1;
  n = strlen(line);
  if(write(fd, line, n) != n){
    close(fd);
    return -1;
  }
  close(fd);
  return 0;
}

static int
ahci_dev_present(char *buf, int len)
{
  if(read_file("/proc/ahci_tune", buf, len) < 0)
    return 0;
  return contains(buf, "dev=3");
}

static int
test_telemetry(void)
{
  char buf[1024];

  if(read_file("/proc/ahci_tune", buf, sizeof(buf)) < 0){
    record_skip("ahci telemetry", "no /proc/ahci_tune");
    return 0;
  }

  record_result("ahci telemetry has cmd timeout",
                contains(buf, "cmd_timeout_us="));
  record_result("ahci telemetry has device",
                contains(buf, "dev="));
  record_result("ahci telemetry has port",
                contains(buf, "port="));
  return 0;
}

static int
test_mount_persist(void)
{
  char buf[1024];
  char path[64];
  int flags;
  int fd;
  int n;

  if(!ahci_dev_present(buf, sizeof(buf))){
    record_skip("ahci mount/persist", "dev=3 missing");
    return 0;
  }

  if(ensure_dir(AHCI_MOUNT) < 0){
    record_result("ahci mount/persist", 0);
    return -1;
  }

  flags = MNT_MAKEDEV(HD_DISK_DEV(AHCI_UNIT));
  if(mount(AHCI_MOUNT, "ext2", flags, 0, 0) < 0){
    record_result("ahci mount", 0);
    return -1;
  }
  record_result("ahci mount", 1);

  strcpy(path, AHCI_MOUNT);
  strcat(path, "/sentinel");
  unlink(path);

  fd = open(path, O_CREATE | O_RDWR | O_TRUNC);
  if(fd < 0){
    record_result("ahci write", 0);
    umount(AHCI_MOUNT);
    return -1;
  }

  n = write(fd, "ahci-ok\n", 8);
  close(fd);
  if(n != 8){
    record_result("ahci write", 0);
    umount(AHCI_MOUNT);
    return -1;
  }
  record_result("ahci write", 1);

  fd = open(path, O_RDONLY);
  if(fd < 0){
    record_result("ahci read", 0);
    umount(AHCI_MOUNT);
    return -1;
  }
  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(n <= 0){
    record_result("ahci read", 0);
    umount(AHCI_MOUNT);
    return -1;
  }
  record_result("ahci read", 1);
  buf[n] = 0;
  record_result("ahci readback", contains(buf, "ahci-ok"));

  if(umount(AHCI_MOUNT) < 0)
    record_result("ahci umount", 0);
  else
    record_result("ahci umount", 1);

  if(mount(AHCI_MOUNT, "ext2", flags, 0, 0) < 0){
    record_result("ahci remount", 0);
    return -1;
  }
  record_result("ahci remount", 1);

  fd = open(path, O_RDONLY);
  if(fd < 0){
    record_result("ahci persisted read", 0);
    umount(AHCI_MOUNT);
    return -1;
  }
  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(n <= 0){
    record_result("ahci persisted read", 0);
    umount(AHCI_MOUNT);
    return -1;
  }
  record_result("ahci persisted read", 1);
  buf[n] = 0;
  record_result("ahci persisted data", contains(buf, "ahci-ok"));

  if(umount(AHCI_MOUNT) < 0)
    record_result("ahci umount", 0);
  else
    record_result("ahci umount", 1);

  return 0;
}

static void
reset_injector(void)
{
  write_tune("test_fail_mode=none\n");
  write_tune("test_fail_count=0\n");
}

static int
test_injection(void)
{
  char buf[1024];
  char path[64];
  int flags;
  int fd;
  int n;

  if(!ahci_dev_present(buf, sizeof(buf))){
    record_skip("ahci injection", "dev=3 missing");
    return 0;
  }

  if(write_tune("rw_retries=2\n") < 0 ||
     write_tune("reset_stats=1\n") < 0){
    record_result("ahci injection setup", 0);
    return -1;
  }

  if(write_tune("test_fail_mode=timeout\n") < 0 ||
     write_tune("test_fail_count=1\n") < 0){
    record_result("ahci injection setup", 0);
    reset_injector();
    return -1;
  }

  if(ensure_dir(AHCI_INJ_MOUNT) < 0){
    record_result("ahci injection mount", 0);
    reset_injector();
    return -1;
  }

  flags = MNT_MAKEDEV(HD_DISK_DEV(AHCI_UNIT));
  if(mount(AHCI_INJ_MOUNT, "ext2", flags, 0, 0) < 0){
    record_result("ahci injection mount", 0);
    reset_injector();
    return -1;
  }
  record_result("ahci injection mount", 1);

  strcpy(path, AHCI_INJ_MOUNT);
  strcat(path, "/inj1");
  unlink(path);

  fd = open(path, O_CREATE | O_RDWR | O_TRUNC);
  if(fd < 0){
    record_result("ahci injection write", 0);
    umount(AHCI_INJ_MOUNT);
    reset_injector();
    return -1;
  }

  n = write(fd, "inject-ok\n", 10);
  close(fd);
  if(n != 10){
    record_result("ahci injection write", 0);
    umount(AHCI_INJ_MOUNT);
    reset_injector();
    return -1;
  }
  record_result("ahci injection write", 1);

  if(read_file("/proc/ahci_tune", buf, sizeof(buf)) < 0){
    record_result("ahci injection telemetry", 0);
    umount(AHCI_INJ_MOUNT);
    reset_injector();
    return -1;
  }
  record_result("ahci injection telemetry", 1);

  record_result("ahci injection class",
                contains(buf, "last_fail_class=1"));
  record_result("ahci injection drained",
                contains(buf, "test_fail_remaining=0"));
  record_result("ahci injection recovered",
                contains(buf, "recover_fail=0"));

  if(umount(AHCI_INJ_MOUNT) < 0)
    record_result("ahci injection umount", 0);
  else
    record_result("ahci injection umount", 1);

  reset_injector();
  return 0;
}

int
main(int argc, char **argv)
{
  int do_inject;

  do_inject = 0;
  if(argc > 1 && strcmp(argv[1], "-i") == 0)
    do_inject = 1;

  dprintf(1, "AHCI Test Suite\n");
  dprintf(1, "==============\n");

  test_telemetry();
  test_mount_persist();
  if(do_inject)
    test_injection();
  else
    record_skip("ahci injection", "use -i to enable");

  dprintf(1, "==============\n");
  dprintf(1, "Results: %d passed, %d failed, %d skipped\n",
          passed, failed, skipped);
  exit(0);
}
