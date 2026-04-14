#include "types.h"
#include "grp.h"
#include "pwd.h"
#include "sys/stat.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "fs.h"
#include "stdio.h"
#include "sys/ioctl.h"
#include "time.h"

#ifndef LS_DEBUG
#define LS_DEBUG 0
#endif

#define LSDBG(...) do { if(LS_DEBUG) dprintf(2, __VA_ARGS__); } while(0)

#define NAME_MAX_LOCAL (DIRSIZ + 1)
#define LS_DIRENT_BATCH 8

struct ls_opts {
  int show_all;
  int almost_all;
  int long_format;
  int human;
  int recursive;
  int reverse;
  int sort_time;
  int sort_size;
  int list_dir_itself;
  int one_per_line;
};

struct ls_entry {
  char name[NAME_MAX_LOCAL];
  struct stat st;
  int stat_ok;
  int is_mountpoint;
};

struct ls_layout {
  int links_w;
  int owner_w;
  int group_w;
  int size_w;
  int time_w;
};

static struct ls_opts g_opts;
static struct mountinfo g_mounts[MOUNTINFO_MAX];
static int g_mount_count;
static int g_mounts_loaded;

static char* human_size(uint size);

static void compute_layout(struct ls_layout *layout, struct ls_entry *ents, int n);

static void
normalize_mount_path(const char *in, char *out, int outsz)
{
  int n;

  if(outsz <= 0)
    return;
  if(in == 0 || in[0] == 0) {
    out[0] = 0;
    return;
  }

  snprintf(out, outsz, "%s", in);
  n = strlen(out);
  while(n > 1 && out[n - 1] == '/') {
    out[n - 1] = 0;
    n--;
  }
}

static void
ls_load_mounts(void)
{
  if(g_mounts_loaded)
    return;
  g_mount_count = mountinfo(g_mounts, MOUNTINFO_MAX);
  if(g_mount_count < 0)
    g_mount_count = 0;
  g_mounts_loaded = 1;
  LSDBG("ls[dbg]: mountinfo loaded count=%d\n", g_mount_count);
}

static int
ls_is_mountpoint_path(const char *path)
{
  char want[64];
  int i;

  ls_load_mounts();
  normalize_mount_path(path, want, sizeof(want));
  for(i = 0; i < g_mount_count; i++) {
    char cur[64];

    normalize_mount_path(g_mounts[i].path, cur, sizeof(cur));
    if(strcmp(cur, want) == 0)
      return 1;
  }
  return 0;
}

static void
format_mtime(int secs, char *out, int outsz)
{
  struct tm tmv;

  if(outsz <= 0)
    return;
  if(secs <= 0 || localtime_r((const time_t*)&secs, &tmv) == 0) {
    snprintf(out, outsz, "-");
    return;
  }

  snprintf(out, outsz, "%d/%d/%d %d:%02d",
           tmv.tm_mon + 1,
           tmv.tm_mday,
           tmv.tm_year + 1900,
           tmv.tm_hour,
           tmv.tm_min);
}

static void
entry_time_text(const struct ls_entry *e, char *out, int outsz)
{
  if(e->st.st_mtime > 0)
    format_mtime(e->st.st_mtime, out, outsz);
  else if(e->is_mountpoint)
    snprintf(out, outsz, "(mountpoint)");
  else
    snprintf(out, outsz, "-");
}

static int
decimal_width(int value)
{
  int width;

  width = 1;
  while(value >= 10) {
    value /= 10;
    width++;
  }
  return width;
}

