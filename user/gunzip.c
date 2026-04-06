#include "types.h"
#include "auxv6/user.h"
#include "auxv6/gzip.h"
#include "fcntl.h"
#include "unistd.h"

static void
usage(void)
{
  dprintf(2, "usage: gunzip [-c] [-k] [file ...]\n");
  exit(1);
}

static int
gunzip_stream(int in_fd, int out_fd, const char *name)
{
  if(aux_gzip_inflate_fd(in_fd, out_fd) < 0) {
    if(name)
      dprintf(2, "gunzip: %s: invalid or unsupported gzip stream\n", name);
    else
      dprintf(2, "gunzip: invalid or unsupported gzip stream\n");
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
    if(gunzip_stream(0, 1, 0) < 0)
      return 1;
    return 0;
  }

  for(i = first_file; i < argc; i++) {
    int in_fd;

    in_fd = open(argv[i], O_RDONLY);
    if(in_fd < 0) {
      dprintf(2, "gunzip: %s: cannot open\n", argv[i]);
      rc = 1;
      continue;
    }

    if(to_stdout) {
      if(gunzip_stream(in_fd, 1, argv[i]) < 0)
        rc = 1;
    } else {
      char out_path[256];
      int out_fd;

      if(aux_gzip_output_name(argv[i], out_path, sizeof(out_path)) < 0) {
        dprintf(2, "gunzip: %s: output path too long\n", argv[i]);
        close(in_fd);
        rc = 1;
        continue;
      }

      out_fd = open(out_path, O_CREATE | O_WRONLY | O_TRUNC);
      if(out_fd < 0) {
        dprintf(2, "gunzip: %s: cannot create %s\n", argv[i], out_path);
        close(in_fd);
        rc = 1;
        continue;
      }

      if(gunzip_stream(in_fd, out_fd, argv[i]) < 0) {
        unlink(out_path);
        rc = 1;
      } else if(!keep_input) {
        if(unlink(argv[i]) < 0)
          dprintf(2, "gunzip: warning: unable to remove %s\n", argv[i]);
      }

      close(out_fd);
    }

    close(in_fd);
  }

  return rc;
}
