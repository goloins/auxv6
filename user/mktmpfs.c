#include "types.h"
#include "stat.h"
#include "auxv6/user.h"

static void
usage(void)
{
  dprintf(2, "usage: mktmpfs <mountpoint> <size>\n");
  exit(1);
}

static int
parse_size(const char *s, uint *out)
{
  uint value;
  uint mult;
  int i;

  if(s == 0 || *s == 0 || out == 0)
    return -1;

  value = 0;
  for(i = 0; s[i] >= '0' && s[i] <= '9'; i++){
    value = value * 10 + (s[i] - '0');
  }
  if(i == 0)
    return -1;

  mult = 1;
  if(s[i] != 0){
    if(s[i + 1] != 0)
      return -1;
    switch(s[i]){
    case 'k':
    case 'K':
      mult = 1024;
      break;
    case 'm':
    case 'M':
      mult = 1024 * 1024;
      break;
    case 'g':
    case 'G':
      mult = 1024 * 1024 * 1024;
      break;
    default:
      return -1;
    }
  }

  if(mult != 0 && value > (0xffffffffU / mult))
    return -1;
  value *= mult;
  if(value == 0)
    return -1;

  *out = value;
  return 0;
}

static int
utoa(uint value, char *buf, int max)
{
  char tmp[16];
  int n;
  int i;

  if(buf == 0 || max <= 1)
    return -1;

  n = 0;
  if(value == 0){
    tmp[n++] = '0';
  } else {
    while(value > 0 && n < (int)sizeof(tmp)){
      tmp[n++] = '0' + (value % 10);
      value /= 10;
    }
  }

  if(n + 1 > max)
    return -1;
  for(i = 0; i < n; i++)
    buf[i] = tmp[n - 1 - i];
  buf[n] = 0;
  return n;
}

int
main(int argc, char *argv[])
{
  struct stat st;
  char data[64];
  char sizebuf[32];
  uint bytes;
  int n;

  if(argc != 3)
    usage();

  if(parse_size(argv[2], &bytes) < 0){
    dprintf(2, "mktmpfs: invalid size '%s'\n", argv[2]);
    return 1;
  }

  if(stat(argv[1], &st) < 0 || st.st_type != T_DIR){
    dprintf(2, "mktmpfs: mountpoint must be an existing directory\n");
    return 1;
  }

  n = utoa(bytes, sizebuf, sizeof(sizebuf));
  if(n < 0)
    return 1;

  strcpy(data, "size=");
  if(strlen(data) + strlen(sizebuf) + 1 > sizeof(data))
    return 1;
  memmove(data + strlen(data), sizebuf, strlen(sizebuf) + 1);

  if(mount(argv[1], "tmpfs", 0, data, strlen(data)) < 0){
    dprintf(2, "mktmpfs: mount failed\n");
    return 1;
  }

  return 0;
}
