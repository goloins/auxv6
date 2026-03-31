#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "fs.h"

#define FSREG_BATCH 16
#define FSREG_PATH_MAX 128

struct scan_target {
  char *path;
  int required;
  int strict_stat;
};

static void
copy_name(char *dst, struct dirent *de)
{
  memmove(dst, de->name, DIRSIZ);
  dst[DIRSIZ] = 0;
}

static int
join_path(char *out, int outsz, char *base, char *name)
{
  int blen;
  int nlen;
  int need;
  int p;

  blen = strlen(base);
  nlen = strlen(name);
  need = blen + ((blen > 1) ? 1 : 0) + nlen + 1;
  if(need > outsz)
    return -1;

  memmove(out, base, blen);
  p = blen;
  if(blen > 1)
    out[p++] = '/';
  memmove(out + p, name, nlen);
  out[p + nlen] = 0;
  return 0;
}

static void
scan_dir(struct scan_target *t, int rounds, int *total_entries)
{
  int r;
  int saw_any;

  saw_any = 0;
  for(r = 0; r < rounds; r++){
    int fd;
    struct stat st;

    fd = open(t->path, O_RDONLY);
    if(fd < 0){
      if(!t->required){
        printf(1, "fsregress: skip %s (not available)\n", t->path);
        return;
      }
      printf(1, "fsregress: FAIL open %s\n", t->path);
      exit();
    }

    if(fstat(fd, &st) < 0 || st.st_type != T_DIR){
      printf(1, "fsregress: FAIL not a dir %s\n", t->path);
      close(fd);
      exit();
    }

    for(;;){
      struct dirent ents[FSREG_BATCH];
      int n;
      int i;

      n = getdents(fd, ents, FSREG_BATCH);
      if(n < 0){
        printf(1, "fsregress: FAIL getdents %s\n", t->path);
        close(fd);
        exit();
      }
      if(n == 0)
        break;

      for(i = 0; i < n; i++){
        char name[DIRSIZ + 1];

        if(ents[i].inum == 0)
          continue;
        copy_name(name, &ents[i]);
        if(name[0] == 0)
          continue;
        saw_any = 1;
        *total_entries = *total_entries + 1;

        if(t->strict_stat && strcmp(name, ".") != 0 && strcmp(name, "..") != 0){
          char full[FSREG_PATH_MAX];
          struct stat est;

          if(join_path(full, sizeof(full), t->path, name) < 0){
            printf(1, "fsregress: FAIL path too long %s/%s\n", t->path, name);
            close(fd);
            exit();
          }
          if(stat(full, &est) < 0){
            printf(1, "fsregress: FAIL stat %s\n", full);
            close(fd);
            exit();
          }
        }
      }
    }

    close(fd);
  }

  if(t->required && !saw_any){
    printf(1, "fsregress: FAIL empty required dir %s\n", t->path);
    exit();
  }

  printf(1, "fsregress: ok %s rounds=%d\n", t->path, rounds);
}

static void
check_mnt_link_cycle(void)
{
  char *a;
  char *b;
  char *c;
  int fd;
  struct stat sa;
  struct stat sb;
  char buf[8];

  a = "/mnt/.fsra";
  b = "/mnt/.fsrb";
  c = "/mnt/.fsrc";

  fd = open("/mnt", O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: skip /mnt link cycle (not available)\n");
    return;
  }
  close(fd);

  unlink(a);
  unlink(b);
  unlink(c);

  fd = open(a, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0){
    printf(1, "fsregress: FAIL create %s\n", a);
    exit();
  }
  if(write(fd, "hi\n", 3) != 3){
    printf(1, "fsregress: FAIL write %s\n", a);
    close(fd);
    exit();
  }
  close(fd);

  if(link(a, b) < 0){
    printf(1, "fsregress: FAIL link %s -> %s\n", a, b);
    exit();
  }

  if(stat(a, &sa) < 0 || stat(b, &sb) < 0){
    printf(1, "fsregress: FAIL stat link pair\n");
    exit();
  }
  if(sa.st_ino != sb.st_ino || sa.st_nlink < 2 || sb.st_nlink < 2){
    printf(1, "fsregress: FAIL bad link metadata\n");
    exit();
  }

  if(unlink(a) < 0){
    printf(1, "fsregress: FAIL unlink %s\n", a);
    exit();
  }

  fd = open(b, O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: FAIL open survivor %s\n", b);
    exit();
  }
  if(read(fd, buf, 3) != 3){
    printf(1, "fsregress: FAIL read survivor %s\n", b);
    close(fd);
    exit();
  }
  close(fd);

  if(unlink(b) < 0){
    printf(1, "fsregress: FAIL final unlink %s\n", b);
    exit();
  }
  if(stat(b, &sb) >= 0){
    printf(1, "fsregress: FAIL stale path after unlink %s\n", b);
    exit();
  }

  fd = open(c, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0){
    printf(1, "fsregress: FAIL recreate after unlink %s\n", c);
    exit();
  }
  if(write(fd, "ok\n", 3) != 3){
    printf(1, "fsregress: FAIL write recreate %s\n", c);
    close(fd);
    exit();
  }
  close(fd);
  if(unlink(c) < 0){
    printf(1, "fsregress: FAIL cleanup %s\n", c);
    exit();
  }

  printf(1, "fsregress: ok /mnt link cycle\n");
}

