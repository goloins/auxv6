#include "types.h"
#include "fcntl.h"
#include "signal.h"
#include "user.h"

#define PS_PATH "/proc/ps"
#define PS_BUF_SIZE 512
#define LINE_MAX 256

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
parse_ps_line(char *line, int *pid_out, char *name_out, int name_sz)
{
  int pid;
  int i;
  char *p;
  char *q;
  int n;

  p = skip_spaces(line);
  if(*p == 0 || *p == '\n')
    return -1;
  if(*p < '0' || *p > '9')
    return -1;

  pid = parse_uint_token(&p);

  for(i = 0; i < 8; i++)
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
  *pid_out = pid;
  return 0;
}

int
main(int argc, char *argv[])
{
  int fd;
  int nread;
  int i;
  int sig;
  int argi;
  int self;
  int found;
  int killed;
  char chunk[PS_BUF_SIZE];
  char line[LINE_MAX];
  int llen;
  char pname[32];

  sig = SIGTERM;
  argi = 1;

  if(argc < 2){
    printf(2, "usage: killall [-signo] name...\n");
    exit();
  }

  if(argv[argi][0] == '-'){
    sig = atoi(argv[argi] + 1);
    argi++;
  }

  if(argi >= argc){
    printf(2, "usage: killall [-signo] name...\n");
    exit();
  }

  fd = open(PS_PATH, O_RDONLY);
  if(fd < 0){
    printf(2, "killall: cannot open %s\n", PS_PATH);
    exit();
  }

  self = getpid();
  found = 0;
  killed = 0;
  llen = 0;

  while((nread = read(fd, chunk, sizeof(chunk))) > 0){
    for(i = 0; i < nread; i++){
      char c;
      int pid;
      int a;

      c = chunk[i];
      if(llen < LINE_MAX - 1)
        line[llen++] = c;

      if(c != '\n')
        continue;

      line[llen] = 0;
      if(parse_ps_line(line, &pid, pname, sizeof(pname)) == 0){
        for(a = argi; a < argc; a++){
          if(strcmp(pname, argv[a]) == 0){
            found++;
            if(pid > 1 && pid != self){
              if(sigsend(pid, sig) == 0){
                killed++;
                printf(1, "killall: matched pid %d (%s)\n", pid, pname);
              } else {
                printf(2, "killall: matched pid %d (%s), signal failed\n", pid, pname);
              }
            } else {
              printf(1, "killall: matched pid %d (%s), skipped\n", pid, pname);
            }
            break;
          }
        }
      }
      llen = 0;
    }
  }

  close(fd);

  if(nread < 0){
    printf(2, "killall: read error\n");
    exit();
  }

  if(found == 0){
    printf(2, "killall: no matching processes\n");
    exit();
  }

  printf(1, "killall: matched %d, signaled %d\n", found, killed);
  exit();
}
