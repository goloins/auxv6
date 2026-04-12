#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "errno.h"
#include "termios.h"
#include "sys/ioctl.h"
#include "auxv6/user.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#define NANO_VERSION "6nano 0.1"
#define NANO_STATUS_MAX 160
#define NANO_PROMPT_MAX 96
#define NANO_MAX_ROWS 80
#define NANO_MAX_COLS 240

#define CTRL_KEY(k) ((k) & 0x1f)

enum nano_key {
  KEY_ARROW_LEFT = 1000,
  KEY_ARROW_RIGHT,
  KEY_ARROW_UP,
  KEY_ARROW_DOWN,
  KEY_DEL,
  KEY_HOME,
  KEY_END,
  KEY_PAGE_UP,
  KEY_PAGE_DOWN,
};

struct nano_row {
  int len;
  char *chars;
};

struct nano_editor {
  int cx;
  int cy;
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  struct nano_row *rows;
  int dirty;
  int quit;
  int quit_confirm;
  char *filename;
  char status[NANO_STATUS_MAX];
  struct termios saved;
  int raw_enabled;
};

static struct nano_editor E;

struct nano_abuf {
  char *b;
  int len;
  int cap;
};

static void
ab_init(struct nano_abuf *ab)
{
  ab->b = 0;
  ab->len = 0;
  ab->cap = 0;
}

static void
ab_append(struct nano_abuf *ab, const char *s, int n)
{
  char *newb;
  int newcap;

  if(n <= 0)
    return;
  if(ab->len + n > ab->cap) {
    newcap = ab->cap * 2;
    if(newcap < ab->len + n)
      newcap = ab->len + n;
    if(newcap < 4096)
      newcap = 4096;
    newb = (char*)realloc(ab->b, (size_t)newcap);
    if(newb == 0)
      return;
    ab->b = newb;
    ab->cap = newcap;
  }
  memmove(ab->b + ab->len, s, (size_t)n);
  ab->len += n;
}

static void
ab_flush(struct nano_abuf *ab)
{
  int off = 0;
  int r;

  while(off < ab->len) {
    r = write(1, ab->b + off, ab->len - off);
    if(r <= 0)
      break;
    off += r;
  }
  ab->len = 0;
}

static void
ab_free(struct nano_abuf *ab)
{
  free(ab->b);
  ab->b = 0;
  ab->len = 0;
  ab->cap = 0;
}

static void
nano_set_status(const char *msg)
{
  if(msg == 0)
    msg = "";
  snprintf(E.status, sizeof(E.status), "%s", msg);
}

static void
nano_disable_raw(void)
{
  if(E.raw_enabled) {
    tcsetattr(0, TCSAFLUSH, &E.saved);
    E.raw_enabled = 0;
  }
}

static void
nano_cleanup_terminal(void)
{
  /* Leave shell with a predictable clean viewport and cursor home. */
  write(1, "\x1b[0m\x1b[2J\x1b[H", sizeof("\x1b[0m\x1b[2J\x1b[H") - 1);
}

static void
nano_die(const char *msg)
{
  nano_disable_raw();
  dprintf(2, "6nano: %s\n", msg);
  exit(1);
}

static void
nano_enable_raw(void)
{
  struct termios raw;

  if(!isatty(0) || !isatty(1))
    nano_die("stdin/stdout must be a tty");
  if(tcgetattr(0, &E.saved) < 0)
    nano_die("tcgetattr failed");

  raw = E.saved;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if(tcsetattr(0, TCSAFLUSH, &raw) < 0)
    nano_die("tcsetattr failed");

  E.raw_enabled = 1;
}

