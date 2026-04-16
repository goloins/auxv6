#include "types.h"
#include "auxv6/user.h"
#include "auxv6/bzip2.h"
#include "fcntl.h"
#include "unistd.h"
#include "errno.h"
#include "stdlib.h"
#include "string.h"
#include "stdio.h"

static void
usage(void)
{
  dprintf(2, "usage: bzip2 [-c] [-k] [-1 to -9] [file ...]\n");
  exit(1);
}

/* Minimal bzip2 compression - delegates to libc support */
static int
bzip2_stream(int in_fd, int out_fd, const char *name, int blocksize)
{
  (void)name;
  (void)blocksize;
  if(aux_bzip2_deflate_fd(in_fd, out_fd) < 0) {
    if(errno == EOPNOTSUPP)
      dprintf(2, "bzip2: bzip2 support is not available in this build\n");
    else
      dprintf(2, "bzip2: compression failed\n");
    return -1;
  }
  return 0;
}

int
main(int argc, char *argv[])
{
  int i;
  int to_stdout;
  int keep_input;
  int blocksize;
  int rc;
  int first_file;

  to_stdout = 0;
  keep_input = 0;
  blocksize = 9;  /* Default block size: 900KB */
  rc = 0;

  for(i = 1; i < argc; i++) {
    if(argv[i][0] != '-' || argv[i][1] == 0)
      break;
    if(strcmp(argv[i], "--") == 0) {
      i++;
      break;
    }

    {
      int j;
      for(j = 1; argv[i][j]; j++) {
        if(argv[i][j] == 'c')
          to_stdout = 1;
        else if(argv[i][j] == 'k')
          keep_input = 1;
        else if(argv[i][j] >= '1' && argv[i][j] <= '9')
          blocksize = argv[i][j] - '0';
        else
          usage();
      }
    }
  }

  first_file = i;

  if(first_file >= argc) {
    if(bzip2_stream(0, 1, 0, blocksize) < 0)
      return 1;
    return 0;
  }

  for(i = first_file; i < argc; i++) {
    int in_fd;

    in_fd = open(argv[i], O_RDONLY);
    if(in_fd < 0) {
      dprintf(2, "bzip2: %s: cannot open\n", argv[i]);
      rc = 1;
      continue;
    }

    if(to_stdout) {
      if(bzip2_stream(in_fd, 1, argv[i], blocksize) < 0)
        rc = 1;
      close(in_fd);
    } else {
      char out_path[256];
      int out_fd;

      snprintf(out_path, sizeof(out_path), "%s.bz2", argv[i]);

      out_fd = open(out_path, O_CREATE | O_WRONLY | O_TRUNC);
      if(out_fd < 0) {
        dprintf(2, "bzip2: %s: cannot create %s\n", argv[i], out_path);
        close(in_fd);
        rc = 1;
        continue;
      }

      if(bzip2_stream(in_fd, out_fd, argv[i], blocksize) < 0) {
        rc = 1;
        close(out_fd);
        close(in_fd);
        unlink(out_path);
        continue;
      }

      close(out_fd);
      close(in_fd);

      if(!keep_input)
        unlink(argv[i]);
    }
  }

  return rc;
}
