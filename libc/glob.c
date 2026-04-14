#include "types.h"
#include "glob.h"
#include "fnmatch.h"
#include "dirent.h"
#include "string.h"
#include "stdlib.h"
#include "sys/stat.h"
#include "errno.h"
#include "auxv6/user.h"

static int
has_meta(const char *s)
{
  while(*s) {
    if(*s == '*' || *s == '?' || *s == '[')
      return 1;
    if(*s == '\\' && s[1])
      s++;
    s++;
  }
  return 0;
}

static char*
xstrdup(const char *s)
{
  int n;
  char *d;

  n = strlen(s) + 1;
  d = (char*)malloc(n);
  if(d == 0)
    return 0;
  memmove(d, s, n);
  return d;
}

static int
path_is_dir(const char *path)
{
  struct stat st;

  if(stat(path, &st) < 0)
    return 0;
  return S_ISDIR(st.st_mode);
}

static char*
path_join(const char *base, const char *name)
{
  int blen;
  int nlen;
  int need_slash;
  int total;
  char *out;

  blen = base ? strlen(base) : 0;
  nlen = name ? strlen(name) : 0;
  need_slash = (blen > 0 && base[blen - 1] != '/');
  total = blen + (need_slash ? 1 : 0) + nlen + 1;

  out = (char*)malloc(total);
  if(out == 0)
    return 0;

  if(blen)
    memmove(out, base, blen);
  if(need_slash)
    out[blen++] = '/';
  if(nlen)
    memmove(out + blen, name, nlen);
  out[blen + nlen] = 0;
  return out;
}

static int
glob_add(glob_t *g, const char *path, int flags)
{
  size_t oldc;
  size_t off;
  size_t total;
  char **newv;
  char *entry;

  off = (flags & GLOB_DOOFFS) ? g->gl_offs : 0;
  oldc = g->gl_pathc;

  entry = xstrdup(path);
  if(entry == 0)
    return GLOB_NOSPACE;

  if((flags & GLOB_MARK) && path_is_dir(path)) {
    int len;
    char *s2;

    len = strlen(entry);
    if(len == 0 || entry[len - 1] != '/') {
      s2 = (char*)realloc(entry, len + 2);
      if(s2 == 0) {
        free(entry);
        return GLOB_NOSPACE;
      }
      entry = s2;
      entry[len] = '/';
      entry[len + 1] = 0;
    }
  }

  total = off + oldc + 2;
  newv = (char**)realloc(g->gl_pathv, total * sizeof(char*));
  if(newv == 0) {
    free(entry);
    return GLOB_NOSPACE;
  }

  g->gl_pathv = newv;
  if(oldc == 0) {
    size_t i;
    for(i = 0; i < off; i++)
      g->gl_pathv[i] = 0;
  }

  g->gl_pathv[off + oldc] = entry;
  g->gl_pathc = oldc + 1;
  g->gl_pathv[off + g->gl_pathc] = 0;
  return 0;
}

static int
glob_cmp(const void *a, const void *b)
{
  const char *sa = *(const char * const *)a;
  const char *sb = *(const char * const *)b;
  return strcmp(sa, sb);
}

static int
expand_component(const char *prefix, const char *rest, int flags,
                 int (*errfunc)(const char*, int), glob_t *g);

static int
dispatch_next(const char *next_prefix, const char *next_rest, int flags,
              int (*errfunc)(const char*, int), glob_t *g)
{
  int rc;
  rc = expand_component(next_prefix, next_rest, flags, errfunc, g);
  return rc;
}