static int
tty_columns(void)
{
  struct winsize ws;

  memset(&ws, 0, sizeof(ws));
  if(ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
    return (int)ws.ws_col;
  return 80;
}

static void
print_entries_compact(struct ls_entry *ents, int n)
{
  int i;
  int r;
  int c;
  int rows;
  int cols;
  int termw;
  int maxw;
  int colw;

  if(n <= 0)
    return;

  maxw = 0;
  for(i = 0; i < n; i++) {
    int w;

    w = strlen(ents[i].name);
    if(w > maxw)
      maxw = w;
  }

  colw = maxw + 2;
  if(colw < 2)
    colw = 2;
  termw = tty_columns();
  cols = termw / colw;
  if(cols < 1)
    cols = 1;
  rows = (n + cols - 1) / cols;

  if(LS_DEBUG)
    dprintf(2, "ls[dbg]: compact print n=%d termw=%d colw=%d cols=%d rows=%d\n",
            n, termw, colw, cols, rows);

  for(r = 0; r < rows; r++) {
    for(c = 0; c < cols; c++) {
      int idx;

      idx = c * rows + r;
      if(idx >= n)
        continue;
      if(c == cols - 1 || idx + rows >= n)
        dprintf(1, "%s", ents[idx].name);
      else
        dprintf(1, "%-*s", colw, ents[idx].name);
    }
    dprintf(1, "\n");
  }
}

static char*
uid_to_name(int uid)
{
  static char result[32];
  struct passwd *pw;
  int j;

  LSDBG("ls[dbg]: uid_to_name uid=%d\n", uid);
  pw = getpwuid((uid_t)uid);
  if(pw != 0) {
    snprintf(result, sizeof(result), "%s", pw->pw_name);
    LSDBG("ls[dbg]: uid_to_name hit uid=%d name=%s\n", uid, result);
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
  LSDBG("ls[dbg]: uid_to_name fallback uid=%d value=%s\n", uid, result);
  return result;
}

static char*
gid_to_name(int gid)
{
  static char result[32];
  struct group *gr;
  int j;

  LSDBG("ls[dbg]: gid_to_name gid=%d\n", gid);
  gr = getgrgid((gid_t)gid);
  if(gr != 0) {
    snprintf(result, sizeof(result), "%s", gr->gr_name);
    LSDBG("ls[dbg]: gid_to_name hit gid=%d name=%s\n", gid, result);
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
  LSDBG("ls[dbg]: gid_to_name fallback gid=%d value=%s\n", gid, result);
  return result;
}

static int
is_dot_or_dotdot(const char *name)
{
  if(strcmp(name, ".") == 0)
    return 1;
  if(strcmp(name, "..") == 0)
    return 1;
  return 0;
}

static int
is_hidden_name(const char *name)
{
  return name[0] == '.';
}

static int
should_show_name(const char *name)
{
  int show;

  if(g_opts.show_all)
    show = 1;
  else if(g_opts.almost_all)
    show = !is_dot_or_dotdot(name);
  else
    show = !is_hidden_name(name);

  LSDBG("ls[dbg]: should_show name='%s' show_all=%d almost_all=%d -> %d\n",
        name, g_opts.show_all, g_opts.almost_all, show);
  return show;
}

static void
path_join(char *out, int outsz, const char *dir, const char *name)
{
  int n;

  if(strcmp(dir, "/") == 0) {
    snprintf(out, outsz, "/%s", name);
    LSDBG("ls[dbg]: path_join dir='/' name='%s' -> '%s'\n", name, out);
    return;
  }

  n = strlen(dir);
  if(n > 0 && dir[n - 1] == '/')
    snprintf(out, outsz, "%s%s", dir, name);
  else
    snprintf(out, outsz, "%s/%s", dir, name);

  LSDBG("ls[dbg]: path_join dir='%s' name='%s' -> '%s'\n", dir, name, out);
}

static char
file_type_char(const struct stat *st)
{
  int t;

  t = st->st_mode & M_IFMT;
  if(t == M_IFDIR) return 'd';
  if(t == M_IFCHR) return 'c';
  if(t == M_IFBLK) return 'b';
  if(t == M_IFLNK) return 'l';
  return '-';
}

static void
format_mode(const struct stat *st, char out[11])
{
  int m;

  m = st->st_mode;
  out[0] = file_type_char(st);
  out[1] = (m & M_IRUSR) ? 'r' : '-';
  out[2] = (m & M_IWUSR) ? 'w' : '-';
  out[3] = (m & M_IXUSR) ? 'x' : '-';
  out[4] = (m & M_IRGRP) ? 'r' : '-';
  out[5] = (m & M_IWGRP) ? 'w' : '-';
  out[6] = (m & M_IXGRP) ? 'x' : '-';
  out[7] = (m & M_IROTH) ? 'r' : '-';
  out[8] = (m & M_IWOTH) ? 'w' : '-';
  out[9] = (m & M_IXOTH) ? 'x' : '-';

  if(m & M_ISUID)
    out[3] = (out[3] == 'x') ? 's' : 'S';
  if(m & M_ISGID)
    out[6] = (out[6] == 'x') ? 's' : 'S';
  if(m & M_ISVTX)
    out[9] = (out[9] == 'x') ? 't' : 'T';

  out[10] = 0;
}

static char*
fmt_size(uint size)
{
  static char buf[16];
  if(g_opts.human)
    return human_size(size);
  snprintf(buf, sizeof(buf), "%u", size);
  LSDBG("ls[dbg]: fmt_size raw=%u out=%s human=%d\n", size, buf, g_opts.human);
  return buf;
}

static int
entry_cmp(const struct ls_entry *a, const struct ls_entry *b)
{
  int d;

  if(g_opts.sort_time) {
    int am, bm;
    am = a->stat_ok ? a->st.st_mtime : 0;
    bm = b->stat_ok ? b->st.st_mtime : 0;
    if(am != bm)
      d = (am < bm) ? 1 : -1;
    else
      d = strcmp(a->name, b->name);
  } else if(g_opts.sort_size) {
    uint as, bs;
    as = a->stat_ok ? a->st.st_size : 0;
    bs = b->stat_ok ? b->st.st_size : 0;
    if(as != bs)
      d = (as < bs) ? 1 : -1;
    else
      d = strcmp(a->name, b->name);
  } else {
    d = strcmp(a->name, b->name);
  }

  if(g_opts.reverse)
    d = -d;
  LSDBG("ls[dbg]: entry_cmp a='%s' b='%s' -> %d (t=%d s=%d r=%d)\n",
        a->name, b->name, d, g_opts.sort_time, g_opts.sort_size, g_opts.reverse);
  return d;
}

static void
sort_entries(struct ls_entry *ents, int n)
{
  int i;
  int j;

  LSDBG("ls[dbg]: sort_entries n=%d\n", n);
  for(i = 1; i < n; i++) {
    struct ls_entry key;
    key = ents[i];
    j = i - 1;
    while(j >= 0 && entry_cmp(&ents[j], &key) > 0) {
      ents[j + 1] = ents[j];
      j--;
    }
    ents[j + 1] = key;
  }
  LSDBG("ls[dbg]: sort_entries done\n");
}

static void
print_entry(const struct ls_entry *e, const struct ls_layout *layout)
{
  LSDBG("ls[dbg]: print_entry name='%s' stat_ok=%d long=%d one=%d\n",
        e->name, e->stat_ok, g_opts.long_format, g_opts.one_per_line);
  if(g_opts.long_format && e->stat_ok) {
    char mode[11];
    char tbuf[40];
    const char *owner;
    const char *group;
    const char *size;

    format_mode(&e->st, mode);
    entry_time_text(e, tbuf, sizeof(tbuf));
    owner = uid_to_name(e->st.st_uid);
    group = gid_to_name(e->st.st_gid);
    size = fmt_size(e->st.st_size);

    dprintf(1, "%s %*d %-*s %-*s %*s %-*s %s\n",
            mode,
            layout ? layout->links_w : 2,
            e->st.st_nlink,
            layout ? layout->owner_w : 5,
            owner,
            layout ? layout->group_w : 5,
            group,
            layout ? layout->size_w : 4,
            size,
            layout ? layout->time_w : 12,
            tbuf,
            e->name);
    return;
  }

  if(g_opts.one_per_line || g_opts.long_format)
    dprintf(1, "%s\n", e->name);
  else
    dprintf(1, "%s\n", e->name);
}

static void
compute_layout(struct ls_layout *layout, struct ls_entry *ents, int n)
{
  int i;

  layout->links_w = 2;
  layout->owner_w = 5;
  layout->group_w = 5;
  layout->size_w = 4;
  layout->time_w = 12;

  for(i = 0; i < n; i++) {
    char tbuf[40];
    int width;

    if(!ents[i].stat_ok)
      continue;

    width = decimal_width(ents[i].st.st_nlink);
    if(width > layout->links_w)
      layout->links_w = width;

    width = strlen(uid_to_name(ents[i].st.st_uid));
    if(width > layout->owner_w)
      layout->owner_w = width;

    width = strlen(gid_to_name(ents[i].st.st_gid));
    if(width > layout->group_w)
      layout->group_w = width;

    width = strlen(fmt_size(ents[i].st.st_size));
    if(width > layout->size_w)
      layout->size_w = width;

    entry_time_text(&ents[i], tbuf, sizeof(tbuf));
    width = strlen(tbuf);
    if(width > layout->time_w)
      layout->time_w = width;
  }

  LSDBG("ls[dbg]: layout links=%d owner=%d group=%d size=%d time=%d\n",
        layout->links_w, layout->owner_w, layout->group_w,
        layout->size_w, layout->time_w);
}

static int
entry_is_dir(const struct ls_entry *e)
{
  if(!e->stat_ok)
    return 0;
  if(S_ISDIR(e->st.st_mode))
    return 1;
  {
    int r;
    r = (e->st.st_mode & M_IFMT) == M_IFDIR;
    LSDBG("ls[dbg]: entry_is_dir name='%s' mode=%o -> %d\n",
          e->name, e->st.st_mode, r);
    return r;
  }
}

static void
list_path(const char *path, int show_header);

static void
list_directory(const char *path, int show_header)
{
  int fd;
  struct dirent des[LS_DIRENT_BATCH];
  struct ls_entry *ents;
  char fullpath[256];
  char childpath[256];
  struct ls_layout layout;
  int n;
  int cap;
  int i;
  int total_batches;
  int total_dirents;
  int total_empty_names;
  int total_hidden_filtered;
  int total_stat_fail;
  int total_added;

  LSDBG("ls[dbg]: list_directory enter path='%s' show_header=%d\n", path, show_header);
  fd = open(path, O_RDONLY);
  if(fd < 0) {
    dprintf(2, "ls: cannot open %s\n", path);
    LSDBG("ls[dbg]: list_directory open failed path='%s'\n", path);
    return;
  }
  LSDBG("ls[dbg]: list_directory open ok path='%s' fd=%d\n", path, fd);

  if(show_header)
    dprintf(1, "%s:\n", path);

  ents = 0;
  n = 0;
  cap = 0;
  total_batches = 0;
  total_dirents = 0;
  total_empty_names = 0;
  total_hidden_filtered = 0;
  total_stat_fail = 0;
  total_added = 0;

  for(;;) {
    int nent;

    nent = getdents(fd, des, LS_DIRENT_BATCH);
    if(nent < 0) {
      dprintf(2, "ls: getdents failed %s\n", path);
      LSDBG("ls[dbg]: list_directory getdents failed path='%s'\n", path);
      if(ents)
        free(ents);
      close(fd);
      return;
    }
    if(nent == 0)
      break;
    total_batches++;
    total_dirents += nent;
    LSDBG("ls[dbg]: list_directory batch=%d nent=%d path='%s'\n",
          total_batches, nent, path);

    for(i = 0; i < nent; i++) {
      struct ls_entry e;

      memset(&e, 0, sizeof(e));
      memmove(e.name, des[i].name, DIRSIZ);
      e.name[DIRSIZ] = 0;
      LSDBG("ls[dbg]: dirent[%d] raw name='%s' inum=%u\n", i, e.name, des[i].inum);

      if(e.name[0] == 0) {
        total_empty_names++;
        LSDBG("ls[dbg]: dirent[%d] skip empty name\n", i);
        continue;
      }
      if(!should_show_name(e.name)) {
        total_hidden_filtered++;
        LSDBG("ls[dbg]: dirent[%d] filtered hidden name='%s'\n", i, e.name);
        continue;
      }

      path_join(fullpath, sizeof(fullpath), path, e.name);
      if(stat(fullpath, &e.st) == 0)
        e.stat_ok = 1;
      else {
        total_stat_fail++;
        LSDBG("ls[dbg]: stat failed fullpath='%s'\n", fullpath);
      }
      e.is_mountpoint = ls_is_mountpoint_path(fullpath);
      LSDBG("ls[dbg]: mountpoint name='%s' fullpath='%s' -> %d\n",
            e.name, fullpath, e.is_mountpoint);

      if(n >= cap) {
        int newcap;
        struct ls_entry *tmp;

        newcap = (cap == 0) ? 32 : cap * 2;
        LSDBG("ls[dbg]: grow entries old_cap=%d new_cap=%d\n", cap, newcap);
        tmp = malloc(sizeof(struct ls_entry) * newcap);
        if(tmp == 0) {
          dprintf(2, "ls: out of memory\n");
          LSDBG("ls[dbg]: malloc failed cap=%d\n", newcap);
          if(ents)
            free(ents);
          close(fd);
          return;
        }
        if(ents)
          memmove(tmp, ents, sizeof(struct ls_entry) * n);
        if(ents)
          free(ents);
        ents = tmp;
        cap = newcap;
      }

      ents[n++] = e;
      total_added++;
      LSDBG("ls[dbg]: accepted name='%s' n=%d\n", e.name, n);
    }
  }

  close(fd);
  LSDBG("ls[dbg]: list_directory summary path='%s' batches=%d dirents=%d empty=%d hidden=%d stat_fail=%d accepted=%d\n",
        path, total_batches, total_dirents, total_empty_names,
        total_hidden_filtered, total_stat_fail, total_added);

  sort_entries(ents, n);
  LSDBG("ls[dbg]: list_directory print_count=%d path='%s'\n", n, path);
  compute_layout(&layout, ents, n);
  if(!g_opts.long_format && !g_opts.one_per_line)
    print_entries_compact(ents, n);
  else
    for(i = 0; i < n; i++)
      print_entry(&ents[i], &layout);

  if(g_opts.recursive) {
    LSDBG("ls[dbg]: list_directory recurse enabled path='%s'\n", path);
    for(i = 0; i < n; i++) {
      if(!entry_is_dir(&ents[i]))
        continue;
      if(is_dot_or_dotdot(ents[i].name))
        continue;
      path_join(childpath, sizeof(childpath), path, ents[i].name);
      LSDBG("ls[dbg]: recurse into '%s'\n", childpath);
      dprintf(1, "\n");
      list_path(childpath, 1);
    }
  }

  if(ents)
    free(ents);
  LSDBG("ls[dbg]: list_directory exit path='%s'\n", path);
}

static void
list_single(const char *path)
{
  struct ls_entry e;
  struct ls_layout layout;

  LSDBG("ls[dbg]: list_single path='%s'\n", path);
  memset(&e, 0, sizeof(e));
  snprintf(e.name, sizeof(e.name), "%s", path);
  if(stat(path, &e.st) == 0)
    e.stat_ok = 1;
  else
    LSDBG("ls[dbg]: list_single stat failed path='%s'\n", path);
  e.is_mountpoint = ls_is_mountpoint_path(path);
  LSDBG("ls[dbg]: list_single mountpoint path='%s' -> %d\n", path, e.is_mountpoint);
  compute_layout(&layout, &e, 1);
  print_entry(&e, &layout);
}

static void
list_path(const char *path, int show_header)
{
  struct stat st;

  LSDBG("ls[dbg]: list_path path='%s' show_header=%d\n", path, show_header);
  if(stat(path, &st) < 0) {
    dprintf(2, "ls: cannot stat %s\n", path);
    LSDBG("ls[dbg]: list_path stat failed path='%s'\n", path);
    return;
  }
    LSDBG("ls[dbg]: list_path stat ok path='%s' mode=%o size=%u\n",
      path, st.st_mode, st.st_size);

  if((S_ISDIR(st.st_mode) || (st.st_mode & M_IFMT) == M_IFDIR) &&
     !g_opts.list_dir_itself) {
    LSDBG("ls[dbg]: list_path treat as directory path='%s'\n", path);
    list_directory(path, show_header);
    return;
  }

  LSDBG("ls[dbg]: list_path treat as single path='%s'\n", path);
  list_single(path);
}

static int
parse_opts(int argc, char **argv, int *first_path)
{
  int i;

  LSDBG("ls[dbg]: parse_opts argc=%d\n", argc);
  memset(&g_opts, 0, sizeof(g_opts));
  for(i = 1; i < argc; i++) {
    char *a;
    int j;

    a = argv[i];
    LSDBG("ls[dbg]: parse_opts arg[%d]='%s'\n", i, a);
    if(a[0] != '-' || a[1] == 0)
      break;
    if(strcmp(a, "--") == 0) {
      i++;
      break;
    }

    for(j = 1; a[j]; j++) {
      switch(a[j]) {
      case 'a':
        g_opts.show_all = 1;
        g_opts.almost_all = 0;
        LSDBG("ls[dbg]: flag -a\n");
        break;
      case 'A':
        if(!g_opts.show_all)
          g_opts.almost_all = 1;
        LSDBG("ls[dbg]: flag -A\n");
        break;
      case 'l':
        g_opts.long_format = 1;
        LSDBG("ls[dbg]: flag -l\n");
        break;
      case 'h':
        g_opts.human = 1;
        LSDBG("ls[dbg]: flag -h\n");
        break;
      case 'R':
        g_opts.recursive = 1;
        LSDBG("ls[dbg]: flag -R\n");
        break;
      case 'r':
        g_opts.reverse = 1;
        LSDBG("ls[dbg]: flag -r\n");
        break;
      case 't':
        g_opts.sort_time = 1;
        g_opts.sort_size = 0;
        LSDBG("ls[dbg]: flag -t\n");
        break;
      case 'S':
        g_opts.sort_size = 1;
        g_opts.sort_time = 0;
        LSDBG("ls[dbg]: flag -S\n");
        break;
      case 'd':
        g_opts.list_dir_itself = 1;
        LSDBG("ls[dbg]: flag -d\n");
        break;
      case '1':
        g_opts.one_per_line = 1;
        LSDBG("ls[dbg]: flag -1\n");
        break;
      default:
        dprintf(2, "ls: invalid option -- %c\n", a[j]);
        dprintf(2, "usage: ls [-aAldhRrtSd1] [path ...]\n");
        return -1;
      }
    }
  }

  if(g_opts.long_format)
    g_opts.one_per_line = 1;
  if(g_opts.human && !g_opts.long_format) {
    // Keep -h meaningful by showing the size column when requested.
    g_opts.long_format = 1;
    g_opts.one_per_line = 1;
  }

  *first_path = i;
  LSDBG("ls[dbg]: parse_opts done first_path=%d opts{a=%d A=%d l=%d h=%d R=%d r=%d t=%d S=%d d=%d one=%d}\n",
        *first_path,
        g_opts.show_all, g_opts.almost_all, g_opts.long_format, g_opts.human,
        g_opts.recursive, g_opts.reverse, g_opts.sort_time, g_opts.sort_size,
        g_opts.list_dir_itself, g_opts.one_per_line);
  return 0;
}
static char*
human_size(uint size)
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

int
main(int argc, char *argv[])
{
  int first_path;
  int i;
  int npaths;

  LSDBG("ls[dbg]: main enter argc=%d\n", argc);
  if(parse_opts(argc, argv, &first_path) < 0)
    exit(1);

  npaths = argc - first_path;
  LSDBG("ls[dbg]: main npaths=%d first_path=%d\n", npaths, first_path);
  if(npaths <= 0){
    LSDBG("ls[dbg]: main default path '.'\n");
    list_path(".", 0);
    exit(0);
  }

  for(i = first_path; i < argc; i++) {
    int show_header;

    show_header = (npaths > 1) ? 1 : 0;
    LSDBG("ls[dbg]: main dispatch path='%s' show_header=%d\n", argv[i], show_header);
    list_path(argv[i], show_header);
    if(i + 1 < argc)
      dprintf(1, "\n");
  }

  LSDBG("ls[dbg]: main exit\n");
  exit(0);
}
