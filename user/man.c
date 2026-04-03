#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fs.h"
#include "fcntl.h"
#include "termios.h"
#include "sys/ioctl.h"

#define MAN_DIR "/usr/share/man"
#define MAN_DEFAULT_PAGE_LINES 24
#define MAN_DEFAULT_COLS 80
#define MAN_DEFAULT_RULE_COLS 40

static char buf[512];
static int pager_enabled;
static int pager_page_lines;
static int pager_lines;
static int pager_cols;
static int pager_col;
static int pager_wrap_pending;
static int pager_quit;
static int style_enabled;

#define ANSI_RESET "\033[0m"
#define ANSI_BOLD "\033[1m"
#define ANSI_UNDERLINE "\033[4m"
#define ANSI_REVERSE "\033[7m"

static void
raw_write1(const char *s, int n)
{
  if(n <= 0)
    return;
  write(1, s, n);
}

static void
raw_puts1(const char *s)
{
  raw_write1(s, strlen(s));
}

static void
detect_terminal_size(int *rows_out, int *cols_out)
{
  struct winsize ws;
  int rows;
  int cols;
  int fds[3];
  int i;

  rows = MAN_DEFAULT_PAGE_LINES;
  cols = MAN_DEFAULT_COLS;
  fds[0] = 1;
  fds[1] = 0;
  fds[2] = 2;

  for(i = 0; i < 3; i++) {
    if(!isatty(fds[i]))
      continue;
    memset(&ws, 0, sizeof(ws));
    if(ioctl(fds[i], TIOCGWINSZ, &ws) < 0)
      continue;
    if(ws.ws_row > 0)
      rows = ws.ws_row;
    if(ws.ws_col > 0)
      cols = ws.ws_col;
    break;
  }

  if(rows_out)
    *rows_out = rows;
  if(cols_out)
    *cols_out = cols;
}

static void pager_prompt(void);
static void putc1(char c);

static void
pager_reset_screenline(void)
{
  pager_col = 0;
  pager_wrap_pending = 0;
}

static void
pager_reset_page(void)
{
  pager_lines = 0;
  pager_reset_screenline();
}

static void
pager_advance_line(void)
{
  if(!pager_enabled)
    return;

  pager_lines++;
  if(pager_page_lines > 0 && pager_lines >= pager_page_lines)
    pager_prompt();
}

static void
pager_before_visible(void)
{
  if(!pager_enabled || !pager_wrap_pending)
    return;

  pager_advance_line();
  pager_reset_screenline();
}

static void
pager_after_cells(int cells)
{
  int available;

  if(!pager_enabled || cells <= 0)
    return;
  if(pager_cols <= 0)
    return;

  while(cells > 0) {
    available = pager_cols - pager_col;
    if(available <= 0) {
      pager_wrap_pending = 1;
      pager_before_visible();
      if(pager_quit)
        return;
      continue;
    }
    if(cells < available) {
      pager_col += cells;
      return;
    }
    pager_col = pager_cols;
    pager_wrap_pending = 1;
    cells -= available;
    if(cells > 0) {
      pager_before_visible();
      if(pager_quit)
        return;
    }
  }
}

static void
pager_after_newline(void)
{
  if(!pager_enabled)
    return;

  pager_advance_line();
  pager_reset_screenline();
}

static void
pager_after_carriage_return(void)
{
  if(!pager_enabled)
    return;

  pager_reset_screenline();
}

static int
pager_prompt_width(void)
{
  int width;

  width = pager_cols;
  if(width <= 0)
    width = MAN_DEFAULT_COLS;
  if(width > 1)
    return width - 1;
  return width;
}

static int
man_rule_width(void)
{
  if(style_enabled && pager_cols > 0)
    return pager_cols;
  return MAN_DEFAULT_RULE_COLS;
}

static void
print_rule(void)
{
  int i;
  int width;

  width = man_rule_width();
  if(width <= 0)
    width = MAN_DEFAULT_RULE_COLS;
  for(i = 0; i < width; i++)
    putc1('-');
  putc1('\n');
}

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
  static const char prompt_full[] = "--More-- (Enter:next, q:quit)";
  static const char prompt_short[] = "--More-- (q:quit)";
  static const char prompt_mini[] = "--More--";
  static const char prompt_tiny[] = ">";
  const char *prompt;
  int prompt_len;
  int clear_len;
  char c;
  int i;

  prompt = prompt_full;
  prompt_len = sizeof(prompt_full) - 1;
  if(prompt_len > pager_prompt_width()) {
    prompt = prompt_short;
    prompt_len = sizeof(prompt_short) - 1;
  }
  if(prompt_len > pager_prompt_width()) {
    prompt = prompt_mini;
    prompt_len = sizeof(prompt_mini) - 1;
  }
  if(prompt_len > pager_prompt_width()) {
    prompt = prompt_tiny;
    prompt_len = sizeof(prompt_tiny) - 1;
  }
  if(prompt_len > pager_prompt_width())
    prompt_len = pager_prompt_width();

  raw_write1(prompt, prompt_len);
  c = read_pager_key();

  raw_write1("\r", 1);
  clear_len = prompt_len;
  for(i = 0; i < clear_len; i++)
    raw_write1(" ", 1);
  raw_write1("\r", 1);

  if(c == 'q' || c == 'Q')
    pager_quit = 1;

  pager_reset_page();
}

static void
putc1(char c)
{
  int tabw;

  if(pager_quit)
    return;

  if(c == '\n') {
    write(1, &c, 1);
    pager_after_newline();
    return;
  }

  if(c == '\r') {
    write(1, &c, 1);
    pager_after_carriage_return();
    return;
  }

  pager_before_visible();
  if(pager_quit)
    return;

  write(1, &c, 1);

  if(!pager_enabled)
    return;

  if(c == '\t') {
    if(pager_cols > 0)
      tabw = 8 - (pager_col % 8);
    else
      tabw = 8;
    pager_after_cells(tabw);
    return;
  }

  if(c >= ' ')
    pager_after_cells(1);
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
  if(style_enabled && !pager_quit)
    raw_puts1(code);
}

static void
style_end(void)
{
  if(style_enabled && !pager_quit)
    raw_puts1(ANSI_RESET);
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
    print_rule();
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
  detect_terminal_size(&pager_page_lines, &pager_cols);
  if(pager_page_lines <= 0)
    pager_page_lines = MAN_DEFAULT_PAGE_LINES;
  if(pager_cols <= 0)
    pager_cols = MAN_DEFAULT_COLS;
  pager_reset_page();
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
