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

    if(fstat(fd, &st) < 0 || st.type != T_DIR){
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
  if(sa.ino != sb.ino || sa.nlink < 2 || sb.nlink < 2){
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

  printf(1, "fsregress: PASS entries=%d\n", total_entries);
  exit();
}
