#include "types.h"
#include "sys/stat.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "stdio.h"
#include "accountdb.h"

#ifndef O_CREATE
#ifdef O_CREAT
#define O_CREATE O_CREAT
#endif
#endif

static int
adb_streq(const char *a, const char *b)
{
  while(*a && *b) {
    if(*a != *b)
      return 0;
    a++;
    b++;
  }
  return *a == 0 && *b == 0;
}

static int
adb_nameeq_len(const char *a, const char *b, int blen)
{
  int i;

  for(i = 0; i < blen; i++) {
    if(a[i] == 0 || a[i] != b[i])
      return 0;
  }
  return a[blen] == 0;
}

static int
adb_parse_uint(const char *s, int len, int *out)
{
  int i;
  int v;

  if(s == 0 || len <= 0 || out == 0)
    return -1;

  v = 0;
  for(i = 0; i < len; i++) {
    if(s[i] < '0' || s[i] > '9')
      return -1;
    v = v * 10 + (s[i] - '0');
  }

  *out = v;
  return 0;
}

static int
adb_copy_field(char *dst, int dsz, const char *src, int n)
{
  if(dst == 0 || dsz <= 0)
    return -1;
  if(src == 0)
    n = 0;
  if(n < 0)
    n = 0;
  if(n >= dsz)
    return -1;
  if(n > 0)
    memmove(dst, src, n);
  dst[n] = 0;
  return 0;
}

static int
adb_append_bytes(char *dst, int *cur, int max, const char *src, int n)
{
  if(dst == 0 || cur == 0 || src == 0 || n < 0)
    return -1;
  if(*cur + n >= max)
    return -1;
  if(n > 0)
    memmove(dst + *cur, src, n);
  *cur += n;
  return 0;
}

