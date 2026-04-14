/*
 * path.c - path canonicalization helpers
 */

#include "types.h"
#include "sys/stat.h"
#include "errno.h"
#include "limits.h"
#include "string.h"
#include "stdlib.h"
#include "auxv6/user.h"

static void
path_reset_root(char *buf)
{
  buf[0] = '/';
  buf[1] = '\0';
}

static int
path_is_root(const char *buf)
{
  return buf[0] == '/' && buf[1] == '\0';
}

static int
path_pop_component(char *buf)
{
  char *slash;

  if(path_is_root(buf))
    return 0;

  slash = strrchr(buf, '/');
  if(slash == 0) {
    path_reset_root(buf);
    return 0;
  }

  if(slash == buf)
    buf[1] = '\0';
  else
    *slash = '\0';

  return 0;
}

static int
path_append_component(char *buf, const char *component)
{
  size_t len;
  size_t clen;

  len = strlen(buf);
  clen = strlen(component);

  if(path_is_root(buf)) {
    if(1 + clen + 1 > PATH_MAX) {
      errno = ENAMETOOLONG;
      return -1;
    }
    memmove(buf + 1, component, clen + 1);
    return 0;
  }

  if(len + 1 + clen + 1 > PATH_MAX) {
    errno = ENAMETOOLONG;
    return -1;
  }

  buf[len] = '/';
  memmove(buf + len + 1, component, clen + 1);
  return 0;
}

static int
path_build_candidate(char *dst, const char *base, const char *component)
{
  if(path_is_root(base)) {
    path_reset_root(dst);
    return path_append_component(dst, component);
  }

  if(strlen(base) + 1 > PATH_MAX) {
    errno = ENAMETOOLONG;
    return -1;
  }

  strcpy(dst, base);
  return path_append_component(dst, component);
}

static int
path_make_absolute(char *dst, const char *path)
{
  char cwd[PATH_MAX];

  if(path == 0 || *path == '\0') {
    errno = EINVAL;
    return -1;
  }

  if(path[0] == '/') {
    if(strlen(path) + 1 > PATH_MAX) {
      errno = ENAMETOOLONG;
      return -1;
    }
    strcpy(dst, path);
    return 0;
  }

  if(getcwd(cwd, sizeof(cwd)) == 0)
    return -1;

  if(strlen(cwd) + 1 + strlen(path) + 1 > PATH_MAX) {
    errno = ENAMETOOLONG;
    return -1;
  }

  strcpy(dst, cwd);
  if(!path_is_root(dst))
    strcat(dst, "/");
  strcat(dst, path);
  return 0;
}

char*
realpath(const char *path, char *resolved_path)
{
  char pending[PATH_MAX];
  char remaining[PATH_MAX];
  char resolved[PATH_MAX];
  char candidate[PATH_MAX];
  char component[NAME_MAX + 1];
  char linkbuf[PATH_MAX];
  char *cursor;
  int nlinks;
  struct stat st;

  if(path_make_absolute(pending, path) < 0)
    return 0;

  path_reset_root(resolved);
  cursor = pending;
  nlinks = 0;

  for(;;) {
    size_t clen;
    int linklen;

    while(*cursor == '/')
      cursor++;
    if(*cursor == '\0')
      break;

    clen = 0;
    while(cursor[clen] != '\0' && cursor[clen] != '/') {
      if(clen >= NAME_MAX) {
        errno = ENAMETOOLONG;
        return 0;
      }
      component[clen] = cursor[clen];
      clen++;
    }
    component[clen] = '\0';
    cursor += clen;
    while(*cursor == '/')
      cursor++;

    if(strcmp(component, ".") == 0)
      continue;
    if(strcmp(component, "..") == 0) {
      path_pop_component(resolved);
      continue;
    }

    if(path_build_candidate(candidate, resolved, component) < 0)
      return 0;

    errno = 0;
    if(lstat(candidate, &st) < 0) {
      if(errno == 0)
        errno = ENOENT;
      return 0;
    }

    if(S_ISLNK(st.st_mode)) {
      if(++nlinks > MAXSYMLINKS) {
        errno = ELOOP;
        return 0;
      }

      linklen = readlink(candidate, linkbuf, sizeof(linkbuf) - 1);
      if(linklen < 0)
        return 0;
      linkbuf[linklen] = '\0';

      if(strlen(cursor) + 1 > sizeof(remaining)) {
        errno = ENAMETOOLONG;
        return 0;
      }
      strcpy(remaining, cursor);

      if(linkbuf[0] == '/')
        path_reset_root(resolved);

      if(strlen(linkbuf) + strlen(remaining) + 2 > sizeof(pending)) {
        errno = ENAMETOOLONG;
        return 0;
      }

      strcpy(pending, linkbuf);
      if(remaining[0] != '\0') {
        if(pending[0] != '\0' && pending[strlen(pending) - 1] != '/')
          strcat(pending, "/");
        strcat(pending, remaining);
      }

      cursor = pending;
      continue;
    }

    if(path_append_component(resolved, component) < 0)
      return 0;
  }

  errno = 0;
  if(stat(resolved, &st) < 0) {
    if(errno == 0)
      errno = ENOENT;
    return 0;
  }

  if(resolved_path == 0) {
    char *copy;

    copy = malloc(strlen(resolved) + 1);
    if(copy == 0) {
      errno = ENOMEM;
      return 0;
    }
    strcpy(copy, resolved);
    return copy;
  }

  strcpy(resolved_path, resolved);
  return resolved_path;
}