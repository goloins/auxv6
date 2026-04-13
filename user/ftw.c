#include "types.h"
#include "ftw.h"
#include "dirent.h"
#include "string.h"
#include "stdlib.h"
#include "auxv6/user.h"

static int
basename_offset(const char *path)
{
  int i;
  int last;

  last = 0;
  for(i = 0; path[i]; i++)
    if(path[i] == '/')
      last = i + 1;
  return last;
}

static int
path_join(const char *base, const char *name, char **out)
{
  int blen;
  int nlen;
  int need_slash;
  char *buf;

  blen = strlen(base);
  nlen = strlen(name);
  need_slash = (blen > 0 && base[blen - 1] != '/');

  buf = (char*)malloc(blen + need_slash + nlen + 1);
  if(buf == 0)
    return -1;

  memmove(buf, base, blen);
  if(need_slash)
    buf[blen++] = '/';
  memmove(buf + blen, name, nlen);
  buf[blen + nlen] = 0;

  *out = buf;
  return 0;
}

static int
walk_one(const char *path,
         int (*fn)(const char*, const struct stat*, int, struct FTW*),
         int flags,
         int root_dev,
         int level)
{
  struct stat st;
  int rc;
  int typeflag;
  struct FTW fb;

  rc = (flags & FTW_PHYS) ? lstat(path, &st) : stat(path, &st);
  if(rc < 0) {
    fb.base = basename_offset(path);
    fb.level = level;
    return fn(path, 0, FTW_NS, &fb);
  }

  fb.base = basename_offset(path);
  fb.level = level;

  if(st.st_type == T_SYMLINK || (st.st_mode & M_IFMT) == M_IFLNK)
    typeflag = FTW_SL;
  else if(st.st_type == T_DIR || (st.st_mode & M_IFMT) == M_IFDIR)
    typeflag = FTW_D;
  else
    typeflag = FTW_F;

  if((flags & FTW_MOUNT) && level > 0 && st.st_dev != root_dev) {
    if(typeflag == FTW_D)
      return fn(path, &st, (flags & FTW_DEPTH) ? FTW_DP : FTW_D, &fb);
    return fn(path, &st, typeflag, &fb);
  }

  if(typeflag != FTW_D)
    return fn(path, &st, typeflag, &fb);

  if(!(flags & FTW_DEPTH)) {
    rc = fn(path, &st, FTW_D, &fb);
    if(rc != 0)
      return rc;
  }

  {
    DIR *dp;
    struct dirent *de;

    dp = opendir(path);
    if(dp == 0) {
      return fn(path, &st, FTW_DNR, &fb);
    }

    while((de = readdir(dp)) != 0) {
      char *child;

      if(strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
        continue;

      if(path_join(path, de->d_name, &child) < 0) {
        closedir(dp);
        return -1;
      }

      rc = walk_one(child, fn, flags, root_dev, level + 1);
      free(child);
      if(rc != 0) {
        closedir(dp);
        return rc;
      }
    }

    closedir(dp);
  }

  if(flags & FTW_DEPTH)
    return fn(path, &st, FTW_DP, &fb);

  return 0;
}

int
nftw(const char *path,
     int (*fn)(const char *fpath, const struct stat *sb,
               int typeflag, struct FTW *ftwbuf),
     int fd_limit,
     int flags)
{
  struct stat st;
  int rc;

  (void)fd_limit;

  if(path == 0 || fn == 0)
    return -1;

  rc = (flags & FTW_PHYS) ? lstat(path, &st) : stat(path, &st);
  if(rc < 0) {
    struct FTW fb;
    fb.base = basename_offset(path);
    fb.level = 0;
    return fn(path, 0, FTW_NS, &fb);
  }

  return walk_one(path, fn, flags, st.st_dev, 0);
}