static void
check_mnt_rename_cycle(void)
{
  char *a;
  char *b;
  char buf[8];
  int fd;
  struct stat sa;
  struct stat sb;

  a = "/mnt/.fsrra";
  b = "/mnt/.fsrrb";

  fd = open("/mnt", O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: skip /mnt rename cycle (not available)\n");
    return;
  }
  close(fd);

  unlink(a);
  unlink(b);

  fd = open(a, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0){
    printf(1, "fsregress: FAIL create %s\n", a);
    exit();
  }
  if(write(fd, "rn\n", 3) != 3){
    printf(1, "fsregress: FAIL write %s\n", a);
    close(fd);
    exit();
  }
  close(fd);

  fd = open(b, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0){
    printf(1, "fsregress: FAIL create %s\n", b);
    exit();
  }
  if(write(fd, "old\n", 4) != 4){
    printf(1, "fsregress: FAIL write %s\n", b);
    close(fd);
    exit();
  }
  close(fd);

  if(rename(a, b) < 0){
    printf(1, "fsregress: FAIL rename %s -> %s\n", a, b);
    exit();
  }
  if(stat(a, &sa) >= 0){
    printf(1, "fsregress: FAIL old path still exists %s\n", a);
    exit();
  }
  if(stat(b, &sb) < 0){
    printf(1, "fsregress: FAIL missing new path %s\n", b);
    exit();
  }
  fd = open(b, O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: FAIL open overwritten path %s\n", b);
    exit();
  }
  if(read(fd, buf, 3) != 3 || buf[0] != 'r' || buf[1] != 'n' || buf[2] != '\n'){
    printf(1, "fsregress: FAIL overwrite content %s\n", b);
    close(fd);
    exit();
  }
  close(fd);

  if(rename(b, b) < 0){
    printf(1, "fsregress: FAIL rename no-op %s\n", b);
    exit();
  }
  if(unlink(b) < 0){
    printf(1, "fsregress: FAIL cleanup %s\n", b);
    exit();
  }

  printf(1, "fsregress: ok /mnt rename cycle\n");
}

static void
check_mnt_crossdir_rename_cycle(void)
{
  char *d1;
  char *d2;
  char *src;
  char *dst;
  char buf[8];
  int fd;
  struct stat st;

  d1 = "/mnt/.fsrd1";
  d2 = "/mnt/.fsrd2";
  src = "/mnt/.fsrd1/src";
  dst = "/mnt/.fsrd2/dst";

  fd = open("/mnt", O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: skip /mnt crossdir rename (not available)\n");
    return;
  }
  close(fd);

  unlink(src);
  unlink(dst);
  unlink(d1);
  unlink(d2);

  if(mkdir(d1) < 0 || mkdir(d2) < 0){
    printf(1, "fsregress: FAIL mkdir crossdir roots\n");
    exit();
  }

  fd = open(src, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0){
    printf(1, "fsregress: FAIL create %s\n", src);
    exit();
  }
  if(write(fd, "xy\n", 3) != 3){
    printf(1, "fsregress: FAIL write %s\n", src);
    close(fd);
    exit();
  }
  close(fd);

  fd = open(dst, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0){
    printf(1, "fsregress: FAIL create %s\n", dst);
    exit();
  }
  if(write(fd, "old\n", 4) != 4){
    printf(1, "fsregress: FAIL write %s\n", dst);
    close(fd);
    exit();
  }
  close(fd);

  if(rename(src, dst) < 0){
    printf(1, "fsregress: FAIL crossdir rename %s -> %s\n", src, dst);
    exit();
  }
  if(stat(src, &st) >= 0){
    printf(1, "fsregress: FAIL stale src after rename %s\n", src);
    exit();
  }
  if(stat(dst, &st) < 0){
    printf(1, "fsregress: FAIL missing dst after rename %s\n", dst);
    exit();
  }

  fd = open(dst, O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: FAIL open dst %s\n", dst);
    exit();
  }
  if(read(fd, buf, 3) != 3 || buf[0] != 'x' || buf[1] != 'y' || buf[2] != '\n'){
    printf(1, "fsregress: FAIL dst content %s\n", dst);
    close(fd);
    exit();
  }
  close(fd);

  if(unlink(dst) < 0 || unlink(d1) < 0 || unlink(d2) < 0){
    printf(1, "fsregress: FAIL crossdir cleanup\n");
    exit();
  }

  printf(1, "fsregress: ok /mnt crossdir rename\n");
}

