#include "types.h"
#include "fcntl.h"
#include "auxv6/user.h"

#define DEFAULT_CTL_PATH "/tmp/audiod.ctl"
#define PS_PATH "/proc/ps"

static char *
skip_spaces(char *p)
{
  while(*p == ' ' || *p == '\t')
    p++;
  return p;
}

static char *
next_token(char *p)
{
  p = skip_spaces(p);
  while(*p && *p != ' ' && *p != '\t' && *p != '\n')
    p++;
  return p;
}

static int
parse_uint_token(char **pp)
{
  int v;
  char *p;

  p = skip_spaces(*pp);
  v = 0;
  while(*p >= '0' && *p <= '9'){
    v = v * 10 + (*p - '0');
    p++;
  }
  *pp = p;
  return v;
}

static int
parse_ps_name(char *line, char *name_out, int name_sz)
{
  int i;
  char *p;
  char *q;
  int n;

  p = skip_spaces(line);
  if(*p == 0 || *p == '\n')
    return -1;
  if(*p < '0' || *p > '9')
    return -1;

  (void)parse_uint_token(&p);
  for(i = 0; i < 9; i++)
    p = next_token(p);

  p = skip_spaces(p);
  q = p;
  while(*q && *q != ' ' && *q != '\t' && *q != '\n')
    q++;

  n = q - p;
  if(n <= 0)
    return -1;
  if(n >= name_sz)
    n = name_sz - 1;
  memmove(name_out, p, n);
  name_out[n] = 0;
  return 0;
}

static int
audiod_is_running(void)
{
  int fd;
  int nread;
  int i;
  int llen;
  char chunk[256];
  char line[256];
  char name[32];

  fd = open(PS_PATH, O_RDONLY);
  if(fd < 0)
    return 0;

  llen = 0;
  while((nread = read(fd, chunk, sizeof(chunk))) > 0){
    for(i = 0; i < nread; i++){
      if(llen < (int)sizeof(line) - 1)
        line[llen++] = chunk[i];
      if(chunk[i] != '\n')
        continue;

      line[llen] = 0;
      if(parse_ps_name(line, name, sizeof(name)) == 0){
        if(strcmp(name, "audiod") == 0){
          close(fd);
          return 1;
        }
      }
      llen = 0;
    }
  }

  close(fd);
  return 0;
}

static void
usage(void)
{
    dprintf(2,
      "usage:\n"
      "  audiodctl [ctl-path] status\n"
      "  audiodctl [ctl-path] set <rate> <channels> <format> <period_frames> <periods> <buffer_frames>\n"
      "  audiodctl [ctl-path] set-write <bytes>\n"
      "  audiodctl [ctl-path] set-timeout <ms>\n"
      "  audiodctl [ctl-path] track-load <slot> <path>   (play raw PCM file once)\n"
      "  audiodctl [ctl-path] track-loop <slot> <path>   (loop raw PCM file)\n"
      "  audiodctl [ctl-path] track-stop <slot>\n"
      "  audiodctl [ctl-path] track-gain <slot> <shift>  (0=full, 1=half, ...)\n");
  exit(1);
}

static int
open_ctl(const char *path)
{
  int fd;

  fd = open(path, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0)
    dprintf(2, "audiodctl: cannot open %s\n", path);
  return fd;
}

int
main(int argc, char **argv)
{
  const char *ctl_path;
  const char *cmd;
  int had_audiod;
  int ai;
  int fd;

  ctl_path = DEFAULT_CTL_PATH;
  ai = 1;

  if(argc < 2)
    usage();
  if(argv[1][0] == '/'){
    ctl_path = argv[1];
    ai = 2;
  }
  if(ai >= argc)
    usage();

  cmd = argv[ai];
  had_audiod = audiod_is_running();

  fd = open_ctl(ctl_path);
  if(fd < 0)
    exit(1);

  if(strcmp(cmd, "status") == 0){
    dprintf(fd, "status\n");
  } else if(strcmp(cmd, "set") == 0){
    if(ai + 6 >= argc)
      usage();
    dprintf(fd, "set %s %s %s %s %s %s\n",
            argv[ai + 1], argv[ai + 2], argv[ai + 3],
            argv[ai + 4], argv[ai + 5], argv[ai + 6]);
  } else if(strcmp(cmd, "set-write") == 0){
    if(ai + 1 >= argc)
      usage();
    dprintf(fd, "set-write %s\n", argv[ai + 1]);
  } else if(strcmp(cmd, "set-timeout") == 0){
    if(ai + 1 >= argc)
      usage();
    dprintf(fd, "set-timeout %s\n", argv[ai + 1]);
  } else if(strcmp(cmd, "track-load") == 0 || strcmp(cmd, "track-loop") == 0){
    if(ai + 2 >= argc)
      usage();
    dprintf(fd, "%s %s %s\n", cmd, argv[ai + 1], argv[ai + 2]);
  } else if(strcmp(cmd, "track-stop") == 0){
    if(ai + 1 >= argc)
      usage();
    dprintf(fd, "track-stop %s\n", argv[ai + 1]);
  } else if(strcmp(cmd, "track-gain") == 0){
    if(ai + 2 >= argc)
      usage();
    dprintf(fd, "track-gain %s %s\n", argv[ai + 1], argv[ai + 2]);
  } else {
    usage();
  }

  close(fd);
  dprintf(1, "audiodctl: queued '%s' to %s\n", cmd, ctl_path);
  if(!had_audiod)
    dprintf(2, "audiodctl: warning: no running audiod process detected; command will be consumed on next audiod loop\n");
  exit(0);
}
