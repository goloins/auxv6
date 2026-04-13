#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "fs.h"

#define FAT_GROW_SIZE 9216
#define FAT_DEFAULT_MOUNT "/fat"
#define FAT_PATH_MAX 256
#define FAT_DIRENT_BATCH 8

static char writebuf[FAT_GROW_SIZE];
static char readbuf[FAT_GROW_SIZE];
static int fatreg_debug;

#define DBG(...) do { if(fatreg_debug) dprintf(1, __VA_ARGS__); } while(0)

static void
debug_dump_parent(char *path)
{
  char dir[FAT_PATH_MAX];
  int i;
  int slash;
  int fd;

  if(!fatreg_debug || path == 0)
    return;

  slash = -1;
  for(i = 0; path[i]; i++){
    if(path[i] == '/')
      slash = i;
  }
  if(slash <= 0)
    return;
  if(slash >= FAT_PATH_MAX)
    return;

  for(i = 0; i < slash; i++)
    dir[i] = path[i];
  dir[slash] = 0;

  fd = open(dir, O_RDONLY);
  DBG("fatregress[d]: dumpdir open(%s) -> %d\n", dir, fd);
  if(fd < 0)
    return;

  for(;;){
    struct dirent ents[FAT_DIRENT_BATCH];
    int n;
    int j;

    n = getdents(fd, ents, FAT_DIRENT_BATCH);
    if(n <= 0)
      break;
    for(j = 0; j < n; j++){
      char name[DIRSIZ + 1];
      if(ents[j].inum == 0)
        continue;
      memmove(name, ents[j].name, DIRSIZ);
      name[DIRSIZ] = 0;
      DBG("fatregress[d]: dumpdir %s entry inum=%d name=%s\n", dir, ents[j].inum, name);
    }
  }

  close(fd);
}

static void
fail(char *msg, char *path)
{
  if(path)
    dprintf(1, "fatregress: FAIL %s %s\n", msg, path);
  else
    dprintf(1, "fatregress: FAIL %s\n", msg);
  exit(1);
}

static void
path_join(char *base, char *leaf, char out[FAT_PATH_MAX])
{
  int blen;
  int llen;
  int i;
  int pos;

  blen = strlen(base);
  llen = strlen(leaf);
  if(blen <= 0)
    fail("bad base path", base);

  if(blen + 1 + llen + 1 > FAT_PATH_MAX)
    fail("path too long", base);

  pos = 0;
  for(i = 0; i < blen; i++)
    out[pos++] = base[i];

  if(base[blen - 1] != '/')
    out[pos++] = '/';

  for(i = 0; i < llen; i++)
    out[pos++] = leaf[i];
  out[pos] = 0;
  DBG("fatregress[d]: join_path base=%s leaf=%s -> %s\n", base, leaf, out);
}

static void
require_dir(char *path)
{
  struct stat st;
  int rc;

  rc = stat(path, &st);
  DBG("fatregress[d]: stat(%s) -> %d type=%d\n", path, rc, (rc < 0) ? -1 : st.st_type);
  if(rc < 0)
    fail("mountpoint missing", path);
  if(st.st_type != T_DIR)
    fail("mountpoint not dir", path);
}

static void
read_expect(char *path, char *want)
{
  int fd;
  int want_len;
  int n;

  fd = open(path, O_RDONLY);
  DBG("fatregress[d]: open(%s,O_RDONLY) -> %d\n", path, fd);
  if(fd < 0){
    debug_dump_parent(path);
    fail("open", path);
  }
  want_len = strlen(want);
  n = read(fd, readbuf, sizeof(readbuf));
  if(n != want_len){
    close(fd);
    fail("read length", path);
  }
  close(fd);
  if(strncmp(readbuf, want, want_len) != 0)
    fail("read contents", path);
}

static void
read_expect2(char *path, char *want1, char *want2)
{
  int fd;
  int n;
  int len1;
  int len2;

  fd = open(path, O_RDONLY);
  DBG("fatregress[d]: open(%s,O_RDONLY) -> %d\n", path, fd);
  if(fd < 0){
    debug_dump_parent(path);
    fail("open", path);
  }

  n = read(fd, readbuf, sizeof(readbuf));
  close(fd);
  if(n < 0)
    fail("read", path);

  len1 = strlen(want1);
  len2 = strlen(want2);
  if((n == len1 && strncmp(readbuf, want1, len1) == 0) ||
     (n == len2 && strncmp(readbuf, want2, len2) == 0))
    return;

  fail("read contents", path);
}

