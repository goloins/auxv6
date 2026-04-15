#include "types.h"
#include "errno.h"
#include "fcntl.h"
#include "grp.h"
#include "pwd.h"
#include "shadow.h"
#include "string.h"
#include "limits.h"
#include "auxv6/user.h"

#define ACCOUNT_DB_MAX    4096
#define ACCOUNT_FIELD_MAX 128
#define GROUP_MEMBERS_MAX 32

static int
accountdb_copy(char *dst, int dstsz, const char *src, int len)
{
  if(dstsz <= 0)
    return 0;
  if(len < 0)
    len = 0;
  if(len >= dstsz)
    len = dstsz - 1;
  if(len > 0)
    memmove(dst, src, len);
  dst[len] = 0;
  return len;
}

static int
accountdb_parse_uint(const char *src, int len, uint *value)
{
  uint out;
  int i;

  if(src == 0 || len <= 0 || value == 0)
    return -1;

  out = 0;
  for(i = 0; i < len; i++) {
    if(src[i] < '0' || src[i] > '9')
      return -1;
    out = out * 10 + (uint)(src[i] - '0');
  }

  *value = out;
  return 0;
}

static int
accountdb_parse_long(const char *src, int len, long *value)
{
  long out;
  int i;

  if(src == 0 || value == 0)
    return -1;
  if(len == 0) {
    *value = -1;
    return 0;
  }

  out = 0;
  for(i = 0; i < len; i++) {
    if(src[i] < '0' || src[i] > '9')
      return -1;
    out = out * 10 + (long)(src[i] - '0');
  }

  *value = out;
  return 0;
}

static int
accountdb_read(const char *path, const char *fallback, char *buf, int bufsz)
{
  int fd;
  int n;

  if(buf == 0 || bufsz <= 1) {
    errno = EINVAL;
    return -1;
  }

  fd = open(path, O_RDONLY);
  if(fd < 0 && fallback != 0)
    fd = open(fallback, O_RDONLY);
  if(fd < 0)
    return -1;

  n = read(fd, buf, bufsz - 1);
  close(fd);
  if(n < 0)
    return -1;

  buf[n] = 0;
  return n;
}

static int
accountdb_next_line(char *buf, int n, int *off, char **line, int *linelen)
{
  int start;
  int end;

  if(buf == 0 || off == 0 || line == 0 || linelen == 0)
    return 0;

  while(*off < n && (buf[*off] == '\n' || buf[*off] == '\r'))
    (*off)++;
  while(*off < n && buf[*off] == '#') {
    while(*off < n && buf[*off] != '\n' && buf[*off] != '\r')
      (*off)++;
    while(*off < n && (buf[*off] == '\n' || buf[*off] == '\r'))
      (*off)++;
  }
  if(*off >= n)
    return 0;

  start = *off;
  end = start;
  while(end < n && buf[end] != '\n' && buf[end] != '\r')
    end++;

  *line = buf + start;
  *linelen = end - start;
  *off = end;
  return 1;
}

static int
accountdb_split_fields(char *line, int linelen, char **fields, int *lengths, int maxfields)
{
  int start;
  int nf;
  int i;

  if(line == 0 || fields == 0 || lengths == 0 || maxfields <= 0)
    return 0;

  start = 0;
  nf = 0;
  for(i = 0; i <= linelen; i++) {
    if(i == linelen || line[i] == ':') {
      if(nf < maxfields) {
        fields[nf] = line + start;
        lengths[nf] = i - start;
        nf++;
      }
      start = i + 1;
    }
  }
  return nf;
}

static char pw_iter_buf[ACCOUNT_DB_MAX];
static int pw_iter_n = -1;
static int pw_iter_off = 0;

void
setpwent(void)
{
  pw_iter_n = accountdb_read("/etc/passwd", 0, pw_iter_buf, sizeof(pw_iter_buf));
  pw_iter_off = 0;
}

void
endpwent(void)
{
  pw_iter_n = -1;
  pw_iter_off = 0;
}