static int
nano_read_key(void)
{
  char c;

  while(read(0, &c, 1) != 1)
    ;


  if(c == '\x1b') {
    char seq[3];

    if(read(0, &seq[0], 1) != 1)
      return '\x1b';
    if(read(0, &seq[1], 1) != 1)
      return '\x1b';

    if(seq[0] == '[') {
      if(seq[1] >= '0' && seq[1] <= '9') {
        if(read(0, &seq[2], 1) != 1)
          return '\x1b';
        if(seq[2] == '~') {
          switch(seq[1]) {
          case '1': return KEY_HOME;
          case '3': return KEY_DEL;
          case '4': return KEY_END;
          case '5': return KEY_PAGE_UP;
          case '6': return KEY_PAGE_DOWN;
          case '7': return KEY_HOME;
          case '8': return KEY_END;
          }
        }
      } else {
        switch(seq[1]) {
        case 'A': return KEY_ARROW_UP;
        case 'B': return KEY_ARROW_DOWN;
        case 'C': return KEY_ARROW_RIGHT;
        case 'D': return KEY_ARROW_LEFT;
        case 'H': return KEY_HOME;
        case 'F': return KEY_END;
        }
      }
    }
    return '\x1b';
  }

  return c;
}

static int
nano_get_window_size(int *rows, int *cols)
{
  struct winsize ws;

  if(ioctl(1, TIOCGWINSZ, &ws) < 0 || ws.ws_col == 0 || ws.ws_row == 0)
    return -1;

  if(ws.ws_row > NANO_MAX_ROWS || ws.ws_col > NANO_MAX_COLS)
    return -1;

  *cols = ws.ws_col;
  *rows = ws.ws_row;
  return 0;
}

static void
nano_row_insert_char(struct nano_row *row, int at, int c)
{
  char *n;

  if(at < 0 || at > row->len)
    at = row->len;

  n = (char*)realloc(row->chars, (size_t)row->len + 2);
  if(n == 0)
    return;
  row->chars = n;

  memmove(&row->chars[at + 1], &row->chars[at], (size_t)(row->len - at + 1));
  row->len++;
  row->chars[at] = (char)c;
}

static void
nano_row_del_char(struct nano_row *row, int at)
{
  if(at < 0 || at >= row->len)
    return;
  memmove(&row->chars[at], &row->chars[at + 1], (size_t)(row->len - at));
  row->len--;
}

static void
nano_insert_row(int at, const char *s, int len)
{
  struct nano_row *nrows;
  struct nano_row *row;

  if(at < 0 || at > E.numrows)
    return;

  nrows = (struct nano_row*)realloc(E.rows, (size_t)(E.numrows + 1) * sizeof(struct nano_row));
  if(nrows == 0)
    return;
  E.rows = nrows;

  memmove(&E.rows[at + 1], &E.rows[at], (size_t)(E.numrows - at) * sizeof(struct nano_row));
  row = &E.rows[at];
  row->len = len;
  row->chars = (char*)malloc((size_t)len + 1);
  if(row->chars == 0) {
    row->len = 0;
    return;
  }
  if(len > 0 && s)
    memmove(row->chars, s, (size_t)len);
  row->chars[len] = '\0';
  E.numrows++;
}

static void
nano_del_row(int at)
{
  if(at < 0 || at >= E.numrows)
    return;
  free(E.rows[at].chars);
  memmove(&E.rows[at], &E.rows[at + 1], (size_t)(E.numrows - at - 1) * sizeof(struct nano_row));
  E.numrows--;
}

static void
nano_insert_char(int c)
{
  if(E.cy == E.numrows)
    nano_insert_row(E.numrows, "", 0);
  if(E.cy < 0 || E.cy >= E.numrows)
    return;

  nano_row_insert_char(&E.rows[E.cy], E.cx, c);
  E.cx++;
  E.dirty = 1;
}

static void
nano_insert_newline(void)
{
  struct nano_row *row;

  if(E.cy < 0 || E.cy > E.numrows)
    return;

  if(E.cy == E.numrows) {
    nano_insert_row(E.numrows, "", 0);
    E.cy = E.numrows - 1;
    E.cx = 0;
    E.dirty = 1;
    return;
  }

  row = &E.rows[E.cy];
  if(E.cx <= 0) {
    nano_insert_row(E.cy, "", 0);
  } else if(E.cx >= row->len) {
    nano_insert_row(E.cy + 1, "", 0);
  } else {
    nano_insert_row(E.cy + 1, &row->chars[E.cx], row->len - E.cx);
    row = &E.rows[E.cy];
    row->len = E.cx;
    row->chars[row->len] = '\0';
  }

  E.cy++;
  E.cx = 0;
  E.dirty = 1;
}

