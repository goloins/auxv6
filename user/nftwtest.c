#include "types.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "unistd.h"
#include "ftw.h"
#include "errno.h"
#include "stdio.h"

static int g_debug;
static char g_root[64];
static char g_dir_a[80];
static char g_dir_b[80];
static char g_leaf_a1[96];
static char g_leaf_a2[96];
static char g_leaf_b3[96];

#define DBG(...) do { if(g_debug) dprintf(1, __VA_ARGS__); } while(0)

static void
init_paths(void)
{
  int pid;

  pid = getpid();
  snprintf(g_root, sizeof(g_root), "/tmp/nftw-test-%d", pid);
  snprintf(g_dir_a, sizeof(g_dir_a), "%s/a", g_root);
  snprintf(g_dir_b, sizeof(g_dir_b), "%s/b", g_root);
  snprintf(g_leaf_a1, sizeof(g_leaf_a1), "%s/leaf1", g_dir_a);
  snprintf(g_leaf_a2, sizeof(g_leaf_a2), "%s/leaf2", g_dir_a);
  snprintf(g_leaf_b3, sizeof(g_leaf_b3), "%s/leaf3", g_dir_b);
}

static int g_dirs;
static int g_files;
static int g_post_dirs;
static int g_ns;

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

  DBG("nftwtest: cleanup begin\n");
  unlink(g_leaf_a1);
  unlink(g_leaf_a2);
  unlink(g_leaf_b3);
  rc = rmdir(g_dir_a);
  DBG("nftwtest: rmdir %s rc=%d errno=%d\n", g_dir_a, rc, errno);
  rc = rmdir(g_dir_b);
  DBG("nftwtest: rmdir %s rc=%d errno=%d\n", g_dir_b, rc, errno);
  rc = rmdir(g_root);
  DBG("nftwtest: rmdir %s rc=%d errno=%d\n", g_root, rc, errno);
  DBG("nftwtest: cleanup end\n");
}

static int
cb_pre(const char *fpath, const struct stat *sb, int typeflag, struct FTW *fb)
{
  (void)sb;
  if(g_debug)
    dprintf(1, "nftwtest: pre path=%s type=%d level=%d base=%d\n",
            fpath, typeflag, fb ? fb->level : -1, fb ? fb->base : -1);

  if(typeflag == FTW_D)
    g_dirs++;
  else if(typeflag == FTW_F)
    g_files++;
  else if(typeflag == FTW_NS)
    g_ns++;
  return 0;
}

static int
cb_post(const char *fpath, const struct stat *sb, int typeflag, struct FTW *fb)
{
  (void)sb;
  if(g_debug)
    dprintf(1, "nftwtest: post path=%s type=%d level=%d base=%d\n",
            fpath, typeflag, fb ? fb->level : -1, fb ? fb->base : -1);

  if(typeflag == FTW_DP)
    g_post_dirs++;
  else if(typeflag == FTW_F)
    g_files++;
  else if(typeflag == FTW_NS)
    g_ns++;
  return 0;
}

int
main(int argc, char **argv)
{
  int rc;
  int mkrc;

  if(argc > 1 && strcmp(argv[1], "-v") == 0)
    g_debug = 1;

  init_paths();
  DBG("nftwtest: start\n");
  cleanup_tree();

  errno = 0;
  mkrc = mkdir(g_root);
  DBG("nftwtest: mkdir %s rc=%d errno=%d\n", g_root, mkrc, errno);
  if(mkrc < 0) {
    dprintf(2, "nftwtest: setup failed\n");
    exit(1);
  }
  errno = 0;
  mkrc = mkdir(g_dir_a);
  DBG("nftwtest: mkdir %s rc=%d errno=%d\n", g_dir_a, mkrc, errno);
  if(mkrc < 0) {
    dprintf(2, "nftwtest: setup failed\n");
    cleanup_tree();
    exit(1);
  }
  errno = 0;
  mkrc = mkdir(g_dir_b);
  DBG("nftwtest: mkdir %s rc=%d errno=%d\n", g_dir_b, mkrc, errno);
  if(mkrc < 0) {
    dprintf(2, "nftwtest: setup failed\n");
    cleanup_tree();
    exit(1);
  }

  DBG("nftwtest: creating leaves\n");
  if(mkfile(g_leaf_a1, "x") < 0 ||
     mkfile(g_leaf_a2, "y") < 0 ||
     mkfile(g_leaf_b3, "z") < 0) {
    dprintf(2, "nftwtest: setup failed\n");
    cleanup_tree();
    exit(1);
  }

  g_dirs = g_files = g_post_dirs = g_ns = 0;
  DBG("nftwtest: nftw pre enter\n");
  rc = nftw(g_root, cb_pre, 16, FTW_PHYS);
  DBG("nftwtest: nftw pre exit rc=%d\n", rc);
  if(rc != 0) {
    dprintf(2, "nftwtest: nftw pre-order failed rc=%d\n", rc);
    cleanup_tree();
    exit(1);
  }

  if(g_dirs < 3 || g_files < 3 || g_ns != 0) {
    dprintf(2, "nftwtest: unexpected pre counts dirs=%d files=%d ns=%d\n",
            g_dirs, g_files, g_ns);
    cleanup_tree();
    exit(1);
  }

  g_files = g_post_dirs = g_ns = 0;
  DBG("nftwtest: nftw depth enter\n");
  rc = nftw(g_root, cb_post, 16, FTW_DEPTH | FTW_PHYS);
  DBG("nftwtest: nftw depth exit rc=%d\n", rc);
  if(rc != 0) {
    dprintf(2, "nftwtest: nftw depth failed rc=%d\n", rc);
    cleanup_tree();
    exit(1);
  }

  if(g_post_dirs < 3 || g_files < 3 || g_ns != 0) {
    dprintf(2, "nftwtest: unexpected depth counts post=%d files=%d ns=%d\n",
            g_post_dirs, g_files, g_ns);
    cleanup_tree();
    exit(1);
  }

  DBG("nftwtest: final counts pre_dirs=%d post_dirs=%d files=%d ns=%d\n",
      g_dirs, g_post_dirs, g_files, g_ns);
  dprintf(1, "nftwtest: PASS\n");
  cleanup_tree();
  exit(0);
}