struct passwd *
getpwent(void)
{
  static struct passwd pw;
  static char namebuf[ACCOUNT_FIELD_MAX];
  static char passbuf[ACCOUNT_FIELD_MAX];
  static char gecosbuf[ACCOUNT_FIELD_MAX];
  static char dirbuf[ACCOUNT_FIELD_MAX];
  static char shellbuf[ACCOUNT_FIELD_MAX];

  if(pw_iter_n < 0)
    setpwent();
  if(pw_iter_n < 0)
    return 0;

  while(pw_iter_off < pw_iter_n) {
    char *fields[8];
    int lengths[8];
    char *line;
    int linelen;
    int nf;
    uint uidval;
    uint gidval;

    if(!accountdb_next_line(pw_iter_buf, pw_iter_n, &pw_iter_off, &line, &linelen))
      break;
    nf = accountdb_split_fields(line, linelen, fields, lengths, 8);
    if(nf < 7)
      continue;
    if(accountdb_parse_uint(fields[2], lengths[2], &uidval) < 0 ||
       accountdb_parse_uint(fields[3], lengths[3], &gidval) < 0)
      continue;

    accountdb_copy(namebuf, sizeof(namebuf), fields[0], lengths[0]);
    accountdb_copy(passbuf, sizeof(passbuf), fields[1], lengths[1]);
    accountdb_copy(gecosbuf, sizeof(gecosbuf), fields[4], lengths[4]);
    accountdb_copy(dirbuf, sizeof(dirbuf), fields[5], lengths[5]);
    if(lengths[6] > 0)
      accountdb_copy(shellbuf, sizeof(shellbuf), fields[6], lengths[6]);
    else
      accountdb_copy(shellbuf, sizeof(shellbuf), "/bin/sh", strlen("/bin/sh"));

    pw.pw_name = namebuf;
    pw.pw_passwd = passbuf;
    pw.pw_uid = (uid_t)uidval;
    pw.pw_gid = (gid_t)gidval;
    pw.pw_gecos = gecosbuf;
    pw.pw_dir = dirbuf;
    pw.pw_shell = shellbuf;
    return &pw;
  }

  return 0;
}

struct passwd *
getpwnam(const char *name)
{
  static char buf[ACCOUNT_DB_MAX];
  static struct passwd pw;
  static char namebuf[ACCOUNT_FIELD_MAX];
  static char passbuf[ACCOUNT_FIELD_MAX];
  static char gecosbuf[ACCOUNT_FIELD_MAX];
  static char dirbuf[ACCOUNT_FIELD_MAX];
  static char shellbuf[ACCOUNT_FIELD_MAX];
  int n;
  int off;

  if(name == 0 || *name == 0) {
    errno = EINVAL;
    return 0;
  }

  n = accountdb_read("/etc/passwd", 0, buf, sizeof(buf));
  if(n < 0)
    return 0;

  off = 0;
  while(off < n) {
    char *fields[8];
    int lengths[8];
    char *line;
    int linelen;
    int nf;
    uint uid;
    uint gid;

    if(!accountdb_next_line(buf, n, &off, &line, &linelen))
      break;
    nf = accountdb_split_fields(line, linelen, fields, lengths, 8);
    if(nf < 7)
      continue;
    if(lengths[0] != (int)strlen(name))
      continue;
    if(strncmp(fields[0], name, lengths[0]) != 0)
      continue;
    if(accountdb_parse_uint(fields[2], lengths[2], &uid) < 0 ||
       accountdb_parse_uint(fields[3], lengths[3], &gid) < 0) {
      errno = EINVAL;
      return 0;
    }

    accountdb_copy(namebuf, sizeof(namebuf), fields[0], lengths[0]);
    accountdb_copy(passbuf, sizeof(passbuf), fields[1], lengths[1]);
    accountdb_copy(gecosbuf, sizeof(gecosbuf), fields[4], lengths[4]);
    accountdb_copy(dirbuf, sizeof(dirbuf), fields[5], lengths[5]);
    if(lengths[6] > 0)
      accountdb_copy(shellbuf, sizeof(shellbuf), fields[6], lengths[6]);
    else
      accountdb_copy(shellbuf, sizeof(shellbuf), "/bin/sh", strlen("/bin/sh"));

    pw.pw_name = namebuf;
    pw.pw_passwd = passbuf;
    pw.pw_uid = (uid_t)uid;
    pw.pw_gid = (gid_t)gid;
    pw.pw_gecos = gecosbuf;
    pw.pw_dir = dirbuf;
    pw.pw_shell = shellbuf;
    return &pw;
  }

  errno = ENOENT;
  return 0;
}