static void
fill_pattern(char *buf, int len)
{
  int i;

  for(i = 0; i < len; i++)
    buf[i] = 'A' + (i % 23);
}

static void
expect_absent(char *path)
{
  struct stat st;

  if(stat(path, &st) >= 0)
    fail("expected absent", path);
}

static int
path_exists(char *path)
{
  struct stat st;
  int rc;

  rc = stat(path, &st);
  DBG("fatregress[d]: path_exists stat(%s) -> %d\n", path, rc);
  return rc >= 0;
}

static void
check_seeded_reads(char *mnt)
{
  char p1[FAT_PATH_MAX];
  char p2[FAT_PATH_MAX];

  path_join(mnt, "hello.txt", p1);
  path_join(mnt, "subdir/note.txt", p2);

  if(!path_exists(p1) || !path_exists(p2)){
    dprintf(1, "fatregress: seeded files absent, skipping seeded-read checks\n");
    return;
  }

  read_expect2(p1,
               "hello from auxv6 fat image\n",
               "hello from auxv6 fat32 image\n");
  read_expect2(p2,
               "subdirectory note from fat image\n",
               "subdirectory note from fat32 image\n");
  dprintf(1, "fatregress: ok seeded reads\n");
}

static void
check_lfn_seeded_reads(char *mnt)
{
  char p1[FAT_PATH_MAX];
  char p2[FAT_PATH_MAX];
  char p3[FAT_PATH_MAX];

  path_join(mnt, "longfilename.txt", p1);
  path_join(mnt, "longnamedir/readme.txt", p2);
  path_join(mnt, "another-long-name-file.txt", p3);

  if(!path_exists(p1) || !path_exists(p2) || !path_exists(p3)){
    dprintf(1, "fatregress: LFN seeded files absent, skipping LFN seeded checks\n");
    return;
  }

  read_expect(p1, "this is a file with a long filename\n");
  read_expect(p2, "file inside long-name directory\n");
  read_expect(p3, "another long filename test file\n");
  dprintf(1, "fatregress: ok LFN seeded reads\n");
}

static void
check_small_write(char *mnt)
{
  char path[FAT_PATH_MAX];
  int fd;
  int n;

  path_join(mnt, "newfile.txt", path);
  DBG("fatregress[d]: unlink(%s) preclean\n", path);
  unlink(path);

  fd = open(path, O_CREATE | O_RDWR | O_TRUNC);
  DBG("fatregress[d]: open(%s,O_CREATE|O_RDWR|O_TRUNC) -> %d\n", path, fd);
  if(fd < 0)
    fail("create", path);
  n = write(fd, "abc123\n", 7);
  if(n != 7){
    close(fd);
    fail("write", path);
  }
  close(fd);

  read_expect(path, "abc123\n");

  if(unlink(path) < 0)
    fail("unlink", path);
  expect_absent(path);
  dprintf(1, "fatregress: ok small write\n");
}

