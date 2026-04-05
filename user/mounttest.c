#include "types.h"
#include "auxv6/user.h"
#include "stat.h"
#include "fcntl.h"

#define TMPFS_MOUNT "/tmp"
#define TMPFS_SIZE_STR "size=65536"
#define TMPFS_TEST_FILE "/tmp/tmpfstest.bin"
#define TMPFS_TEST_WRITE 60000
#define TMPFS_OVERWRITE 70000

static char writebuf[TMPFS_OVERWRITE];

int
main(int argc, char *argv[])
{
  struct mountinfo entries[MOUNTINFO_MAX];
  struct stat st;
  int n, i;

  dprintf(1, "=== Mount Test Utility ===\n");
  dprintf(1, "\nCurrent mounts before mount(2):\n");
  n = mountinfo(entries, MOUNTINFO_MAX);
  dprintf(1, "Found %d mounts\n", n);
  for(i = 0; i < n; i++) {
    dprintf(1, "  %s (dev=%d flags=%d)\n", entries[i].path, entries[i].dev, entries[i].flags);
  }

  dprintf(1, "\nAttempting to mount procfs at /proc...\n");
  int ret = mount("/proc", "procfs", 0, 0, 0);
  if(ret < 0) {
    dprintf(1, "mount() failed with error %d\n", ret);
  } else {
    dprintf(1, "mount() succeeded!\n");

    dprintf(1, "\nCurrent mounts after mount(2):\n");
    n = mountinfo(entries, MOUNTINFO_MAX);
    dprintf(1, "Found %d mounts\n", n);
    for(i = 0; i < n; i++) {
      dprintf(1, "  %s (dev=%d flags=%d fstype=%s)\n", 
             entries[i].path, entries[i].dev, entries[i].flags, entries[i].fstype);
    }
  }

  dprintf(1, "\nAttempting to mount tmpfs at %s...\n", TMPFS_MOUNT);
  if(stat(TMPFS_MOUNT, &st) < 0)
    mkdir(TMPFS_MOUNT);
  ret = mount(TMPFS_MOUNT, "tmpfs", 0, TMPFS_SIZE_STR, strlen(TMPFS_SIZE_STR));
  if(ret < 0) {
    dprintf(1, "tmpfs mount failed\n");
  } else {
    int fd;
    int n;

    memset(writebuf, 'A', sizeof(writebuf));
    unlink(TMPFS_TEST_FILE);
    fd = open(TMPFS_TEST_FILE, O_CREATE | O_WRONLY | O_TRUNC);
    if(fd < 0){
      dprintf(1, "tmpfs open failed\n");
    } else {
      n = write(fd, writebuf, TMPFS_TEST_WRITE);
      if(n != TMPFS_TEST_WRITE)
        dprintf(1, "tmpfs short write (%d)\n", n);
      n = write(fd, writebuf, TMPFS_OVERWRITE);
      if(n >= TMPFS_OVERWRITE)
        dprintf(1, "tmpfs overflow write unexpectedly succeeded\n");
      close(fd);
    }

    umount(TMPFS_MOUNT);
  }

  dprintf(1, "\n=== Test Complete ===\n");
  exit(0);
}
