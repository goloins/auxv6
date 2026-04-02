#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fs.h"
#include "fcntl.h"

#define MAN_DIR "/usr/share/man"
#define MAN_PAGE_LINES 22

static char buf[512];
static int pager_enabled;
static int pager_lines;
static int pager_quit;

static void
pager_prompt(void)
{
  char c;

  write(1, "--More-- (Enter:next, q:quit)", 29);
  if(read(0, &c, 1) != 1)
    c = 'q';
  write(1, "\r                             \r", 31);

  if(c == 'q' || c == 'Q')
    pager_quit = 1;

  pager_lines = 0;
}

static void
putc1(char c)
{
  if(pager_quit)
    return;

  write(1, &c, 1);

  if(pager_enabled && c == '\n') {
    pager_lines++;
    if(pager_lines >= MAN_PAGE_LINES)
      pager_prompt();
  }
}

static void
puts1(const char *s)
{
  write(1, s, strlen(s));
}

static void
print_upper(const char *s)
{
  int i;
  char c;

  for(i = 0; s[i]; i++) {
    c = s[i];
    if(c >= 'a' && c <= 'z')
      c = c - 'a' + 'A';
    putc1(c);
  }
}

static void
render_line(char *line, int *in_code)
{
  int n;

  n = strlen(line);
  while(n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
    line[--n] = 0;

  if(strncmp(line, "```", 3) == 0) {
    *in_code = !*in_code;
    putc1('\n');
    return;
  }

  if(*in_code) {
    puts1("    ");
    puts1(line);
    putc1('\n');
    return;
  }

  if(strncmp(line, "# ", 2) == 0) {
    putc1('\n');
    print_upper(line + 2);
    puts1("\n\n");
    return;
  }

  if(strncmp(line, "## ", 3) == 0) {
    int i;
    int len;

    putc1('\n');
    puts1(line + 3);
    putc1('\n');
    len = strlen(line + 3);
    for(i = 0; i < len; i++)
      putc1('-');
    puts1("\n");
    return;
  }

  if(strncmp(line, "### ", 4) == 0) {
    puts1("\n* ");
    puts1(line + 4);
    puts1("\n");
    return;
  }

  if(strncmp(line, "- ", 2) == 0) {
    puts1("  - ");
    puts1(line + 2);
    putc1('\n');
    return;
  }

  puts1(line);
  putc1('\n');
}

static void
usage(void)
{
  printf(2, "usage: man [-l] topic\n");
  printf(2, "       pager keys: Enter=next page, q=quit\n");
}

static int
has_suffix(const char *s, const char *suffix)
{
  int ls;
  int lx;

  ls = strlen(s);
  lx = strlen(suffix);
  if(ls < lx)
    return 0;
  return strcmp((char *)s + ls - lx, suffix) == 0;
}

static int
render_markdown_file(const char *path)
{
  int fd;
  int n;
  char line[512];
  int llen;
  int i;
  int in_code;

  fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;

  pager_enabled = isatty(0) && isatty(1);
  pager_lines = 0;
  pager_quit = 0;

  llen = 0;
  in_code = 0;

  while((n = read(fd, buf, sizeof(buf))) > 0) {
    for(i = 0; i < n; i++) {
      if(pager_quit)
        break;
      line[llen++] = buf[i];
      if(buf[i] == '\n' || llen >= (int)sizeof(line) - 1) {
        line[llen] = 0;
        render_line(line, &in_code);
        llen = 0;
      }
    }
    if(pager_quit)
      break;
  }

  if(!pager_quit && llen > 0) {
    line[llen] = 0;
    render_line(line, &in_code);
  }

  close(fd);
  if(n < 0) {
    printf(2, "man: read error: %s\n", path);
    return -1;
  }
  return 0;
}

static int
build_topic_path(char *out, int outsz, const char *topic, int with_md)
{
  int n;
  int i;

  n = 0;
  for(i = 0; MAN_DIR[i]; i++) {
    if(n + 1 >= outsz)
      return -1;
    out[n++] = MAN_DIR[i];
  }
  if(n + 1 >= outsz)
    return -1;
  out[n++] = '/';

  for(i = 0; topic[i]; i++) {
    if(n + 1 >= outsz)
      return -1;
    out[n++] = topic[i];
  }

  if(with_md) {
    if(n + 3 >= outsz)
      return -1;
    out[n++] = '.';
    out[n++] = 'm';
    out[n++] = 'd';
  }

  out[n] = 0;
  return 0;
}

static void
list_topics(void)
{
  int fd;
  int nent;
  int i;
  struct dirent des[16];

  fd = open(MAN_DIR, O_RDONLY);
  if(fd < 0) {
    printf(2, "man: cannot open %s\n", MAN_DIR);
    return;
  }

  while((nent = getdents(fd, des, 16)) > 0) {
    for(i = 0; i < nent; i++) {
      char name[DIRSIZ + 1];
      int j;

      memmove(name, des[i].name, DIRSIZ);
      name[DIRSIZ] = 0;

      j = strlen(name) - 1;
      while(j >= 0 && name[j] == ' ')
        name[j--] = 0;

      if(name[0] == 0)
        continue;
      if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        continue;

      if(has_suffix(name, ".md"))
        name[strlen(name) - 3] = 0;

      printf(1, "%s\n", name);
    }
  }

  close(fd);
}

int
main(int argc, char *argv[])
{
  char path[128];

  if(argc != 2) {
    usage();
    exit();
  }

  if(strcmp(argv[1], "-l") == 0) {
    list_topics();
    exit();
  }

  if(strchr(argv[1], '/')) {
    if(render_markdown_file(argv[1]) < 0)
      printf(2, "man: no entry for %s\n", argv[1]);
    exit();
  }

  if(build_topic_path(path, sizeof(path), argv[1], 1) == 0 && render_markdown_file(path) == 0)
    exit();

  if(build_topic_path(path, sizeof(path), argv[1], 0) == 0 && render_markdown_file(path) == 0)
    exit();

  printf(2, "man: no entry for %s\n", argv[1]);
  printf(2, "man: run 'man -l' to list topics\n");
  exit();
}