static void
check_growth_and_truncate(char *mnt)
{
  char path[FAT_PATH_MAX];
  int fd;
  int off;
  struct stat st;

  path_join(mnt, "grow.bin", path);
  DBG("fatregress[d]: unlink(%s) preclean\n", path);
  unlink(path);
  fill_pattern(writebuf, sizeof(writebuf));

  fd = open(path, O_CREATE | O_WRONLY | O_TRUNC);
  DBG("fatregress[d]: open(%s,O_CREATE|O_WRONLY|O_TRUNC) -> %d\n", path, fd);
  if(fd < 0)
    fail("create", path);
  for(off = 0; off < (int)sizeof(writebuf); off += 1024){
    int chunk;
    int n;

    chunk = (int)sizeof(writebuf) - off;
    if(chunk > 1024)
      chunk = 1024;
    n = write(fd, writebuf + off, chunk);
    if(n != chunk){
      close(fd);
      fail("grow write", path);
    }
  }
  close(fd);

  if(stat(path, &st) < 0)
    fail("stat", path);
  if(st.st_size != (int)sizeof(writebuf))
    fail("stat size", path);

  fd = open(path, O_RDONLY);
  if(fd < 0)
    fail("reopen", path);
  for(off = 0; off < (int)sizeof(readbuf); off += 1024){
    int chunk;
    int n;

    chunk = (int)sizeof(readbuf) - off;
    if(chunk > 1024)
      chunk = 1024;
    n = read(fd, readbuf + off, chunk);
    if(n != chunk){
      close(fd);
      fail("grow read", path);
    }
  }
  close(fd);

  for(off = 0; off < (int)sizeof(writebuf); off++){
    if(readbuf[off] != writebuf[off])
      fail("grow verify", path);
  }

  fd = open(path, O_WRONLY | O_TRUNC);
  if(fd < 0)
    fail("truncate open", path);
  close(fd);

  if(stat(path, &st) < 0)
    fail("post-truncate stat", path);
  if(st.st_size != 0)
    fail("post-truncate size", path);

  if(unlink(path) < 0)
    fail("cleanup unlink", path);
  expect_absent(path);
  dprintf(1, "fatregress: ok growth/truncate\n");
}

static void
check_lfn_write_unlink(char *mnt)
{
  char path[FAT_PATH_MAX];
  int fd;
  int n;

  path_join(mnt, "this-is-a-new-long-filename.dat", path);
  DBG("fatregress[d]: unlink(%s) preclean\n", path);
  unlink(path);

  fd = open(path, O_CREATE | O_RDWR | O_TRUNC);
  DBG("fatregress[d]: open(%s,O_CREATE|O_RDWR|O_TRUNC) -> %d\n", path, fd);
  if(fd < 0)
    fail("lfn create", path);
  n = write(fd, "lfn write ok\n", 13);
  if(n != 13){
    close(fd);
    fail("lfn write", path);
  }
  close(fd);

  read_expect(path, "lfn write ok\n");

  if(unlink(path) < 0)
    fail("lfn unlink", path);
  expect_absent(path);
  dprintf(1, "fatregress: ok LFN write/unlink\n");
}

static void
check_rename_cycles(char *mnt)
{
  char a[FAT_PATH_MAX];
  char b[FAT_PATH_MAX];
  char d1[FAT_PATH_MAX];
  char d2[FAT_PATH_MAX];
  char src[FAT_PATH_MAX];
  char dst[FAT_PATH_MAX];
  char dirsrc[FAT_PATH_MAX];
  char dirdst[FAT_PATH_MAX];
  char sub[FAT_PATH_MAX];
  char bad[FAT_PATH_MAX];
  int fd;
  struct stat st;

  path_join(mnt, "rename-a.txt", a);
  path_join(mnt, "rename-b.txt", b);
  path_join(mnt, "rdir1", d1);
  path_join(mnt, "rdir2", d2);
  path_join(mnt, "rdir1/src.txt", src);
  path_join(mnt, "rdir2/dst.txt", dst);
  path_join(mnt, "dirsrc", dirsrc);
  path_join(mnt, "dirdst", dirdst);
  path_join(mnt, "dirdst/sub", sub);
  path_join(mnt, "dirdst/sub/oops", bad);

  unlink(a);
  unlink(b);
  unlink(src);
  unlink(dst);
  rmdir(dirsrc);
  rmdir(sub);
  rmdir(dirdst);
  rmdir(d1);
  rmdir(d2);

  fd = open(a, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0)
    fail("rename create", a);
  if(write(fd, "src\n", 4) != 4){
    close(fd);
    fail("rename write", a);
  }
  close(fd);

  fd = open(b, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0)
    fail("rename create", b);
  if(write(fd, "old\n", 4) != 4){
    close(fd);
    fail("rename write", b);
  }
  close(fd);

  if(rename(a, b) < 0)
    fail("rename overwrite", b);
  expect_absent(a);
  read_expect(b, "src\n");
  if(unlink(b) < 0)
    fail("rename cleanup", b);

  if(mkdir(d1) < 0 || mkdir(d2) < 0)
    fail("rename mkdir roots", mnt);

  fd = open(src, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0)
    fail("crossdir create", src);
  if(write(fd, "xy\n", 3) != 3){
    close(fd);
    fail("crossdir write", src);
  }
  close(fd);

  fd = open(dst, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0)
    fail("crossdir create", dst);
  if(write(fd, "old\n", 4) != 4){
    close(fd);
    fail("crossdir write", dst);
  }
  close(fd);

  if(rename(src, dst) < 0)
    fail("crossdir rename", dst);
  expect_absent(src);
  read_expect(dst, "xy\n");
  if(unlink(dst) < 0)
    fail("crossdir cleanup", dst);
  if(rmdir(d1) < 0 || rmdir(d2) < 0)
    fail("crossdir cleanup dir", mnt);

  if(mkdir(dirsrc) < 0)
    fail("dirrename mkdir", dirsrc);
  if(rename(dirsrc, dirdst) < 0)
    fail("dirrename move", dirdst);
  expect_absent(dirsrc);
  if(stat(dirdst, &st) < 0 || st.st_type != T_DIR)
    fail("dirrename stat", dirdst);
  if(mkdir(sub) < 0)
    fail("dirrename subdir", sub);
  if(rename(dirdst, bad) >= 0)
    fail("dirrename subtree", bad);
  if(rmdir(sub) < 0)
    fail("dirrename cleanup", sub);
  if(rmdir(dirdst) < 0)
    fail("dirrename cleanup", dirdst);

  dprintf(1, "fatregress: ok rename\n");
}

