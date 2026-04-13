#include "auxv6/user.h"
#include "fcntl.h"

static int
sum_fd(int fd, const char *name)
{
  uchar buf[1024];
  uint n;
  uint blocks;
  uint s;
  int r;
  int i;

  n = 0;
  s = 0;
  while((r = read(fd, buf, sizeof(buf))) > 0) {
    n += (uint)r;
    for(i = 0; i < r; i++) {
      s = ((s >> 1) | ((s & 1) << 15));
      s = (s + buf[i]) & 0xffff;
    }
  }
  if(r < 0)
    return -1;

  blocks = (n + 1023) / 1024;
  if(name)
    dprintf(1, "%u %u %s\n", s, blocks, name);
  else
    dprintf(1, "%u %u\n", s, blocks);
  return 0;
}

int
main(int argc, char *argv[])
{
  int rc;
  int i;

  rc = 0;
  if(argc == 1) {
    if(sum_fd(0, 0) < 0)
      return 1;
    return 0;
  }

  for(i = 1; i < argc; i++) {
    int fd;
    if(strcmp(argv[i], "-") == 0) {
      if(sum_fd(0, 0) < 0)
        rc = 1;
      continue;
    }
    fd = open(argv[i], O_RDONLY);
    if(fd < 0) {
      dprintf(2, "sum: %s: cannot open\n", argv[i]);
      rc = 1;
      continue;
    }
    if(sum_fd(fd, argv[i]) < 0) {
      dprintf(2, "sum: %s: read error\n", argv[i]);
      rc = 1;
    }
    close(fd);
  }

  return rc;
}
