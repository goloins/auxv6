#include "types.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "ftw.h"
#include "errno.h"
#include "stdio.h"

static int g_debug;
static char g_root[64];
static char g_dir_a[80];
static char g_dir_b[80];
static char g_leaf_a[96];
static char g_leaf_b[96];

#define DBG(...) do { if(g_debug) dprintf(1, __VA_ARGS__); } while(0)

static void
init_paths(void)
{
  int pid;

  pid = getpid();
  snprintf(g_root, sizeof(g_root), "/tmp/ftw-test-%d", pid);
  snprintf(g_dir_a, sizeof(g_dir_a), "%s/a", g_root);
  snprintf(g_dir_b, sizeof(g_dir_b), "%s/b", g_root);
  snprintf(g_leaf_a, sizeof(g_leaf_a), "%s/leaf1", g_dir_a);
  snprintf(g_leaf_b, sizeof(g_leaf_b), "%s/leaf2", g_dir_b);
}

static int g_dirs;
static int g_files;
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

  DBG("ftwtest: cleanup begin\n");
  unlink(g_leaf_a);
  unlink(g_leaf_b);
  rc = rmdir(g_dir_a);
  DBG("ftwtest: rmdir %s rc=%d errno=%d\n", g_dir_a, rc, errno);
  rc = rmdir(g_dir_b);
  DBG("ftwtest: rmdir %s rc=%d errno=%d\n", g_dir_b, rc, errno);
  rc = rmdir(g_root);
  DBG("ftwtest: rmdir %s rc=%d errno=%d\n", g_root, rc, errno);
  DBG("ftwtest: cleanup end\n");
}

static int
cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *fb)
{
  (void)sb;
  if(g_debug)
    dprintf(1, "ftwtest: cb path=%s type=%d level=%d base=%d\n",
            fpath, typeflag, fb ? fb->level : -1, fb ? fb->base : -1);

  (void)sb;
  (void)fb;

  if(typeflag == FTW_D || typeflag == FTW_DP)
    g_dirs++;
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
  DBG("ftwtest: start\n");
  cleanup_tree();
  errno = 0;
  mkrc = mkdir(g_root);
  DBG("ftwtest: mkdir %s rc=%d errno=%d\n", g_root, mkrc, errno);
  if(mkrc < 0) {
    dprintf(2, "ftwtest: setup failed\n");
    exit(1);
  }
  errno = 0;
  mkrc = mkdir(g_dir_a);
  DBG("ftwtest: mkdir %s rc=%d errno=%d\n", g_dir_a, mkrc, errno);
  if(mkrc < 0) {
    dprintf(2, "ftwtest: setup failed\n");
    cleanup_tree();
    exit(1);
  }
  errno = 0;
  mkrc = mkdir(g_dir_b);
  DBG("ftwtest: mkdir %s rc=%d errno=%d\n", g_dir_b, mkrc, errno);
  if(mkrc < 0) {
    dprintf(2, "ftwtest: setup failed\n");
    cleanup_tree();
    exit(1);
  }

  DBG("ftwtest: creating leaves\n");
  if(mkfile(g_leaf_a, "1") < 0 ||
     mkfile(g_leaf_b, "2") < 0) {
    dprintf(2, "ftwtest: setup failed\n");
    cleanup_tree();
    exit(1);
  }

  g_dirs = g_files = g_ns = 0;
  DBG("ftwtest: nftw enter\n");
  rc = nftw(g_root, cb, 16, FTW_PHYS);
  DBG("ftwtest: nftw exit rc=%d\n", rc);
  if(rc != 0) {
    dprintf(2, "ftwtest: nftw failed rc=%d\n", rc);
    cleanup_tree();
    exit(1);
  }

  if(g_dirs < 3 || g_files < 2 || g_ns != 0) {
    dprintf(2, "ftwtest: unexpected counts dirs=%d files=%d ns=%d\n",
            g_dirs, g_files, g_ns);
    cleanup_tree();
    exit(1);
  }

  DBG("ftwtest: final counts dirs=%d files=%d ns=%d\n", g_dirs, g_files, g_ns);
  dprintf(1, "ftwtest: PASS\n");
  cleanup_tree();
  exit(0);
}