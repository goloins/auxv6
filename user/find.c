#include "types.h"
#include "ftw.h"
#include "fnmatch.h"
#include "stdlib.h"
#include "string.h"
#include "auxv6/user.h"

struct find_opts {
  const char *name_pat;
  const char *path_pat;
  int type_filter;
  int have_type;
  int min_depth;
  int max_depth;
  int have_min_depth;
  int have_max_depth;
  int do_print;
  int seen_action;
};

static struct find_opts g_opt;

static const char *
path_base(const char *p)
{
  const char *q;

  q = p + strlen(p);
  while(q > p && q[-1] == '/')
    q--;
  while(q > p && q[-1] != '/')
    q--;
  return q;
}

static int
matches_type(const struct stat *st)
{
  if(!g_opt.have_type)
    return 1;

  if(st == 0)
    return 0;

  if(g_opt.type_filter == 'f')
    return S_ISREG(st->st_mode);
  if(g_opt.type_filter == 'd')
    return S_ISDIR(st->st_mode);
  if(g_opt.type_filter == 'l')
    return S_ISLNK(st->st_mode);
  if(g_opt.type_filter == 'b')
    return (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode));
  if(g_opt.type_filter == 'c')
    return (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode));
  return 0;
}

static int
match_entry(const char *fpath, const struct stat *st, int depth)
{
  if(g_opt.have_min_depth && depth < g_opt.min_depth)
    return 0;
  if(g_opt.have_max_depth && depth > g_opt.max_depth)
    return 0;

  if(g_opt.name_pat && fnmatch(g_opt.name_pat, path_base(fpath), 0) != 0)
    return 0;
  if(g_opt.path_pat && fnmatch(g_opt.path_pat, fpath, FNM_PATHNAME) != 0)
    return 0;
  if(!matches_type(st))
    return 0;

  return 1;
}

static int
walk_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *fb)
{
  if(typeflag == FTW_NS)
    return 0;

  if(match_entry(fpath, sb, fb ? fb->level : 0) && g_opt.do_print)
    dprintf(1, "%s\n", fpath);

  return 0;
}

static void
usage(void)
{
  dprintf(2,
          "usage: find [path ...] [expression]\n"
      "expression: [-name pattern] [-path pattern] [-type f|d|l|b|c]\n"
          "            [-mindepth n] [-maxdepth n] [-print]\n");
  exit(1);
}

int
main(int argc, char *argv[])
{
  const char *paths[32];
  int npaths;
  int i;

  memset(&g_opt, 0, sizeof(g_opt));

  npaths = 0;
  i = 1;
  while(i < argc && argv[i][0] != '-') {
    if(npaths >= (int)(sizeof(paths) / sizeof(paths[0]))) {
      dprintf(2, "find: too many paths\n");
      exit(1);
    }
    paths[npaths++] = argv[i++];
  }
  if(npaths == 0)
    paths[npaths++] = ".";

  while(i < argc) {
    if(strcmp(argv[i], "-name") == 0) {
      if(i + 1 >= argc)
        usage();
      g_opt.name_pat = argv[i + 1];
      i += 2;
      continue;
    }
    if(strcmp(argv[i], "-path") == 0) {
      if(i + 1 >= argc)
        usage();
      g_opt.path_pat = argv[i + 1];
      i += 2;
      continue;
    }
    if(strcmp(argv[i], "-type") == 0) {
      if(i + 1 >= argc || strlen(argv[i + 1]) != 1)
        usage();
      g_opt.type_filter = argv[i + 1][0];
      g_opt.have_type = 1;
      i += 2;
      continue;
    }
    if(strcmp(argv[i], "-mindepth") == 0) {
      if(i + 1 >= argc)
        usage();
      g_opt.min_depth = atoi(argv[i + 1]);
      if(g_opt.min_depth < 0)
        g_opt.min_depth = 0;
      g_opt.have_min_depth = 1;
      i += 2;
      continue;
    }
    if(strcmp(argv[i], "-maxdepth") == 0) {
      if(i + 1 >= argc)
        usage();
      g_opt.max_depth = atoi(argv[i + 1]);
      if(g_opt.max_depth < 0)
        g_opt.max_depth = 0;
      g_opt.have_max_depth = 1;
      i += 2;
      continue;
    }
    if(strcmp(argv[i], "-print") == 0) {
      g_opt.do_print = 1;
      g_opt.seen_action = 1;
      i++;
      continue;
    }

    dprintf(2, "find: unsupported expression term: %s\n", argv[i]);
    usage();
  }

  if(!g_opt.seen_action)
    g_opt.do_print = 1;

  for(i = 0; i < npaths; i++) {
    if(nftw(paths[i], walk_cb, 16, FTW_PHYS) < 0) {
      dprintf(2, "find: cannot walk %s\n", paths[i]);
      exit(1);
    }
  }

  exit(0);
}
