/*
 * isotest - Test ISO filesystem and loop device functionality
 *
 * This program tests:
 * - Loop device setup
 * - ISO filesystem mounting
 * - Directory listing
 * - File reading
 * - Clean unmounting
 */

#include "types.h"
#include "stat.h"
#include "fs.h"
#include "auxv6/user.h"
#include "fcntl.h"

#define TEST_ISO "/test.iso"
#define MOUNT_POINT "/mnt/iso"
#define LOOP_DEV 0

static int passed = 0;
static int failed = 0;

static void
test_result(char *name, int success)
{
  if(success){
    printf(1, "[PASS] %s\n", name);
    passed++;
  } else {
    printf(1, "[FAIL] %s\n", name);
    failed++;
  }
}

static int
file_exists(char *path)
{
  struct stat st;
  return stat(path, &st) >= 0;
}

static int
is_directory(char *path)
{
  struct stat st;
  if(stat(path, &st) < 0)
    return 0;
  return st.st_type == T_DIR;
}

static int
read_file_content(char *path, char *buf, int maxlen)
{
  int fd, n;
  
  fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;
  
  n = read(fd, buf, maxlen - 1);
  close(fd);
  
  if(n < 0)
    return -1;
  
  buf[n] = 0;
  return n;
}

static void
test_loop_setup(void)
{
  int r;
  
  printf(1, "\n=== Testing Loop Device Setup ===\n");
  
  /* Check if test ISO exists */
  if(!file_exists(TEST_ISO)){
    printf(2, "Warning: %s not found, some tests will be skipped\n", TEST_ISO);
    printf(2, "Create a test ISO first using: mkisofs -o /test.iso /some/directory\n");
    return;
  }
  
  /* Setup loop device */
  r = loopsetup(LOOP_DEV, TEST_ISO, 0, 0);
  test_result("loopsetup()", r >= 0);
  
  if(r < 0){
    printf(2, "Cannot continue tests without loop device\n");
    return;
  }
  
  /* Check loop status */
  uint inum, offset, nblocks, flags;
  r = loopstatus(LOOP_DEV, &inum, &offset, &nblocks, &flags);
  test_result("loopstatus()", r > 0);
  printf(1, "  Loop device has %d blocks, offset %d, mounted=%s, backing inode %d\n",
         nblocks, offset, (flags & LOOP_STATUS_MOUNTED) ? "yes" : "no", inum);
}

static void
test_iso_mount(void)
{
  int r;
  
  printf(1, "\n=== Testing ISO Filesystem Mount ===\n");
  
  /* Create mount point */
  if(!is_directory(MOUNT_POINT)){
    r = mkdir(MOUNT_POINT);
    if(r < 0 && !is_directory(MOUNT_POINT)){
      printf(2, "Cannot create mount point %s\n", MOUNT_POINT);
      return;
    }
  }
  
  /* Mount ISO */
  /* MNT_HASDEV | (device << 16) to specify loop device */
  int loop_devnum = 40 + LOOP_DEV;  /* LOOP_DEV_BASE + loopnum */
  int flags = 0x10000 | (loop_devnum << 16);  /* MNT_HASDEV with device number */
  
  r = mount(MOUNT_POINT, "isofs", flags);
  test_result("mount(isofs)", r >= 0);
  
  if(r < 0){
    printf(2, "Mount failed, cannot continue directory tests\n");
    return;
  }
  
  /* Check mount point is now a directory */
  test_result("mount point accessible", is_directory(MOUNT_POINT));
}

static void
test_directory_operations(void)
{
  int fd;
  struct dirent de;
  int count = 0;
  
  printf(1, "\n=== Testing Directory Operations ===\n");
  
  /* Open and read root directory */
  fd = open(MOUNT_POINT, O_RDONLY);
  test_result("open(mount_point)", fd >= 0);
  
  if(fd < 0)
    return;
  
  printf(1, "Directory listing of %s:\n", MOUNT_POINT);
  while(read(fd, &de, sizeof(de)) == sizeof(de)){
    if(de.inum == 0)
      continue;
    printf(1, "  %s (inum=%d)\n", de.name, de.inum);
    count++;
  }
  close(fd);
  
  test_result("directory has entries", count > 0);
  printf(1, "  Found %d entries\n", count);
}

static void
test_file_reading(void)
{
  char buf[256];
  char path[64];
  int n;
  
  printf(1, "\n=== Testing File Reading ===\n");
  
  /* Try to read a file called README if it exists */
  strcpy(path, MOUNT_POINT);
  strcpy(path + strlen(MOUNT_POINT), "/README");
  
  if(file_exists(path)){
    n = read_file_content(path, buf, sizeof(buf));
    test_result("read README", n > 0);
    if(n > 0){
      printf(1, "  First %d bytes: %.50s%s\n", n, buf, n > 50 ? "..." : "");
    }
  } else {
    printf(1, "  No README file found (not an error)\n");
  }
  
  /* Try to read a file called test.txt if it exists */
  strcpy(path, MOUNT_POINT);
  strcpy(path + strlen(MOUNT_POINT), "/TEST.TXT");
  
  if(file_exists(path)){
    n = read_file_content(path, buf, sizeof(buf));
    test_result("read TEST.TXT", n >= 0);
    if(n > 0){
      printf(1, "  Content: %s\n", buf);
    }
  }
}

static void
test_cleanup(void)
{
  int r;
  
  printf(1, "\n=== Cleanup ===\n");
  
  /* Unmount */
  r = umount(MOUNT_POINT);
  test_result("umount()", r >= 0);
  
  /* Teardown loop */
  r = loopteardown(LOOP_DEV);
  test_result("loopteardown()", r >= 0);
}

int
main(int argc, char *argv[])
{
  printf(1, "ISO Filesystem Test Suite\n");
  printf(1, "==========================\n");
  
  if(argc > 1 && strcmp(argv[1], "-h") == 0){
    printf(1, "Usage: isotest\n");
    printf(1, "\nTests loop device and ISO filesystem functionality.\n");
    printf(1, "Expects %s to exist (create with mkisofs on host).\n", TEST_ISO);
    exit();
  }
  
  test_loop_setup();
  test_iso_mount();
  test_directory_operations();
  test_file_reading();
  test_cleanup();
  
  printf(1, "\n==========================\n");
  printf(1, "Results: %d passed, %d failed\n", passed, failed);
  
  if(failed > 0)
    printf(1, "\nSome tests failed. Check output above.\n");
  else
    printf(1, "\nAll tests passed!\n");
  
  exit();
}
