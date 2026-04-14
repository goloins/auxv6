#include "types.h"
#include "sys/stat.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "string.h"

static int
touch_one(const char *path, int no_create)
{
  struct stat st;

  if(stat(path, &st) < 0) {
    int fd;
    if(no_create)
      return 0;
    fd = open(path, O_CREATE | O_WRONLY);
    if(fd < 0)
      return -1;
    close(fd);
    return 0;
  }

  if(S_ISREG(st.st_mode)) {
    int fd;
    fd = open(path, O_RDWR);
    if(fd < 0)
      return -1;

    if(st.st_size > 0) {
      char ch;
      if(read(fd, &ch, 1) == 1) {
        if(lseek(fd, 0, SEEK_SET) < 0 || write(fd, &ch, 1) != 1) {
          close(fd);
          return -1;
        }
      }
    } else {
      char ch = '\n';
      if(write(fd, &ch, 1) != 1 || ftruncate(fd, 0) < 0) {
        close(fd);
        return -1;
      }
    }

    close(fd);
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  int i;
  int no_create;
  int rc;

  if(argc < 2) {
    dprintf(2, "usage: touch [-c] file...\n");
    return 1;
  }

  no_create = 0;
  rc = 0;

  i = 1;
  if(strcmp(argv[i], "-c") == 0) {
    no_create = 1;
    i++;
  }

  if(i >= argc) {
    dprintf(2, "usage: touch [-c] file...\n");
    return 1;
  }

  for(; i < argc; i++) {
    if(touch_one(argv[i], no_create) < 0) {
      dprintf(2, "touch: %s: failed\n", argv[i]);
      rc = 1;
    }
  }

  return rc;
}
