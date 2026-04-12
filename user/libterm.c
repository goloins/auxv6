/*
 * libterm.c — lightweight ANSI terminal control library for auxv6
 *
 * See include/libterm.h for the public API and design rationale.
 *
 * All escape sequences target ANSI/VT100 terminals.  The auxv6 console and
 * all standard terminal emulators understand this subset.
 */

#include "types.h"
#include "auxv6/user.h"    /* syscall wrappers, ioctl, poll, tcgetattr … */
#include "termios.h"       /* struct termios, struct winsize               */
#include "sys/ioctl.h"     /* TIOCGWINSZ                                   */
#include "poll.h"          /* struct pollfd, POLLIN                        */
#include "libterm.h"

/* -----------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------- */

/* Write exactly n bytes to ts->ofd, retrying on short writes. */
static void
ts_write(struct termstate *ts, const char *buf, int n)
{
  int off = 0, r;
  while(off < n){
    r = write(ts->ofd, buf + off, n - off);
    if(r <= 0) break;
    off += r;
  }
}

/* Write a NUL-terminated string. */
static void
ts_puts(struct termstate *ts, const char *s)
{
  int n = 0;
  while(s[n]) n++;
  ts_write(ts, s, n);
}

/*
 * Append the decimal representation of unsigned integer v into buf[*n].
 * buf must have at least 12 bytes of space from the offset *n.
 */
static void
append_uint(char *buf, int *n, unsigned int v)
{
  char tmp[12];
  int d = 0, i;
  do { tmp[d++] = '0' + (v % 10); v /= 10; } while(v);
  for(i = d - 1; i >= 0; i--)
    buf[(*n)++] = tmp[i];
}

/* -----------------------------------------------------------------------
 * Lifecycle
 * --------------------------------------------------------------------- */

void
term_init(struct termstate *ts, int ifd, int ofd)
{
  ts->ifd    = ifd;
  ts->ofd    = ofd;
  ts->active = 0;
  ts->rows   = 24;
  ts->cols   = 80;
}

void
term_update_size(struct termstate *ts)
{
  struct winsize ws;
  if(ioctl(ts->ifd, TIOCGWINSZ, (int)&ws) == 0){
    if(ws.ws_row > 0) ts->rows = (int)ws.ws_row;
    if(ws.ws_col > 0) ts->cols = (int)ws.ws_col;
  }
}

int
term_enter(struct termstate *ts)
{
  struct termios raw;

  if(ts->active)
    return 0;

  if(tcgetattr(ts->ifd, &ts->saved) < 0)
    return -1;

  /* Raw mode: no canonical processing, no echo, no signals. */
  raw           = ts->saved;
  raw.c_iflag  &= ~(uint)(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
  raw.c_oflag  &= ~(uint)(OPOST);
  raw.c_cflag  |=  (uint)(CS8);
  raw.c_lflag  &= ~(uint)(ECHO | ICANON | ISIG | IEXTEN);
  raw.c_cc[VMIN]  = 1;
  raw.c_cc[VTIME] = 0;

  if(tcsetattr(ts->ifd, TCSAFLUSH, &raw) < 0)
    return -1;

  ts->active = 1;
  term_update_size(ts);

  /* Alternate screen + hidden cursor */
  ts_puts(ts, "\033[?1049h\033[?25l");
  return 0;
}

void
term_leave(struct termstate *ts)
{
  if(!ts->active)
    return;

  /* Restore cursor/main screen and leave a clean, canonical shell viewport. */
  ts_puts(ts, "\033[0m\033[?25h\033[?1049l\033[H\033[2J");

  tcsetattr(ts->ifd, TCSAFLUSH, &ts->saved);
  ts->active = 0;
}

/* -----------------------------------------------------------------------
 * Output primitives
 * --------------------------------------------------------------------- */

void
term_write(struct termstate *ts, const char *buf, int n)
{
  ts_write(ts, buf, n);
}

void
term_puts(struct termstate *ts, const char *s)
{
  ts_puts(ts, s);
}

/*
 * Move to 0-based (row, col).  Emits \033[R;CH with 1-based coordinates.
 */
void
term_move(struct termstate *ts, int row, int col)
{
  char buf[24];
  int n = 0;
  buf[n++] = '\033'; buf[n++] = '[';
  append_uint(buf, &n, (unsigned int)(row + 1));
  buf[n++] = ';';
  append_uint(buf, &n, (unsigned int)(col + 1));
  buf[n++] = 'H';
  ts_write(ts, buf, n);
}

void
term_clear(struct termstate *ts)
{
  ts_puts(ts, "\033[2J\033[H");
}

void
term_clreol(struct termstate *ts)
{
  ts_puts(ts, "\033[K");
}

void
term_clreos(struct termstate *ts)
{
  ts_puts(ts, "\033[J");
}

/* -----------------------------------------------------------------------
 * SGR attributes
 * --------------------------------------------------------------------- */

void
term_reset_attrs(struct termstate *ts)
{
  ts_puts(ts, "\033[0m");
}

void
term_attr(struct termstate *ts, int attrs)
{
  char buf[48];
  int n = 0, first = 1;

  buf[n++] = '\033'; buf[n++] = '[';

  if(attrs == TERM_RESET){
    buf[n++] = '0';
  } else {
#define EMIT(code) do {                                       \
      if(!first) buf[n++] = ';';                            \
      buf[n++] = '0' + ((code) / 10);                      \
      buf[n++] = '0' + ((code) % 10);                      \
      first = 0;                                            \
    } while(0)
    if(attrs & TERM_BOLD)    EMIT(1);
    if(attrs & TERM_DIM)     EMIT(2);
    if(attrs & TERM_ULINE)   EMIT(4);
    if(attrs & TERM_BLINK)   EMIT(5);
    if(attrs & TERM_REVERSE) EMIT(7);
    if(attrs & TERM_INVIS)   EMIT(8);
#undef EMIT
  }

  buf[n++] = 'm';
  ts_write(ts, buf, n);
}