struct passwd *
getpwuid(uid_t uid)
{
  static char buf[ACCOUNT_DB_MAX];
  static char namebuf[ACCOUNT_FIELD_MAX];
  static char passbuf[ACCOUNT_FIELD_MAX];
  static char gecosbuf[ACCOUNT_FIELD_MAX];
  static char dirbuf[ACCOUNT_FIELD_MAX];
  static char shellbuf[ACCOUNT_FIELD_MAX];
  static struct passwd pw;
  int n;
  int off;

  n = accountdb_read("/etc/passwd", 0, buf, sizeof(buf));
  if(n < 0)
    return 0;

  off = 0;
  while(off < n) {
    char *fields[8];
    int lengths[8];
    char *line;
    int linelen;
    int nf;
    uint uidval;
    uint gidval;

    if(!accountdb_next_line(buf, n, &off, &line, &linelen))
      break;
    nf = accountdb_split_fields(line, linelen, fields, lengths, 8);
    if(nf < 7)
      continue;
    if(accountdb_parse_uint(fields[2], lengths[2], &uidval) < 0 ||
       accountdb_parse_uint(fields[3], lengths[3], &gidval) < 0) {
      errno = EINVAL;
      return 0;
    }
    if((uid_t)uidval != uid)
      continue;

    accountdb_copy(namebuf, sizeof(namebuf), fields[0], lengths[0]);
    accountdb_copy(passbuf, sizeof(passbuf), fields[1], lengths[1]);
    accountdb_copy(gecosbuf, sizeof(gecosbuf), fields[4], lengths[4]);
    accountdb_copy(dirbuf, sizeof(dirbuf), fields[5], lengths[5]);
    if(lengths[6] > 0)
      accountdb_copy(shellbuf, sizeof(shellbuf), fields[6], lengths[6]);
    else
      accountdb_copy(shellbuf, sizeof(shellbuf), "/bin/sh", strlen("/bin/sh"));

    pw.pw_name = namebuf;
    pw.pw_passwd = passbuf;
    pw.pw_uid = (uid_t)uidval;
    pw.pw_gid = (gid_t)gidval;
    pw.pw_gecos = gecosbuf;
    pw.pw_dir = dirbuf;
    pw.pw_shell = shellbuf;
    return &pw;
  }

  errno = ENOENT;
  return 0;
}

struct spwd *
getspnam(const char *name)
{
  static char buf[ACCOUNT_DB_MAX];
  static struct spwd sp;
  static char namebuf[ACCOUNT_FIELD_MAX];
  static char passbuf[ACCOUNT_FIELD_MAX];
  int n;
  int off;

  if(name == 0 || *name == 0) {
    errno = EINVAL;
    return 0;
  }

  n = accountdb_read("/etc/shadow", 0, buf, sizeof(buf));
  if(n < 0)
    return 0;

  off = 0;
  while(off < n) {
    char *fields[9];
    int lengths[9];
    char *line;
    int linelen;
    int nf;

    if(!accountdb_next_line(buf, n, &off, &line, &linelen))
      break;
    nf = accountdb_split_fields(line, linelen, fields, lengths, 9);
    if(nf < 2)
      continue;
    if(lengths[0] != (int)strlen(name))
      continue;
    if(strncmp(fields[0], name, lengths[0]) != 0)
      continue;

    accountdb_copy(namebuf, sizeof(namebuf), fields[0], lengths[0]);
    accountdb_copy(passbuf, sizeof(passbuf), fields[1], lengths[1]);

    sp.sp_namp = namebuf;
    sp.sp_pwdp = passbuf;
    sp.sp_lstchg = -1;
    sp.sp_min = -1;
    sp.sp_max = -1;
    sp.sp_warn = -1;
    sp.sp_inact = -1;
    sp.sp_expire = -1;
    sp.sp_flag = 0;

    if(nf > 2 && accountdb_parse_long(fields[2], lengths[2], &sp.sp_lstchg) < 0) {
      errno = EINVAL;
      return 0;
    }
    if(nf > 3 && accountdb_parse_long(fields[3], lengths[3], &sp.sp_min) < 0) {
      errno = EINVAL;
      return 0;
    }
    if(nf > 4 && accountdb_parse_long(fields[4], lengths[4], &sp.sp_max) < 0) {
      errno = EINVAL;
      return 0;
    }
    if(nf > 5 && accountdb_parse_long(fields[5], lengths[5], &sp.sp_warn) < 0) {
      errno = EINVAL;
      return 0;
    }
    if(nf > 6 && accountdb_parse_long(fields[6], lengths[6], &sp.sp_inact) < 0) {
      errno = EINVAL;
      return 0;
    }
    if(nf > 7 && accountdb_parse_long(fields[7], lengths[7], &sp.sp_expire) < 0) {
      errno = EINVAL;
      return 0;
    }
    if(nf > 8) {
      long flag;
      if(accountdb_parse_long(fields[8], lengths[8], &flag) < 0) {
        errno = EINVAL;
        return 0;
      }
      sp.sp_flag = (unsigned long)flag;
    }

    return &sp;
  }

  errno = ENOENT;
  return 0;
}