static void
nano_del_char(void)
{
  struct nano_row *row;
  int prevlen;

  if(E.cy < 0 || E.cy >= E.numrows)
    return;
  if(E.cx == 0 && E.cy == 0)
    return;

  row = &E.rows[E.cy];
  if(E.cx > 0) {
    nano_row_del_char(row, E.cx - 1);
    E.cx--;
  } else {
    prevlen = E.rows[E.cy - 1].len;
    E.rows[E.cy - 1].chars = (char*)realloc(E.rows[E.cy - 1].chars,
                                            (size_t)prevlen + (size_t)row->len + 1);
    if(E.rows[E.cy - 1].chars == 0)
      return;
    memmove(&E.rows[E.cy - 1].chars[prevlen], row->chars, (size_t)row->len + 1);
    E.rows[E.cy - 1].len = prevlen + row->len;
    nano_del_row(E.cy);
    E.cy--;
    E.cx = prevlen;
  }

  E.dirty = 1;
}

static void
nano_del_at_cursor(void)
{
  struct nano_row *row;

  if(E.cy < 0 || E.cy >= E.numrows)
    return;

  row = &E.rows[E.cy];
  if(E.cx < row->len) {
    nano_row_del_char(row, E.cx);
    E.dirty = 1;
    return;
  }
  if(E.cy + 1 >= E.numrows)
    return;

  row->chars = (char*)realloc(row->chars, (size_t)row->len + (size_t)E.rows[E.cy + 1].len + 1);
  if(row->chars == 0)
    return;
  memmove(&row->chars[row->len], E.rows[E.cy + 1].chars, (size_t)E.rows[E.cy + 1].len + 1);
  row->len += E.rows[E.cy + 1].len;
  nano_del_row(E.cy + 1);
  E.dirty = 1;
}

static void
nano_scroll(void)
{
  int viewcols = E.screencols - 2;

  if(viewcols < 1)
    viewcols = 1;

  if(E.cy < E.rowoff)
    E.rowoff = E.cy;
  if(E.cy >= E.rowoff + E.screenrows)
    E.rowoff = E.cy - E.screenrows + 1;
  if(E.cx < E.coloff)
    E.coloff = E.cx;
  if(E.cx >= E.coloff + viewcols)
    E.coloff = E.cx - viewcols + 1;
}

static void
nano_draw_rows(struct nano_abuf *ab)
{
  int y;
  int viewcols = E.screencols - 2;

  if(viewcols < 1)
    viewcols = 1;

  for(y = 0; y < E.screenrows; y++) {
    char pos[24];
    char spaces[256];
    int fill;
    int pn;
    int filerow = y + E.rowoff;

    pn = snprintf(pos, sizeof(pos), "\x1b[%d;1H", y + 1);
    if(pn > 0)
      ab_append(ab, pos, pn);

    ab_append(ab, "\x1b[44;37m \x1b[0m", 13);

    if(filerow >= E.numrows) {
      if(E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen;
        int padding;

        welcomelen = snprintf(welcome, sizeof(welcome), "%s", NANO_VERSION);
        if(welcomelen > viewcols)
          welcomelen = viewcols;

        padding = (viewcols - welcomelen) / 2;
        fill = padding;
        if(fill > (int)sizeof(spaces))
          fill = (int)sizeof(spaces);
        if(fill > 0) {
          memset(spaces, ' ', fill);
          ab_append(ab, spaces, fill);
        }
        ab_append(ab, welcome, welcomelen);
        fill = viewcols - padding - welcomelen;
        if(fill > (int)sizeof(spaces))
          fill = (int)sizeof(spaces);
        if(fill > 0) {
          memset(spaces, ' ', fill);
          ab_append(ab, spaces, fill);
        }
      } else {
        fill = viewcols;
        while(fill > 0) {
          int chunk = fill;
          if(chunk > (int)sizeof(spaces))
            chunk = (int)sizeof(spaces);
          memset(spaces, ' ', chunk);
          ab_append(ab, spaces, chunk);
          fill -= chunk;
        }
      }
    } else {
      int len = E.rows[filerow].len - E.coloff;
      if(len < 0)
        len = 0;
      if(len > viewcols)
        len = viewcols;
      if(len > 0)
        ab_append(ab, &E.rows[filerow].chars[E.coloff], len);

      fill = viewcols - len;
      while(fill > 0) {
        int chunk = fill;
        if(chunk > (int)sizeof(spaces))
          chunk = (int)sizeof(spaces);
        memset(spaces, ' ', chunk);
        ab_append(ab, spaces, chunk);
        fill -= chunk;
      }
    }

    ab_append(ab, "\x1b[44;37m \x1b[0m", 13);
  }
}