int
adb_is_valid_name(const char *s)
{
  int i;

  if(s == 0 || *s == 0)
    return 0;

  for(i = 0; s[i]; i++) {
    char c;

    c = s[i];
    if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
       (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')
      continue;
    return 0;
  }

  return 1;
}

int
adb_read_file(const char *path, char *buf, int bufsz, int *n_out)
{
  int fd;
  int n;

  if(path == 0 || buf == 0 || bufsz <= 1 || n_out == 0)
    return -1;

  fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;
  n = read(fd, buf, bufsz - 1);
  close(fd);
  if(n < 0)
    return -1;

  buf[n] = 0;
  *n_out = n;
  return 0;
}

int
adb_write_file_atomic(const char *path, const char *tmp_path, const char *buf, int n)
{
  int fd;

  if(path == 0 || tmp_path == 0 || buf == 0 || n < 0)
    return -1;

  unlink(tmp_path);
  fd = open(tmp_path, O_CREAT | O_WRONLY);
  if(fd < 0)
    return -1;
  if(write(fd, buf, n) != n) {
    close(fd);
    unlink(tmp_path);
    return -1;
  }
  close(fd);

  unlink(path);
  if(link(tmp_path, path) < 0) {
    unlink(tmp_path);
    return -1;
  }
  unlink(tmp_path);
  return 0;
}

static int
adb_split_fields(const char *line, int linelen, const char **fields, int *lengths, int maxf)
{
  int i;
  int start;
  int nf;

  start = 0;
  nf = 0;
  for(i = 0; i <= linelen; i++) {
    if(i == linelen || line[i] == ':') {
      if(nf < maxf) {
        fields[nf] = line + start;
        lengths[nf] = i - start;
        nf++;
      }
      start = i + 1;
    }
  }
  return nf;
}

int
adb_parse_passwd_line(const char *line, int linelen, struct adb_passwd_entry *out)
{
  const char *f[8];
  int l[8];
  int nf;

  if(line == 0 || out == 0 || linelen <= 0)
    return -1;

  nf = adb_split_fields(line, linelen, f, l, 8);
  if(nf < 7)
    return -1;
  if(adb_copy_field(out->name, sizeof(out->name), f[0], l[0]) < 0 ||
     adb_copy_field(out->passwd, sizeof(out->passwd), f[1], l[1]) < 0 ||
     adb_parse_uint(f[2], l[2], &out->uid) < 0 ||
     adb_parse_uint(f[3], l[3], &out->gid) < 0 ||
     adb_copy_field(out->gecos, sizeof(out->gecos), f[4], l[4]) < 0 ||
     adb_copy_field(out->home, sizeof(out->home), f[5], l[5]) < 0 ||
     adb_copy_field(out->shell, sizeof(out->shell), f[6], l[6]) < 0)
    return -1;
  return 0;
}

int
adb_parse_group_line(const char *line, int linelen, struct adb_group_entry *out)
{
  const char *f[4];
  int l[4];
  int nf;

  if(line == 0 || out == 0 || linelen <= 0)
    return -1;

  nf = adb_split_fields(line, linelen, f, l, 4);
  if(nf < 2)
    return -1;

  if(nf >= 3) {
    if(adb_copy_field(out->name, sizeof(out->name), f[0], l[0]) < 0 ||
       adb_copy_field(out->passwd, sizeof(out->passwd), f[1], l[1]) < 0 ||
       adb_parse_uint(f[2], l[2], &out->gid) < 0)
      return -1;
    if(nf >= 4) {
      if(adb_copy_field(out->members, sizeof(out->members), f[3], l[3]) < 0)
        return -1;
    } else {
      out->members[0] = 0;
    }
  } else {
    if(adb_copy_field(out->name, sizeof(out->name), f[0], l[0]) < 0 ||
       adb_parse_uint(f[1], l[1], &out->gid) < 0)
      return -1;
    out->passwd[0] = 0;
    out->members[0] = 0;
  }

  return 0;
}

static int
adb_find_user(const char *buf, int n, const char *name, int uid, int by_uid,
              struct adb_passwd_entry *out)
{
  int i;

  i = 0;
  while(i < n) {
    int start;
    int end;
    struct adb_passwd_entry ent;

    while(i < n && (buf[i] == '\n' || buf[i] == '\r'))
      i++;
    if(i >= n)
      break;
    if(buf[i] == '#') {
      while(i < n && buf[i] != '\n' && buf[i] != '\r')
        i++;
      continue;
    }

    start = i;
    while(i < n && buf[i] != '\n' && buf[i] != '\r')
      i++;
    end = i;

    if(adb_parse_passwd_line(buf + start, end - start, &ent) < 0)
      continue;

    if((by_uid && ent.uid == uid) || (!by_uid && adb_streq(ent.name, name))) {
      if(out)
        *out = ent;
      return 0;
    }
  }

  return -1;
}

int
adb_find_user_by_name(const char *buf, int n, const char *name, struct adb_passwd_entry *out)
{
  if(name == 0)
    return -1;
  return adb_find_user(buf, n, name, -1, 0, out);
}

int
adb_find_user_by_uid(const char *buf, int n, int uid, struct adb_passwd_entry *out)
{
  return adb_find_user(buf, n, 0, uid, 1, out);
}

static int
adb_find_group(const char *buf, int n, const char *name, int gid, int by_gid,
               struct adb_group_entry *out)
{
  int i;

  i = 0;
  while(i < n) {
    int start;
    int end;
    struct adb_group_entry ent;

    while(i < n && (buf[i] == '\n' || buf[i] == '\r'))
      i++;
    if(i >= n)
      break;
    if(buf[i] == '#') {
      while(i < n && buf[i] != '\n' && buf[i] != '\r')
        i++;
      continue;
    }

    start = i;
    while(i < n && buf[i] != '\n' && buf[i] != '\r')
      i++;
    end = i;

    if(adb_parse_group_line(buf + start, end - start, &ent) < 0)
      continue;

    if((by_gid && ent.gid == gid) || (!by_gid && adb_streq(ent.name, name))) {
      if(out)
        *out = ent;
      return 0;
    }
  }

  return -1;
}

int
adb_find_group_by_name(const char *buf, int n, const char *name, struct adb_group_entry *out)
{
  if(name == 0)
    return -1;
  return adb_find_group(buf, n, name, -1, 0, out);
}

int
adb_find_group_by_gid(const char *buf, int n, int gid, struct adb_group_entry *out)
{
  return adb_find_group(buf, n, 0, gid, 1, out);
}

int
adb_next_uid(const char *buf, int n, int min_uid)
{
  int i;
  int max_uid;

  max_uid = min_uid - 1;
  i = 0;
  while(i < n) {
    int start;
    int end;
    struct adb_passwd_entry ent;

    while(i < n && (buf[i] == '\n' || buf[i] == '\r'))
      i++;
    if(i >= n)
      break;
    if(buf[i] == '#') {
      while(i < n && buf[i] != '\n' && buf[i] != '\r')
        i++;
      continue;
    }

    start = i;
    while(i < n && buf[i] != '\n' && buf[i] != '\r')
      i++;
    end = i;

    if(adb_parse_passwd_line(buf + start, end - start, &ent) == 0 && ent.uid > max_uid)
      max_uid = ent.uid;
  }

  return max_uid + 1;
}

int
adb_next_gid(const char *buf, int n, int min_gid)
{
  int i;
  int max_gid;

  max_gid = min_gid - 1;
  i = 0;
  while(i < n) {
    int start;
    int end;
    struct adb_group_entry ent;

    while(i < n && (buf[i] == '\n' || buf[i] == '\r'))
      i++;
    if(i >= n)
      break;
    if(buf[i] == '#') {
      while(i < n && buf[i] != '\n' && buf[i] != '\r')
        i++;
      continue;
    }

    start = i;
    while(i < n && buf[i] != '\n' && buf[i] != '\r')
      i++;
    end = i;

    if(adb_parse_group_line(buf + start, end - start, &ent) == 0 && ent.gid > max_gid)
      max_gid = ent.gid;
  }

  return max_gid + 1;
}

int
adb_append_raw_line(char *dst, int *cur, int max, const char *line, int linelen)
{
  if(adb_append_bytes(dst, cur, max, line, linelen) < 0)
    return -1;
  if(adb_append_bytes(dst, cur, max, "\n", 1) < 0)
    return -1;
  return 0;
}

int
adb_append_passwd_line(char *dst, int *cur, int max, const struct adb_passwd_entry *ent)
{
  char line[ADB_NAME_MAX + ADB_PASSWD_MAX + ADB_GECOS_MAX + ADB_PATH_MAX + ADB_SHELL_MAX + 64];
  int n;

  if(dst == 0 || cur == 0 || ent == 0)
    return -1;

  n = snprintf(line, sizeof(line), "%s:%s:%d:%d:%s:%s:%s\n",
               ent->name,
               ent->passwd[0] ? ent->passwd : "x",
               ent->uid,
               ent->gid,
               ent->gecos,
               ent->home,
               ent->shell[0] ? ent->shell : "/bin/sh");
  if(n < 0 || n >= (int)sizeof(line))
    return -1;
  return adb_append_bytes(dst, cur, max, line, n);
}

int
adb_append_group_line(char *dst, int *cur, int max, const struct adb_group_entry *ent)
{
  static char line[ADB_NAME_MAX + ADB_PASSWD_MAX + ADB_MEMBERS_MAX + 64];
  int n;

  if(dst == 0 || cur == 0 || ent == 0)
    return -1;

  n = snprintf(line, sizeof(line), "%s:%s:%d:%s\n",
               ent->name,
               ent->passwd[0] ? ent->passwd : "x",
               ent->gid,
               ent->members);
  if(n < 0 || n >= (int)sizeof(line))
    return -1;
  return adb_append_bytes(dst, cur, max, line, n);
}

int
adb_group_has_member(const struct adb_group_entry *gr, const char *name)
{
  int i;
  int start;

  if(gr == 0 || name == 0 || *name == 0)
    return 0;

  i = 0;
  while(gr->members[i]) {
    int end;

    start = i;
    while(gr->members[i] && gr->members[i] != ',')
      i++;
    end = i;
    if(end > start && adb_nameeq_len(name, gr->members + start, end - start))
      return 1;
    if(gr->members[i] == ',')
      i++;
  }

  return 0;
}

int
adb_group_add_member(struct adb_group_entry *gr, const char *name)
{
  int cur;
  int n;

  if(gr == 0 || name == 0 || *name == 0)
    return -1;
  if(adb_group_has_member(gr, name))
    return 0;

  cur = strlen(gr->members);
  n = strlen(name);
  if(cur > 0) {
    if(cur + 1 + n >= (int)sizeof(gr->members))
      return -1;
    gr->members[cur++] = ',';
    memmove(gr->members + cur, name, n);
    cur += n;
    gr->members[cur] = 0;
    return 0;
  }

  if(n >= (int)sizeof(gr->members))
    return -1;
  memmove(gr->members, name, n);
  gr->members[n] = 0;
  return 0;
}

int
adb_group_remove_member(struct adb_group_entry *gr, const char *name)
{
  int i;
  int out;
  int removed;

  if(gr == 0 || name == 0 || *name == 0)
    return -1;

  i = 0;
  out = 0;
  removed = 0;
  while(gr->members[i]) {
    int start;
    int end;
    int is_target;

    start = i;
    while(gr->members[i] && gr->members[i] != ',')
      i++;
    end = i;
    is_target = (end > start) && adb_nameeq_len(name, gr->members + start, end - start);

    if(!is_target) {
      int len;

      len = end - start;
      if(len > 0) {
        if(out > 0)
          gr->members[out++] = ',';
        memmove(gr->members + out, gr->members + start, len);
        out += len;
      }
    } else {
      removed = 1;
    }

    if(gr->members[i] == ',')
      i++;
  }

  gr->members[out] = 0;
  return removed ? 0 : -1;
}