struct group *
getgrnam(const char *name)
{
  static char buf[ACCOUNT_DB_MAX];
  static struct group gr;
  static char namebuf[ACCOUNT_FIELD_MAX];
  static char passbuf[ACCOUNT_FIELD_MAX];
  static char membersbuf[ACCOUNT_FIELD_MAX];
  static char *members[GROUP_MEMBERS_MAX + 1];
  int n;
  int off;

  if(name == 0 || *name == 0) {
    errno = EINVAL;
    return 0;
  }

  n = accountdb_read("/etc/group", "/etc/groups", buf, sizeof(buf));
  if(n < 0)
    return 0;

  off = 0;
  while(off < n) {
    char *fields[4];
    int lengths[4];
    char *line;
    int linelen;
    int nf;
    uint gid;
    int member_count;

    if(!accountdb_next_line(buf, n, &off, &line, &linelen))
      break;
    nf = accountdb_split_fields(line, linelen, fields, lengths, 4);
    if(nf < 2)
      continue;
    if(lengths[0] != (int)strlen(name))
      continue;
    if(strncmp(fields[0], name, lengths[0]) != 0)
      continue;

    if(nf >= 3) {
      if(accountdb_parse_uint(fields[2], lengths[2], &gid) < 0) {
        errno = EINVAL;
        return 0;
      }
      accountdb_copy(passbuf, sizeof(passbuf), fields[1], lengths[1]);
      if(nf >= 4)
        accountdb_copy(membersbuf, sizeof(membersbuf), fields[3], lengths[3]);
      else
        membersbuf[0] = 0;
    } else {
      if(accountdb_parse_uint(fields[1], lengths[1], &gid) < 0) {
        errno = EINVAL;
        return 0;
      }
      passbuf[0] = 0;
      membersbuf[0] = 0;
    }

    accountdb_copy(namebuf, sizeof(namebuf), fields[0], lengths[0]);
    member_count = 0;
    if(membersbuf[0] != 0) {
      char *p;

      p = membersbuf;
      while(*p && member_count < GROUP_MEMBERS_MAX) {
        members[member_count++] = p;
        while(*p && *p != ',')
          p++;
        if(*p == ',')
          *p++ = 0;
      }
    }
    members[member_count] = 0;

    gr.gr_name = namebuf;
    gr.gr_passwd = passbuf;
    gr.gr_gid = (gid_t)gid;
    gr.gr_mem = members;
    return &gr;
  }

  errno = ENOENT;
  return 0;
}

struct group *
getgrgid(gid_t gid)
{
  static char buf[ACCOUNT_DB_MAX];
  static struct group gr;
  static char namebuf[ACCOUNT_FIELD_MAX];
  static char passbuf[ACCOUNT_FIELD_MAX];
  static char membersbuf[ACCOUNT_FIELD_MAX];
  static char *members[GROUP_MEMBERS_MAX + 1];
  int n;
  int off;

  n = accountdb_read("/etc/group", "/etc/groups", buf, sizeof(buf));
  if(n < 0)
    return 0;

  off = 0;
  while(off < n) {
    char *fields[4];
    int lengths[4];
    char *line;
    int linelen;
    int nf;
    uint gidval;
    int member_count;

    if(!accountdb_next_line(buf, n, &off, &line, &linelen))
      break;
    nf = accountdb_split_fields(line, linelen, fields, lengths, 4);
    if(nf < 2)
      continue;

    if(nf >= 3) {
      if(accountdb_parse_uint(fields[2], lengths[2], &gidval) < 0) {
        errno = EINVAL;
        return 0;
      }
      accountdb_copy(passbuf, sizeof(passbuf), fields[1], lengths[1]);
      if(nf >= 4)
        accountdb_copy(membersbuf, sizeof(membersbuf), fields[3], lengths[3]);
      else
        membersbuf[0] = 0;
    } else {
      if(accountdb_parse_uint(fields[1], lengths[1], &gidval) < 0) {
        errno = EINVAL;
        return 0;
      }
      passbuf[0] = 0;
      membersbuf[0] = 0;
    }
    if((gid_t)gidval != gid)
      continue;

    accountdb_copy(namebuf, sizeof(namebuf), fields[0], lengths[0]);
    member_count = 0;
    if(membersbuf[0] != 0) {
      char *p;

      p = membersbuf;
      while(*p && member_count < GROUP_MEMBERS_MAX) {
        members[member_count++] = p;
        while(*p && *p != ',')
          p++;
        if(*p == ',')
          *p++ = 0;
      }
    }
    members[member_count] = 0;

    gr.gr_name = namebuf;
    gr.gr_passwd = passbuf;
    gr.gr_gid = (gid_t)gidval;
    gr.gr_mem = members;
    return &gr;
  }

  errno = ENOENT;
  return 0;
}
/* Sequential group file iteration */
static char  _grent_buf[ACCOUNT_DB_MAX];
static int   _grent_n   = -1;   /* -1 = not loaded */
static int   _grent_off = 0;