static void
nano_draw_status_bar(struct nano_abuf *ab)
{
  char pos[24];
  char left[96];
  char right[64];
  char spaces[256];
  int llen;
  int rlen;
  int pad;
  int pn;

  pn = snprintf(pos, sizeof(pos), "\x1b[%d;1H", E.screenrows + 1);
  if(pn > 0)
    ab_append(ab, pos, pn);

  snprintf(left, sizeof(left), " %.40s%s ",
           E.filename ? E.filename : "[No Name]",
           E.dirty ? " [+]" : "");
  snprintf(right, sizeof(right), " ^G Help ^O WriteOut ^X Exit  %d,%d ", E.cy + 1, E.cx + 1);

  llen = strlen(left);
  rlen = strlen(right);
  if(llen > E.screencols)
    llen = E.screencols;

  ab_append(ab, "\x1b[7m", 4);
  ab_append(ab, left, llen);

  pad = E.screencols - llen - rlen;
  if(pad > 0) {
    if(pad > (int)sizeof(spaces))
      pad = (int)sizeof(spaces);
    memset(spaces, ' ', pad);
    ab_append(ab, spaces, pad);
  }

  if(rlen > E.screencols)
    rlen = E.screencols;
  if(rlen > 0) {
    int start = 0;
    if(llen + rlen > E.screencols)
      start = llen + rlen - E.screencols;
    ab_append(ab, right + start, rlen - start);
  }

  ab_append(ab, "\x1b[m", 3);
}

static void
nano_draw_message_bar(struct nano_abuf *ab)
{
  char pos[24];
  int pn;
  int n;

  pn = snprintf(pos, sizeof(pos), "\x1b[%d;1H", E.screenrows + 2);
  if(pn > 0)
    ab_append(ab, pos, pn);

  ab_append(ab, "\x1b[K", 3);
  n = strlen(E.status);
  if(n > E.screencols)
    n = E.screencols;
  if(n > 0)
    ab_append(ab, E.status, n);
}

static void
nano_update_window_size(void)
{
  int rows;
  int cols;
  int new_screenrows;
  int new_screencols;

  if(nano_get_window_size(&rows, &cols) < 0)
    return;

  new_screenrows = rows - 2;
  if(new_screenrows < 1)
    new_screenrows = 1;
  if(new_screenrows > NANO_MAX_ROWS)
    new_screenrows = NANO_MAX_ROWS;
  new_screencols = cols;
  if(new_screencols < 20)
    new_screencols = 20;
  if(new_screencols > NANO_MAX_COLS)
    new_screencols = NANO_MAX_COLS;

  E.screenrows = new_screenrows;
  E.screencols = new_screencols;
}

static void
nano_refresh_screen(void)
{
  struct nano_abuf ab;
  char buf[32];
  int viewcols;
  int n;
  int curx;
  int cury;

  nano_update_window_size();
  nano_scroll();

  ab_init(&ab);

  ab_append(&ab, "\x1b[0m\x1b[37;40m\x1b[H\x1b[2J", 19);

  nano_draw_rows(&ab);
  nano_draw_status_bar(&ab);
  nano_draw_message_bar(&ab);

  viewcols = E.screencols - 2;
  if(viewcols < 1)
    viewcols = 1;

  cury = (E.cy - E.rowoff) + 1;
  curx = (E.cx - E.coloff) + 2;
  if(cury < 1)
    cury = 1;
  if(cury > E.screenrows)
    cury = E.screenrows;
  if(curx < 1)
    curx = 1;
  if(curx > viewcols + 1)
    curx = viewcols + 1;

  n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cury, curx);
  if(n > 0)
    ab_append(&ab, buf, n);

  ab_flush(&ab);
  ab_free(&ab);
}

