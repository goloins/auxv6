/*
 * tempfile.c - temporary-file and temporary-directory helpers
 */

#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "errno.h"
#include "stdlib.h"
#include "auxv6/user.h"

#define TEMP_ATTEMPTS 256

static const char tempfile_alphabet[] =
  "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static uint tempfile_counter;

static char*
tempfile_find_xs(char *template)
{
  char *end;
  char *start;

  if(template == 0) {
    errno = EINVAL;
    return 0;
  }

  end = template + strlen(template);
  start = end;
  while(start > template && start[-1] == 'X')
    start--;

  if(end - start < 6) {
    errno = EINVAL;
    return 0;
  }

  return start;
}

static uint
tempfile_seed(void)
{
  tempfile_counter++;
  return (uint)getpid() ^ ((uint)uptime() << 8) ^ tempfile_counter;
}

static void
tempfile_fill(char *xs, int nxs, uint seed)
{
  int i;

  for(i = 0; i < nxs; i++) {
    xs[i] = tempfile_alphabet[seed % (sizeof(tempfile_alphabet) - 1)];
    seed = seed / (sizeof(tempfile_alphabet) - 1) + 1;
  }
}

static int
tempfile_exists(const char *path)
{
  struct stat st;

  errno = 0;
  if(lstat(path, &st) == 0)
    return 1;
  if(errno == ENOENT)
    return 0;
  return -1;
}

char*
mktemp(char *template)
{
  char *xs;
  int nxs;
  int i;

  xs = tempfile_find_xs(template);
  if(xs == 0)
    return 0;

  nxs = strlen(xs);
  for(i = 0; i < TEMP_ATTEMPTS; i++) {
    int exists;

    tempfile_fill(xs, nxs, tempfile_seed() + (uint)i);
    exists = tempfile_exists(template);
    if(exists == 0)
      return template;
    if(exists < 0)
      return 0;
  }

  template[0] = '\0';
  errno = EEXIST;
  return 0;
}

int
mkostemp(char *template, int flags)
{
  char *xs;
  int nxs;
  int oflags;
  int fd;
  int i;

  if((flags & ~(O_APPEND | O_NONBLOCK | O_CLOEXEC)) != 0) {
    errno = EINVAL;
    return -1;
  }

  xs = tempfile_find_xs(template);
  if(xs == 0)
    return -1;

  nxs = strlen(xs);
  oflags = O_RDWR | O_CREATE | O_EXCL | (flags & (O_APPEND | O_NONBLOCK));

  for(i = 0; i < TEMP_ATTEMPTS; i++) {
    tempfile_fill(xs, nxs, tempfile_seed() + (uint)i);
    fd = open(template, oflags);
    if(fd >= 0) {
      if(flags & O_CLOEXEC)
        fcntl(fd, F_SETFD, FD_CLOEXEC);
      return fd;
    }
    if(errno != EEXIST)
      return -1;
  }

  errno = EEXIST;
  return -1;
}

int
mkstemp(char *template)
{
  return mkostemp(template, 0);
}

char*
mkdtemp(char *template)
{
  char *xs;
  int nxs;
  int i;

  xs = tempfile_find_xs(template);
  if(xs == 0)
    return 0;

  nxs = strlen(xs);
  for(i = 0; i < TEMP_ATTEMPTS; i++) {
    tempfile_fill(xs, nxs, tempfile_seed() + (uint)i);
    if(mkdir(template) == 0)
      return template;
    if(errno != EEXIST)
      return 0;
  }

  errno = EEXIST;
  return 0;
}