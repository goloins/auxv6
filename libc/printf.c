#include "types.h"
#include "stdio.h"
#include "auxv6/user.h"

int
vdprintf(int fd, const char *fmt, va_list ap)
{
  char small[256];
  char *buf;
  int n;
  int off;
  int rc;
  va_list ap_copy;

  va_copy(ap_copy, ap);
  n = vsnprintf(small, sizeof(small), fmt, ap_copy);
  va_end(ap_copy);
  if(n < 0)
    return -1;

  buf = small;
  if(n >= (int)sizeof(small)) {
    buf = (char*)malloc((size_t)n + 1);
    if(buf == 0)
      return -1;
    va_copy(ap_copy, ap);
    vsnprintf(buf, (size_t)n + 1, fmt, ap_copy);
    va_end(ap_copy);
  }

  off = 0;
  while(off < n) {
    rc = write(fd, buf + off, (size_t)(n - off));
    if(rc <= 0) {
      if(buf != small)
        free(buf);
      return -1;
    }
    off += rc;
  }

  if(buf != small)
    free(buf);
  return n;
}

int
dprintf(int fd, const char *fmt, ...)
{
  va_list ap;
  int n;

  va_start(ap, fmt);
  n = vdprintf(fd, fmt, ap);
  va_end(ap);
  return n;
}