static void
check_mnt_dir_rename_cycle(void)
{
  char *d1;
  char *d2;
  char *src;
  char *dst;
  char *sub;
  char *bad;
  int fd;
  struct stat st;

  d1 = "/mnt/.fsdd1";
  d2 = "/mnt/.fsdd2";
  src = "/mnt/.fsdd1/src";
  dst = "/mnt/.fsdd2/dst";
  sub = "/mnt/.fsdd2/dst/sub";
  bad = "/mnt/.fsdd2/dst/sub/oops";

  fd = open("/mnt", O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: skip /mnt dir rename (not available)\n");
    return;
  }
  close(fd);

  unlink(sub);
  unlink(dst);
  unlink(src);
  unlink(d1);
  unlink(d2);

  if(mkdir(d1) < 0 || mkdir(d2) < 0 || mkdir(src) < 0){
    printf(1, "fsregress: FAIL mkdir dir rename roots\n");
    exit();
  }

  if(rename(src, dst) < 0){
    printf(1, "fsregress: FAIL dir rename %s -> %s\n", src, dst);
    exit();
  }
  if(stat(src, &st) >= 0){
    printf(1, "fsregress: FAIL stale src dir after rename %s\n", src);
    exit();
  }
  if(stat(dst, &st) < 0 || st.st_type != T_DIR){
    printf(1, "fsregress: FAIL missing dst dir after rename %s\n", dst);
    exit();
  }

  if(mkdir(sub) < 0){
    printf(1, "fsregress: FAIL mkdir subdir under moved dir %s\n", sub);
    exit();
  }

  if(rename(d2, bad) >= 0){
    printf(1, "fsregress: FAIL subtree move unexpectedly succeeded\n");
    exit();
  }

  if(unlink(sub) < 0 || unlink(dst) < 0 || unlink(d1) < 0 || unlink(d2) < 0){
    printf(1, "fsregress: FAIL dir rename cleanup\n");
    exit();
  }

  printf(1, "fsregress: ok /mnt dir rename cycle\n");
}

static void
check_mnt_indirect_write_cycle(void)
{
  char *p;
  char buf[1024];
  int fd;
  int i;
  int j;
  struct stat st;

  p = "/mnt/.fsbig";

  fd = open("/mnt", O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: skip /mnt indirect write (not available)\n");
    return;
  }
  close(fd);

  unlink(p);
  fd = open(p, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0){
    printf(1, "fsregress: FAIL create %s\n", p);
    exit();
  }

  for(i = 0; i < 20; i++){
    char v;

    v = 'A' + (i % 26);
    for(j = 0; j < sizeof(buf); j++)
      buf[j] = v;
    if(write(fd, buf, sizeof(buf)) != sizeof(buf)){
      printf(1, "fsregress: FAIL write block %d %s\n", i, p);
      close(fd);
      exit();
    }
  }
  close(fd);

  if(stat(p, &st) < 0 || st.st_size != 20 * 1024){
    printf(1, "fsregress: FAIL size after indirect write %s\n", p);
    exit();
  }

  fd = open(p, O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: FAIL reopen %s\n", p);
    exit();
  }

  for(i = 0; i < 20; i++){
    char v;

    v = 'A' + (i % 26);
    if(read(fd, buf, sizeof(buf)) != sizeof(buf)){
      printf(1, "fsregress: FAIL read block %d %s\n", i, p);
      close(fd);
      exit();
    }
    for(j = 0; j < sizeof(buf); j++){
      if(buf[j] != v){
        printf(1, "fsregress: FAIL data mismatch block %d byte %d %s\n", i, j, p);
        close(fd);
        exit();
      }
    }
  }
  close(fd);

  if(unlink(p) < 0){
    printf(1, "fsregress: FAIL cleanup %s\n", p);
    exit();
  }

  printf(1, "fsregress: ok /mnt indirect write cycle\n");
}

