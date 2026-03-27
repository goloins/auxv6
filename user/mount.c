#include "types.h"
#include "user.h"
#include "stat.h"
#include "fcntl.h"

static int
parse_int(const char *s)
{
  int v;

  v = 0;
  while(*s >= '0' && *s <= '9'){
    v = v * 10 + (*s - '0');
    s++;
  }
  return v;
}

static int
parse_flags(const char *s)
{
  if(strcmp(s, "rw") == 0)
    return 0;
  if(strcmp(s, "ro") == 0)
    return 1;
  return parse_int(s);
}

static char*
skip_space(char *s)
{
  while(*s == ' ' || *s == '\t')
    s++;
  return s;
}

static int
mount_from_fstab(const char *fstab)
{
  char buf[1024];
  char *line;
  int fd;
  int n;
  int mounted;
  int failed;

  fd = open(fstab, O_RDONLY);
  if(fd < 0){
    printf(2, "mount: cannot open %s\n", fstab);
    return -1;
  }

  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(n < 0){
    printf(2, "mount: cannot read %s\n", fstab);
    return -1;
  }
  buf[n] = 0;

  mounted = 0;
  failed = 0;
  line = buf;
  while(*line){
    char *cur;
    char *nl;
    char *f0;
    char *f1;
    char *f2;
    char *f3;
    char *path;
    char *fstype;
    int flags;

    nl = line;
    while(*nl && *nl != '\n')
      nl++;
    if(*nl == '\n'){
      *nl = 0;
      nl++;
    }

    cur = skip_space(line);
    if(*cur == 0 || *cur == '#'){
      line = nl;
      continue;
    }

    f0 = cur;
    while(*cur && *cur != ' ' && *cur != '\t')
      cur++;
    if(*cur == 0){
      line = nl;
      continue;
    }
    *cur++ = 0;

    cur = skip_space(cur);
    if(*cur == 0){
      line = nl;
      continue;
    }
    f1 = cur;
    while(*cur && *cur != ' ' && *cur != '\t')
      cur++;
    if(*cur)
      *cur++ = 0;

    cur = skip_space(cur);
    f2 = cur;
    while(*cur && *cur != ' ' && *cur != '\t')
      cur++;
    if(*cur)
      *cur++ = 0;

    cur = skip_space(cur);
    f3 = cur;
    while(*cur && *cur != ' ' && *cur != '\t')
      cur++;
    *cur = 0;

    // Accept both formats:
    // 1) <mountpoint> <fstype> <opts>
    // 2) <dev> <mountpoint> <fstype> <opts>
    if(f2[0] != 0 && f1[0] == '/'){
      path = f1;
      fstype = f2;
      flags = (f3[0] == 0) ? 0 : parse_flags(f3);
    } else {
      path = f0;
      fstype = f1;
      flags = (f2[0] == 0) ? 0 : parse_flags(f2);
    }

    if(mount(path, fstype, flags) < 0){
      printf(2, "mount: %s %s failed\n", path, fstype);
      failed++;
    } else {
      mounted++;
    }

    line = nl;
  }

  if(failed)
    return -1;
  return mounted;
}

int
main(int argc, char *argv[])
{
  char *path;
  char *fstype;
  int flags;

  if(argc == 1){
    struct mountinfo entries[MOUNTINFO_MAX];
    int i;
    int n;

    n = mountinfo(entries, MOUNTINFO_MAX);
    if(n < 0){
      printf(2, "mount: mountinfo failed\n");
      exit();
    }
    for(i = 0; i < n; i++)
      printf(1, "%s on %s flags=%d\n", entries[i].fstype, entries[i].path, entries[i].flags);
    exit();
  }

  if(argc == 2){
    if(mount_from_fstab(argv[1]) < 0)
      exit();
    exit();
  }

  if(argc < 3 || argc > 4){
    printf(2, "usage: mount [fstab]|<path> <fstype> [flags]\n");
    exit();
  }

  path = argv[1];
  fstype = argv[2];
  flags = (argc == 4) ? parse_flags(argv[3]) : 0;

  if(mount(path, fstype, flags) < 0)
    printf(2, "mount: %s %s failed\n", path, fstype);

  exit();
}
