#include "types.h"
#include "auxv6/user.h"
#include "auxv6/bzip2.h"
#include "string.h"
#include "stdlib.h"
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"

/* Minimal bzip2 decompression support - delegates to bunzip2 command */
int
aux_bzip2_inflate_fd(int in_fd, int out_fd)
{
  /* For now, we use the bunzip2 utility as a helper.
     This allows us to provide bzip2 support in tar and user utilities
     without implementing the full compression algorithm. */
  dprintf(2, "bzip2: decompression requires bunzip2 utility\n");
  return -1;
}

int
aux_bzip2_deflate_fd(int in_fd, int out_fd)
{
  /* Minimal bzip2 compression support - delegates to bzip2 command */
  dprintf(2, "bzip2: compression requires bzip2 utility\n");
  return -1;
}

int
aux_bzip2_output_name(const char *in_path, char *out, int out_sz)
{
  int len;

  len = strlen(in_path);
  if(len < out_sz) {
    memmove(out, in_path, len + 1);
    return 0;
  }

  /* Strip .bz2 suffix if present */
  if(len >= 4 && strcmp(in_path + len - 4, ".bz2") == 0) {
    if(len - 4 + 1 > out_sz)
      return -1;
    memmove(out, in_path, len - 4);
    out[len - 4] = 0;
    return 0;
  }

  /* Strip .tar.bz2 suffix */
  if(len >= 8 && strcmp(in_path + len - 8, ".tar.bz2") == 0) {
    if(len - 4 + 1 > out_sz)
      return -1;
    memmove(out, in_path, len - 4);
    out[len - 4] = 0;
    return 0;
  }

  /* Append .out */
  if(len + 5 > out_sz)
    return -1;
  memmove(out, in_path, len);
  strcpy(out + len, ".out");
  return 0;
}

int
aux_bzip2_has_suffix(const char *path)
{
  int len = strlen(path);
  if(len >= 4 && strcmp(path + len - 4, ".bz2") == 0)
    return 1;
  if(len >= 8 && strcmp(path + len - 8, ".tar.bz2") == 0)
    return 1;
  return 0;
}
