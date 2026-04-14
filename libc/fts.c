#include "types.h"
#include "fts.h"
#include "dirent.h"
#include "string.h"
#include "stdlib.h"
#include "errno.h"
#include "auxv6/user.h"

static int
name_off(const char *path)
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
is_dir_stat(const struct stat *st)
{
  return S_ISDIR(st->st_mode);
}

static int
is_lnk_stat(const struct stat *st)
{
  return S_ISLNK(st->st_mode);
}

static FTSENT*
mk_ent(const char *path, FTSENT *parent, int level, int info, struct stat *stp)
{
  int nlen;
  int off;
  FTSENT *e;

  off = name_off(path);
  nlen = strlen(path + off);
  if(level == 0 && nlen == 0)
    nlen = strlen(path);
  if(nlen == 0)
    nlen = 1;

  e = (FTSENT*)malloc(sizeof(FTSENT) + nlen + 1);
  if(e == 0)
    return 0;
  memset(e, 0, sizeof(FTSENT) + nlen + 1);

  if(level == 0 && strlen(path + off) == 0)
    memmove(e->fts_name, path, nlen);
  else
    memmove(e->fts_name, path + off, nlen);
  e->fts_name[nlen] = 0;

  e->fts_path = strdup(path);
  if(e->fts_path == 0) {
    free(e);
    return 0;
  }
  e->fts_accpath = e->fts_path;
  e->fts_parent = parent;
  e->fts_level = level;
  e->fts_info = info;
  e->fts_namelen = nlen;
  e->fts_pathlen = strlen(path);

  if(stp) {
    e->fts_statp = (struct stat*)malloc(sizeof(struct stat));
    if(e->fts_statp == 0) {
      free(e->fts_path);
      free(e);
      return 0;
    }
    memmove(e->fts_statp, stp, sizeof(struct stat));
    e->fts_dev = stp->st_dev;
    e->fts_ino = stp->st_ino;
    e->fts_nlink = stp->st_nlink;
  }

  return e;
}

static int
push_ent(FTS *f, FTSENT *e)
{
  FTSENT **newents;
  int ncap;

  if(f->_count >= f->_cap) {
    ncap = (f->_cap == 0) ? 64 : f->_cap * 2;
    newents = (FTSENT**)realloc(f->_ents, ncap * sizeof(FTSENT*));
    if(newents == 0)
      return -1;
    f->_ents = newents;
    f->_cap = ncap;
  }
  f->_ents[f->_count++] = e;
  return 0;
}

static int
join_path(const char *base, const char *name, char **out)
{
  int blen;
  int nlen;
  int slash;
  char *buf;

  blen = strlen(base);
  nlen = strlen(name);
  slash = (blen > 0 && base[blen - 1] != '/');
  buf = (char*)malloc(blen + slash + nlen + 1);
  if(buf == 0)
    return -1;
  memmove(buf, base, blen);
  if(slash)
    buf[blen++] = '/';
  memmove(buf + blen, name, nlen);
  buf[blen + nlen] = 0;
  *out = buf;
  return 0;
}

static int
build_tree(FTS *f, FTSENT *parent, const char *path, int level)
{
  struct stat st;
  int rc;
  int is_phys;
  FTSENT *pre;

  is_phys = (f->_options & FTS_PHYSICAL) != 0;
  rc = is_phys ? lstat(path, &st) : stat(path, &st);
  if(rc < 0) {
    pre = mk_ent(path, parent, level, FTS_NS, 0);
    if(pre == 0)
      return -1;
    pre->fts_errno = errno;
    return push_ent(f, pre);
  }

  if(is_lnk_stat(&st)) {
    pre = mk_ent(path, parent, level, FTS_SL, &st);
    if(pre == 0)
      return -1;
    return push_ent(f, pre);
  }

  if(!is_dir_stat(&st)) {
    pre = mk_ent(path, parent, level, FTS_F, &st);
    if(pre == 0)
      return -1;
    return push_ent(f, pre);
  }

  pre = mk_ent(path, parent, level, FTS_D, &st);
  if(pre == 0)
    return -1;
  if(push_ent(f, pre) < 0)
    return -1;

  {
    DIR *dp;
    struct dirent *de;

    dp = opendir(path);
    if(dp == 0) {
      pre->fts_info = FTS_DNR;
    } else {
      while((de = readdir(dp)) != 0) {
        char *child;
        if(strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
          continue;
        if(join_path(path, de->d_name, &child) < 0) {
          closedir(dp);
          return -1;
        }
        rc = build_tree(f, pre, child, level + 1);
        free(child);
        if(rc < 0) {
          closedir(dp);
          return -1;
        }
      }
      closedir(dp);
    }
  }

  {
    FTSENT *post;
    post = mk_ent(path, parent, level, FTS_DP, &st);
    if(post == 0)
      return -1;
    if(push_ent(f, post) < 0)
      return -1;
  }

  return 0;
}

FTS *
fts_open(char * const *path_argv, int options,
         int (*compar)(const FTSENT **, const FTSENT **))
{
  FTS *f;
  int i;

  if(path_argv == 0 || path_argv[0] == 0)
    return 0;

  f = (FTS*)malloc(sizeof(FTS));
  if(f == 0)
    return 0;
  memset(f, 0, sizeof(*f));

  f->_options = options;
  f->fts_compar = compar;

  for(i = 0; path_argv[i]; i++) {
    if(build_tree(f, 0, path_argv[i], 0) < 0) {
      fts_close(f);
      return 0;
    }
  }

  return f;
}

FTSENT *
fts_read(FTS *f)
{
  if(f == 0)
    return 0;
  if(f->_index >= f->_count)
    return 0;
  f->fts_cur = f->_ents[f->_index++];
  return f->fts_cur;
}

FTSENT *
fts_children(FTS *f, int instr)
{
  int i;
  FTSENT *first;
  FTSENT *prev;

  (void)instr;

  if(f == 0 || f->fts_cur == 0)
    return 0;
  if(f->fts_cur->fts_info != FTS_D)
    return 0;

  first = 0;
  prev = 0;
  for(i = 0; i < f->_count; i++) {
    FTSENT *e;
    e = f->_ents[i];
    if(e->fts_parent != f->fts_cur)
      continue;
    if(e->fts_info == FTS_DP)
      continue;
    if(first == 0)
      first = e;
    if(prev)
      prev->fts_link = e;
    prev = e;
  }

  if(prev)
    prev->fts_link = 0;
  f->fts_child = first;
  return first;
}

int
fts_close(FTS *f)
{
  int i;

  if(f == 0)
    return -1;

  for(i = 0; i < f->_count; i++) {
    if(f->_ents[i]) {
      if(f->_ents[i]->fts_statp)
        free(f->_ents[i]->fts_statp);
      if(f->_ents[i]->fts_path)
        free(f->_ents[i]->fts_path);
      free(f->_ents[i]);
    }
  }
  free(f->_ents);
  free(f);
  return 0;
}