static void
check_mkdir_roundtrip(char *mnt)
{
  char dir[FAT_PATH_MAX];
  char file[FAT_PATH_MAX];
  int fd;
  int n;
  struct stat st;

  path_join(mnt, "newdir", dir);
  path_join(mnt, "newdir/inside.txt", file);
  DBG("fatregress[d]: mkdir-roundtrip dir=%s file=%s\n", dir, file);

  /* Clean up from any previous run */
  DBG("fatregress[d]: preclean unlink(%s), rmdir(%s)\n", file, dir);
  unlink(file);
  rmdir(dir);

  if(mkdir(dir) < 0)
    fail("mkdir", dir);

  if(stat(dir, &st) < 0)
    fail("stat dir", dir);
  if(st.st_type != T_DIR)
    fail("dir type", dir);

  fd = open(file, O_CREATE | O_RDWR | O_TRUNC);
  if(fd < 0)
    fail("create in dir", file);
  n = write(fd, "dir test\n", 9);
  if(n != 9){
    close(fd);
    fail("write in dir", file);
  }
  close(fd);
  read_expect(file, "dir test\n");

  if(unlink(file) < 0)
    fail("unlink in dir", file);
  if(rmdir(dir) < 0)
    fail("rmdir", dir);
  expect_absent(dir);
  dprintf(1, "fatregress: ok mkdir/rmdir\n");
}

int
main(int argc, char **argv)
{
  char *mnt;
  int argi;
  int mount_set;

  mnt = FAT_DEFAULT_MOUNT;
  fatreg_debug = 0;
  argi = 1;
  mount_set = 0;

  while(argi < argc){
    if(strcmp(argv[argi], "-d") == 0){
      fatreg_debug = 1;
      argi++;
      continue;
    }
    if(argv[argi][0] == '/' && !mount_set){
      mnt = argv[argi];
      mount_set = 1;
      argi++;
      continue;
    }
    dprintf(1, "usage: fatregress [-d] [mountpoint]\n");
    exit(1);
  }
  DBG("fatregress[d]: parsed mount=%s debug=%d\n", mnt, fatreg_debug);
  require_dir(mnt);

  dprintf(1, "fatregress: testing mountpoint %s\n", mnt);

  check_seeded_reads(mnt);
  check_lfn_seeded_reads(mnt);
  check_small_write(mnt);
  check_growth_and_truncate(mnt);
  check_lfn_write_unlink(mnt);
  check_rename_cycles(mnt);
  check_mkdir_roundtrip(mnt);

  dprintf(1, "fatregress: all checks passed\n");
  exit(0);
}