static int
expand_component(const char *prefix, const char *rest, int flags,
                 int (*errfunc)(const char*, int), glob_t *g)
{
  const char *slash;
  int comp_len;
  char comp[NAME_MAX + 1];
  const char *tail;
  int meta;

  while(*rest == '/')
    rest++;

  if(*rest == 0)
    return glob_add(g, prefix[0] ? prefix : ".", flags);

  slash = strchr(rest, '/');
  comp_len = slash ? (int)(slash - rest) : strlen(rest);
  if(comp_len <= 0 || comp_len > NAME_MAX)
    return GLOB_NOMATCH;

  memmove(comp, rest, comp_len);
  comp[comp_len] = 0;
  tail = slash ? slash + 1 : rest + comp_len;
  meta = has_meta(comp);

  if(!meta) {
    char *next;
    int rc;

    if(prefix[0] == 0)
      next = xstrdup(comp);
    else
      next = path_join(prefix, comp);
    if(next == 0)
      return GLOB_NOSPACE;

    rc = dispatch_next(next, tail, flags, errfunc, g);
    free(next);
    return rc;
  }

  {
    DIR *dp;
    struct dirent *de;
    int matched;
    int first_rc;
    const char *dirpath;

    matched = 0;
    first_rc = 0;
    dirpath = prefix[0] ? prefix : ".";
    dp = opendir(dirpath);
    if(dp == 0) {
      if(errfunc && errfunc(dirpath, errno) != 0)
        return GLOB_ABORTED;
      if(flags & GLOB_ERR)
        return GLOB_ABORTED;
      return GLOB_NOMATCH;
    }

    while((de = readdir(dp)) != 0) {
      char *next;
      int rc;

      if(comp[0] != '.' && de->d_name[0] == '.')
        continue;
      if(fnmatch(comp, de->d_name, FNM_PERIOD) != 0)
        continue;

      matched = 1;
      if(prefix[0] == 0)
        next = xstrdup(de->d_name);
      else
        next = path_join(prefix, de->d_name);
      if(next == 0) {
        closedir(dp);
        return GLOB_NOSPACE;
      }

      rc = dispatch_next(next, tail, flags, errfunc, g);
      free(next);
      if(rc == GLOB_NOSPACE || rc == GLOB_ABORTED) {
        closedir(dp);
        return rc;
      }
      if(rc == 0)
        first_rc = 0;
    }

    closedir(dp);
    if(!matched)
      return GLOB_NOMATCH;
    return first_rc;
  }
}

int
glob(const char *pattern, int flags,
     int (*errfunc)(const char *epath, int eerrno), glob_t *pglob)
{
  char *prefix;
  const char *rest;
  int rc;
  size_t off;

  if(pattern == 0 || pglob == 0)
    return GLOB_NOMATCH;

  if(!(flags & GLOB_APPEND)) {
    pglob->gl_pathc = 0;
    pglob->gl_pathv = 0;
    if(!(flags & GLOB_DOOFFS))
      pglob->gl_offs = 0;
  }

  if(*pattern == '/') {
    prefix = xstrdup("/");
    rest = pattern + 1;
  } else {
    prefix = xstrdup("");
    rest = pattern;
  }
  if(prefix == 0)
    return GLOB_NOSPACE;

  rc = expand_component(prefix, rest, flags, errfunc, pglob);
  free(prefix);

  if(rc == GLOB_NOMATCH && (flags & GLOB_NOCHECK)) {
    if(glob_add(pglob, pattern, flags) != 0)
      return GLOB_NOSPACE;
    rc = 0;
  }

  if(rc != 0)
    return rc;

  off = (flags & GLOB_DOOFFS) ? pglob->gl_offs : 0;
  if(!(flags & GLOB_NOSORT) && pglob->gl_pathc > 1)
    qsort(pglob->gl_pathv + off, pglob->gl_pathc, sizeof(char*), glob_cmp);

  if(pglob->gl_pathc == 0)
    return GLOB_NOMATCH;
  return 0;
}

void
globfree(glob_t *pglob)
{
  size_t i;
  size_t off;

  if(pglob == 0)
    return;
  if(pglob->gl_pathv == 0) {
    pglob->gl_pathc = 0;
    return;
  }

  off = pglob->gl_offs;
  for(i = 0; i < pglob->gl_pathc; i++)
    free(pglob->gl_pathv[off + i]);

  free(pglob->gl_pathv);
  pglob->gl_pathv = 0;
  pglob->gl_pathc = 0;
}