static void
check_mnt_indirect_edge_cycle(void)
{
  char *p;
  char buf[1024];
  int fd;
  int i;
  int j;
  struct stat st;

  p = "/mnt/.fsedge";

  fd = open("/mnt", O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: skip /mnt indirect edge (not available)\n");
    return;
  }
  close(fd);

  unlink(p);
  fd = open(p, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0){
    printf(1, "fsregress: FAIL create %s\n", p);
    exit();
  }

  // Write exactly direct-block coverage first (12 * 1024), then one more block.
  for(i = 0; i < 13; i++){
    char v;

    v = 'a' + (i % 26);
    for(j = 0; j < sizeof(buf); j++)
      buf[j] = v;
    if(write(fd, buf, sizeof(buf)) != sizeof(buf)){
      printf(1, "fsregress: FAIL boundary write block %d %s\n", i, p);
      close(fd);
      exit();
    }
  }
  close(fd);

  if(stat(p, &st) < 0 || st.st_size != 13 * 1024){
    printf(1, "fsregress: FAIL boundary size %s\n", p);
    exit();
  }

  fd = open(p, O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: FAIL boundary reopen %s\n", p);
    exit();
  }
  for(i = 0; i < 13; i++){
    char v;

    v = 'a' + (i % 26);
    if(read(fd, buf, sizeof(buf)) != sizeof(buf)){
      printf(1, "fsregress: FAIL boundary read block %d %s\n", i, p);
      close(fd);
      exit();
    }
    for(j = 0; j < sizeof(buf); j++){
      if(buf[j] != v){
        printf(1, "fsregress: FAIL boundary data mismatch block %d byte %d %s\n", i, j, p);
        close(fd);
        exit();
      }
    }
  }
  close(fd);

  // Truncate file after indirect usage and confirm size and new contents reset correctly.
  fd = open(p, O_WRONLY | O_TRUNC);
  if(fd < 0){
    printf(1, "fsregress: FAIL boundary trunc open %s\n", p);
    exit();
  }
  for(j = 0; j < sizeof(buf); j++)
    buf[j] = 'Z';
  if(write(fd, buf, sizeof(buf)) != sizeof(buf)){
    printf(1, "fsregress: FAIL boundary rewrite %s\n", p);
    close(fd);
    exit();
  }
  close(fd);

  if(stat(p, &st) < 0 || st.st_size != 1024){
    printf(1, "fsregress: FAIL boundary trunc size %s\n", p);
    exit();
  }

  fd = open(p, O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: FAIL boundary final reopen %s\n", p);
    exit();
  }
  if(read(fd, buf, sizeof(buf)) != sizeof(buf)){
    printf(1, "fsregress: FAIL boundary final read %s\n", p);
    close(fd);
    exit();
  }
  close(fd);
  for(j = 0; j < sizeof(buf); j++){
    if(buf[j] != 'Z'){
      printf(1, "fsregress: FAIL boundary final data mismatch byte %d %s\n", j, p);
      exit();
    }
  }

  if(unlink(p) < 0){
    printf(1, "fsregress: FAIL boundary cleanup %s\n", p);
    exit();
  }

  printf(1, "fsregress: ok /mnt indirect edge cycle\n");
}

