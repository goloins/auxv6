#include "types.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "errno.h"
#include "string.h"
#include "time.h"
#include "utmp.h"
#include "utmpx.h"
#include "lastlog.h"

#define UTMPX_FILE "/var/run/utmp"
#define WTMPX_FILE "/var/log/wtmp"
#define LASTLOG_FILE "/var/log/lastlog"

static int utmpx_fd = -1;

static int
open_utmpx_rw(void)
{
  if(utmpx_fd >= 0)
    return utmpx_fd;

  utmpx_fd = open(UTMPX_FILE, O_CREATE | O_RDWR);
  return utmpx_fd;
}

static void
copy_cstr(char *dst, int dstsz, const char *src)
{
  int n;

  if(dst == 0 || dstsz <= 0)
    return;
  if(src == 0) {
    dst[0] = 0;
    return;
  }

  n = strlen(src);
  if(n >= dstsz)
    n = dstsz - 1;
  memmove(dst, src, n);
  dst[n] = 0;
}

void
setutxent(void)
{
  int fd;

  fd = open_utmpx_rw();
  if(fd < 0)
    return;
  lseek(fd, 0, SEEK_SET);
}

void
endutxent(void)
{
  if(utmpx_fd >= 0) {
    close(utmpx_fd);
    utmpx_fd = -1;
  }
}

struct utmpx *
getutxent(void)
{
  static struct utmpx out;
  int fd;
  int n;

  fd = open_utmpx_rw();
  if(fd < 0)
    return 0;

  n = read(fd, &out, sizeof(out));
  if(n != sizeof(out))
    return 0;

  return &out;
}

struct utmpx *
getutxid(const struct utmpx *id)
{
  struct utmpx *u;

  if(id == 0)
    return 0;

  setutxent();
  while((u = getutxent()) != 0) {
    if(u->ut_type != id->ut_type)
      continue;
    if(id->ut_id[0] != 0 && memcmp(u->ut_id, id->ut_id, sizeof(u->ut_id)) != 0)
      continue;
    return u;
  }
  return 0;
}

struct utmpx *
getutxline(const struct utmpx *line)
{
  struct utmpx *u;

  if(line == 0)
    return 0;

  setutxent();
  while((u = getutxent()) != 0) {
    if(u->ut_type != USER_PROCESS && u->ut_type != LOGIN_PROCESS)
      continue;
    if(strncmp(u->ut_line, line->ut_line, sizeof(u->ut_line)) == 0)
      return u;
  }
  return 0;
}

struct utmpx *
pututxline(const struct utmpx *ut)
{
  static struct utmpx out;
  struct utmpx cur;
  int fd;
  int n;
  int off;

  if(ut == 0) {
    errno = EINVAL;
    return 0;
  }

  fd = open_utmpx_rw();
  if(fd < 0)
    return 0;

  off = 0;
  lseek(fd, 0, SEEK_SET);
  while((n = read(fd, &cur, sizeof(cur))) == sizeof(cur)) {
    if(cur.ut_line[0] != 0 && strncmp(cur.ut_line, ut->ut_line, sizeof(cur.ut_line)) == 0)
      break;
    off += sizeof(cur);
  }

  if(n == sizeof(cur))
    lseek(fd, off, SEEK_SET);
  else
    lseek(fd, 0, SEEK_END);

  if(write(fd, ut, sizeof(*ut)) != sizeof(*ut))
    return 0;

  out = *ut;
  return &out;
}

void
updwtmpx(const char *wtmpx_file, const struct utmpx *utx)
{
  int fd;

  if(utx == 0)
    return;

  if(wtmpx_file == 0)
    wtmpx_file = WTMPX_FILE;

  fd = open(wtmpx_file, O_CREATE | O_WRONLY | O_APPEND);
  if(fd < 0)
    return;
  write(fd, utx, sizeof(*utx));
  close(fd);
}

void
setutent(void)
{
  setutxent();
}

void
endutent(void)
{
  endutxent();
}

struct utmp *
getutent(void)
{
  return (struct utmp*)getutxent();
}

struct utmp *
getutid(const struct utmp *id)
{
  return (struct utmp*)getutxid((const struct utmpx*)id);
}

struct utmp *
getutline(const struct utmp *line)
{
  return (struct utmp*)getutxline((const struct utmpx*)line);
}

struct utmp *
pututline(const struct utmp *ut)
{
  return (struct utmp*)pututxline((const struct utmpx*)ut);
}

void
updwtmp(const char *wtmp_file, const struct utmp *ut)
{
  updwtmpx(wtmp_file, (const struct utmpx*)ut);
}

void
logwtmp(const char *line, const char *name, const char *host)
{
  struct utmpx utx;

  memset(&utx, 0, sizeof(utx));
  utx.ut_type = USER_PROCESS;
  utx.ut_pid = getpid();
  copy_cstr(utx.ut_line, sizeof(utx.ut_line), line);
  copy_cstr(utx.ut_user, sizeof(utx.ut_user), name);
  copy_cstr(utx.ut_host, sizeof(utx.ut_host), host);
  utx.ut_tv.tv_sec = time(0);
  utx.ut_tv.tv_usec = 0;

  updwtmpx(WTMPX_FILE, &utx);
}

int
write_lastlog(uid_t uid, const char *line, const char *host, time_t when)
{
  int fd;
  struct lastlog ll;
  off_t off;

  if(uid < 0)
    return -1;

  fd = open(LASTLOG_FILE, O_CREATE | O_RDWR);
  if(fd < 0)
    return -1;

  memset(&ll, 0, sizeof(ll));
  ll.ll_time = when;
  copy_cstr(ll.ll_line, sizeof(ll.ll_line), line);
  copy_cstr(ll.ll_host, sizeof(ll.ll_host), host);

  off = (off_t)uid * (off_t)sizeof(ll);
  if(lseek(fd, off, SEEK_SET) < 0) {
    close(fd);
    return -1;
  }
  if(write(fd, &ll, sizeof(ll)) != sizeof(ll)) {
    close(fd);
    return -1;
  }

  close(fd);
  return 0;
}