static void
nano_move_cursor(int key)
{
  struct nano_row *row;

  row = (E.cy >= 0 && E.cy < E.numrows) ? &E.rows[E.cy] : 0;

  switch(key) {
  case KEY_ARROW_LEFT:
    if(E.cx > 0) {
      E.cx--;
    } else if(E.cy > 0) {
      E.cy--;
      E.cx = E.rows[E.cy].len;
    }
    break;
  case KEY_ARROW_RIGHT:
    if(row && E.cx < row->len) {
      E.cx++;
    } else if(row && E.cx == row->len && E.cy + 1 < E.numrows) {
      E.cy++;
      E.cx = 0;
    }
    break;
  case KEY_ARROW_UP:
    if(E.cy > 0)
      E.cy--;
    break;
  case KEY_ARROW_DOWN:
    if(E.cy + 1 < E.numrows)
      E.cy++;
    break;
  }

  row = (E.cy >= 0 && E.cy < E.numrows) ? &E.rows[E.cy] : 0;
  if(row && E.cx > row->len)
    E.cx = row->len;
}

static int
nano_set_filename(const char *name)
{
  char *copy;

  if(name == 0 || name[0] == '\0')
    return -1;

  copy = strdup(name);
  if(copy == 0)
    return -1;

  if(E.filename)
    free(E.filename);
  E.filename = copy;
  return 0;
}

static int
nano_prompt_filename(const char *prompt, char *buf, int buflen)
{
  int len = 0;
  int c;

  if(buflen <= 1)
    return -1;
  buf[0] = '\0';

  while(1) {
    snprintf(E.status, sizeof(E.status), prompt, buf);
    nano_refresh_screen();

    c = nano_read_key();
    if(c == '\x1b') {
      nano_set_status("Save cancelled");
      return -1;
    }
    if(c == '\r' || c == '\n') {
      if(len == 0)
        continue;
      buf[len] = '\0';
      return 0;
    }
    if(c == 127 || c == '\b') {
      if(len > 0)
        buf[--len] = '\0';
      continue;
    }
    if(c >= 32 && c <= 126 && len + 1 < buflen) {
      buf[len++] = (char)c;
      buf[len] = '\0';
    }
  }
}

static int
nano_save_file(void)
{
  int fd;
  int i;

  if(E.filename == 0 || E.filename[0] == '\0') {
    char namebuf[NANO_PROMPT_MAX];

    if(nano_prompt_filename("Write file: %s (ESC to cancel)", namebuf, sizeof(namebuf)) < 0)
      return -1;
    if(nano_set_filename(namebuf) < 0) {
      nano_set_status("out of memory");
      return -1;
    }
  }

  fd = open(E.filename, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0) {
    nano_set_status("write failed");
    return -1;
  }

  for(i = 0; i < E.numrows; i++) {
    if(E.rows[i].len > 0 && write(fd, E.rows[i].chars, E.rows[i].len) != E.rows[i].len) {
      close(fd);
      nano_set_status("write failed");
      return -1;
    }
    if(write(fd, "\n", 1) != 1) {
      close(fd);
      nano_set_status("write failed");
      return -1;
    }
  }

  close(fd);
  E.dirty = 0;
  E.quit_confirm = 0;
  nano_set_status("Wrote file");
  return 0;
}

