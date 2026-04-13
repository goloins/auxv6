#include "types.h"
#include "auxv6/user.h"
#include "fcntl.h"

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
next_tok(char **pp, char *tok, int max)
{
  char *p;
  int n;

  p = *pp;
  while(*p == ' ' || *p == '\t')
    p++;
  if(*p == 0 || *p == '\n')
    return 0;

  n = 0;
  while(*p && *p != ' ' && *p != '\t' && *p != '\n'){
    if(n < max - 1)
      tok[n++] = *p;
    p++;
  }
  tok[n] = 0;
  *pp = p;
  return 1;
}

static void
humanize_parts(uint bytes, uint *value, char *unit)
{
  static char units[] = {'B', 'K', 'M', 'G'};
  uint div;
  uint idx;

  div = 1;
  idx = 0;
  while(idx < 3 && bytes / div >= 1024){
    div *= 1024;
    idx++;
  }

  *value = bytes / div;
  *unit = units[idx];
}

int
main(int argc, char *argv[])
{
  char text[2048];
  char *p;
  char *line;
  char *nl;
  char tok[6][32];
  int t;
  int human;
  int dev;
  int total;
  int freeb;
  int bsize;
  int used;
  uint total_bytes;
  uint used_bytes;
  uint avail_bytes;
  uint htotal;
  uint hused;
  uint havail;
  char utotal;
  char uused;
  char uavail;

  human = 0;
  if(argc == 2 && strcmp(argv[1], "-h") == 0)
    human = 1;
  else if(argc != 1){
    dprintf(2, "usage: df [-h]\n");
    exit(0);
  }

  if(read_text("/proc/mountstats", text, sizeof(text)) < 0){
    dprintf(2, "df: cannot read /proc/mountstats\n");
    exit(0);
  }

  if(!human)
    dprintf(1, "Filesystem 1K-blocks      Used Available Mounted on\n");
  else
    dprintf(1, "Filesystem      Size      Used     Avail Mounted on\n");

  p = text;
  while(*p){
    line = p;
    while(*p && *p != '\n')
      p++;
    if(*p == '\n')
      *p++ = 0;

    if(line[0] == 0 || line[0] == 'd')
      continue;

    nl = line;
    for(t = 0; t < 6; t++){
      if(!next_tok(&nl, tok[t], sizeof(tok[t])))
        break;
    }
    if(t < 6)
      continue;

    dev = atoi(tok[0]);
    total = atoi(tok[3]);
    freeb = atoi(tok[4]);
    bsize = atoi(tok[5]);
    if(total < 0)
      total = 0;
    if(freeb < 0)
      freeb = 0;
    if(freeb > total)
      freeb = total;
    used = total - freeb;

    if(!human){
      dprintf(1, "%-10s %9d %9d %9d %s\n",
             tok[2],
             (total * bsize) / 1024,
             (used * bsize) / 1024,
             (freeb * bsize) / 1024,
             tok[1]);
    } else {
      total_bytes = (uint)total * (uint)bsize;
      used_bytes = (uint)used * (uint)bsize;
      avail_bytes = (uint)freeb * (uint)bsize;
            humanize_parts(total_bytes, &htotal, &utotal);
            humanize_parts(used_bytes, &hused, &uused);
            humanize_parts(avail_bytes, &havail, &uavail);
            dprintf(1, "%-10s %8u%c %8u%c %8u%c %s\n",
              tok[2], htotal, utotal, hused, uused, havail, uavail, tok[1]);
    }

    (void)dev;
  }

  exit(0);
}
