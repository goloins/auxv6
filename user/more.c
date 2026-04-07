#include "types.h"
#include "fcntl.h"
#include "stat.h"
#include "termios.h"
#include "sys/ioctl.h"
#include "auxv6/user.h"

#define DEFAULT_PAGE_LINES 24

struct pager_state {
  int is_less;
  int page_lines;
  int line_count;
  int quit;
};

static const char *
base_name(const char *p)
{
  const char *q;

  if(p == 0)
    return "more";
  q = p + strlen(p);
  while(q > p && q[-1] != '/')
    q--;
  return q;
}

static void
usage(void)
{
  dprintf(2, "usage: more [-n lines] [file ...]\n");
  dprintf(2, "       less [-n lines] [file ...]\n");
  exit(1);
}

static int
detect_page_lines(void)
{
  struct winsize ws;
  int rows;

  rows = DEFAULT_PAGE_LINES;
  if(isatty(1) && ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 1)
    rows = ws.ws_row - 1;
  return rows;
}

static void
erase_prompt(void)
{
  write(2, "\r                    \r", 22);
}

static int
prompt_next(struct pager_state *st)
{
  char c;

  if(st->quit)
    return 0;

  if(st->is_less)
    dprintf(2, "\r: ");
  else
    dprintf(2, "\r--More--");

  if(read(0, &c, 1) != 1)
    c = 'q';

  erase_prompt();

  if(c == 'q' || c == 'Q') {
    st->quit = 1;
    return 0;
  }
  if(c == '\n' || c == '\r') {
    st->line_count = st->page_lines - 1;
    if(st->line_count < 0)
      st->line_count = 0;
    return 1;
  }

  st->line_count = 0;
  return 1;
}

static int
page_fd(int fd, struct pager_state *st)
{
  char buf[512];
  int n;
  int i;

  while((n = read(fd, buf, sizeof(buf))) > 0) {
    if(write(1, buf, n) != n) {
      dprintf(2, "more: write error\n");
      return -1;
    }

    for(i = 0; i < n; i++) {
      if(buf[i] != '\n')
        continue;
      st->line_count++;
      if(st->page_lines > 0 && st->line_count >= st->page_lines && isatty(0) && isatty(1)) {
        if(!prompt_next(st))
          return 0;
      }
    }
  }

  if(n < 0) {
    dprintf(2, "more: read error\n");
    return -1;
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  struct pager_state st;
  int i;
  int argi;

  memset(&st, 0, sizeof(st));
  st.is_less = (strcmp(base_name(argv[0]), "less") == 0);
  st.page_lines = detect_page_lines();

  argi = 1;
  while(argi < argc && argv[argi][0] == '-') {
    if(strcmp(argv[argi], "-h") == 0 || strcmp(argv[argi], "--help") == 0)
      usage();
    if(strcmp(argv[argi], "-n") == 0) {
      if(argi + 1 >= argc)
        usage();
      st.page_lines = atoi(argv[argi + 1]);
      if(st.page_lines <= 0)
        st.page_lines = DEFAULT_PAGE_LINES;
      argi += 2;
      continue;
    }
    break;
  }

  if(argi >= argc) {
    if(page_fd(0, &st) < 0)
      exit(1);
    exit(0);
  }

  for(i = argi; i < argc; i++) {
    int fd;

    fd = open(argv[i], O_RDONLY);
    if(fd < 0) {
      dprintf(2, "more: cannot open %s\n", argv[i]);
      continue;
    }

    if(argc - argi > 1)
      dprintf(1, "::::::::::::::\n%s\n::::::::::::::\n", argv[i]);

    if(page_fd(fd, &st) < 0) {
      close(fd);
      exit(1);
    }
    close(fd);

    if(st.quit)
      break;
  }

  exit(0);
}
