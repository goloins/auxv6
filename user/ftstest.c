#include "types.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "unistd.h"
#include "fts.h"
#include "errno.h"
#include "stdio.h"

static int g_debug;
static char g_root[64];
static char g_dir_d1[80];
static char g_dir_d2[80];
static char g_leaf_f1[96];
static char g_leaf_f2[96];

#define DBG(...) do { if(g_debug) dprintf(1, __VA_ARGS__); } while(0)

static void
init_paths(void)
{
  int pid;

  pid = getpid();
  snprintf(g_root, sizeof(g_root), "/tmp/fts-test-%d", pid);
  snprintf(g_dir_d1, sizeof(g_dir_d1), "%s/d1", g_root);
  snprintf(g_dir_d2, sizeof(g_dir_d2), "%s/d2", g_root);
  snprintf(g_leaf_f1, sizeof(g_leaf_f1), "%s/f1", g_dir_d1);
  snprintf(g_leaf_f2, sizeof(g_leaf_f2), "%s/f2", g_dir_d2);
}

static int
mkfile(const char *path, const char *data)
{
  int fd;
  int n;
  int len;

  fd = open(path, O_CREATE | O_WRONLY);
  if(fd < 0)
    return -1;
  len = strlen(data);
  n = write(fd, data, len);
  close(fd);
  return (n == len) ? 0 : -1;
}

static void
cleanup_tree(void)
{
  int rc;

  DBG("ftstest: cleanup begin\n");
  unlink(g_leaf_f1);
  unlink(g_leaf_f2);
  rc = rmdir(g_dir_d1);
  DBG("ftstest: rmdir %s rc=%d errno=%d\n", g_dir_d1, rc, errno);
  rc = rmdir(g_dir_d2);
  DBG("ftstest: rmdir %s rc=%d errno=%d\n", g_dir_d2, rc, errno);
  rc = rmdir(g_root);
  DBG("ftstest: rmdir %s rc=%d errno=%d\n", g_root, rc, errno);
  DBG("ftstest: cleanup end\n");
}

int
main(int argc, char **argv)
{
  FTS *f;
  FTSENT *ent;
  FTSENT *kids;
  int dirs_pre;
  int dirs_post;
  int files;
  int saw_children;
  char *roots[2];
  int mkrc;

  if(argc > 1 && strcmp(argv[1], "-v") == 0)
    g_debug = 1;

  init_paths();
  DBG("ftstest: start\n");
  cleanup_tree();

  errno = 0;
  mkrc = mkdir(g_root);
  DBG("ftstest: mkdir %s rc=%d errno=%d\n", g_root, mkrc, errno);
  if(mkrc < 0) {
    dprintf(2, "ftstest: setup failed\n");
    exit(1);
  }
  errno = 0;
  mkrc = mkdir(g_dir_d1);
  DBG("ftstest: mkdir %s rc=%d errno=%d\n", g_dir_d1, mkrc, errno);
  if(mkrc < 0) {
    dprintf(2, "ftstest: setup failed\n");
    cleanup_tree();
    exit(1);
  }
  errno = 0;
  mkrc = mkdir(g_dir_d2);
  DBG("ftstest: mkdir %s rc=%d errno=%d\n", g_dir_d2, mkrc, errno);
  if(mkrc < 0) {
    dprintf(2, "ftstest: setup failed\n");
    cleanup_tree();
    exit(1);
  }

  DBG("ftstest: creating leaves\n");
  if(mkfile(g_leaf_f1, "a") < 0 ||
     mkfile(g_leaf_f2, "b") < 0) {
    dprintf(2, "ftstest: setup failed\n");
    cleanup_tree();
    exit(1);
  }

  roots[0] = g_root;
  roots[1] = 0;

  DBG("ftstest: fts_open enter\n");
  f = fts_open(roots, FTS_PHYSICAL | FTS_NOCHDIR, 0);
  DBG("ftstest: fts_open exit ptr=%p\n", f);
  if(f == 0) {
    dprintf(2, "ftstest: fts_open failed\n");
    cleanup_tree();
    exit(1);
  }

  dirs_pre = dirs_post = files = saw_children = 0;

  while((ent = fts_read(f)) != 0) {
    if(g_debug)
      dprintf(1, "ftstest: read path=%s info=%d level=%d parent=%p\n",
              ent->fts_path ? ent->fts_path : "(null)",
              ent->fts_info, ent->fts_level, ent->fts_parent);

    if(ent->fts_info == FTS_D) {
      dirs_pre++;
      if(strcmp(ent->fts_path, g_root) == 0) {
        DBG("ftstest: fts_children on root\n");
        kids = fts_children(f, 0);
        DBG("ftstest: fts_children returned %p\n", kids);
        if(kids)
          saw_children = 1;
      }
    } else if(ent->fts_info == FTS_DP) {
      dirs_post++;
    } else if(ent->fts_info == FTS_F) {
      files++;
    }
  }

  DBG("ftstest: fts_close enter\n");
  if(fts_close(f) < 0) {
    dprintf(2, "ftstest: fts_close failed\n");
    cleanup_tree();
    exit(1);
  }
  DBG("ftstest: fts_close exit\n");

  if(dirs_pre < 3 || dirs_post < 3 || files < 2 || !saw_children) {
    dprintf(2,
            "ftstest: unexpected counts pre=%d post=%d files=%d children=%d\n",
            dirs_pre, dirs_post, files, saw_children);
    cleanup_tree();
    exit(1);
  }

  DBG("ftstest: final counts pre=%d post=%d files=%d children=%d\n",
      dirs_pre, dirs_post, files, saw_children);
  dprintf(1, "ftstest: PASS\n");
  cleanup_tree();
  exit(0);
}