static void
nano_process_keypress(void)
{
  int c = nano_read_key();

  switch(c) {
  case CTRL_KEY('x'):
    if(E.dirty && E.quit_confirm == 0) {
      E.quit_confirm = 1;
      nano_set_status("Unsaved changes. Press ^O to write, or ^X again to quit.");
      return;
    }
    E.quit = 1;
    return;
  case CTRL_KEY('o'):
    nano_save_file();
    return;
  case CTRL_KEY('g'):
    nano_set_status("^G Help  ^O WriteOut  ^X Exit  Arrows Move  Enter Newline");
    E.quit_confirm = 0;
    return;
  case KEY_ARROW_UP:
  case KEY_ARROW_DOWN:
  case KEY_ARROW_LEFT:
  case KEY_ARROW_RIGHT:
    nano_move_cursor(c);
    E.quit_confirm = 0;
    return;
  case KEY_PAGE_UP:
  case KEY_PAGE_DOWN:
    {
      int times = E.screenrows;
      if(c == KEY_PAGE_UP)
        E.cy = E.rowoff;
      else if(c == KEY_PAGE_DOWN)
        E.cy = E.rowoff + E.screenrows - 1;

      if(E.cy > E.numrows)
        E.cy = E.numrows;

      while(times--)
        nano_move_cursor(c == KEY_PAGE_UP ? KEY_ARROW_UP : KEY_ARROW_DOWN);
    }
    E.quit_confirm = 0;
    return;
  case KEY_HOME:
    E.cx = 0;
    E.quit_confirm = 0;
    return;
  case KEY_END:
    if(E.cy >= 0 && E.cy < E.numrows)
      E.cx = E.rows[E.cy].len;
    E.quit_confirm = 0;
    return;
  case 127:
  case '\b':
    nano_del_char();
    E.quit_confirm = 0;
    return;
  case KEY_DEL:
    nano_del_at_cursor();
    E.quit_confirm = 0;
    return;
  case '\r':
  case '\n':
    nano_insert_newline();
    E.quit_confirm = 0;
    return;
  default:
    break;
  }

  if(c == '\t' || (c >= 32 && c <= 126)) {
    nano_insert_char(c);
    E.quit_confirm = 0;
  }
}

static void
nano_open(const char *filename)
{
  FILE *fp;
  char *line;
  size_t cap;
  ssize_t got;

  if(filename == 0)
    return;

  E.filename = strdup(filename);
  if(E.filename == 0)
    nano_die("out of memory");

  fp = fopen(filename, "r");
  if(fp == 0) {
    if(errno == ENOENT) {
      nano_set_status("New file");
      return;
    }
    nano_set_status("open failed (new buffer)");
    return;
  }

  line = 0;
  cap = 0;
  while((got = getline(&line, &cap, fp)) >= 0) {
    while(got > 0 && (line[got - 1] == '\n' || line[got - 1] == '\r'))
      got--;
    nano_insert_row(E.numrows, line, got);
  }
  free(line);
  fclose(fp);
  E.dirty = 0;
}

static void
nano_init(void)
{
  int rows;
  int cols;

  memset(&E, 0, sizeof(E));

  if(nano_get_window_size(&rows, &cols) < 0) {
    rows = 24;
    cols = 80;
  }
  E.screenrows = rows - 2;
  if(E.screenrows < 1)
    E.screenrows = 1;
  if(E.screenrows > NANO_MAX_ROWS)
    E.screenrows = NANO_MAX_ROWS;
  E.screencols = cols;
  if(E.screencols < 20)
    E.screencols = 20;
  if(E.screencols > NANO_MAX_COLS)
    E.screencols = NANO_MAX_COLS;
}

static void
nano_usage(void)
{
  dprintf(2, "usage: 6nano [file]\n");
  dprintf(2, "keys: ^O save, ^X quit, arrows move, enter newline\n");
  exit(1);
}

int
main(int argc, char *argv[])
{
  if(argc > 2)
    nano_usage();

  nano_init();
  nano_enable_raw();

  if(argc == 2)
    nano_open(argv[1]);

  if(E.numrows == 0)
    nano_insert_row(0, "", 0);

  nano_set_status("^G Help | ^O WriteOut | ^X Exit");

  while(!E.quit) {
    nano_refresh_screen();
    nano_process_keypress();
  }

  nano_disable_raw();
  nano_cleanup_terminal();
  return 0;
}