static void
check_mnt_append_boundary_cycle(void)
{
  char *p;
  char buf[1024];
  int fd;
  int i;
  int j;
  struct stat st;

  p = "/mnt/.fsapp";

  fd = open("/mnt", O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: skip /mnt append boundary (not available)\n");
    return;
  }
  close(fd);

  unlink(p);
  fd = open(p, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0){
    printf(1, "fsregress: FAIL create %s\n", p);
    exit();
  }
  for(j = 0; j < sizeof(buf); j++)
    buf[j] = 'Q';
  for(i = 0; i < 12; i++){
    if(write(fd, buf, sizeof(buf)) != sizeof(buf)){
      printf(1, "fsregress: FAIL append base write block %d %s\n", i, p);
      close(fd);
      exit();
    }
  }
  close(fd);

  fd = open(p, O_WRONLY | O_APPEND);
  if(fd < 0){
    printf(1, "fsregress: FAIL append open %s\n", p);
    exit();
  }
  for(j = 0; j < sizeof(buf); j++)
    buf[j] = 'R';
  if(write(fd, buf, sizeof(buf)) != sizeof(buf) ||
     write(fd, buf, sizeof(buf)) != sizeof(buf)){
    printf(1, "fsregress: FAIL append writes %s\n", p);
    close(fd);
    exit();
  }
  close(fd);

  if(stat(p, &st) < 0 || st.st_size != 14 * 1024){
    printf(1, "fsregress: FAIL append size %s\n", p);
    exit();
  }

  fd = open(p, O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: FAIL append read open %s\n", p);
    exit();
  }
  for(i = 0; i < 14; i++){
    char v;

    v = (i < 12) ? 'Q' : 'R';
    if(read(fd, buf, sizeof(buf)) != sizeof(buf)){
      printf(1, "fsregress: FAIL append read block %d %s\n", i, p);
      close(fd);
      exit();
    }
    for(j = 0; j < sizeof(buf); j++){
      if(buf[j] != v){
        printf(1, "fsregress: FAIL append data mismatch block %d byte %d %s\n", i, j, p);
        close(fd);
        exit();
      }
    }
  }
  close(fd);

  if(unlink(p) < 0){
    printf(1, "fsregress: FAIL append cleanup %s\n", p);
    exit();
  }

  printf(1, "fsregress: ok /mnt append boundary cycle\n");
}

static void
check_mnt_write_fail_rollback_cycle(void)
{
  char *p;
  char buf[1024];
  char big[2048];
  int fd;
  int i;
  struct stat st;

  p = "/mnt/.fsfail";

  fd = open("/mnt", O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: skip /mnt write fail rollback (not available)\n");
    return;
  }
  close(fd);

  unlink(p);
  fd = open(p, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0){
    printf(1, "fsregress: FAIL create %s\n", p);
    exit();
  }

  for(i = 0; i < sizeof(buf); i++)
    buf[i] = 'W';
  for(i = 0; i < sizeof(big); i++)
    big[i] = 'W';

  // Fail block allocation on the 2nd allocation call in this sequence.
  if(ext2fail(1, 1) < 0){
    printf(1, "fsregress: FAIL ext2fail arm\n");
    close(fd);
    exit();
  }
  if(write(fd, big, sizeof(big)) >= 0){
    printf(1, "fsregress: FAIL expected forced write failure %s\n", p);
    ext2fail(0, 0);
    close(fd);
    exit();
  }
  if(ext2fail(0, 0) < 0){
    printf(1, "fsregress: FAIL ext2fail disarm\n");
    close(fd);
    exit();
  }
  close(fd);

  if(stat(p, &st) < 0 || st.st_size != 0){
    printf(1, "fsregress: FAIL rollback size %s\n", p);
    exit();
  }

  fd = open(p, O_WRONLY | O_TRUNC);
  if(fd < 0){
    printf(1, "fsregress: FAIL rollback reopen %s\n", p);
    exit();
  }
  if(write(fd, buf, sizeof(buf)) != sizeof(buf)){
    printf(1, "fsregress: FAIL rollback rewrite %s\n", p);
    close(fd);
    exit();
  }
  close(fd);

  if(stat(p, &st) < 0 || st.st_size != sizeof(buf)){
    printf(1, "fsregress: FAIL rollback final size %s\n", p);
    exit();
  }

  if(unlink(p) < 0){
    printf(1, "fsregress: FAIL rollback cleanup %s\n", p);
    exit();
  }

  printf(1, "fsregress: ok /mnt write fail rollback cycle\n");
}

