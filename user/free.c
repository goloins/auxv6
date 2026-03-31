#include "types.h"
#include "user.h"
#include "fcntl.h"
static char *human_kb(int kb);
static int
read_text(const char *path, char *buf, int max)
{
  int fd;
  int n;
  int off;

  fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;

  off = 0;
  while(off < max - 1){
    n = read(fd, buf + off, max - 1 - off);
    if(n < 0){
      close(fd);
      return -1;
    }
    if(n == 0)
      break;
    off += n;
  }
  buf[off] = 0;
  close(fd);
  return off;
}

static int
find_kb_value(char *text, const char *key)
{
  int i;
  int j;

  for(i = 0; text[i]; i++){
    for(j = 0; key[j] && text[i + j] == key[j]; j++)
      ;
    if(key[j] != 0)
      continue;

    i += j;
    while(text[i] && (text[i] < '0' || text[i] > '9'))
      i++;
    return atoi(&text[i]);
  }

  return -1;
}

int
main(int argc, char *argv[])
{
  char buf[256];
  int total_kb;
  int free_kb;
  int used_kb;
  int human;

  human = 0;
  if(argc == 2 && strcmp(argv[1], "-h") == 0)
    human = 1;
  else if(argc != 1){
    printf(2, "usage: free [-h]\n");
    exit();
  }

  if(read_text("/proc/meminfo", buf, sizeof(buf)) < 0){
    printf(2, "free: cannot read /proc/meminfo\n");
    exit();
  }

  total_kb = find_kb_value(buf, "MemTotal:");
  free_kb = find_kb_value(buf, "MemFree:");
  if(total_kb < 0 || free_kb < 0){
    printf(2, "free: malformed /proc/meminfo\n");
    exit();
  }

    used_kb = total_kb - free_kb;
  if(used_kb < 0)
    used_kb = 0;

  if(!human){
    printf(1, "              total        used        free\n");
    printf(1, "Mem:    %11d %11d %11d\n", total_kb, used_kb, free_kb);
  } else {
    printf(1, "              total        used        free\n");
    printf(1, "Mem:    %11s %11s %11s\n",
      human_kb(total_kb), human_kb(used_kb), human_kb(free_kb));
  }

  exit();
}

static char hbuf[3][16];
static int  hbuf_idx;

static char *
human_kb(int kb)
{
  char *buf;
  int   val;
  char  unit;
  int   i;

  buf = hbuf[hbuf_idx % 3];
  hbuf_idx++;

  if(kb >= 1024 * 1024){
    val  = kb / (1024 * 1024);
    unit = 'G';
  } else if(kb >= 1024){
    val  = kb / 1024;
    unit = 'M';
  } else {
    val  = kb;
    unit = 'K';
  }

  /* manual itoa+unit — no snprintf in userspace */
  i = 14;
  buf[15] = 0;
  buf[i--] = unit;
  if(val == 0){
    buf[i--] = '0';
  } else {
    while(val > 0){
      buf[i--] = '0' + (val % 10);
      val /= 10;
    }
  }
  return &buf[i + 1];
}