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

#define VI_VERSION "6vi 0.1"
#define VI_STATUS_MAX 160
#define VI_CMD_MAX 64
#define VI_MAX_ROWS 60
#define VI_MAX_COLS 200

enum vi_mode {
  MODE_NORMAL = 0,
  MODE_INSERT,
  MODE_COMMAND,
};

enum vi_key {
  KEY_ARROW_LEFT = 1000,
  KEY_ARROW_RIGHT,
  KEY_ARROW_UP,
  KEY_ARROW_DOWN,
  KEY_DEL,
  KEY_HOME,
  KEY_END,
};

struct vi_row {
  int len;
  char *chars;
};

struct vi_editor {
  int cx;
  int cy;
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  struct vi_row *rows;
  int dirty;
  int quit;
  enum vi_mode mode;
  char *filename;
  char status[VI_STATUS_MAX];
  char cmd[VI_CMD_MAX];
  int cmdlen;
  struct termios saved;
  int raw_enabled;
};

static struct vi_editor E;

/* --- append buffer: collects an entire refresh frame for one atomic write --- */
struct vi_abuf {
  char *b;
  int len;
  int cap;
};

static void
ab_init(struct vi_abuf *ab)
{
  ab->b = 0;
  ab->len = 0;
  ab->cap = 0;
}

static void
ab_append(struct vi_abuf *ab, const char *s, int n)
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
ab_flush(struct vi_abuf *ab)
{
  int off = 0, r;
  while(off < ab->len) {
    r = write(1, ab->b + off, ab->len - off);
    if(r <= 0) break;
    off += r;
  }
  ab->len = 0;
}

static void
ab_free(struct vi_abuf *ab)
{
  free(ab->b);
  ab->b = 0;
  ab->len = 0;
  ab->cap = 0;
}

static void
vi_set_status(const char *msg)
{
  if(msg == 0)
    msg = "";
  snprintf(E.status, sizeof(E.status), "%s", msg);
}

static void
vi_disable_raw(void)
{
  if(E.raw_enabled) {
    tcsetattr(0, TCSAFLUSH, &E.saved);
    E.raw_enabled = 0;
  }
}

static void
vi_cleanup_terminal(void)
{
  /* Leave shell with a predictable clean viewport and cursor home. */
  write(1, "\x1b[0m\x1b[2J\x1b[H", sizeof("\x1b[0m\x1b[2J\x1b[H") - 1);
}

static void
vi_die(const char *msg)
{
  vi_disable_raw();
  dprintf(2, "6vi: %s\n", msg);
  exit(1);
}

static void
vi_enable_raw(void)
{
  struct termios raw;

  if(!isatty(0) || !isatty(1))
    vi_die("stdin/stdout must be a tty");
  if(tcgetattr(0, &E.saved) < 0)
    vi_die("tcgetattr failed");

  raw = E.saved;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if(tcsetattr(0, TCSAFLUSH, &raw) < 0)
    vi_die("tcsetattr failed");

  E.raw_enabled = 1;
}

static int
vi_read_key(void)
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
vi_get_window_size(int *rows, int *cols)
{
  struct winsize ws;

  if(ioctl(1, TIOCGWINSZ, &ws) < 0 || ws.ws_col == 0 || ws.ws_row == 0)
    return -1;

  /* Some console paths can transiently report pixel-like magnitudes. */
  if(ws.ws_row > VI_MAX_ROWS || ws.ws_col > VI_MAX_COLS)
    return -1;

  *cols = ws.ws_col;
  *rows = ws.ws_row;
  return 0;
}

static void
vi_row_insert_char(struct vi_row *row, int at, int c)
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
vi_row_del_char(struct vi_row *row, int at)
{
  if(at < 0 || at >= row->len)
    return;
  memmove(&row->chars[at], &row->chars[at + 1], (size_t)(row->len - at));
  row->len--;
}

