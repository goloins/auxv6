#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "auxv6/user.h"

#define TAIL_BUFSZ 4096
#define DEFAULT_LINES 10
#define DEFAULT_FOLLOW_TICKS 10

static int
read_all(const char *path, char *buf, int max)
{
  int fd;
  int n;
  int off;

  fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;

  off = 0;
  while(off < max - 1) {
    n = read(fd, buf + off, max - 1 - off);
    if(n < 0) {
      close(fd);
      return -1;
    }
    if(n == 0)
      break;
    off += n;
  }
  close(fd);

  buf[off] = 0;
  return off;
}

static int
same_bytes(const char *a, const char *b, int n)
{
  int i;

  for(i = 0; i < n; i++) {
    if(a[i] != b[i])
      return 0;
  }
  return 1;
}

static int
find_last_n_lines_start(const char *buf, int len, int lines)
{
  int i;
  int seen;

  if(lines <= 0)
    return len;

  seen = 0;
  for(i = len - 1; i >= 0; i--) {
    if(buf[i] == '\n') {
      seen++;
      if(seen > lines)
        return i + 1;
    }
  }
  return 0;
}

static void
print_tail_lines(const char *buf, int len, int lines)
{
  int start;

  if(len <= 0)
    return;

  start = find_last_n_lines_start(buf, len, lines);
  if(start < len)
    write(1, buf + start, len - start);
}

static int
follow_file(const char *path, int interval_ticks)
{
  char cur[TAIL_BUFSZ];
  char prev[TAIL_BUFSZ];
  int cur_len;
  int prev_len;

  prev_len = -1;

  for(;;) {
    cur_len = read_all(path, cur, sizeof(cur));
    if(cur_len < 0) {
      dprintf(2, "tail: cannot read %s\n", path);
      return -1;
    }

    if(cur_len != prev_len || !same_bytes(cur, prev, cur_len)) {
      if(cur_len > 0)
        write(1, cur, cur_len);
      if(cur_len == 0 || cur[cur_len - 1] != '\n')
        write(1, "\n", 1);
      memmove(prev, cur, cur_len);
      prev_len = cur_len;
    }

    sleep(interval_ticks);
  }
}

int
main(int argc, char *argv[])
{
  int follow;
  int lines;
  int interval_ticks;
  int path_i;
  char buf[TAIL_BUFSZ];
  int len;

  follow = 0;
  lines = DEFAULT_LINES;
  interval_ticks = DEFAULT_FOLLOW_TICKS;
  path_i = 1;

  if(argc < 2) {
    dprintf(2, "usage: tail [-f] file [interval_ticks]\n");
    exit(1);
  }

  if(strcmp(argv[path_i], "-f") == 0) {
    follow = 1;
    path_i++;
  }

  if(path_i >= argc) {
    dprintf(2, "usage: tail [-f] file [interval_ticks]\n");
    exit(1);
  }

  if(follow && path_i + 1 < argc) {
    interval_ticks = atoi(argv[path_i + 1]);
    if(interval_ticks <= 0)
      interval_ticks = DEFAULT_FOLLOW_TICKS;
  }

  len = read_all(argv[path_i], buf, sizeof(buf));
  if(len < 0) {
    dprintf(2, "tail: cannot open %s\n", argv[path_i]);
    exit(1);
  }

  if(follow) {
    if(len > 0)
      write(1, buf, len);
    if(len == 0 || buf[len - 1] != '\n')
      write(1, "\n", 1);
    if(follow_file(argv[path_i], interval_ticks) < 0)
      exit(1);
    exit(0);
  }

  print_tail_lines(buf, len, lines);
  exit(0);
}
