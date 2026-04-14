#include "types.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "unistd.h"
#include "errno.h"
#include "stdlib.h"
#include "string.h"
#include "stdio.h"

static void
usage(void)
{
  dprintf(2, "usage: bunzip2 [-c] [-k] [file ...]\n");
  exit(1);
}

/* Minimal bzip2 decompression - delegates to/uses libc support */
static int
bunzip2_stream(int in_fd, int out_fd, const char *name)
{
  dprintf(2, "bunzip2: bzip2 decompression not yet implemented\n");
  return -1;
}

int
main(int argc, char *argv[])
{
  int i;
  int to_stdout;
  int keep_input;
  int rc;
  int first_file;

  to_stdout = 0;
  keep_input = 0;
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
        else
          usage();
      }
    }
  }

  first_file = i;

  if(first_file >= argc) {
    if(bunzip2_stream(0, 1, 0) < 0)
      return 1;
    return 0;
  }

  for(i = first_file; i < argc; i++) {
    int in_fd;

    in_fd = open(argv[i], O_RDONLY);
    if(in_fd < 0) {
      dprintf(2, "bunzip2: %s: cannot open\n", argv[i]);
      rc = 1;
      continue;
    }

    if(to_stdout) {
      if(bunzip2_stream(in_fd, 1, argv[i]) < 0)
        rc = 1;
      close(in_fd);
    } else {
      char out_path[256];
      int out_fd;

      if(strlen(argv[i]) >= 4 && strcmp(argv[i] + strlen(argv[i]) - 4, ".bz2") == 0) {
        snprintf(out_path, sizeof(out_path), "%.*s", (int)(strlen(argv[i]) - 4), argv[i]);
      } else if(strlen(argv[i]) >= 8 && strcmp(argv[i] + strlen(argv[i]) - 8, ".tar.bz2") == 0) {
        snprintf(out_path, sizeof(out_path), "%.*s.tar", (int)(strlen(argv[i]) - 4), argv[i]);
      } else {
        snprintf(out_path, sizeof(out_path), "%s.out", argv[i]);
      }

      out_fd = open(out_path, O_CREATE | O_WRONLY | O_TRUNC);
      if(out_fd < 0) {
        dprintf(2, "bunzip2: %s: cannot create %s\n", argv[i], out_path);
        close(in_fd);
        rc = 1;
        continue;
      }

      if(bunzip2_stream(in_fd, out_fd, argv[i]) < 0) {
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