void
term_color(struct termstate *ts, int fg, int bg)
{
  char buf[24];
  int n = 0, first = 1;

  buf[n++] = '\033'; buf[n++] = '[';

  if(fg >= 0 && fg <= 9){
    if(!first) buf[n++] = ';';
    first = 0;
    buf[n++] = '3'; buf[n++] = '0' + fg;
  }
  if(bg >= 0 && bg <= 9){
    if(!first) buf[n++] = ';';
    (void)first;
    buf[n++] = '4'; buf[n++] = '0' + bg;
  }
  if(n == 2){ return; }  /* nothing to emit */

  buf[n++] = 'm';
  ts_write(ts, buf, n);
}

void
term_highlight(struct termstate *ts, int fg)
{
  /* ESC[1;3Xm — bold + foreground colour */
  char buf[10];
  int n = 0;
  buf[n++] = '\033'; buf[n++] = '[';
  buf[n++] = '1';    buf[n++] = ';';
  buf[n++] = '3';    buf[n++] = (char)('0' + (fg & 0xf));
  buf[n++] = 'm';
  ts_write(ts, buf, n);
}

/* -----------------------------------------------------------------------
 * Cursor visibility
 * --------------------------------------------------------------------- */

void term_hide_cursor(struct termstate *ts) { ts_puts(ts, "\033[?25l"); }
void term_show_cursor(struct termstate *ts) { ts_puts(ts, "\033[?25h"); }

/* -----------------------------------------------------------------------
 * Input
 * --------------------------------------------------------------------- */

int
term_poll_key(struct termstate *ts, int timeout_ms)
{
  struct pollfd pfd;
  unsigned char c;
  int r;

  pfd.fd     = ts->ifd;
  pfd.events = POLLIN;
  r = poll(&pfd, 1, timeout_ms);
  if(r <= 0)
    return 0;
  if(read(ts->ifd, &c, 1) != 1)
    return -1;
  return (int)c;
}

int
term_read_key(struct termstate *ts)
{
  unsigned char c;
  if(read(ts->ifd, &c, 1) != 1)
    return -1;
  return (int)c;
}

/* -----------------------------------------------------------------------
 * Formatting helpers
 * --------------------------------------------------------------------- */

int
term_fmt_uint(char *buf, unsigned int v)
{
  char tmp[12];
  int n = 0, i;
  do { tmp[n++] = '0' + (v % 10); v /= 10; } while(v);
  for(i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
  return n;
}

int
term_fmt_lavg(char *buf, unsigned int fp)
{
  /* Fixed-point divisor = 2048 (LAVG_FSHIFT = 11) */
  unsigned int whole = fp / 2048;
  unsigned int frac  = (fp % 2048) * 100 / 2048;
  int n = 0;
  n += term_fmt_uint(buf + n, whole);
  buf[n++] = '.';
  if(frac < 10) buf[n++] = '0';
  n += term_fmt_uint(buf + n, frac);
  return n;
}