static void
vi_insert_row(int at, const char *s, int len)
{
  struct vi_row *nrows;
  struct vi_row *row;

  if(at < 0 || at > E.numrows)
    return;

  nrows = (struct vi_row*)realloc(E.rows, (size_t)(E.numrows + 1) * sizeof(struct vi_row));
  if(nrows == 0)
    return;
  E.rows = nrows;

  memmove(&E.rows[at + 1], &E.rows[at], (size_t)(E.numrows - at) * sizeof(struct vi_row));
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
vi_del_row(int at)
{
  if(at < 0 || at >= E.numrows)
    return;
  free(E.rows[at].chars);
  memmove(&E.rows[at], &E.rows[at + 1], (size_t)(E.numrows - at - 1) * sizeof(struct vi_row));
  E.numrows--;
}

static void
vi_insert_char(int c)
{
  if(E.cy == E.numrows)
    vi_insert_row(E.numrows, "", 0);
  if(E.cy < 0 || E.cy >= E.numrows)
    return;
  vi_row_insert_char(&E.rows[E.cy], E.cx, c);
  E.cx++;
  E.dirty = 1;
}

static void
vi_insert_newline(void)
{
  struct vi_row *row;

  if(E.cy < 0 || E.cy > E.numrows)
    return;

  if(E.cy == E.numrows) {
    vi_insert_row(E.numrows, "", 0);
    E.cy = E.numrows - 1;
    E.cx = 0;
    E.dirty = 1;
    return;
  }

  row = &E.rows[E.cy];
  if(E.cx <= 0) {
    vi_insert_row(E.cy, "", 0);
  } else if(E.cx >= row->len) {
    vi_insert_row(E.cy + 1, "", 0);
  } else {
    vi_insert_row(E.cy + 1, &row->chars[E.cx], row->len - E.cx);
    row = &E.rows[E.cy];
    row->len = E.cx;
    row->chars[row->len] = '\0';
  }

  E.cy++;
  E.cx = 0;
  E.dirty = 1;
}

static void
vi_del_char(void)
{
  struct vi_row *row;
  int prevlen;

  if(E.cy < 0 || E.cy >= E.numrows)
    return;
  if(E.cx == 0 && E.cy == 0)
    return;

  row = &E.rows[E.cy];
  if(E.cx > 0) {
    vi_row_del_char(row, E.cx - 1);
    E.cx--;
  } else {
    prevlen = E.rows[E.cy - 1].len;
    E.rows[E.cy - 1].chars = (char*)realloc(E.rows[E.cy - 1].chars,
                                            (size_t)prevlen + (size_t)row->len + 1);
    if(E.rows[E.cy - 1].chars == 0)
      return;
    memmove(&E.rows[E.cy - 1].chars[prevlen], row->chars, (size_t)row->len + 1);
    E.rows[E.cy - 1].len = prevlen + row->len;
    vi_del_row(E.cy);
    E.cy--;
    E.cx = prevlen;
  }

  E.dirty = 1;
}

static void
vi_del_at_cursor(void)
{
  struct vi_row *row;

  if(E.cy < 0 || E.cy >= E.numrows)
    return;
  row = &E.rows[E.cy];
  if(E.cx < row->len) {
    vi_row_del_char(row, E.cx);
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
  vi_del_row(E.cy + 1);
  E.dirty = 1;
}

static void
vi_scroll(void)
{
  if(E.cy < E.rowoff)
    E.rowoff = E.cy;
  if(E.cy >= E.rowoff + E.screenrows)
    E.rowoff = E.cy - E.screenrows + 1;
  if(E.cx < E.coloff)
    E.coloff = E.cx;
  if(E.cx >= E.coloff + E.screencols)
    E.coloff = E.cx - E.screencols + 1;
}

static void
vi_draw_rows(struct vi_abuf *ab)
{
  int y;

  for(y = 0; y < E.screenrows; y++) {
    char pos[24];
    int pn;
    int filerow = y + E.rowoff;

    pn = snprintf(pos, sizeof(pos), "\x1b[%d;1H", y + 1);
    if(pn > 0)
      ab_append(ab, pos, pn);

    if(filerow >= E.numrows) {
      if(E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        char spaces[256];
        int welcomelen;
        int padding;
        int fill;

        welcomelen = snprintf(welcome, sizeof(welcome), "%s", VI_VERSION);
        if(welcomelen > E.screencols)
          welcomelen = E.screencols;

        padding = (E.screencols - welcomelen) / 2;
        if(padding > 0) {
          ab_append(ab, "~", 1);
          padding--;
        }
        fill = padding;
        if(fill > (int)sizeof(spaces))
          fill = (int)sizeof(spaces);
        if(fill > 0) {
          memset(spaces, ' ', fill);
          ab_append(ab, spaces, fill);
        }
        ab_append(ab, welcome, welcomelen);
      } else {
        ab_append(ab, "~", 1);
      }
    } else {
      int len = E.rows[filerow].len - E.coloff;
      if(len < 0)
        len = 0;
      if(len > E.screencols)
        len = E.screencols;
      if(len > 0)
        ab_append(ab, &E.rows[filerow].chars[E.coloff], len);
    }

    ab_append(ab, "\x1b[K", 3);
  }
}

static const char*
vi_mode_name(void)
{
  switch(E.mode) {
  case MODE_INSERT:
    return "INSERT";
  case MODE_COMMAND:
    return "COMMAND";
  default:
    return "NORMAL";
  }
}

static void
vi_draw_status_bar(struct vi_abuf *ab)
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
  snprintf(right, sizeof(right), " %s  %d,%d ", vi_mode_name(), E.cy + 1, E.cx + 1);

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
vi_draw_cmdline(struct vi_abuf *ab)
{
  char pos[24];
  int pn;

  pn = snprintf(pos, sizeof(pos), "\x1b[%d;1H", E.screenrows + 2);
  if(pn > 0)
    ab_append(ab, pos, pn);

  ab_append(ab, "\x1b[K", 3);
  if(E.mode == MODE_COMMAND) {
    ab_append(ab, ":", 1);
    if(E.cmdlen > 0)
      ab_append(ab, E.cmd, E.cmdlen);
  } else if(E.status[0] != '\0') {
    int n = strlen(E.status);
    if(n > E.screencols)
      n = E.screencols;
    if(n > 0)
      ab_append(ab, E.status, n);
  }
}

static void
vi_update_window_size(void)
{
  int rows;
  int cols;
  int new_screenrows;
  int new_screencols;

  if(vi_get_window_size(&rows, &cols) < 0)
    return;

  new_screenrows = rows - 2;
  if(new_screenrows < 1)
    new_screenrows = 1;
  if(new_screenrows > VI_MAX_ROWS)
    new_screenrows = VI_MAX_ROWS;
  new_screencols = cols;
  if(new_screencols < 20)
    new_screencols = 20;
  if(new_screencols > VI_MAX_COLS)
    new_screencols = VI_MAX_COLS;

  E.screenrows = new_screenrows;
  E.screencols = new_screencols;
}

static void
vi_refresh_screen(void)
{
  struct vi_abuf ab;
  char buf[32];
  int n;
  int curx;
  int cury;

  vi_update_window_size();
  vi_scroll();

  ab_init(&ab);

  /* Force normal white-on-black before redraw to avoid stale SGR drift. */
  ab_append(&ab, "\x1b[0m\x1b[37;40m\x1b[H\x1b[2J", 19);

  vi_draw_rows(&ab);
  vi_draw_status_bar(&ab);
  vi_draw_cmdline(&ab);

  cury = (E.cy - E.rowoff) + 1;
  curx = (E.cx - E.coloff) + 1;
  if(cury < 1)
    cury = 1;
  if(curx < 1)
    curx = 1;

  /* position cursor only; avoid private cursor visibility modes */
  n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cury, curx);
  if(n > 0)
    ab_append(&ab, buf, n);

  /* single write() sends the entire rendered frame atomically */
  ab_flush(&ab);
  ab_free(&ab);
}

static void
vi_move_cursor(int key)
{
  struct vi_row *row;

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
vi_save_file(void)
{
  int fd;
  int i;

  if(E.filename == 0 || E.filename[0] == '\0') {
    vi_set_status("No file name. Open with: 6vi <file>");
    return -1;
  }

  fd = open(E.filename, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0) {
    vi_set_status("write failed");
    return -1;
  }

  for(i = 0; i < E.numrows; i++) {
    if(E.rows[i].len > 0 && write(fd, E.rows[i].chars, E.rows[i].len) != E.rows[i].len) {
      close(fd);
      vi_set_status("write failed");
      return -1;
    }
    if(write(fd, "\n", 1) != 1) {
      close(fd);
      vi_set_status("write failed");
      return -1;
    }
  }

  close(fd);
  E.dirty = 0;
  vi_set_status("written");
  return 0;
}

static int
vi_set_filename(const char *name)
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
vi_parse_write_command(const char *cmd, int *want_quit)
{
  const char *p;
  int wq;
  int namelen;
  char namebuf[VI_CMD_MAX];

  if(cmd == 0 || cmd[0] != 'w')
    return 0;

  p = cmd + 1;
  wq = 0;
  if(*p == 'q') {
    wq = 1;
    p++;
  }

  while(*p == ' ' || *p == '\t')
    p++;

  if(*p != '\0') {
    const char *end;

    end = p + strlen(p);
    while(end > p && (end[-1] == ' ' || end[-1] == '\t'))
      end--;

    namelen = end - p;
    if(namelen <= 0 || namelen >= (int)sizeof(namebuf)) {
      vi_set_status("Invalid filename");
      return 1;
    }

    memmove(namebuf, p, (size_t)namelen);
    namebuf[namelen] = '\0';

    if(vi_set_filename(namebuf) < 0) {
      vi_set_status("out of memory");
      return 1;
    }
  }

  if(vi_save_file() == 0 && wq)
    *want_quit = 1;
  return 1;
}

static void
vi_exec_command(void)
{
  char *cmd;
  char *end;
  int want_quit;

  E.cmd[E.cmdlen] = '\0';
  cmd = E.cmd;
  while(*cmd == ' ' || *cmd == '\t')
    cmd++;
  end = cmd + strlen(cmd);
  while(end > cmd && (end[-1] == ' ' || end[-1] == '\t'))
    end--;
  *end = '\0';

  want_quit = 0;

  if(vi_parse_write_command(cmd, &want_quit)) {
    if(want_quit)
      E.quit = 1;
  } else if(strcmp(cmd, "q") == 0) {
    if(E.dirty)
      vi_set_status("No write since last change (use :q!)");
    else
      E.quit = 1;
  } else if(strcmp(cmd, "q!") == 0) {
    E.quit = 1;
  } else if(strcmp(cmd, "x") == 0) {
    if(vi_save_file() == 0)
      E.quit = 1;
  } else if(cmd[0] != '\0') {
    vi_set_status("Unknown command");
  }

  E.mode = MODE_NORMAL;
  E.cmdlen = 0;
}

static void
vi_process_keypress(void)
{
  int c = vi_read_key();

  if(E.mode == MODE_COMMAND) {
    if(c == '\r' || c == '\n') {
      vi_exec_command();
      return;
    }
    if(c == '\x1b') {
      E.mode = MODE_NORMAL;
      E.cmdlen = 0;
      return;
    }
    if(c == 127 || c == '\b') {
      if(E.cmdlen > 0)
        E.cmdlen--;
      return;
    }
    if(c >= 32 && c <= 126 && E.cmdlen + 1 < VI_CMD_MAX) {
      E.cmd[E.cmdlen++] = (char)c;
    }
    return;
  }

  if(c == 17) {
    E.quit = 1;
    return;
  }

  if(c == KEY_ARROW_UP || c == KEY_ARROW_DOWN || c == KEY_ARROW_LEFT || c == KEY_ARROW_RIGHT) {
    vi_move_cursor(c);
    return;
  }

  if(c == KEY_HOME) {
    E.cx = 0;
    return;
  }
  if(c == KEY_END) {
    if(E.cy >= 0 && E.cy < E.numrows)
      E.cx = E.rows[E.cy].len;
    return;
  }

  if(E.mode == MODE_NORMAL) {
    switch(c) {
    case 'i':
      E.mode = MODE_INSERT;
      vi_set_status("-- INSERT --");
      break;
    case ':':
      E.mode = MODE_COMMAND;
      E.cmdlen = 0;
      break;
    case 'x':
    case KEY_DEL:
      vi_del_at_cursor();
      break;
    default:
      break;
    }
    return;
  }

  if(E.mode == MODE_INSERT) {
    switch(c) {
    case '\x1b':
      E.mode = MODE_NORMAL;
      vi_set_status("");
      break;
    case '\r':
    case '\n':
      vi_insert_newline();
      break;
    case 127:
    case '\b':
      vi_del_char();
      break;
    case KEY_DEL:
      vi_del_at_cursor();
      break;
    default:
      if(c == '\t' || (c >= 32 && c <= 126))
        vi_insert_char(c);
      break;
    }
  }
}

static void
vi_open(const char *filename)
{
  FILE *fp;
  char *line;
  size_t cap;
  ssize_t got;

  if(filename == 0)
    return;

  E.filename = strdup(filename);
  if(E.filename == 0)
    vi_die("out of memory");

  fp = fopen(filename, "r");
  if(fp == 0) {
    if(errno == ENOENT)
      return;
    vi_set_status("new file");
    return;
  }

  line = 0;
  cap = 0;
  while((got = getline(&line, &cap, fp)) >= 0) {
    while(got > 0 && (line[got - 1] == '\n' || line[got - 1] == '\r'))
      got--;
    vi_insert_row(E.numrows, line, got);
  }
  free(line);
  fclose(fp);
  E.dirty = 0;
}

static void
vi_init(void)
{
  int rows;
  int cols;

  memset(&E, 0, sizeof(E));
  E.mode = MODE_NORMAL;

  if(vi_get_window_size(&rows, &cols) < 0) {
    rows = 24;
    cols = 80;
  }
  E.screenrows = rows - 2;
  if(E.screenrows < 1)
    E.screenrows = 1;
  if(E.screenrows > VI_MAX_ROWS)
    E.screenrows = VI_MAX_ROWS;
  E.screencols = cols;
  if(E.screencols < 20)
    E.screencols = 20;
  if(E.screencols > VI_MAX_COLS)
    E.screencols = VI_MAX_COLS;
}

static void
vi_usage(void)
{
  dprintf(2, "usage: 6vi [file]\n");
  dprintf(2, "keys: arrows move, i insert, esc normal, :w :q :wq\n");
  exit(1);
}

int
main(int argc, char *argv[])
{
  if(argc > 2)
    vi_usage();

  vi_init();
  vi_enable_raw();

  if(argc == 2)
    vi_open(argv[1]);

  if(E.numrows == 0)
    vi_insert_row(0, "", 0);

  vi_set_status("arrows move | i insert | :w save | :q quit");

  while(!E.quit) {
    vi_refresh_screen();
    vi_process_keypress();
  }

  vi_disable_raw();
  vi_cleanup_terminal();
  return 0;
}