static void
check_mnt_unlink_inodefail_cycle(void)
{
  char *p;
  char buf[8];
  int fd;
  struct stat st;

  p = "/mnt/.fsiunlk";

  fd = open("/mnt", O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: skip /mnt unlink inodefail (not available)\n");
    return;
  }
  close(fd);

  unlink(p);
  fd = open(p, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0){
    printf(1, "fsregress: FAIL create %s\n", p);
    exit();
  }
  if(write(fd, "ok\n", 3) != 3){
    printf(1, "fsregress: FAIL seed write %s\n", p);
    close(fd);
    exit();
  }
  close(fd);

  // Force first inode write in unlink path to fail (parent inode update).
  if(ext2fail(2, 0) < 0){
    printf(1, "fsregress: FAIL ext2fail inode arm\n");
    exit();
  }
  if(unlink(p) >= 0){
    printf(1, "fsregress: FAIL expected unlink failure %s\n", p);
    ext2fail(0, 0);
    exit();
  }
  if(ext2fail(0, 0) < 0){
    printf(1, "fsregress: FAIL ext2fail inode disarm\n");
    exit();
  }

  if(stat(p, &st) < 0){
    printf(1, "fsregress: FAIL missing file after failed unlink %s\n", p);
    exit();
  }
  fd = open(p, O_RDONLY);
  if(fd < 0 || read(fd, buf, 3) != 3){
    printf(1, "fsregress: FAIL read survivor after failed unlink %s\n", p);
    if(fd >= 0)
      close(fd);
    exit();
  }
  close(fd);

  if(unlink(p) < 0){
    printf(1, "fsregress: FAIL final unlink %s\n", p);
    exit();
  }

  printf(1, "fsregress: ok /mnt unlink inodefail cycle\n");
}

static void
check_mnt_mkdir_allocfail_cycle(void)
{
  char *p;
  int fd;
  struct stat st;

  p = "/mnt/.fsnospace";

  fd = open("/mnt", O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: skip /mnt mkdir allocfail (not available)\n");
    return;
  }
  close(fd);

  unlink(p);

  // Force allocation failure in ext2_alloc_block() for mkdir data block allocation.
  if(ext2fail(1, 0) < 0){
    printf(1, "fsregress: FAIL ext2fail alloc arm\n");
    exit();
  }
  if(mkdir(p) >= 0){
    printf(1, "fsregress: FAIL expected mkdir allocation failure %s\n", p);
    ext2fail(0, 0);
    exit();
  }
  if(ext2fail(0, 0) < 0){
    printf(1, "fsregress: FAIL ext2fail alloc disarm\n");
    exit();
  }

  if(stat(p, &st) >= 0){
    printf(1, "fsregress: FAIL stale dir after failed mkdir %s\n", p);
    exit();
  }

  if(mkdir(p) < 0){
    printf(1, "fsregress: FAIL mkdir after allocfail %s\n", p);
    exit();
  }
  if(unlink(p) < 0){
    printf(1, "fsregress: FAIL cleanup mkdir allocfail %s\n", p);
    exit();
  }

  printf(1, "fsregress: ok /mnt mkdir allocfail cycle\n");
}

static void
check_mnt_indirect_capacity_cycle(void)
{
  char *p;
  char buf[1024];
  int fd;
  int i;
  int j;
  struct stat st;

  // For ext2 image built with 1KiB blocks, single-indirect coverage is 12 + 256 blocks.
  int max_blocks;

  p = "/mnt/.fscap";
  max_blocks = 12 + 256;

  fd = open("/mnt", O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: skip /mnt indirect capacity (not available)\n");
    return;
  }
  close(fd);

  unlink(p);
  fd = open(p, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0){
    printf(1, "fsregress: FAIL create %s\n", p);
    exit();
  }

  for(j = 0; j < sizeof(buf); j++)
    buf[j] = 'C';
  for(i = 0; i < max_blocks; i++){
    if(write(fd, buf, sizeof(buf)) != sizeof(buf)){
      printf(1, "fsregress: FAIL capacity base write block %d %s\n", i, p);
      close(fd);
      exit();
    }
  }

  if(write(fd, buf, sizeof(buf)) >= 0){
    printf(1, "fsregress: FAIL expected capacity write failure %s\n", p);
    close(fd);
    exit();
  }
  close(fd);

  if(stat(p, &st) < 0 || st.st_size != max_blocks * 1024){
    printf(1, "fsregress: FAIL capacity size %s\n", p);
    exit();
  }

  fd = open(p, O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: FAIL capacity reopen %s\n", p);
    exit();
  }
  for(i = 0; i < max_blocks; i++){
    if(read(fd, buf, sizeof(buf)) != sizeof(buf)){
      printf(1, "fsregress: FAIL capacity read block %d %s\n", i, p);
      close(fd);
      exit();
    }
    for(j = 0; j < sizeof(buf); j++){
      if(buf[j] != 'C'){
        printf(1, "fsregress: FAIL capacity data mismatch block %d byte %d %s\n", i, j, p);
        close(fd);
        exit();
      }
    }
  }
  close(fd);

  if(unlink(p) < 0){
    printf(1, "fsregress: FAIL capacity cleanup %s\n", p);
    exit();
  }

  printf(1, "fsregress: ok /mnt indirect capacity cycle\n");
}

