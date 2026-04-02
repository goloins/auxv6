#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fs.h"
#include "fcntl.h"
#include "termios.h"

#define MAN_DIR "/usr/share/man"
#define MAN_PAGE_LINES 24

static char buf[512];
static int pager_enabled;
static int pager_lines;
static int pager_quit;
static int style_enabled;

#define ANSI_RESET "\033[0m"
#define ANSI_BOLD "\033[1m"
#define ANSI_UNDERLINE "\033[4m"
#define ANSI_REVERSE "\033[7m"

static char
read_pager_key(void)
{
  struct termios oldt;
  struct termios newt;
  char c;

  c = 'q';
  if(!isatty(0)) {
    if(read(0, &c, 1) != 1)
      c = 'q';
    return c;
  }

  if(tcgetattr(0, &oldt) < 0) {
    if(read(0, &c, 1) != 1)
      c = 'q';
    return c;
  }

  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  newt.c_cc[VMIN] = 1;
  newt.c_cc[VTIME] = 0;

  if(tcsetattr(0, TCSANOW, &newt) < 0) {
    if(read(0, &c, 1) != 1)
      c = 'q';
    return c;
  }

  if(read(0, &c, 1) != 1)
    c = 'q';

  tcsetattr(0, TCSANOW, &oldt);
  return c;
}

static void
pager_prompt(void)
{
  char c;

  write(1, "--More-- (Enter:next, q:quit)", 29);
  c = read_pager_key();
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
  int i;

  for(i = 0; s[i]; i++)
    putc1(s[i]);
}

static void
style_begin(const char *code)
{
  if(style_enabled)
    puts1(code);
}

static void
style_end(void)
{
  if(style_enabled)
    puts1(ANSI_RESET);
}

static int
find_closing(const char *s, int start, const char *tok, int tlen)
{
  int i;

  for(i = start; s[i]; i++) {
    if(strncmp(s + i, tok, tlen) == 0)
      return i;
  }
  return -1;
}

static void
render_span(const char *s, int start, int end)
{
  int i;

  for(i = start; i < end; i++)
    putc1(s[i]);
}

static void
render_inline(char *line)
{
  int i;

  for(i = 0; line[i]; ) {
    int j;

    if(line[i] == '`') {
      j = find_closing(line, i + 1, "`", 1);
      if(j > i + 1) {
        style_begin(ANSI_REVERSE);
        render_span(line, i + 1, j);
        style_end();
        i = j + 1;
        continue;
      }
    }

    if(line[i] == '*' && line[i + 1] == '*') {
      j = find_closing(line, i + 2, "**", 2);
      if(j > i + 2) {
        style_begin(ANSI_BOLD);
        render_span(line, i + 2, j);
        style_end();
        i = j + 2;
        continue;
      }
    }

    if(line[i] == '*') {
      j = find_closing(line, i + 1, "*", 1);
      if(j > i + 1) {
        style_begin(ANSI_UNDERLINE);
        render_span(line, i + 1, j);
        style_end();
        i = j + 1;
        continue;
      }
    }

    if(line[i] == '[') {
      int k;
      int l;

      k = find_closing(line, i + 1, "]", 1);
      if(k > i + 1 && line[k + 1] == '(') {
        l = find_closing(line, k + 2, ")", 1);
        if(l > k + 2) {
          style_begin(ANSI_UNDERLINE);
          render_span(line, i + 1, k);
          style_end();
          puts1(" (");
          render_span(line, k + 2, l);
          putc1(')');
          i = l + 1;
          continue;
        }
      }
    }

    putc1(line[i]);
    i++;
  }
}

static int
is_ordered_list_item(char *line)
{
  int i;

  i = 0;
  while(line[i] >= '0' && line[i] <= '9')
    i++;
  return (i > 0 && line[i] == '.' && line[i + 1] == ' ');
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
  int i;

  n = strlen(line);
  while(n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
    line[--n] = 0;

  i = 0;
  while(line[i] == ' ' || line[i] == '\t')
    i++;

  if(line[i] == 0) {
    putc1('\n');
    return;
  }

  if(strncmp(line + i, "```", 3) == 0) {
    *in_code = !*in_code;
    putc1('\n');
    return;
  }

  if(*in_code) {
    puts1("    ");
    puts1(line + i);
    putc1('\n');
    return;
  }

  if(strncmp(line + i, "# ", 2) == 0) {
    putc1('\n');
    style_begin(ANSI_BOLD);
    print_upper(line + i + 2);
    style_end();
    puts1("\n\n");
    return;
  }

  if(strncmp(line + i, "## ", 3) == 0) {
    int j;
    int len;

    putc1('\n');
    style_begin(ANSI_BOLD);
    render_inline(line + i + 3);
    style_end();
    putc1('\n');
    len = strlen(line + i + 3);
    for(j = 0; j < len; j++)
      putc1('-');
    puts1("\n");
    return;
  }

  if(strncmp(line + i, "### ", 4) == 0) {
    puts1("\n* ");
    style_begin(ANSI_BOLD);
    render_inline(line + i + 4);
    style_end();
    puts1("\n");
    return;
  }

  if(strncmp(line + i, "- ", 2) == 0) {
    puts1("  - ");
    render_inline(line + i + 2);
    putc1('\n');
    return;
  }

  if(is_ordered_list_item(line + i)) {
    int j;

    j = 0;
    while(line[i + j] >= '0' && line[i + j] <= '9')
      j++;
    puts1("  ");
    render_span(line + i, 0, j);
    puts1(") ");
    render_inline(line + i + j + 2);
    putc1('\n');
    return;
  }

  if(strncmp(line + i, "> ", 2) == 0) {
    style_begin(ANSI_BOLD);
    puts1("| ");
    style_end();
    render_inline(line + i + 2);
    putc1('\n');
    return;
  }

  if(strcmp(line + i, "---") == 0 || strcmp(line + i, "***") == 0 || strcmp(line + i, "___") == 0) {
    puts1("----------------------------------------\n");
    return;
  }

  render_inline(line + i);
    putc1('\n');
}

static void
usage(void)
{
  dprintf(2, "usage: man [-l] topic\n");
  dprintf(2, "       pager keys: Enter=next page, q=quit\n");
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
  style_enabled = isatty(1);
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
    dprintf(2, "man: read error: %s\n", path);
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

static int
list_topics(void)
{
  int fd;
  int nent;
  int i;
  struct dirent des[16];

  fd = open(MAN_DIR, O_RDONLY);
  if(fd < 0) {
    dprintf(2, "man: cannot open %s\n", MAN_DIR);
    return -1;
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

      dprintf(1, "%s\n", name);
    }
  }

  close(fd);
  return 0;
}

int
main(int argc, char *argv[])
{
  char path[128];

  if(argc != 2) {
    usage();
    exit(1);
  }

  if(strcmp(argv[1], "-l") == 0) {
    exit(list_topics() < 0 ? 1 : 0);
  }

  if(strchr(argv[1], '/')) {
    int rc;

    rc = render_markdown_file(argv[1]);
    if(rc < 0)
      dprintf(2, "man: no entry for %s\n", argv[1]);
    exit(rc < 0 ? 1 : 0);
  }

  if(build_topic_path(path, sizeof(path), argv[1], 1) == 0 && render_markdown_file(path) == 0)
    exit(0);

  if(build_topic_path(path, sizeof(path), argv[1], 0) == 0 && render_markdown_file(path) == 0)
    exit(0);

  dprintf(2, "man: no entry for %s\n", argv[1]);
  dprintf(2, "man: run 'man -l' to list topics\n");
  exit(1);
}
