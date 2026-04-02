#include "types.h"
#include "auxv6/user.h"
#include "stat.h"
#include "fcntl.h"

int
main(int argc, char *argv[])
{
  struct mountinfo entries[8];
  int n, i;

  dprintf(1, "=== Mount Test Utility ===\n");
  dprintf(1, "\nCurrent mounts before mount(2):\n");
  n = mountinfo(entries, 8);
  dprintf(1, "Found %d mounts\n", n);
  for(i = 0; i < n; i++) {
    dprintf(1, "  %s (dev=%d flags=%d)\n", entries[i].path, entries[i].dev, entries[i].flags);
  }

  dprintf(1, "\nAttempting to mount procfs at /proc...\n");
  int ret = mount("/proc", "procfs", 0);
  if(ret < 0) {
    dprintf(1, "mount() failed with error %d\n", ret);
  } else {
    dprintf(1, "mount() succeeded!\n");

    dprintf(1, "\nCurrent mounts after mount(2):\n");
    n = mountinfo(entries, 8);
    dprintf(1, "Found %d mounts\n", n);
    for(i = 0; i < n; i++) {
      dprintf(1, "  %s (dev=%d flags=%d fstype=%s)\n", 
             entries[i].path, entries[i].dev, entries[i].flags, entries[i].fstype);
    }
  }

  dprintf(1, "\n=== Test Complete ===\n");
  exit(0);
}
