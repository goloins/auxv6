#include "types.h"
#include "stat.h"
#include "fs.h"
#include "auxv6/user.h"
#include "fcntl.h"

#define LOOP_DEV_BASE 40
#define LOOP_DEV 1
#define TEST_IMG "/tmp/looptest.img"
#define TEST_ISO "/tmp/test.iso"
#define MOUNT_POINT "/mnt/loopbusy"
#define TEST_BLOCKS 4

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
make_test_image(void)
{
  int fd;
  int i;
  char blk[BSIZE];

  for(i = 0; i < BSIZE; i++)
    blk[i] = (char)(i & 0xff);

  fd = open(TEST_IMG, O_CREATE | O_RDWR | O_TRUNC);
  if(fd < 0)
    return -1;

  for(i = 0; i < TEST_BLOCKS; i++){
    if(write(fd, blk, sizeof(blk)) != sizeof(blk)){
      close(fd);
      return -1;
    }
  }

  close(fd);
  return 0;
}

static void
cleanup_loop(void)
{
  umount(MOUNT_POINT);
  loopteardown(LOOP_DEV);
}

static void
test_setup_validation(void)
{
  int r;

  dprintf(1, "\n=== setup validation ===\n");

  cleanup_loop();

  r = loopsetup(LOOP_DEV, TEST_IMG, 1, 0);
  record_result("loopsetup rejects non-aligned offset", r < 0);

  r = loopsetup(LOOP_DEV, TEST_IMG, TEST_BLOCKS * BSIZE, 0);
  record_result("loopsetup rejects offset past EOF", r < 0);

  r = loopsetup(LOOP_DEV, TEST_IMG, BSIZE, TEST_BLOCKS);
  record_result("loopsetup rejects nblocks beyond backing size", r < 0);
}

static void
test_status_metadata(void)
{
  int r;
  uint inum;
  uint offset;
  uint nblocks;
  uint flags;

  dprintf(1, "\n=== status metadata ===\n");

  cleanup_loop();

  r = loopsetup(LOOP_DEV, TEST_IMG, BSIZE, 2);
  record_result("loopsetup succeeds with valid offset/nblocks", r == 0);
  if(r < 0)
    return;

  inum = 0;
  offset = 0;
  nblocks = 0;
  flags = 0;
  r = loopstatus(LOOP_DEV, &inum, &offset, &nblocks, &flags);
  record_result("loopstatus reports active", r > 0);
  record_result("loopstatus reports configured offset", offset == BSIZE);
  record_result("loopstatus reports configured nblocks", nblocks == 2);
  record_result("loopstatus mounted flag clear before mount",
                (flags & LOOP_STATUS_MOUNTED) == 0);

  r = loopteardown(LOOP_DEV);
  record_result("loopteardown succeeds when not mounted", r == 0);
}

static void
test_busy_teardown(void)
{
  struct stat st;
  int r;
  int flags;
  uint inum;
  uint offset;
  uint nblocks;
  uint lflags;

  dprintf(1, "\n=== mounted busy teardown guard ===\n");

  if(stat(TEST_ISO, &st) < 0){
    record_skip("mounted loop teardown guard", "/test.iso missing");
    return;
  }

  cleanup_loop();

  r = loopsetup(LOOP_DEV, TEST_ISO, 0, 0);
  record_result("loopsetup test iso", r == 0);
  if(r < 0)
    return;

  if(stat(MOUNT_POINT, &st) < 0)
    mkdir(MOUNT_POINT);

  flags = MNT_MAKEDEV(LOOP_DEV_BASE + LOOP_DEV);
  r = mount(MOUNT_POINT, "isofs", flags, 0, 0);
  record_result("mount loop-backed iso", r == 0);
  if(r < 0){
    loopteardown(LOOP_DEV);
    return;
  }

  inum = 0;
  offset = 0;
  nblocks = 0;
  lflags = 0;
  r = loopstatus(LOOP_DEV, &inum, &offset, &nblocks, &lflags);
  record_result("loopstatus reports mounted flag", r > 0 &&
                (lflags & LOOP_STATUS_MOUNTED) != 0);

  r = loopteardown(LOOP_DEV);
  record_result("loopteardown blocked while mounted", r < 0);

  r = umount(MOUNT_POINT);
  record_result("umount loop-backed iso", r == 0);

  r = loopteardown(LOOP_DEV);
  record_result("loopteardown succeeds after umount", r == 0);
}

int
main(void)
{
  dprintf(1, "Loop Device Test Suite\n");
  dprintf(1, "======================\n");

  if(make_test_image() < 0){
    dprintf(2, "looptest: failed to create %s\n", TEST_IMG);
    exit(0);
  }

  test_setup_validation();
  test_status_metadata();
  test_busy_teardown();

  unlink(TEST_IMG);

  dprintf(1, "\n======================\n");
  dprintf(1, "Results: %d passed, %d failed, %d skipped\n", passed, failed, skipped);
  exit(0);
}