void
setgrent(void)
{
  _grent_n = accountdb_read("/etc/group", "/etc/groups",
                             _grent_buf, sizeof(_grent_buf));
  _grent_off = 0;
}

void
endgrent(void)
{
  _grent_n   = -1;
  _grent_off = 0;
}

struct group *
getgrent(void)
{
  static struct group     gr;
  static char  namebuf[ACCOUNT_FIELD_MAX];
  static char  passbuf[ACCOUNT_FIELD_MAX];
  static char  membersbuf[ACCOUNT_FIELD_MAX];
  static char *members[GROUP_MEMBERS_MAX + 1];
  char *line;
  int   linelen;
  char *fields[4];
  int   lengths[4];
  int   nf;
  uint  gidval;
  int   member_count;

  if(_grent_n < 0)
    setgrent();
  if(_grent_n <= 0)
    return 0;

  while(_grent_off < _grent_n) {
    if(!accountdb_next_line(_grent_buf, _grent_n, &_grent_off, &line, &linelen))
      break;
    nf = accountdb_split_fields(line, linelen, fields, lengths, 4);
    if(nf < 2)
      continue;

    if(nf >= 3) {
      if(accountdb_parse_uint(fields[2], lengths[2], &gidval) < 0)
        continue;
      accountdb_copy(passbuf, sizeof(passbuf), fields[1], lengths[1]);
      if(nf >= 4)
        accountdb_copy(membersbuf, sizeof(membersbuf), fields[3], lengths[3]);
      else
        membersbuf[0] = 0;
    } else {
      if(accountdb_parse_uint(fields[1], lengths[1], &gidval) < 0)
        continue;
      passbuf[0] = 0;
      membersbuf[0] = 0;
    }

    accountdb_copy(namebuf, sizeof(namebuf), fields[0], lengths[0]);
    member_count = 0;
    if(membersbuf[0]) {
      char *p = membersbuf;
      while(*p && member_count < GROUP_MEMBERS_MAX) {
        members[member_count++] = p;
        while(*p && *p != ',') p++;
        if(*p == ',') *p++ = 0;
      }
    }
    members[member_count] = 0;
    gr.gr_name   = namebuf;
    gr.gr_passwd = passbuf;
    gr.gr_gid    = (gid_t)gidval;
    gr.gr_mem    = members;
    return &gr;
  }
  return 0;
}

int
initgroups(const char *user, gid_t group)
{
  gid_t gids[NGROUPS_MAX];
  int ngids;
  struct group *gr;
  int i;

  if(user == 0 || *user == 0) {
    errno = EINVAL;
    return -1;
  }

  ngids = 0;
  if(group >= 0)
    gids[ngids++] = group;

  setgrent();
  while((gr = getgrent()) != 0) {
    if(gr->gr_mem) {
      for(i = 0; gr->gr_mem[i] != 0; i++) {
        if(strcmp(gr->gr_mem[i], user) == 0) {
          int j;
          int exists;

          exists = 0;
          for(j = 0; j < ngids; j++) {
            if(gids[j] == gr->gr_gid) {
              exists = 1;
              break;
            }
          }
          if(!exists && ngids < NGROUPS_MAX)
            gids[ngids++] = gr->gr_gid;
          break;
        }
      }
    }
  }
  endgrent();

  return setgroups((size_t)ngids, gids);
}
