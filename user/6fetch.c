#include "types.h"
#include "sys/stat.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "string.h"
#include "pwd.h"
#include "stdio.h"

#define UNAME_RAW_MAX 128
#define HOST_MAX       64
#define FIELD_MAX      48
#define MEMINFO_MAX   256

static void
trim(char *buf)
{
  int i;

  for(i = strlen(buf) - 1; i >= 0; i--) {
    if(buf[i] == '\n' || buf[i] == '\r' ||
       buf[i] == ' '  || buf[i] == '\t')
      buf[i] = '\0';
    else
      break;
  }
}

static int
read_text_file(const char *path, char *buf, int bufsz)
{
  int fd;
  int n;

  fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;
  n = read(fd, buf, bufsz - 1);
  close(fd);
  if(n <= 0)
    return -1;
  buf[n] = '\0';
  trim(buf);
  return 0;
}

static void
parse_uname(const char *raw, char *sysname, char *release, char *machine, int sz)
{
  int i;
  char *fields[3];
  char tmp[UNAME_RAW_MAX];
  char *tok;
  char *start;

  fields[0] = sysname;
  fields[1] = release;
  fields[2] = machine;

  strncpy(tmp, raw, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';

  tok = tmp;
  for(i = 0; i < 3; i++) {
    while(*tok == ' ')
      tok++;
    start = tok;
    while(*tok && *tok != ' ')
      tok++;
    if(*tok == ' ')
      *tok++ = '\0';

    if(*start == '\0')
      strncpy(fields[i], "unknown", sz);
    else
      strncpy(fields[i], start, sz - 1);
    fields[i][sz - 1] = '\0';
  }
}

static void
format_uptime(int ticks, char *buf, int bufsz)
{
  int secs;
  int days;
  int hours;
  int mins;

  if(ticks < 0)
    ticks = 0;
  secs = ticks / 100;
  days = secs / 86400;
  secs %= 86400;
  hours = secs / 3600;
  secs %= 3600;
  mins = secs / 60;

  if(days > 0)
    snprintf(buf, bufsz, "%dd %02dh %02dm", days, hours, mins);
  else
    snprintf(buf, bufsz, "%02dh %02dm", hours, mins);
}

static int
parse_mem_kb(const char *line)
{
  const char *p;

  p = line;
  while(*p && (*p < '0' || *p > '9'))
    p++;
  if(!*p)
    return -1;
  return atoi(p);
}

static void
read_meminfo(int *total_kb, int *free_kb)
{
  char buf[MEMINFO_MAX];
  char *s;
  char *line;
  char *next;

  *total_kb = -1;
  *free_kb = -1;

  if(read_text_file("/proc/meminfo", buf, sizeof(buf)) < 0)
    return;

  s = buf;
  while(*s) {
    line = s;
    while(*s && *s != '\n')
      s++;
    if(*s == '\n') {
      *s = '\0';
      s++;
    }
    next = s;

    if(strncmp(line, "MemTotal:", 9) == 0)
      *total_kb = parse_mem_kb(line);
    else if(strncmp(line, "MemFree:", 8) == 0)
      *free_kb = parse_mem_kb(line);

    s = next;
  }
}

int
main(void)
{
  char raw[UNAME_RAW_MAX];
  char sysname[FIELD_MAX];
  char release[FIELD_MAX];
  char machine[FIELD_MAX];
  char host[HOST_MAX];
  char user[32];
  char uptime_str[32];
  int total_kb;
  int free_kb;
  int used_mb;
  int total_mb;
  struct passwd *pw;

  if(uname(raw, sizeof(raw)) < 0)
    strncpy(raw, "auxv6 unknown i386", sizeof(raw) - 1);
  raw[sizeof(raw) - 1] = '\0';
  trim(raw);
  parse_uname(raw, sysname, release, machine, FIELD_MAX);

  if(read_text_file("/etc/hostname", host, sizeof(host)) < 0)
    strncpy(host, "localhost", sizeof(host));
  host[sizeof(host) - 1] = '\0';

  pw = getpwuid((uid_t)getuid());
  if(pw == 0)
    strncpy(user, "unknown", sizeof(user));
  else
    snprintf(user, sizeof(user), "%s", pw->pw_name);
  user[sizeof(user) - 1] = '\0';

  format_uptime(uptime(), uptime_str, sizeof(uptime_str));

  read_meminfo(&total_kb, &free_kb);
  if(total_kb > 0 && free_kb >= 0) {
    total_mb = total_kb / 1024;
    used_mb = (total_kb - free_kb) / 1024;
  } else {
    total_mb = -1;
    used_mb = -1;
  }

  dprintf(1, "      .--.\n");
  dprintf(1, "     |o_o |   %s@%s\n", user, host);
  dprintf(1, "     |:_/ |   os:      %s\n", sysname);
  dprintf(1, "    //   \\ \\  kernel:  %s\n", release);
  dprintf(1, "   (|     | ) machine: %s\n", machine);
  dprintf(1, "  /'\\_   _/`\\ uptime:  %s\n", uptime_str);
  if(total_mb >= 0)
    dprintf(1, "  \\___)=(___/ memory:  %d MiB / %d MiB\n", used_mb, total_mb);
  else
    dprintf(1, "  \\___)=(___/ memory:  unknown\n");

  exit(0);
}