static void
check_mnt_churn_cycle(void)
{
  char *base;
  char *lnk;
  char *mv;
  int i;
  int fd;
  struct stat s1;
  struct stat s2;
  char buf[8];

  base = "/mnt/.fschurn_a";
  lnk = "/mnt/.fschurn_b";
  mv = "/mnt/.fschurn_c";

  fd = open("/mnt", O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: skip /mnt churn (not available)\n");
    return;
  }
  close(fd);

  for(i = 0; i < 100; i++){
    unlink(base);
    unlink(lnk);
    unlink(mv);

    fd = open(base, O_CREATE | O_WRONLY | O_TRUNC);
    if(fd < 0){
      printf(1, "fsregress: FAIL churn create iter=%d\n", i);
      exit();
    }
    if(write(fd, "ch\n", 3) != 3){
      printf(1, "fsregress: FAIL churn write iter=%d\n", i);
      close(fd);
      exit();
    }
    close(fd);

    if(link(base, lnk) < 0){
      printf(1, "fsregress: FAIL churn link iter=%d\n", i);
      exit();
    }
    if(rename(lnk, mv) < 0){
      printf(1, "fsregress: FAIL churn rename iter=%d\n", i);
      exit();
    }

    if(stat(base, &s1) < 0 || stat(mv, &s2) < 0){
      printf(1, "fsregress: FAIL churn stat iter=%d\n", i);
      exit();
    }
    if(s1.st_ino != s2.st_ino || s1.st_nlink < 2){
      printf(1, "fsregress: FAIL churn metadata iter=%d\n", i);
      exit();
    }

    fd = open(mv, O_RDONLY);
    if(fd < 0 || read(fd, buf, 3) != 3){
      printf(1, "fsregress: FAIL churn read iter=%d\n", i);
      if(fd >= 0)
        close(fd);
      exit();
    }
    close(fd);

    if(unlink(base) < 0 || unlink(mv) < 0){
      printf(1, "fsregress: FAIL churn cleanup iter=%d\n", i);
      exit();
    }
  }

  printf(1, "fsregress: ok /mnt churn cycle\n");
}

static void
check_mnt_generic_fsfault_cycle(void)
{
  char *p;
  char buf[1024];
  char big[2048];
  int fd;
  int i;
  struct stat st;

  // ext2 is mounted on dev 2 in this setup.
  int ext2dev;

  p = "/mnt/.fsgen";
  ext2dev = 2;

  fd = open("/mnt", O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: skip /mnt generic fsfault (not available)\n");
    return;
  }
  close(fd);

  unlink(p);
  fd = open(p, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0){
    printf(1, "fsregress: FAIL create %s\n", p);
    exit();
  }

  for(i = 0; i < sizeof(buf); i++)
    buf[i] = 'G';
  for(i = 0; i < sizeof(big); i++)
    big[i] = 'G';

  if(fsfault(ext2dev, 1, 1) < 0){
    printf(1, "fsregress: FAIL fsfault arm\n");
    close(fd);
    exit();
  }
  if(write(fd, big, sizeof(big)) >= 0){
    printf(1, "fsregress: FAIL expected generic fsfault write failure %s\n", p);
    fsfault(ext2dev, 0, 0);
    close(fd);
    exit();
  }
  if(fsfault(ext2dev, 0, 0) < 0){
    printf(1, "fsregress: FAIL fsfault disarm\n");
    close(fd);
    exit();
  }
  close(fd);

  if(stat(p, &st) < 0 || st.st_size != 0){
    printf(1, "fsregress: FAIL generic fsfault rollback size %s\n", p);
    exit();
  }

  fd = open(p, O_WRONLY | O_TRUNC);
  if(fd < 0){
    printf(1, "fsregress: FAIL generic fsfault reopen %s\n", p);
    exit();
  }
  if(write(fd, buf, sizeof(buf)) != sizeof(buf)){
    printf(1, "fsregress: FAIL generic fsfault rewrite %s\n", p);
    close(fd);
    exit();
  }
  close(fd);

  if(unlink(p) < 0){
    printf(1, "fsregress: FAIL generic fsfault cleanup %s\n", p);
    exit();
  }

  printf(1, "fsregress: ok /mnt generic fsfault cycle\n");
}

