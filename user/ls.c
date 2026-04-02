#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fs.h"
#include "fcntl.h"

static char*
uid_to_name(int uid)
{
  static char buf[512];
  static char result[32];
  static int loaded = 0;
  int fd, n, i, j;

  if(!loaded){
    fd = open("/etc/passwd", O_RDONLY);
    if(fd >= 0){
      n = read(fd, buf, sizeof(buf) - 1);
      close(fd);
      if(n > 0) buf[n] = 0;
      else buf[0] = 0;
    } else {
      buf[0] = 0;
    }
    loaded = 1;
  }

  i = 0;
  n = strlen(buf);
  while(i < n){
    int fstart[4], flen[4], nf, uidval;
    if(buf[i] == '#'){ while(i < n && buf[i] != '\n') i++; i++; continue; }
    nf = 0; fstart[0] = i;
    for(j = i; j <= n; j++){
      if(j == n || buf[j] == '\n' || buf[j] == '\r' || buf[j] == ':'){
        if(nf < 4){ flen[nf] = j - fstart[nf]; nf++; }
        if(j == n || buf[j] != ':' || nf >= 4){ i = j + 1; break; }
        if(nf < 4) fstart[nf] = j + 1;
      }
    }
    if(nf < 3) continue;
    uidval = 0;
    for(j = 0; j < flen[2]; j++){
      char c = buf[fstart[2] + j];
      if(c < '0' || c > '9'){ uidval = -1; break; }
      uidval = uidval * 10 + (c - '0');
    }
    if(uidval != uid) continue;
    j = flen[0];
    if(j >= (int)sizeof(result)) j = sizeof(result) - 1;
    memmove(result, buf + fstart[0], j);
    result[j] = 0;
    return result;
  }

  /* fallback: print numeric */
  {
    uint u = (uid < 0) ? 0 : (uint)uid;
    int k = 0;
    char tmp[12];
    if(u == 0){ tmp[0] = '0'; k = 1; }
    else { while(u > 0){ tmp[k++] = '0' + u%10; u /= 10; } }
    for(j = 0; j < k; j++) result[j] = tmp[k - 1 - j];
    result[k] = 0;
  }
  return result;
}

static char*
gid_to_name(int gid)
{
  static char buf[512];
  static char result[32];
  static int loaded = 0;
  int fd, n, i, j;

  if(!loaded){
    fd = open("/etc/groups", O_RDONLY);
    if(fd >= 0){
      n = read(fd, buf, sizeof(buf) - 1);
      close(fd);
      if(n > 0) buf[n] = 0;
      else buf[0] = 0;
    } else {
      buf[0] = 0;
    }
    loaded = 1;
  }

  i = 0;
  n = strlen(buf);
  while(i < n){
    int fstart[2], flen[2], nf, gidval;
    if(buf[i] == '#'){ while(i < n && buf[i] != '\n') i++; i++; continue; }
    nf = 0; fstart[0] = i;
    for(j = i; j <= n; j++){
      if(j == n || buf[j] == '\n' || buf[j] == '\r' || buf[j] == ':'){
        if(nf < 2){ flen[nf] = j - fstart[nf]; nf++; }
        if(j == n || buf[j] != ':' || nf >= 2){ i = j + 1; break; }
        if(nf < 2) fstart[nf] = j + 1;
      }
    }
    if(nf < 2) continue;
    gidval = 0;
    for(j = 0; j < flen[1]; j++){
      char c = buf[fstart[1] + j];
      if(c < '0' || c > '9'){ gidval = -1; break; }
      gidval = gidval * 10 + (c - '0');
    }
    if(gidval != gid) continue;
    j = flen[0];
    if(j >= (int)sizeof(result)) j = sizeof(result) - 1;
    memmove(result, buf + fstart[0], j);
    result[j] = 0;
    return result;
  }

  /* fallback: print numeric */
  {
    uint g = (gid < 0) ? 0 : (uint)gid;
    int k = 0;
    char tmp[12];
    if(g == 0){ tmp[0] = '0'; k = 1; }
    else { while(g > 0){ tmp[k++] = '0' + g%10; g /= 10; } }
    for(j = 0; j < k; j++) result[j] = tmp[k - 1 - j];
    result[k] = 0;
  }
  return result;
}

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  return buf;
}

static char*
fmtsize(uint size)
{
  static char buf[12];
  uint val, frac, v;
  char suf;
  int i, j;
  char tmp[10];

  if(size >= 1073741824U){
    val = size / 1073741824U;
    frac = (size % 1073741824U) * 10 / 1073741824U;
    suf = 'G';
  } else if(size >= 1048576){
    val = size / 1048576;
    frac = (size % 1048576) * 10 / 1048576;
    suf = 'M';
  } else if(size >= 1024){
    val = size / 1024;
    frac = (size % 1024) * 10 / 1024;
    suf = 'K';
  } else {
    val = size;
    frac = 0;
    suf = 'B';
  }

  i = 0;
  j = 0;
  v = val;
  if(v == 0){
    tmp[j++] = '0';
  } else {
    while(v > 0){ tmp[j++] = '0' + (v % 10); v /= 10; }
  }
  while(j > 0) buf[i++] = tmp[--j];
  if(suf != 'B' && frac > 0){
    buf[i++] = '.';
    buf[i++] = '0' + frac;
  }
  buf[i++] = suf;
  buf[i] = 0;
  return buf;
}

void
ls(char *path)
{
  char buf[512], *p;
  int fd;
  struct dirent des[16];
  struct stat st;

  if((fd = open(path, 0)) < 0){
    printf(2, "ls: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    printf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.st_type){
  case T_FILE:
    printf(1, "%s %d %s %s %s %d%d%d%d\n", fmtname(path), st.st_type, uid_to_name(st.st_uid), gid_to_name(st.st_gid), fmtsize(st.st_size), (st.st_mode>>9)&7, (st.st_mode>>6)&7, (st.st_mode>>3)&7, st.st_mode&7);
    break;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf(1, "ls: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    for(;;){
      int nent;
      int i;

      nent = getdents(fd, des, 16);
      if(nent < 0){
        printf(1, "ls: getdents failed %s\n", path);
        break;
      }
      if(nent == 0)
        break;

      for(i = 0; i < nent; i++){
        memmove(p, des[i].name, DIRSIZ);
        p[DIRSIZ] = 0;
        if(stat(buf, &st) < 0){
          printf(1, "ls: cannot stat %s\n", buf);
          continue;
        }
        printf(1, "%s %d %s %s %s %d%d%d%d\n", fmtname(buf), st.st_type, uid_to_name(st.st_uid), gid_to_name(st.st_gid), fmtsize(st.st_size), (st.st_mode>>9)&7, (st.st_mode>>6)&7, (st.st_mode>>3)&7, st.st_mode&7);
      }
    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){
    ls(".");
    exit();
  }
  for(i=1; i<argc; i++)
    ls(argv[i]);
  exit();
}
