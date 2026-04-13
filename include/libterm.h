/*
 * libterm.h — lightweight terminal control for interactive auxv6 utilities
 *
 * Provides the minimal subset of ncurses-style terminal operations needed
 * for full-screen tools (top, htop-like monitors, simple TUI editors).  No
 * dynamic allocation; all state is held in a single struct termstate that the
 * caller manages.  Thread-safety is the caller's responsibility.
 *
 * Design goals:
 *   - No dependency on terminfo/termcap; targets ANSI/VT100 terminals only,
 *     which is what the auxv6 console and any reasonable emulator provide.
 *   - All writes go through a caller-supplied output fd (typically stdout/1).
 *   - Cursor positioning uses 1-based ANSI coordinates internally but the
 *     public API uses 0-based (row, col) for C convenience.
 *   - Key input is non-blocking via poll(); the caller chooses the timeout.
 *
 * Typical lifecycle:
 *   struct termstate ts;
 *   term_init(&ts, STDIN_FILENO, STDOUT_FILENO);
 *   term_enter(&ts);          // raw mode, alt screen, hidden cursor
 *   ...draw loop...
 *   term_leave(&ts);          // restore everything
 */

#ifndef AUXV6_LIBTERM_H
#define AUXV6_LIBTERM_H

#include "termios.h"   /* struct termios, struct winsize */

/* -----------------------------------------------------------------------
 * Terminal state
 * --------------------------------------------------------------------- */

struct termstate {
  int  ifd;             /* input file descriptor (usually STDIN_FILENO)  */
  int  ofd;             /* output file descriptor (usually STDOUT_FILENO) */
  int  active;          /* non-zero while in raw/alt-screen mode          */
  int  rows;            /* last known terminal height in rows              */
  int  cols;            /* last known terminal width in columns            */
  struct termios saved; /* termios saved at term_enter(), restored on leave */
};

/* -----------------------------------------------------------------------
 * Lifecycle
 * --------------------------------------------------------------------- */

/* Initialise ts with the given I/O fds.  Does not change terminal state. */
void term_init(struct termstate *ts, int ifd, int ofd);

/* Enter full-screen mode:
 *   - saves current termios
 *   - sets RAW mode (no ECHO, no ICANON, VMIN=1, VTIME=0)
 *   - switches to alternate screen buffer (\033[?1049h)
 *   - hides cursor (\033[?25l)
 *   - queries and stores current window size
 * Returns 0 on success, -1 on error (errno set). */
int  term_enter(struct termstate *ts);

/* Leave full-screen mode (safe to call more than once):
 *   - shows cursor (\033[?25h)
 *   - returns to main screen buffer (\033[?1049l)
 *   - restores saved termios
 * Should be registered with atexit() or a signal handler. */
void term_leave(struct termstate *ts);

/* Refresh the stored row/col dimensions via TIOCGWINSZ.
 * Call this from a SIGWINCH handler. */
void term_update_size(struct termstate *ts);

/* -----------------------------------------------------------------------
 * Output primitives — all writes flush immediately to ts->ofd
 * --------------------------------------------------------------------- */

/* Move cursor to (row, col), both 0-based. */
void term_move(struct termstate *ts, int row, int col);

/* Clear entire screen and home cursor. */
void term_clear(struct termstate *ts);

/* Erase from cursor to end of current line. */
void term_clreol(struct termstate *ts);

/* Erase from cursor to end of screen. */
void term_clreos(struct termstate *ts);

/* Write n bytes from buf to ts->ofd. */
void term_write(struct termstate *ts, const char *buf, int n);

/* Write NUL-terminated string to ts->ofd. */
void term_puts(struct termstate *ts, const char *s);

/* -----------------------------------------------------------------------
 * SGR (Select Graphic Rendition) attribute control
 * --------------------------------------------------------------------- */

/* SGR attribute bits — can be OR'd together in term_attr(). */
#define TERM_RESET   0x0000
#define TERM_BOLD    0x0001
#define TERM_DIM     0x0002
#define TERM_ULINE   0x0004   /* underline  */
#define TERM_BLINK   0x0008
#define TERM_REVERSE 0x0010   /* reverse video */
#define TERM_INVIS   0x0020   /* invisible (conceal) */

/* Standard ANSI foreground colour indices (30–37); use with term_color(). */
#define TERM_BLACK   0
#define TERM_RED     1
#define TERM_GREEN   2
#define TERM_YELLOW  3
#define TERM_BLUE    4
#define TERM_MAGENTA 5
#define TERM_CYAN    6
#define TERM_WHITE   7
#define TERM_DEFAULT 9  /* terminal default colour */

/* Reset all attributes and colours. */
void term_reset_attrs(struct termstate *ts);

/* Set SGR attributes (TERM_BOLD etc. OR'd together).
 * Does not affect colour; use term_color() separately. */
void term_attr(struct termstate *ts, int attrs);

/* Set foreground and background colours (TERM_RED etc.).
 * Pass TERM_DEFAULT (-1) to leave unset. */
void term_color(struct termstate *ts, int fg, int bg);

/* Convenience: bold + specific foreground colour. */
void term_highlight(struct termstate *ts, int fg);

/* -----------------------------------------------------------------------
 * Cursor visibility
 * --------------------------------------------------------------------- */

void term_hide_cursor(struct termstate *ts);
void term_show_cursor(struct termstate *ts);

/* -----------------------------------------------------------------------
 * Input
 * --------------------------------------------------------------------- */

/*
 * Wait up to timeout_ms milliseconds for a keypress on ts->ifd.
 * Returns the byte read (0–255), 0 on timeout, or -1 on error.
 * Escape sequences are NOT decoded; the caller receives the raw byte
 * and must handle multi-byte sequences itself if needed.
 */
int term_poll_key(struct termstate *ts, int timeout_ms);

/* Read one key, blocking indefinitely. */
int term_read_key(struct termstate *ts);

/* -----------------------------------------------------------------------
 * Miscellaneous helpers
 * --------------------------------------------------------------------- */

/* Format an unsigned integer into buf (no NUL terminator).
 * Returns the number of bytes written.  buf must be at least 12 bytes. */
int term_fmt_uint(char *buf, unsigned int v);

/* Format a fixed-point load average value (divisor=2048) as "X.YY" into buf.
 * Returns bytes written.  buf must be at least 8 bytes. */
int term_fmt_lavg(char *buf, unsigned int fp);

#endif /* AUXV6_LIBTERM_H */