static void
check_mnt_devnode_cycle(void)
{
  char *cpath;
  char *bpath;
  int dev;
  int fd;
  char buf[32];
  struct stat st;

  cpath = "/mnt/.fsrdevc";
  bpath = "/mnt/.fsrdevb";

  fd = open("/mnt", O_RDONLY);
  if(fd < 0){
    printf(1, "fsregress: skip /mnt devnode cycle (not available)\n");
    return;
  }
  close(fd);

  unlink(cpath);
  unlink(bpath);

  if(mknod(cpath, M_IFCHR, 1, 7) < 0){
    printf(1, "fsregress: FAIL mknod %s\n", cpath);
    exit();
  }

  if(stat(cpath, &st) < 0){
    printf(1, "fsregress: FAIL stat dev %s\n", cpath);
    exit();
  }
  if(st.st_type != T_DEV){
    printf(1, "fsregress: FAIL wrong type dev %s\n", cpath);
    exit();
  }
  if((st.st_mode & M_IFMT) != M_IFCHR || st.st_major != 1 || st.st_minor != 7){
    printf(1, "fsregress: FAIL bad dev ids major=%d minor=%d\n", st.st_major, st.st_minor);
    exit();
  }

  dev = -1;
  for(fd = 0; fd < HD_DISK_UNITS; fd++){
    if(devblocks(HD_DISK_DEV(fd)) > 0){
      dev = HD_DISK_DEV(fd);
      break;
    }
  }
  if(dev >= 0){
    if(mknod(bpath, M_IFBLK, 2, dev) < 0){
      printf(1, "fsregress: FAIL block mknod %s\n", bpath);
      exit();
    }
    if(stat(bpath, &st) < 0){
      printf(1, "fsregress: FAIL stat block dev %s\n", bpath);
      exit();
    }
    if(st.st_type != T_DEV || (st.st_mode & M_IFMT) != M_IFBLK || st.st_major != 2 || st.st_minor != dev){
      printf(1, "fsregress: FAIL bad block dev metadata major=%d minor=%d mode=%x\n", st.st_major, st.st_minor, st.st_mode);
      exit();
    }
    fd = open(bpath, O_RDONLY);
    if(fd < 0){
      printf(1, "fsregress: FAIL open block dev %s\n", bpath);
      exit();
    }
    if(read(fd, buf, sizeof(buf)) != sizeof(buf)){
      printf(1, "fsregress: FAIL read block dev %s\n", bpath);
      close(fd);
      exit();
    }
    close(fd);
  }

  if(unlink(cpath) < 0){
    printf(1, "fsregress: FAIL cleanup dev %s\n", cpath);
    exit();
  }
  if(dev >= 0 && unlink(bpath) < 0){
    printf(1, "fsregress: FAIL cleanup block dev %s\n", bpath);
    exit();
  }

  printf(1, "fsregress: ok /mnt devnode cycle\n");
}

int
main(int argc, char *argv[])
{
  struct scan_target targets[] = {
    { "/",    1, 1 },
    { "/proc", 1, 0 },
    { "/mnt",  0, 1 },
  };
  int rounds;
  int total_entries;
  int i;

  rounds = 20;
  if(argc > 1)
    rounds = atoi(argv[1]);
  if(rounds <= 0)
    rounds = 1;

  total_entries = 0;
  printf(1, "fsregress: start rounds=%d\n", rounds);

  for(i = 0; i < sizeof(targets)/sizeof(targets[0]); i++)
    scan_dir(&targets[i], rounds, &total_entries);

  check_mnt_link_cycle();
  check_mnt_rename_cycle();
  check_mnt_crossdir_rename_cycle();
  check_mnt_dir_rename_cycle();
  check_mnt_indirect_write_cycle();
  check_mnt_indirect_edge_cycle();
  check_mnt_append_boundary_cycle();
  check_mnt_write_fail_rollback_cycle();
  check_mnt_unlink_inodefail_cycle();
  check_mnt_mkdir_allocfail_cycle();
  check_mnt_indirect_capacity_cycle();
  check_mnt_churn_cycle();
  check_mnt_generic_fsfault_cycle();
  check_mnt_devnode_cycle();

  printf(1, "fsregress: PASS entries=%d\n", total_entries);
  exit();
}
