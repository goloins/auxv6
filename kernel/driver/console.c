// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "termios.h"
#include "signal.h"
#include "x86.h"
#include "graphics/display.h"
#include "graphics/framebuffer.h"
#include "graphics/font.h"
#include "graphics/render.h"

struct console_tty_state;

static void consputc(int);
static void consputc_ansi(struct console_tty_state *t, int c);
static struct console_tty_state *console_tty_by_index(int tty);

static int panicked = 0;

#define KMSG_RING_SIZE 16384

static char kmsg_ring[KMSG_RING_SIZE];
static uint kmsg_head;
static uint kmsg_size;

static struct {
  struct spinlock lock;
  int locking;
} cons;

static void
kmsg_append_char_locked(int c)
{
  char ch;

  if(c == 0x100)
    return;

  ch = (char)(c & 0xff);
  kmsg_ring[kmsg_head] = ch;
  kmsg_head = (kmsg_head + 1) % KMSG_RING_SIZE;
  if(kmsg_size < KMSG_RING_SIZE)
    kmsg_size++;
}

int
console_kmsg_read(char *dst, int max)
{
  uint start;
  uint i;
  int n;

  if(dst == 0 || max <= 0)
    return 0;

  acquire(&cons.lock);
  if(kmsg_size == 0) {
    release(&cons.lock);
    return 0;
  }

  n = (max < (int)kmsg_size) ? max : (int)kmsg_size;
  start = (kmsg_head + KMSG_RING_SIZE - kmsg_size) % KMSG_RING_SIZE;
  start = (start + (kmsg_size - n)) % KMSG_RING_SIZE;

  for(i = 0; i < (uint)n; i++)
    dst[i] = kmsg_ring[(start + i) % KMSG_RING_SIZE];

  release(&cons.lock);
  return n;
}

struct console_input_state {
  char buf[128];
  uint r;
  uint w;
  uint e;
};

struct console_ansi_state {
  int state;
  int params[8];
  int nparams;
  int cur_param;
  int question;
  int greater;
  int dollar;
  int bang;
  uchar attr;
  int scroll_top;
  int scroll_bot;
  int saved_cursor;
  uchar saved_attr;
  int g0_charset;
  int g1_charset;
  int gl_charset;
  int cursor_keys_app;
  int origin_mode;
  int insert_mode;
  int newline_mode;
  int wraparound;
  int reverse_video;
  int underline;
  int italic;
  int strikethrough;
  int dim;
  int cursor_blink;
  int cursor_visible;
  int mouse_x10;
  int mouse_x11;
  int mouse_sgr;
  int mouse_urxvt;
  int mouse_highlight;
  int focus_event;
  int alt_scroll;
  int meta_eightbit;
  int backarrow_key;
  int cursor_save_mode;
  int keypad_app;
  int alt_keypad;
  int keyboard_select;
  int bracketed_paste;
  int last_glyph;
  uint utf8_accum;
  int utf8_need;
  uint utf8_min;
  int alt_active;
  int alt_saved_cursor;
  uchar alt_saved_attr;
  ushort alt_buf[25 * 80];
};

struct console_tty_state {
  int fg_pgid;
  struct termios termios;
  struct termios pending_termios;
  int pending_termios_valid;
  int pending_termios_action;
  int output_busy;
  struct winsize winsize;
  struct console_input_state input;
  struct console_ansi_state ansi;
  ushort screen[25 * 80];
  int cursor;
};

#define CONSOLE_NTTY 1

/* Default termios: POSIX-sane initial state */
static struct console_tty_state console_tty_default = {
  .fg_pgid = 1,
  .termios = {
    .c_iflag = ICRNL,
    .c_oflag = OPOST | ONLCR,
    .c_cflag = CS8 | CREAD | CLOCAL,
    .c_lflag = ECHO | ICANON | ISIG | ECHOE | IEXTEN,
    .c_cc = {
      [VINTR]  = 3,
      [VQUIT]  = 28,
      [VERASE] = 127,
      [VKILL]  = 21,
      [VEOF]   = 4,
      [VTIME]  = 0,
      [VMIN]   = 1,
      [VSWTC]  = 0,
      [VSTART] = 17,
      [VSTOP]  = 19,
      [VSUSP]  = 26,
      [VEOL]   = 0,
    },
  },
  .winsize = { .ws_row = 24, .ws_col = 80 },
  .ansi = {
    .state = 0,
    .attr = 0x07,
    .scroll_top = 0,
    .scroll_bot = 23,
    .saved_attr = 0x07,
    .g0_charset = 0,
    .g1_charset = 0,
    .gl_charset = 0,
    .cursor_keys_app = 0,
    .origin_mode = 0,
    .insert_mode = 0,
    .newline_mode = 0,
    .wraparound = 1,
    .reverse_video = 0,
    .underline = 0,
    .italic = 0,
    .strikethrough = 0,
    .dim = 0,
    .cursor_blink = 1,
    .cursor_visible = 1,
    .mouse_x10 = 0,
    .mouse_x11 = 0,
    .mouse_sgr = 0,
    .mouse_urxvt = 0,
    .mouse_highlight = 0,
    .focus_event = 0,
    .alt_scroll = 0,
    .meta_eightbit = 0,
    .backarrow_key = 0,
    .cursor_save_mode = 0,
    .keypad_app = 0,
    .alt_keypad = 0,
    .keyboard_select = 0,
    .bracketed_paste = 0,
    .last_glyph = -1,
    .alt_saved_attr = 0x07,
  },
};

static struct console_tty_state cttys[CONSOLE_NTTY];
static int console_active_tty = 0;

static ushort *cga_hw = (ushort*)P2V(0xb8000);
static ushort *crt = (ushort*)P2V(0xb8000);
static int cga_render_offscreen = 0;

static struct display_device *console_gfx_dev;
static struct framebuffer *console_gfx_fb;
static struct render_context *console_gfx_ctx;
static struct vt_surface *console_gfx_vts;
static int console_gfx_announced;
static int console_gfx_warned;
static int console_logo_enabled = 1;
static uint console_gfx_stat_sync_calls;
static uint console_gfx_stat_cells_changed;
static uint console_gfx_stat_cells_rendered;
static uint console_gfx_stat_flush_calls;
static uint console_gfx_stat_flush_pixels;

int
console_logo_get_enabled(void)
{
  return console_logo_enabled ? 1 : 0;
}

int
console_logo_set_enabled(int enabled)
{
  console_logo_enabled = enabled ? 1 : 0;
  return 0;
}

uint
console_gfx_stats_sync_calls(void)
{
  return console_gfx_stat_sync_calls;
}

uint
console_gfx_stats_cells_changed(void)
{
  return console_gfx_stat_cells_changed;
}

uint
console_gfx_stats_cells_rendered(void)
{
  return console_gfx_stat_cells_rendered;
}

uint
console_gfx_stats_flush_calls(void)
{
  return console_gfx_stat_flush_calls;
}

uint
console_gfx_stats_flush_pixels(void)
{
  return console_gfx_stat_flush_pixels;
}

static void
console_stamp_logo_textmode_locked(struct console_tty_state *t)
{
  static const char text[] = "A/UXV6";
  static const uchar attr[6] = {
    0x0A, /* light green */
    0x0E, /* yellow */
    0x06, /* brown/orange-ish */
    0x0C, /* light red */
    0x0D, /* light magenta */
    0x09, /* light blue */
  };
  int col0;
  int i;

  if(!t)
    return;
  if(!console_logo_enabled)
    return;

  col0 = 80 - 6;
  for(i = 0; i < 6; i++)
    t->screen[col0 + i] = (ushort)(text[i] | ((ushort)attr[i] << 8));

  if(t == console_tty_by_index(console_active_tty)) {
    for(i = 0; i < 6; i++)
      cga_hw[col0 + i] = t->screen[col0 + i];
  }
}

static void
console_gfx_draw_logo_char(struct framebuffer *fb, int x, int y, int scale,
                           uint color, char ch)
{
  int row;
  int col;
  uchar rows[7];

  memset(rows, 0, sizeof(rows));
  switch(ch) {
  case 'A':
    rows[0] = 0x0E; rows[1] = 0x11; rows[2] = 0x11; rows[3] = 0x1F;
    rows[4] = 0x11; rows[5] = 0x11; rows[6] = 0x11;
    break;
  case '/':
    rows[0] = 0x01; rows[1] = 0x02; rows[2] = 0x04; rows[3] = 0x08;
    rows[4] = 0x10; rows[5] = 0x00; rows[6] = 0x00;
    break;
  case 'U':
    rows[0] = 0x11; rows[1] = 0x11; rows[2] = 0x11; rows[3] = 0x11;
    rows[4] = 0x11; rows[5] = 0x11; rows[6] = 0x0E;
    break;
  case 'X':
    rows[0] = 0x11; rows[1] = 0x11; rows[2] = 0x0A; rows[3] = 0x04;
    rows[4] = 0x0A; rows[5] = 0x11; rows[6] = 0x11;
    break;
  case 'V':
    rows[0] = 0x11; rows[1] = 0x11; rows[2] = 0x11; rows[3] = 0x11;
    rows[4] = 0x11; rows[5] = 0x0A; rows[6] = 0x04;
    break;
  case '6':
    rows[0] = 0x0E; rows[1] = 0x10; rows[2] = 0x10; rows[3] = 0x1E;
    rows[4] = 0x11; rows[5] = 0x11; rows[6] = 0x0E;
    break;
  default:
    return;
  }

  for(row = 0; row < 7; row++) {
    for(col = 0; col < 5; col++) {
      if(rows[row] & (1 << (4 - col)))
        fb_fill_rect(fb, x + col * scale, y + row * scale, scale, scale, color);
    }
  }
}

static void
console_gfx_draw_logo_locked(void)
{
  static const char text[] = "A/UXV6";
  static const uint rainbow[6] = {
    0x002DBE60, /* green */
    0x00F7D154, /* yellow */
    0x00F29F3F, /* orange */
    0x00E24A3B, /* red */
    0x008E56D9, /* violet */
    0x003B82F6, /* blue */
  };
  int i;
  int scale;
  int char_w;
  int char_h;
  int spacing;
  int total_w;
  int x;
  int y;

  if(!console_gfx_fb)
    return;
  if(!console_logo_enabled)
    return;

  scale = 2;
  char_w = 5 * scale;
  char_h = 7 * scale;
  spacing = scale;
  total_w = 6 * char_w + 5 * spacing;
  x = (int)console_gfx_fb->width - total_w - 8;
  y = 6;
  if(x < 0)
    x = 0;

  fb_fill_rect(console_gfx_fb, x - 4, y - 3, total_w + 8, char_h + 6, 0x00000000);
  for(i = 0; i < 6; i++)
    console_gfx_draw_logo_char(console_gfx_fb,
                               x + i * (char_w + spacing),
                               y,
                               scale,
                               rainbow[i],
                               text[i]);
}

static int
console_gfx_ensure_locked(void)
{
  struct render_context tmp;
  const char *msg;
  const char *p;

  if(console_gfx_vts)
    return 1;

  console_gfx_dev = display_get_primary();
  if(!console_gfx_dev) {
    if(!console_gfx_warned) {
      msg = "console: gfx mirror unavailable (no display device)\n";
      for(p = msg; *p; p++)
        uartputc(*p);
      console_gfx_warned = 1;
    }
    return 0;
  }

  if(!console_gfx_dev->ops) {
    if(!console_gfx_warned) {
      msg = "console: gfx mirror unavailable (display ops not implemented)\n";
      for(p = msg; *p; p++)
        uartputc(*p);
      console_gfx_warned = 1;
    }
    return 0;
  }

  if(!console_gfx_fb) {
    console_gfx_fb = display_create_framebuffer(console_gfx_dev, 640, 400, PIXFMT_XRGB8888);
    if(!console_gfx_fb)
      return 0;
  }

  if(console_gfx_dev->num_crtcs > 0)
    display_set_scanout(console_gfx_dev, &console_gfx_dev->crtcs[0], console_gfx_fb);

  if(!console_gfx_ctx)
    console_gfx_ctx = render_context_create(console_gfx_fb, font_builtin_default());
  if(!console_gfx_ctx)
    return 0;

  memset(&tmp, 0, sizeof(tmp));
  tmp = *console_gfx_ctx;
  console_gfx_vts = vt_surface_create(80, 25, &tmp);
  if(!console_gfx_vts)
    return 0;

  console_gfx_vts->fb_x = 0;
  console_gfx_vts->fb_y = 0;

  if(!console_gfx_announced) {
    const char *msg = "console: gfx mirror enabled\n";
    const char *p;
    for(p = msg; *p; p++)
      uartputc(*p);
    console_gfx_announced = 1;
  }

  return 1;
}

static void
console_gfx_sync_from_tty_locked(struct console_tty_state *t)
{
  int i;
  int changed;
  int changed_cells;
  int old_cursor_x;
  int old_cursor_y;
  int new_cursor_x;
  int new_cursor_y;
  int rendered;
  struct dirty_rect rect;
  uint area;

  if(!t)
    return;
  if(!console_gfx_ensure_locked())
    return;

  console_gfx_stat_sync_calls++;

  acquire(&console_gfx_vts->lock);
  if(!console_gfx_vts->cells) {
    release(&console_gfx_vts->lock);
    return;
  }

  changed = 0;
  changed_cells = 0;
  old_cursor_x = console_gfx_vts->cursor_x;
  old_cursor_y = console_gfx_vts->cursor_y;

  for(i = 0; i < 25 * 80; i++) {
    ushort s = t->screen[i];
    struct text_cell tc;
    struct text_cell *dst;
    tc.codepoint = (uint)(s & 0x00FF);
    tc.attr = 0;
    tc.fg_color = (uchar)((s >> 8) & 0x0F);
    tc.bg_color = (uchar)(((s >> 12) & 0x0F));
    tc.width = 1;

    dst = &console_gfx_vts->cells[i];
    if(dst->codepoint != tc.codepoint ||
       dst->attr != tc.attr ||
       dst->fg_color != tc.fg_color ||
       dst->bg_color != tc.bg_color ||
       dst->width != tc.width) {
      *dst = tc;
      if(console_gfx_vts->dirty)
        console_gfx_vts->dirty[i] = 1;
      changed = 1;
      changed_cells++;
    }
  }

  new_cursor_x = t->cursor % 80;
  new_cursor_y = t->cursor / 80;
  if(new_cursor_x != old_cursor_x || new_cursor_y != old_cursor_y) {
    if(old_cursor_x >= 0 && old_cursor_x < (int)console_gfx_vts->width &&
       old_cursor_y >= 0 && old_cursor_y < (int)console_gfx_vts->height &&
       console_gfx_vts->dirty)
      console_gfx_vts->dirty[old_cursor_y * (int)console_gfx_vts->width + old_cursor_x] = 1;
    if(new_cursor_x >= 0 && new_cursor_x < (int)console_gfx_vts->width &&
       new_cursor_y >= 0 && new_cursor_y < (int)console_gfx_vts->height &&
       console_gfx_vts->dirty)
      console_gfx_vts->dirty[new_cursor_y * (int)console_gfx_vts->width + new_cursor_x] = 1;
    changed = 1;
  }

  console_gfx_vts->cursor_x = new_cursor_x;
  console_gfx_vts->cursor_y = new_cursor_y;
  if(changed)
    console_gfx_vts->any_dirty = 1;
  release(&console_gfx_vts->lock);

  if(!changed)
    return;

  console_gfx_stat_cells_changed += (uint)changed_cells;

  rendered = vt_render_dirty(console_gfx_vts);
  if(rendered > 0)
    console_gfx_stat_cells_rendered += (uint)rendered;
  vt_render_cursor(console_gfx_vts);
  console_gfx_draw_logo_locked();

  area = 0;
  if(fb_is_dirty(console_gfx_fb)) {
    fb_get_dirty_rect(console_gfx_fb, &rect);
    if(rect.right >= rect.left && rect.bottom >= rect.top)
      area = (uint)(rect.right - rect.left + 1) * (uint)(rect.bottom - rect.top + 1);
  }

  display_flush(console_gfx_dev, console_gfx_fb);
  console_gfx_stat_flush_calls++;
  console_gfx_stat_flush_pixels += area;
}

static int
console_tty_index_clamp(int tty)
{
  if(tty < 0 || tty >= CONSOLE_NTTY)
    return 0;
  return tty;
}

static struct console_tty_state *
console_tty_by_index(int tty)
{
  return &cttys[console_tty_index_clamp(tty)];
}

static int
console_current_tty_index(void)
{
  struct proc *p;

  p = myproc();
  if(p && p->tty >= 0 && p->tty < CONSOLE_NTTY)
    return p->tty;
  return console_active_tty;
}

static struct console_tty_state *
console_current_tty(void)
{
  return console_tty_by_index(console_current_tty_index());
}

static int
console_tty_index_from_inode(struct inode *ip)
{
  int m;

  if(ip == 0)
    return console_current_tty_index();

  m = ip->minor;
  if(m <= 0)
    return console_current_tty_index();
  if(m > CONSOLE_NTTY)
    return console_current_tty_index();
  return m - 1;
}

/* --------------------------------------------------------------------------
 * Kernel debug output helpers
 * -------------------------------------------------------------------------- */

static void
cprintint_w(uint x, int base, int neg, int width, int zero, int left)
{
  static char digits[] = "0123456789abcdef";
  char buf[16];
  int i;
  int pad;

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while((x /= base) != 0);
  if(neg)
    buf[i++] = '-';

  pad = width - i;
  if(!left)
    while(pad-- > 0) consputc(zero ? '0' : ' ');
  while(--i >= 0)
    consputc(buf[i]);
  if(left)
    while(pad-- > 0) consputc(' ');
}

void
cprintf(char *fmt, ...)
{
  int i, c, locking;
  uint *argp;
  char *s;
  int width, prec, have_prec, left, zero;

  locking = cons.locking;
  if(locking)
    acquire(&cons.lock);

  if(fmt == 0)
    panic("null fmt");

  argp = (uint*)(void*)(&fmt + 1);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++) {
    if(c != '%') { consputc(c); continue; }

    /* Flags */
    left = 0; zero = 0;
    while(1) {
      c = fmt[++i] & 0xff;
      if(c == '-')      { left = 1; }
      else if(c == '0') { zero = 1; }
      else break;
    }
    if(left) zero = 0;

    /* Width */
    width = 0;
    while(c >= '0' && c <= '9') {
      width = width * 10 + (c - '0');
      c = fmt[++i] & 0xff;
    }

    /* Precision */
    have_prec = 0; prec = 0;
    if(c == '.') {
      have_prec = 1;
      c = fmt[++i] & 0xff;
      if(c == '*') {
        prec = (int)*argp++;
        c = fmt[++i] & 0xff;
      } else {
        while(c >= '0' && c <= '9') {
          prec = prec * 10 + (c - '0');
          c = fmt[++i] & 0xff;
        }
      }
      zero = 0;
    }

    /* Length modifier (no-op on 32-bit) */
    if(c == 'l')
      c = fmt[++i] & 0xff;

    if(c == 0) break;
    switch(c) {
    case 'd': case 'i': {
      int v = (int)*argp++;
      uint x = (v < 0) ? (uint)(-v) : (uint)v;
      cprintint_w(x, 10, v < 0, width, zero, left);
      break;
    }
    case 'u':
      cprintint_w(*argp++, 10, 0, width, zero, left);
      break;
    case 'x': case 'X': case 'p':
      cprintint_w(*argp++, 16, 0, width, zero, left);
      break;
    case 'o':
      cprintint_w(*argp++, 8, 0, width, zero, left);
      break;
    case 'c': {
      char ch = (char)*argp++;
      int pad = width - 1;
      if(!left) while(pad-- > 0) consputc(' ');
      consputc(ch);
      if(left)  while(pad-- > 0) consputc(' ');
      break;
    }
    case 's': {
      int n, pad;
      if((s = (char*)*argp++) == 0) s = "(null)";
      n = 0;
      while(s[n] && (!have_prec || n < prec)) n++;
      pad = width - n;
      if(!left) while(pad-- > 0) consputc(' ');
      for(int j = 0; j < n; j++) consputc(s[j]);
      if(left)  while(pad-- > 0) consputc(' ');
      break;
    }
    case '%': consputc('%'); break;
    default:  consputc('%'); consputc(c); break;
    }
  }

  if(locking)
    release(&cons.lock);
}

void
panic(char *s)
{
  int i;
  uint pcs[10];

  cli();
  cons.locking = 0;
  cprintf("lapicid %d: panic: ", lapicid());
  cprintf(s);
  cprintf("\n");
  getcallerpcs(&s, pcs);
  for(i = 0; i < 10; i++)
    cprintf(" %p", pcs[i]);
  panicked = 1;
  for(;;)
    ;
}

/* --------------------------------------------------------------------------
 * CGA helpers
 * -------------------------------------------------------------------------- */

#define BACKSPACE 0x100
#define CRTPORT   0x3d4

static int cga_cursor = 0;

static void
console_flush_tty_locked(struct console_tty_state *t)
{
  if(t != console_tty_by_index(console_active_tty))
    return;
  console_stamp_logo_textmode_locked(t);
  memmove(cga_hw, t->screen, sizeof(t->screen));
  cga_render_offscreen = 0;
  cga_cursor = t->cursor;
  outb(CRTPORT, 14); outb(CRTPORT+1, t->cursor >> 8);
  outb(CRTPORT, 15); outb(CRTPORT+1, t->cursor);
  console_gfx_sync_from_tty_locked(t);
}

static void
cga_write_cursor(int pos)
{
  if(pos < 0)
    pos = 0;
  if(pos >= 25*80)
    pos = 25*80 - 1;
  cga_cursor = pos;
  if(cga_render_offscreen)
    return;
  outb(CRTPORT, 14); outb(CRTPORT+1, pos >> 8);
  outb(CRTPORT, 15); outb(CRTPORT+1, pos);
}

static int
cga_read_cursor(void)
{
  int pos;
  outb(CRTPORT, 14); pos  = inb(CRTPORT+1) << 8;
  outb(CRTPORT, 15); pos |= inb(CRTPORT+1);
  cga_cursor = pos;
  return pos;
}

static void
cga_fill(int from, int to, uchar a)
{
  int i;
  ushort blank = (ushort)(' ' | ((ushort)a << 8));
  if(from < 0)   from = 0;
  if(to > 25*80) to   = 25*80;
  for(i = from; i < to; i++)
    crt[i] = blank;
}

static void
cga_scroll_up(int top, int bot, int n, uchar a)
{
  int lines = bot - top + 1;
  if(n <= 0) return;
  if(n >= lines) { cga_fill(top*80, (bot+1)*80, a); return; }
  memmove(crt + top*80, crt + (top+n)*80, (lines-n)*80*sizeof(ushort));
  cga_fill((bot+1-n)*80, (bot+1)*80, a);
}

static void
cga_scroll_down(int top, int bot, int n, uchar a)
{
  int lines = bot - top + 1;
  int i;
  if(n <= 0) return;
  if(n >= lines) { cga_fill(top*80, (bot+1)*80, a); return; }
  for(i = bot; i >= top + n; i--)
    memmove(crt + i*80, crt + (i-n)*80, 80*sizeof(ushort));
  cga_fill(top*80, (top+n)*80, a);
}

/* --------------------------------------------------------------------------
 * Legacy kernel output path (no ANSI) - for cprintf/panic
 * -------------------------------------------------------------------------- */

static void
cgaputc_kernel(int c)
{
  int pos = cga_cursor;

  if(c == '\n')
    pos += 80 - pos % 80;
  else if(c == BACKSPACE) {
    if(pos > 0) --pos;
  } else
    crt[pos++] = (c & 0xff) | 0x0700;

  if(pos < 0 || pos > 25*80) pos = 0;

  if((pos / 80) >= 24) {
    memmove(crt, crt + 80, sizeof(crt[0]) * 23 * 80);
    pos -= 80;
    memset(crt + pos, 0, sizeof(crt[0]) * (24*80 - pos));
  }

  cga_write_cursor(pos);
  crt[pos] = ' ' | 0x0700;
}

void
consputc(int c)
{
  struct console_tty_state *t;

  if(panicked) { cli(); for(;;); }
  if(c == BACKSPACE) {
    uartputc('\b'); uartputc(' '); uartputc('\b');
  } else
    uartputc(c);
  kmsg_append_char_locked(c);
  cgaputc_kernel(c);

  // Kernel-path output writes directly to hardware; mirror it into the
  // active virtual tty backing store so later tty rendering does not
  // rewind to stale cursor/screen state.
  t = console_tty_by_index(console_active_tty);
  memmove(t->screen, cga_hw, sizeof(t->screen));
  console_stamp_logo_textmode_locked(t);
  t->cursor = cga_cursor;
  console_gfx_sync_from_tty_locked(t);
}

/* --------------------------------------------------------------------------
 * ANSI/VT parser state
 * -------------------------------------------------------------------------- */

#define ANSI_NORMAL  0
#define ANSI_ESC     1
#define ANSI_CSI     2
#define ANSI_OSC     3
#define ANSI_ESC_G0  4
#define ANSI_ESC_G1  5
#define ANSI_OSC_ESC 6

#define ANSI_CS_ASCII 0
#define ANSI_CS_DEC   1

static void
ansi_save_cursor(struct console_tty_state *t)
{
  t->ansi.saved_cursor = cga_cursor;
  t->ansi.saved_attr = t->ansi.attr;
}

static void
ansi_restore_cursor(struct console_tty_state *t)
{
  cga_write_cursor(t->ansi.saved_cursor);
  t->ansi.attr = t->ansi.saved_attr;
}

static void
ansi_enter_alt_screen(struct console_tty_state *t)
{
  if(t->ansi.alt_active)
    return;
  memmove(t->ansi.alt_buf, crt, sizeof(t->ansi.alt_buf));
  t->ansi.alt_saved_cursor = cga_cursor;
  t->ansi.alt_saved_attr = t->ansi.attr;
  t->ansi.alt_active = 1;
  cga_fill(0, 25 * 80, t->ansi.attr);
  cga_write_cursor(0);
}

static void
ansi_leave_alt_screen(struct console_tty_state *t)
{
  if(!t->ansi.alt_active)
    return;
  memmove(crt, t->ansi.alt_buf, sizeof(t->ansi.alt_buf));
  cga_write_cursor(t->ansi.alt_saved_cursor);
  t->ansi.attr = t->ansi.alt_saved_attr;
  t->ansi.alt_active = 0;
}

static void
ansi_apply_private_mode(struct console_tty_state *t, int mode, int set)
{
  if(mode == 1) {
    t->ansi.cursor_keys_app = set ? 1 : 0;
  } else if(mode == 6) {
    t->ansi.origin_mode = set ? 1 : 0;
    if(set)
      cga_write_cursor(t->ansi.scroll_top * 80);
    else
      cga_write_cursor(0);
  } else if(mode == 5) {
    t->ansi.reverse_video = set ? 1 : 0;
  } else if(mode == 12) {
    t->ansi.cursor_blink = set ? 1 : 0;
  } else if(mode == 7) {
    t->ansi.wraparound = set ? 1 : 0;
  } else if(mode == 25) {
    t->ansi.cursor_visible = set ? 1 : 0;
  } else if(mode == 1000) {
    t->ansi.mouse_x10 = set ? 1 : 0;
  } else if(mode == 1001) {
    /* highlight mode - treat same as x10 */
    t->ansi.mouse_highlight = set ? 1 : 0;
  } else if(mode == 1002) {
    /* button event tracking */
    t->ansi.mouse_x11 = set ? 1 : 0;
  } else if(mode == 1003) {
    /* any event tracking */
    t->ansi.mouse_x11 = set ? 1 : 0;
  } else if(mode == 1004) {
    /* focus event */
    t->ansi.focus_event = set ? 1 : 0;
  } else if(mode == 1005) {
    /* UTF-8 mouse encoding */
    t->ansi.mouse_sgr = set ? 1 : 0;
  } else if(mode == 1006) {
    /* SGR mouse encoding */
    t->ansi.mouse_sgr = set ? 1 : 0;
  } else if(mode == 1007) {
    /* alt scroll */
    t->ansi.alt_scroll = set ? 1 : 0;
  } else if(mode == 1015) {
    /* URXVT mouse encoding */
    t->ansi.mouse_urxvt = set ? 1 : 0;
  } else if(mode == 1034) {
    /* meta escape mode */
    t->ansi.meta_eightbit = set ? 0 : 1;
  } else if(mode == 1035) {
    /* numeric keypad mode */
    t->ansi.keypad_app = set ? 1 : 0;
  } else if(mode == 1036) {
    /* alt sends escape */
    t->ansi.meta_eightbit = set ? 1 : 0;
  } else if(mode == 1039) {
    /* alt sends escape (alternative) */
    t->ansi.meta_eightbit = set ? 1 : 0;
  } else if(mode == 2004) {
    /* bracketed paste mode */
    t->ansi.bracketed_paste = set ? 1 : 0;
  } else if(mode == 9) {
    /* X11 mouse - treat as x10 */
    t->ansi.mouse_x10 = set ? 1 : 0;
  } else if(mode == 67) {
    /* backspace key mode */
    t->ansi.backarrow_key = set ? 1 : 0;
  } else if(mode == 66) {
    /* application keypad mode */
    t->ansi.keypad_app = set ? 1 : 0;
  } else if(mode == 69) {
    /* alt keypad */
    t->ansi.alt_keypad = set ? 1 : 0;
  } else if(mode == 95) {
    /* shift + F3 ... */
    t->ansi.keyboard_select = set ? 1 : 0;
  } else if(mode == 1048) {
    t->ansi.cursor_save_mode = set ? 1 : 0;
    if(set)
      ansi_save_cursor(t);
    else
      ansi_restore_cursor(t);
  } else if(mode == 47 || mode == 1047 || mode == 1049) {
    if(set) {
      ansi_save_cursor(t);
      ansi_enter_alt_screen(t);
    } else {
      ansi_leave_alt_screen(t);
      ansi_restore_cursor(t);
    }
  }
}


static void
ansi_apply_mode(struct console_tty_state *t, int mode, int set)
{
  if(mode == 4)
    t->ansi.insert_mode = set ? 1 : 0;
  else if(mode == 20)
    t->ansi.newline_mode = set ? 1 : 0;
}

static int
ansi_mode_state(struct console_tty_state *t, int mode)
{
  if(mode == 4)
    return t->ansi.insert_mode ? 1 : 2;
  if(mode == 20)
    return t->ansi.newline_mode ? 1 : 2;
  return 0;
}

static int
ansi_private_mode_state(struct console_tty_state *t, int mode)
{
  if(mode == 1)
    return t->ansi.cursor_keys_app ? 1 : 2;
  if(mode == 6)
    return t->ansi.origin_mode ? 1 : 2;
  if(mode == 5)
    return t->ansi.reverse_video ? 1 : 2;
  if(mode == 12)
    return t->ansi.cursor_blink ? 1 : 2;
  if(mode == 7)
    return t->ansi.wraparound ? 1 : 2;
  if(mode == 25)
    return t->ansi.cursor_visible ? 1 : 2;
  if(mode == 1000)
    return t->ansi.mouse_x10 ? 1 : 2;
  if(mode == 1001)
    return t->ansi.mouse_highlight ? 1 : 2;
  if(mode == 1002)
    return t->ansi.mouse_x11 ? 1 : 2;
  if(mode == 1003)
    return t->ansi.mouse_x11 ? 1 : 2;
  if(mode == 1004)
    return t->ansi.focus_event ? 1 : 2;
  if(mode == 1005)
    return t->ansi.mouse_sgr ? 1 : 2;
  if(mode == 1006)
    return t->ansi.mouse_sgr ? 1 : 2;
  if(mode == 1007)
    return t->ansi.alt_scroll ? 1 : 2;
  if(mode == 1015)
    return t->ansi.mouse_urxvt ? 1 : 2;
  if(mode == 1034)
    return t->ansi.meta_eightbit ? 2 : 1;
  if(mode == 1035)
    return t->ansi.keypad_app ? 1 : 2;
  if(mode == 1036)
    return t->ansi.meta_eightbit ? 1 : 2;
  if(mode == 1039)
    return t->ansi.meta_eightbit ? 1 : 2;
  if(mode == 2004)
    return t->ansi.bracketed_paste ? 1 : 2;
  if(mode == 9)
    return t->ansi.mouse_x10 ? 1 : 2;
  if(mode == 67)
    return t->ansi.backarrow_key ? 1 : 2;
  if(mode == 66)
    return t->ansi.keypad_app ? 1 : 2;
  if(mode == 1048)
    return t->ansi.cursor_save_mode ? 1 : 2;
  if(mode == 69)
    return t->ansi.alt_keypad ? 1 : 2;
  if(mode == 95)
    return t->ansi.keyboard_select ? 1 : 2;
  if(mode == 47 || mode == 1047 || mode == 1049)
    return t->ansi.alt_active ? 1 : 2;
  return 0;
}

static void
ansi_soft_reset(struct console_tty_state *t)
{
  t->ansi.attr = 0x07;
  t->ansi.scroll_top = 0;
  t->ansi.scroll_bot = 23;
  t->ansi.g0_charset = ANSI_CS_ASCII;
  t->ansi.g1_charset = ANSI_CS_ASCII;
  t->ansi.gl_charset = 0;
  t->ansi.cursor_keys_app = 0;
  t->ansi.origin_mode = 0;
  t->ansi.insert_mode = 0;
  t->ansi.newline_mode = 0;
  t->ansi.wraparound = 1;
  t->ansi.reverse_video = 0;
  t->ansi.underline = 0;
  t->ansi.italic = 0;
  t->ansi.strikethrough = 0;
  t->ansi.dim = 0;
  t->ansi.cursor_blink = 1;
  t->ansi.cursor_visible = 1;
  t->ansi.mouse_x10 = 0;
  t->ansi.mouse_x11 = 0;
  t->ansi.mouse_sgr = 0;
  t->ansi.mouse_urxvt = 0;
  t->ansi.mouse_highlight = 0;
  t->ansi.focus_event = 0;
  t->ansi.alt_scroll = 0;
  t->ansi.meta_eightbit = 0;
  t->ansi.backarrow_key = 0;
  t->ansi.cursor_save_mode = 0;
  t->ansi.keypad_app = 0;
  t->ansi.alt_keypad = 0;
  t->ansi.keyboard_select = 0;
  t->ansi.bracketed_paste = 0;
  t->ansi.utf8_need = 0;
  t->ansi.question = 0;
  t->ansi.greater = 0;
  t->ansi.dollar = 0;
  t->ansi.bang = 0;
}

static void
console_queue_input_byte_locked(struct console_tty_state *t, char ch)
{
  uint cap;

  cap = (uint)sizeof(t->input.buf);
  if(t->input.e - t->input.r >= cap)
    return;
  t->input.buf[t->input.e++ % cap] = ch;
}

static void
console_queue_input_uint_locked(struct console_tty_state *t, int v)
{
  char tmp[12];
  int n;

  if(v == 0) {
    console_queue_input_byte_locked(t, '0');
    return;
  }
  if(v < 0)
    v = 1;
  n = 0;
  while(v > 0 && n < (int)sizeof(tmp)) {
    tmp[n++] = (char)('0' + (v % 10));
    v /= 10;
  }
  while(n > 0)
    console_queue_input_byte_locked(t, tmp[--n]);
}

static void
console_queue_input_cstr_locked(struct console_tty_state *t, const char *s)
{
  if(s == 0)
    return;
  while(*s)
    console_queue_input_byte_locked(t, *s++);
}

static void
console_queue_dsr_reply_locked(struct console_tty_state *t, int dec_private, int mode)
{
  int row;
  int col;

  if(mode == 5) {
    console_queue_input_byte_locked(t, '\033');
    console_queue_input_byte_locked(t, '[');
    if(dec_private)
      console_queue_input_byte_locked(t, '?');
    console_queue_input_byte_locked(t, '0');
    console_queue_input_byte_locked(t, 'n');
  } else if(mode == 6) {
    row = (cga_cursor / 80) + 1;
    col = (cga_cursor % 80) + 1;
    console_queue_input_byte_locked(t, '\033');
    console_queue_input_byte_locked(t, '[');
    if(dec_private)
      console_queue_input_byte_locked(t, '?');
    console_queue_input_uint_locked(t, row);
    console_queue_input_byte_locked(t, ';');
    console_queue_input_uint_locked(t, col);
    console_queue_input_byte_locked(t, 'R');
  } else if(dec_private && mode == 15) {
    /* DEC printer status report: no printer attached. */
    console_queue_input_byte_locked(t, '\033');
    console_queue_input_cstr_locked(t, "[?13n");
  } else if(dec_private && mode == 25) {
    /* DEC user-defined key status: unlocked. */
    console_queue_input_byte_locked(t, '\033');
    console_queue_input_cstr_locked(t, "[?21n");
  } else {
    return;
  }

  t->input.w = t->input.e;
  wakeup(&t->input.r);
}

static void
console_queue_da_reply_locked(struct console_tty_state *t, int dec_private, int secondary)
{
  (void)dec_private;

  console_queue_input_byte_locked(t, '\033');
  console_queue_input_byte_locked(t, '[');
  if(secondary) {
    console_queue_input_byte_locked(t, '>');
    console_queue_input_cstr_locked(t, "0;0;0c");
  } else {
    console_queue_input_byte_locked(t, '?');
    console_queue_input_cstr_locked(t, "1;0c");
  }
  t->input.w = t->input.e;
  wakeup(&t->input.r);
}

static void
console_queue_mode_reply_locked(struct console_tty_state *t, int dec_private, int mode)
{
  int state;

  if(dec_private)
    state = ansi_private_mode_state(t, mode);
  else
    state = ansi_mode_state(t, mode);

  console_queue_input_byte_locked(t, '\033');
  console_queue_input_byte_locked(t, '[');
  if(dec_private)
    console_queue_input_byte_locked(t, '?');
  console_queue_input_uint_locked(t, mode);
  console_queue_input_byte_locked(t, ';');
  console_queue_input_uint_locked(t, state);
  console_queue_input_cstr_locked(t, "$y");

  t->input.w = t->input.e;
  wakeup(&t->input.r);
}

static void
console_queue_simple_t_reply_locked(struct console_tty_state *t, int code)
{
  console_queue_input_byte_locked(t, '\033');
  console_queue_input_byte_locked(t, '[');
  console_queue_input_uint_locked(t, code);
  console_queue_input_byte_locked(t, 't');
  t->input.w = t->input.e;
  wakeup(&t->input.r);
}

static void
console_queue_pair_t_reply_locked(struct console_tty_state *t, int code, int a, int b)
{
  console_queue_input_byte_locked(t, '\033');
  console_queue_input_byte_locked(t, '[');
  console_queue_input_uint_locked(t, code);
  console_queue_input_byte_locked(t, ';');
  console_queue_input_uint_locked(t, a);
  console_queue_input_byte_locked(t, ';');
  console_queue_input_uint_locked(t, b);
  console_queue_input_byte_locked(t, 't');
  t->input.w = t->input.e;
  wakeup(&t->input.r);
}

static void
console_queue_osc_reply_locked(struct console_tty_state *t, char sel, const char *text)
{
  console_queue_input_byte_locked(t, '\033');
  console_queue_input_byte_locked(t, ']');
  console_queue_input_byte_locked(t, sel);
  if(text)
    console_queue_input_cstr_locked(t, text);
  console_queue_input_byte_locked(t, '\033');
  console_queue_input_byte_locked(t, '\\');
  t->input.w = t->input.e;
  wakeup(&t->input.r);
}

static int
ansi_dec_special_graphics(int ch)
{
  switch(ch) {
  case 'j': return 0xD9;  /* lower-right corner */
  case 'k': return 0xBF;  /* upper-right corner */
  case 'l': return 0xDA;  /* upper-left corner */
  case 'm': return 0xC0;  /* lower-left corner */
  case 'n': return 0xC5;  /* crossing lines */
  case 'q': return 0xC4;  /* horizontal line */
  case 't': return 0xC3;  /* tee right */
  case 'u': return 0xB4;  /* tee left */
  case 'v': return 0xC1;  /* tee up */
  case 'w': return 0xC2;  /* tee down */
  case 'x': return 0xB3;  /* vertical line */
  case '~': return 0xF9;  /* bullet */
  default: return ch;
  }
}

static int
ansi_map_codepoint_to_cga(uint cp)
{
  switch(cp) {
  case 0x2022: return 0xF9;  /* bullet */
  case 0x2500: return 0xC4;
  case 0x2501: return 0xC4;
  case 0x2502: return 0xB3;
  case 0x2503: return 0xB3;
  case 0x250C: return 0xDA;
  case 0x2510: return 0xBF;
  case 0x2514: return 0xC0;
  case 0x2518: return 0xD9;
  case 0x251C: return 0xC3;
  case 0x2524: return 0xB4;
  case 0x252C: return 0xC2;
  case 0x2534: return 0xC1;
  case 0x253C: return 0xC5;
  case 0x2550: return 0xCD;  /* double horizontal */
  case 0x2551: return 0xBA;  /* double vertical */
  case 0x2554: return 0xC9;
  case 0x2557: return 0xBB;
  case 0x255A: return 0xC8;
  case 0x255D: return 0xBC;
  case 0x2560: return 0xCC;
  case 0x2563: return 0xB9;
  case 0x2566: return 0xCB;
  case 0x2569: return 0xCA;
  case 0x256C: return 0xCE;
  case 0x2580: return 0xDF;
  case 0x2584: return 0xDC;
  case 0x2588: return 0xDB;
  case 0x258C: return 0xDD;
  case 0x2590: return 0xDE;
  case 0x2591: return 0xB0;
  case 0x2592: return 0xB1;
  case 0x2593: return 0xB2;
  default:
    if(cp < 0x80)
      return (int)cp;
    if(cp <= 0xFF)
      return (int)cp;
    return '?';
  }
}

static int
ansi_translate_glyph(struct console_tty_state *t, int ch)
{
  int active;

  if(ch < 0x20)
    return ch;

  active = (t->ansi.gl_charset == 0) ? t->ansi.g0_charset : t->ansi.g1_charset;
  if(active == ANSI_CS_DEC)
    return ansi_dec_special_graphics(ch);
  return ch;
}

/* Returns -1 when a UTF-8 sequence is still incomplete and no glyph should be emitted. */
static int
ansi_decode_utf8_or_single(struct console_tty_state *t, int ch)
{
  uchar b;

  b = (uchar)ch;
  if(t->ansi.utf8_need == 0) {
    if(b < 0x80)
      return ansi_translate_glyph(t, (int)b);
    if((b & 0xE0) == 0xC0) {
      t->ansi.utf8_need = 1;
      t->ansi.utf8_accum = b & 0x1F;
      t->ansi.utf8_min = 0x80;
      return -1;
    }
    if((b & 0xF0) == 0xE0) {
      t->ansi.utf8_need = 2;
      t->ansi.utf8_accum = b & 0x0F;
      t->ansi.utf8_min = 0x800;
      return -1;
    }
    if((b & 0xF8) == 0xF0) {
      t->ansi.utf8_need = 3;
      t->ansi.utf8_accum = b & 0x07;
      t->ansi.utf8_min = 0x10000;
      return -1;
    }
    if(b >= 0xA0)
      return (int)b;
    return '?';
  }

  if((b & 0xC0) != 0x80) {
    t->ansi.utf8_need = 0;
    return ansi_decode_utf8_or_single(t, ch);
  }

  t->ansi.utf8_accum = (t->ansi.utf8_accum << 6) | (uint)(b & 0x3F);
  t->ansi.utf8_need--;
  if(t->ansi.utf8_need > 0)
    return -1;

  if(t->ansi.utf8_accum < t->ansi.utf8_min)
    return '?';
  if(t->ansi.utf8_accum > 0x10FFFF)
    return '?';
  if(t->ansi.utf8_accum >= 0xD800 && t->ansi.utf8_accum <= 0xDFFF)
    return '?';
  return ansi_map_codepoint_to_cga(t->ansi.utf8_accum);
}

static void
cgaputc_ansi(struct console_tty_state *t, int c)
{
  int pos, row, col, n, r2, c2, p1, p2;
  int row_base, row_max;
  uchar a = t->ansi.attr;

  pos = cga_cursor;
  row = pos / 80;
  col = pos % 80;

  switch(t->ansi.state) {
  case ANSI_NORMAL:
    if(c == 0x1B) { t->ansi.utf8_need = 0; t->ansi.state = ANSI_ESC; return; }
    if(c == 0x0E) { t->ansi.gl_charset = 1; return; }  /* SO: shift out, use G1 */
    if(c == 0x0F) { t->ansi.gl_charset = 0; return; }  /* SI: shift in, use G0 */
    if(c == '\r') { cga_write_cursor(row * 80); return; }
    if(c == '\n') {
      if(row == t->ansi.scroll_bot) {
        cga_scroll_up(t->ansi.scroll_top, t->ansi.scroll_bot, 1, a);
        cga_write_cursor(t->ansi.scroll_bot * 80 + col);
      } else if(row < 24) {
        cga_write_cursor((row + 1) * 80 + col);
      }
      return;
    }
    if(c == '\b' || c == 0x08) { if(col > 0) cga_write_cursor(pos-1); return; }
    if(c == BACKSPACE) {
      if(pos > 0) {
        pos--;
        crt[pos] = (ushort)(' ' | ((ushort)a << 8));
        cga_write_cursor(pos);
      }
      return;
    }
    if(c == '\t') {
      c2 = (col+8)&~7; if(c2>79) c2=79;
      cga_write_cursor(row*80+c2); return;
    }
    if(c == '\a') return;
    if(c >= 0x20 && c < 0x100) {
      c = ansi_decode_utf8_or_single(t, c);
      if(c < 0)
        return;
      t->ansi.last_glyph = c;
      if(t->ansi.insert_mode && col < 79)
        memmove(crt + pos + 1, crt + pos, (79 - col) * sizeof(ushort));
      crt[pos] = (ushort)((c & 0xff) | ((ushort)a << 8));
      if(col == 79) {
        if(t->ansi.wraparound) {
          if(row == t->ansi.scroll_bot) {
            cga_scroll_up(t->ansi.scroll_top, t->ansi.scroll_bot, 1, a);
            pos = t->ansi.scroll_bot * 80;
          } else {
            pos = (row < 24) ? (row+1)*80 : 24*80;
          }
        } else {
          pos = row * 80 + 79;
        }
      } else {
        pos++;
      }
      cga_write_cursor(pos);
    }
    return;

  case ANSI_ESC:
    if(c == '[') {
      t->ansi.state=ANSI_CSI; t->ansi.nparams=0; t->ansi.cur_param=0; t->ansi.question=0; t->ansi.greater=0; t->ansi.dollar=0; t->ansi.bang=0;
      memset(t->ansi.params, 0, sizeof(t->ansi.params));
      return;
    }
    if(c == ']') { t->ansi.state=ANSI_OSC; return; }
    if(c == '(') { t->ansi.state=ANSI_ESC_G0; return; }
    if(c == ')') { t->ansi.state=ANSI_ESC_G1; return; }
    if(c == '=') { t->ansi.keypad_app = 1; t->ansi.state = ANSI_NORMAL; return; }
    if(c == '>') { t->ansi.keypad_app = 0; t->ansi.state = ANSI_NORMAL; return; }
    if(c == '7') { ansi_save_cursor(t); t->ansi.state = ANSI_NORMAL; return; }
    if(c == '8') { ansi_restore_cursor(t); t->ansi.state = ANSI_NORMAL; return; }
    if(c == 'c') {
      ansi_leave_alt_screen(t);
      ansi_soft_reset(t);
      cga_fill(0, 25*80, 0x07); cga_write_cursor(0);
    } else if(c == 'Z') {
      console_queue_da_reply_locked(t, 1, 0);
    } else if(c == 'M') {
      if(row == t->ansi.scroll_top)
        cga_scroll_down(t->ansi.scroll_top, t->ansi.scroll_bot, 1, a);
      else if(row > 0)
        cga_write_cursor((row-1)*80+col);
    }
    t->ansi.state = ANSI_NORMAL;
    return;

  case ANSI_OSC:
    if(c == '\a' || c == 0x9C) {
      t->ansi.state = ANSI_NORMAL;
      return;
    }
    if(c == 0x1B) {
      t->ansi.state = ANSI_OSC_ESC;
      return;
    }
    return;

  case ANSI_OSC_ESC:
    if(c == '\\' || c == '\a' || c == 0x9C)
      t->ansi.state = ANSI_NORMAL;
    else
      t->ansi.state = ANSI_OSC;
    return;

  case ANSI_ESC_G0:
    t->ansi.g0_charset = (c == '0') ? ANSI_CS_DEC : ANSI_CS_ASCII;
    t->ansi.state = ANSI_NORMAL;
    return;

  case ANSI_ESC_G1:
    t->ansi.g1_charset = (c == '0') ? ANSI_CS_DEC : ANSI_CS_ASCII;
    t->ansi.state = ANSI_NORMAL;
    return;

  case ANSI_CSI:
    if(c == '?') { t->ansi.question=1; return; }
    if(c == '>') { t->ansi.greater=1; return; }
    if(c == '$') { t->ansi.dollar=1; return; }
    if(c == '!') { t->ansi.bang=1; return; }
    if(c >= '0' && c <= '9') {
      t->ansi.cur_param = t->ansi.cur_param*10 + (c-'0'); return;
    }
    if(c == ';') {
      if(t->ansi.nparams < 8) t->ansi.params[t->ansi.nparams++] = t->ansi.cur_param;
      t->ansi.cur_param = 0; return;
    }
    if(t->ansi.nparams < 8) t->ansi.params[t->ansi.nparams++] = t->ansi.cur_param;
    t->ansi.cur_param = 0; t->ansi.state = ANSI_NORMAL;

    p1 = (t->ansi.nparams>=1) ? t->ansi.params[0] : 0;
    p2 = (t->ansi.nparams>=2) ? t->ansi.params[1] : 0;

    switch(c) {
    case 'a': n=p1?p1:1; c2=col+n; if(c2>79)c2=79; cga_write_cursor(row*80+c2); break;
    case 'A': n=p1?p1:1; r2=row-n; if(r2<t->ansi.scroll_top)r2=t->ansi.scroll_top; cga_write_cursor(r2*80+col); break;
    case 'B': n=p1?p1:1; r2=row+n; if(r2>t->ansi.scroll_bot)r2=t->ansi.scroll_bot; cga_write_cursor(r2*80+col); break;
    case 'C': n=p1?p1:1; c2=col+n; if(c2>79)c2=79; cga_write_cursor(row*80+c2); break;
    case 'D': n=p1?p1:1; c2=col-n; if(c2<0)c2=0; cga_write_cursor(row*80+c2); break;
    case 'd':
      row_base = t->ansi.origin_mode ? t->ansi.scroll_top : 0;
      row_max = t->ansi.origin_mode ? t->ansi.scroll_bot : 24;
      r2 = (p1 ? p1 - 1 : 0) + row_base;
      if(r2 < row_base)
        r2 = row_base;
      if(r2 > row_max)
        r2 = row_max;
      cga_write_cursor(r2*80+col);
      break;
    case 'e': n=p1?p1:1; r2=row+n; if(r2>t->ansi.scroll_bot)r2=t->ansi.scroll_bot; cga_write_cursor(r2*80+col); break;
    case 'E': n=p1?p1:1; r2=row+n; if(r2>24)r2=24; cga_write_cursor(r2*80); break;
    case 'F': n=p1?p1:1; r2=row-n; if(r2<0)r2=0; cga_write_cursor(r2*80); break;
    case 'G': c2=p1?p1-1:0; if(c2<0)c2=0; if(c2>79)c2=79; cga_write_cursor(row*80+c2); break;
    case 'H': case 'f':
      row_base = t->ansi.origin_mode ? t->ansi.scroll_top : 0;
      row_max = t->ansi.origin_mode ? t->ansi.scroll_bot : 24;
      r2 = (p1 ? p1 - 1 : 0) + row_base;
      c2 = p2 ? p2 - 1 : 0;
      if(r2 < row_base)
        r2 = row_base;
      if(r2 > row_max)
        r2 = row_max;
      if(c2 < 0)
        c2 = 0;
      if(c2 > 79)
        c2 = 79;
      cga_write_cursor(r2*80+c2); break;
    case 'J':
      if(p1==0) cga_fill(pos, 25*80, a);
      else if(p1==1) cga_fill(0, pos+1, a);
      else cga_fill(0, 25*80, a);
      break;
    case 'K':
      if(p1==0) cga_fill(row*80+col, row*80+80, a);
      else if(p1==1) cga_fill(row*80, row*80+col+1, a);
      else cga_fill(row*80, row*80+80, a);
      break;
    case 'L': n=p1?p1:1; cga_scroll_down(row, t->ansi.scroll_bot, n, a); break;
    case 'M': n=p1?p1:1; cga_scroll_up(row,   t->ansi.scroll_bot, n, a); break;
    case 'P': {
      int av=80-col; n=p1?p1:1; if(n>av)n=av;
      memmove(crt+pos, crt+pos+n, (av-n)*sizeof(ushort));
      cga_fill(pos+av-n, pos+av, a); break; }
    case 'X': {
      int av=80-col; n=p1?p1:1; if(n>av)n=av;
      cga_fill(pos, pos+n, a); break; }
    case 'b':
      n = p1 ? p1 : 1;
      if(t->ansi.last_glyph >= 0) {
        int rep;
        for(rep = 0; rep < n; rep++)
          cgaputc_ansi(t, t->ansi.last_glyph);
      }
      break;
    case '@': {
      int av=80-col; int i2; n=p1?p1:1; if(n>av)n=av;
      for(i2=av-1; i2>=n; i2--) crt[pos+i2]=crt[pos+i2-n];
      cga_fill(pos, pos+n, a); break; }
    case 'S': n=p1?p1:1; cga_scroll_up(t->ansi.scroll_top,  t->ansi.scroll_bot, n, a); break;
    case 'T': n=p1?p1:1; cga_scroll_down(t->ansi.scroll_top, t->ansi.scroll_bot, n, a); break;
    case 'm': {
      static const uchar cga_fg[] = {0,4,2,6,1,5,3,7};
      int i2;
      if(t->ansi.nparams == 0) { 
        t->ansi.attr=0x07; 
        t->ansi.underline=0; 
        t->ansi.italic=0; 
        t->ansi.strikethrough=0; 
        t->ansi.dim=0; 
        break; 
      }
      for(i2=0; i2<t->ansi.nparams; i2++) {
        int sg=t->ansi.params[i2];
        if(sg==0) {
          t->ansi.attr=0x07;
          t->ansi.underline=0;
          t->ansi.italic=0;
          t->ansi.strikethrough=0;
          t->ansi.dim=0;
        }
        else if(sg==1)          t->ansi.attr|=0x08;
        else if(sg==2||sg==22)  { t->ansi.attr&=~0x08; t->ansi.dim=(sg==2)?1:0; }
        else if(sg==3)          t->ansi.italic=1;
        else if(sg==4)          t->ansi.underline=1;
        else if(sg==5)          t->ansi.attr|=0x80;
        else if(sg==9)          t->ansi.strikethrough=1;
        else if(sg==23)         t->ansi.italic=0;
        else if(sg==24)         t->ansi.underline=0;
        else if(sg==25)         t->ansi.attr&=~0x80;
        else if(sg==29)         t->ansi.strikethrough=0;
        else if(sg==7)          { uchar fg=t->ansi.attr&0x0F,bg=(t->ansi.attr>>4)&0x0F; t->ansi.attr=(uchar)((fg<<4)|bg); }
        else if(sg==27)         t->ansi.attr=0x07;
        else if(sg>=30&&sg<=37) t->ansi.attr=(t->ansi.attr&0xF0)|cga_fg[sg-30];
        else if(sg==39)         t->ansi.attr=(t->ansi.attr&0xF0)|0x07;
        else if(sg>=40&&sg<=47) t->ansi.attr=(t->ansi.attr&0x0F)|(uchar)(cga_fg[sg-40]<<4);
        else if(sg==49)         t->ansi.attr&=0x0F;
        else if(sg>=90&&sg<=97) t->ansi.attr=(t->ansi.attr&0xF0)|(uchar)(cga_fg[sg-90]|0x08);
        else if(sg>=100&&sg<=107) t->ansi.attr=(t->ansi.attr&0x0F)|(uchar)((cga_fg[sg-100]|0x08)<<4);
      }
      break; }
    case 'r':
      r2=p1?p1-1:0; n=p2?p2-1:23;
      if(r2 < 0)
        r2 = 0;
      if(n > 24)
        n = 24;
      if(r2<n) { t->ansi.scroll_top=r2; t->ansi.scroll_bot=n; }
      cga_write_cursor(0);
      break;
    case 's':
      ansi_save_cursor(t);
      break;
    case 'u':
      ansi_restore_cursor(t);
      break;
    case 'p':
      if(t->ansi.bang)
        ansi_soft_reset(t);
      else if(t->ansi.dollar)
        console_queue_mode_reply_locked(t, t->ansi.question, p1);
      break;
    case 'n':
      if(!t->ansi.bang)
        console_queue_dsr_reply_locked(t, t->ansi.question, p1);
      break;
    case 'c':
      if(!t->ansi.bang)
        console_queue_da_reply_locked(t, t->ansi.question, t->ansi.greater);
      break;
    case 't':
      if(!t->ansi.question && !t->ansi.greater && !t->ansi.dollar && !t->ansi.bang) {
        int rows = t->winsize.ws_row > 0 ? t->winsize.ws_row : 24;
        int cols = t->winsize.ws_col > 0 ? t->winsize.ws_col : 80;
        switch(p1) {
        case 11:
          console_queue_simple_t_reply_locked(t, 1);
          break;
        case 13:
          console_queue_pair_t_reply_locked(t, 3, 1, 1);
          break;
        case 14:
          console_queue_pair_t_reply_locked(t, 4, rows * 16, cols * 8);
          break;
        case 15:
          console_queue_pair_t_reply_locked(t, 5, rows * 16, cols * 8);
          break;
        case 16:
          console_queue_pair_t_reply_locked(t, 6, 16, 8);
          break;
        case 18:
          console_queue_pair_t_reply_locked(t, 8, rows, cols);
          break;
        case 19:
          console_queue_pair_t_reply_locked(t, 9, rows, cols);
          break;
        case 20:
          console_queue_osc_reply_locked(t, 'L', "auxv6");
          break;
        case 21:
          console_queue_osc_reply_locked(t, 'l', "auxv6");
          break;
        default:
          break;
        }
      }
      break;
    case 'h':
      if(t->ansi.nparams == 0) {
        if(t->ansi.question)
          ansi_apply_private_mode(t, 0, 1);
        else
          ansi_apply_mode(t, 0, 1);
      } else {
        int i2;
        for(i2 = 0; i2 < t->ansi.nparams; i2++) {
          if(t->ansi.question)
            ansi_apply_private_mode(t, t->ansi.params[i2], 1);
          else
            ansi_apply_mode(t, t->ansi.params[i2], 1);
        }
      }
      break;
    case 'l':
      if(t->ansi.nparams == 0) {
        if(t->ansi.question)
          ansi_apply_private_mode(t, 0, 0);
        else
          ansi_apply_mode(t, 0, 0);
      } else {
        int i2;
        for(i2 = 0; i2 < t->ansi.nparams; i2++) {
          if(t->ansi.question)
            ansi_apply_private_mode(t, t->ansi.params[i2], 0);
          else
            ansi_apply_mode(t, t->ansi.params[i2], 0);
        }
      }
      break;
    default: break;
    }
    return;
  }
}

static void
consputc_ansi(struct console_tty_state *t, int c)
{
  struct console_tty_state *active;

  if(panicked) { cli(); for(;;); }
  active = console_tty_by_index(console_active_tty);
  if(t == active) {
    if(c == BACKSPACE) {
      uartputc('\b'); uartputc(' '); uartputc('\b');
    } else if(c >= 0 && c < 0x100) {
      uartputc(c);
    }
  }

  crt = t->screen;
  cga_render_offscreen = 1;
  cga_cursor = t->cursor;
  cgaputc_ansi(t, c);
  t->cursor = cga_cursor;
  crt = cga_hw;
  cga_render_offscreen = 0;
  if(t == active)
    console_flush_tty_locked(t);
}

/* --------------------------------------------------------------------------
 * Input ring buffer
 * -------------------------------------------------------------------------- */

#define INPUT_BUF 128

#define C(x) ((x)-'@')

/* Special keyboard key codes (matching kbd.h) */
#define KEY_HOME  0xE0
#define KEY_END   0xE1
#define KEY_UP    0xE2
#define KEY_DN    0xE3
#define KEY_LF    0xE4
#define KEY_RT    0xE5
#define KEY_PGUP  0xE6
#define KEY_PGDN  0xE7
#define KEY_INS   0xE8
#define KEY_DEL   0xE9
#define KEY_F1    0xEA
#define KEY_F2    0xEB
#define KEY_F3    0xEC
#define KEY_F4    0xED
#define KEY_F5    0xEE
#define KEY_F6    0xEF
#define KEY_F7    0xF0
#define KEY_F8    0xF1
#define KEY_F9    0xF2
#define KEY_F10   0xF3
#define KEY_F11   0xF4
#define KEY_F12   0xF5

static int
console_utf8_erase_len(struct console_tty_state *t)
{
  uint e;
  int n;
  uchar b;

  if(t->input.e == t->input.w)
    return 0;
  if(!(t->termios.c_iflag & IUTF8))
    return 1;

  e = t->input.e;
  n = 1;
  while(n < 4 && e > t->input.w + (uint)n) {
    b = (uchar)t->input.buf[(e - (uint)n) % INPUT_BUF];
    if((b & 0xC0) != 0x80)
      break;
    n++;
  }
  return n;
}

static void
console_echo_input_char(struct console_tty_state *t, int c, int canonical, uchar veof)
{
  if(!(t->termios.c_lflag & ECHO) &&
     !((t->termios.c_lflag & ECHONL) && c == '\n'))
    return;

  if(canonical && veof && c == (int)veof)
    return;

  if((t->termios.c_lflag & ECHOCTL) &&
     c != '\n' && c != '\r' && c != '\t' &&
     ((c >= 0 && c < 0x20) || c == 0x7f)) {
    consputc('^');
    consputc(c == 0x7f ? '?' : (c + '@'));
    return;
  }

  consputc(c);
}

/* --------------------------------------------------------------------------
 * Foreground pgrp / termios accessors
 * -------------------------------------------------------------------------- */

void
console_set_foreground_pgid(int tty, int pgid)
{
  struct console_tty_state *t;

  acquire(&cons.lock);
  t = console_tty_by_index(tty);
  t->fg_pgid = pgid;
  release(&cons.lock);
}

int
console_get_foreground_pgid(int tty)
{
  struct console_tty_state *t;
  int pgid;

  acquire(&cons.lock);
  t = console_tty_by_index(tty);
  pgid = t->fg_pgid;
  release(&cons.lock);
  return pgid;
}

int
console_get_termios(int tty, struct termios *tp)
{
  struct console_tty_state *t;

  if(tp == 0) return -1;
  acquire(&cons.lock);
  t = console_tty_by_index(tty);
  *tp = t->termios;
  release(&cons.lock);
  return 0;
}

int
console_set_termios(int tty, const struct termios *tp, int optional_actions)
{
  struct console_tty_state *t;

  if(tp == 0) return -1;
  if(optional_actions != TCSANOW   &&
     optional_actions != TCSADRAIN &&
     optional_actions != TCSAFLUSH)
    return -1;
  acquire(&cons.lock);
  t = console_tty_by_index(tty);

  if(optional_actions == TCSANOW) {
    t->termios = *tp;
  } else if(t->output_busy > 0) {
    t->pending_termios = *tp;
    t->pending_termios_valid = 1;
    t->pending_termios_action = optional_actions;
    release(&cons.lock);
    return 0;
  } else {
    t->termios = *tp;
    if(optional_actions == TCSAFLUSH)
      t->input.r = t->input.w = t->input.e;
  }

  release(&cons.lock);
  return 0;
}

void
console_set_active_tty(int tty)
{
  acquire(&cons.lock);
  console_active_tty = console_tty_index_clamp(tty);
  console_flush_tty_locked(console_tty_by_index(console_active_tty));
  release(&cons.lock);
}

int
console_get_active_tty(void)
{
  int tty;

  acquire(&cons.lock);
  tty = console_active_tty;
  release(&cons.lock);
  return tty;
}

/* --------------------------------------------------------------------------
 * ioctl dispatcher
 * -------------------------------------------------------------------------- */

int
console_ioctl(int fd, int request, uint arg)
{
  struct console_tty_state *t;
  struct winsize *ws;
  int *pgidp;
  int *intp;
  int pgid;
  int tty;
  struct proc *p;

  (void)fd;
  t = console_current_tty();

  switch(request) {
  case 0x5401:  /* TCGETS */
    if(arg == 0) return -1;
    acquire(&cons.lock);
    *(struct termios *)arg = t->termios;
    release(&cons.lock);
    return 0;

  case 0x5402:  /* TCSETS */
  case 0x5403:  /* TCSETSW */
  case 0x5404:  /* TCSETSF */
    if(arg == 0) return -1;
    if(request == 0x5402)
      return console_set_termios(console_current_tty_index(), (const struct termios *)arg, TCSANOW);
    if(request == 0x5403)
      return console_set_termios(console_current_tty_index(), (const struct termios *)arg, TCSADRAIN);
    return console_set_termios(console_current_tty_index(), (const struct termios *)arg, TCSAFLUSH);

  case 0x5413:  /* TIOCGWINSZ */
    if(arg == 0) return -1;
    ws = (struct winsize *)arg;
    acquire(&cons.lock);
    *ws = t->winsize;
    release(&cons.lock);
    return 0;

  case 0x5414:  /* TIOCSWINSZ */
    if(arg == 0) return -1;
    ws = (struct winsize *)arg;
    acquire(&cons.lock);
    t->winsize = *ws;
    if(t->winsize.ws_row > 0 && t->winsize.ws_row <= 25)
      t->ansi.scroll_bot = t->winsize.ws_row - 1;
    pgid = t->fg_pgid;
    release(&cons.lock);
    proc_signal_pgid(pgid, SIGWINCH);
    return 0;

  case 0x540F:  /* TIOCGPGRP */
    if(arg == 0) return -1;
    pgidp = (int *)arg;
    acquire(&cons.lock);
    *pgidp = t->fg_pgid;
    release(&cons.lock);
    return 0;

  case 0x5410:  /* TIOCSPGRP */
    if(arg == 0) return -1;
    pgidp = (int *)arg;
    acquire(&cons.lock);
    t->fg_pgid = *pgidp;
    release(&cons.lock);
    return 0;

  case 0x540E:  /* TIOCSCTTY */
    p = myproc();
    if(p) {
      tty = (int)arg;
      if(tty < 0 || tty >= CONSOLE_NTTY)
        tty = console_get_active_tty();
      p->tty = tty;
    }
    return 0;

  case 0x5411:  /* TIOCOUTQ */
    if(arg == 0) return -1;
    *(int *)arg = 0;
    return 0;

  case 0x541B:  /* FIONREAD / TIOCINQ */
    if(arg == 0) return -1;
    acquire(&cons.lock);
    *(int *)arg = (int)(t->input.w - t->input.r);
    release(&cons.lock);
    return 0;

  case 0x54A0:  /* TIOCGACTTTY */
    if(arg == 0) return -1;
    intp = (int *)arg;
    *intp = console_get_active_tty();
    return 0;

  case 0x54A1:  /* TIOCSACTTTY */
    tty = (int)arg;
    if(tty < 0 || tty >= CONSOLE_NTTY)
      return -1;
    console_set_active_tty(tty);
    return 0;

  case 0x54A2:  /* TIOCGNTTY */
    if(arg == 0) return -1;
    intp = (int *)arg;
    *intp = CONSOLE_NTTY;
    return 0;

  case 0x54A3:  /* TIOCISATTY */
    return 1;

  case 0x540B:  /* TCFLSH */
    if((int)arg != TCIFLUSH && (int)arg != TCOFLUSH && (int)arg != TCIOFLUSH)
      return -1;
    if((int)arg == TCIFLUSH || (int)arg == TCIOFLUSH) {
      acquire(&cons.lock);
      t->input.r = t->input.w = t->input.e;
      release(&cons.lock);
    }
    return 0;

  default:
    return -1;
  }
}

/* --------------------------------------------------------------------------
 * consoleintr: keyboard/UART interrupt handler with full line discipline
 * -------------------------------------------------------------------------- */

void
consoleintr(int (*getc)(void))
{
  struct console_tty_state *t;
  int c, doprocdump = 0;
  int canonical, echo, isig;
  uchar vintr, vquit, vsusp, vkill, verase, veof, veol;
  const char *esc_seq;
  const char *p;

  acquire(&cons.lock);
  while((c = getc()) >= 0) {
    t = console_tty_by_index(console_active_tty);

    if(c == KEY_F1 || c == KEY_F2 || c == KEY_F3 || c == KEY_F4) {
      console_active_tty = c - KEY_F1;
      console_flush_tty_locked(console_tty_by_index(console_active_tty));
      continue;
    }

    canonical = (t->termios.c_lflag & ICANON) != 0;
    echo      = (t->termios.c_lflag & ECHO)   != 0;
    isig      = (t->termios.c_lflag & ISIG)   != 0;

    vintr  = t->termios.c_cc[VINTR];
    vquit  = t->termios.c_cc[VQUIT];
    vsusp  = t->termios.c_cc[VSUSP];
    vkill  = t->termios.c_cc[VKILL];
    verase = t->termios.c_cc[VERASE];
    veof   = t->termios.c_cc[VEOF];
    veol   = t->termios.c_cc[VEOL];

    /* Ctrl+P: process dump debug */
    if(c == C('P')) { doprocdump = 1; continue; }

    /* ISTRIP: clear the 8th bit for regular byte input. */
    if((t->termios.c_iflag & ISTRIP) && c >= 0 && c < 0x100 &&
       c != KEY_HOME && c != KEY_END && c != KEY_UP && c != KEY_DN &&
       c != KEY_LF && c != KEY_RT && c != KEY_PGUP && c != KEY_PGDN &&
       c != KEY_INS && c != KEY_DEL && c != KEY_F1 && c != KEY_F2 &&
       c != KEY_F3 && c != KEY_F4)
      c &= 0x7f;

    /* ISIG: signal characters */
    if(isig) {
      if(vintr && c == (int)vintr) {
        consputc('^'); consputc('C'); consputc('\n');
        proc_signal_pgid(t->fg_pgid, SIGINT);
        if(!(t->termios.c_lflag & NOFLSH))
          t->input.r = t->input.w = t->input.e;
        continue;
      }
      if(vquit && c == (int)vquit) {
        proc_signal_pgid(t->fg_pgid, SIGQUIT);
        if(!(t->termios.c_lflag & NOFLSH))
          t->input.r = t->input.w = t->input.e;
        continue;
      }
      if(vsusp && c == (int)vsusp) {
        consputc('^'); consputc('Z'); consputc('\n');
        proc_signal_pgid(t->fg_pgid, SIGTSTP);
        if(!(t->termios.c_lflag & NOFLSH))
          t->input.r = t->input.w = t->input.e;
        continue;
      }
    }

    /* IXON: swallow ^S/^Q */
    if(t->termios.c_iflag & IXON) {
      if(t->termios.c_cc[VSTOP]  && c == (int)t->termios.c_cc[VSTOP])  continue;
      if(t->termios.c_cc[VSTART] && c == (int)t->termios.c_cc[VSTART]) continue;
    }

    /* Special keyboard keys: inject ANSI escape sequences */
    esc_seq = 0;
    switch(c) {
    case KEY_UP:   esc_seq = t->ansi.cursor_keys_app ? "\033OA" : "\033[A";  break;
    case KEY_DN:   esc_seq = t->ansi.cursor_keys_app ? "\033OB" : "\033[B";  break;
    case KEY_RT:   esc_seq = t->ansi.cursor_keys_app ? "\033OC" : "\033[C";  break;
    case KEY_LF:   esc_seq = t->ansi.cursor_keys_app ? "\033OD" : "\033[D";  break;
    case KEY_HOME: esc_seq = "\033[H";  break;
    case KEY_END:  esc_seq = "\033[F";  break;
    case KEY_PGUP: esc_seq = "\033[5~"; break;
    case KEY_PGDN: esc_seq = "\033[6~"; break;
    case KEY_INS:  esc_seq = "\033[2~"; break;
    case KEY_DEL:  esc_seq = "\033[3~"; break;
    case KEY_F1:   esc_seq = "\033[11~"; break;
    case KEY_F2:   esc_seq = "\033[12~"; break;
    case KEY_F3:   esc_seq = "\033[13~"; break;
    case KEY_F4:   esc_seq = "\033[14~"; break;
    case KEY_F5:   esc_seq = "\033[15~"; break;
    case KEY_F6:   esc_seq = "\033[17~"; break;
    case KEY_F7:   esc_seq = "\033[18~"; break;
    case KEY_F8:   esc_seq = "\033[19~"; break;
    case KEY_F9:   esc_seq = "\033[20~"; break;
    case KEY_F10:  esc_seq = "\033[21~"; break;
    case KEY_F11:  esc_seq = "\033[23~"; break;
    case KEY_F12:  esc_seq = "\033[24~"; break;
    default: break;
    }
    if(esc_seq) {
      for(p = esc_seq; *p && t->input.e - t->input.r < INPUT_BUF; p++)
        t->input.buf[t->input.e++ % INPUT_BUF] = *p;
      t->input.w = t->input.e;
      wakeup(&t->input.r);
      continue;
    }

    /* ICANON: line editing */
    if(canonical) {
      if(vkill && c == (int)vkill) {
        while(t->input.e != t->input.w &&
              t->input.buf[(t->input.e-1) % INPUT_BUF] != '\n') {
          int erase_n = console_utf8_erase_len(t);
          int j;
          if(erase_n <= 0)
            break;
          t->input.e -= erase_n;
          if(t->termios.c_lflag & (ECHOE|ECHOKE))
            for(j = 0; j < erase_n; j++)
              consputc(BACKSPACE);
        }
        continue;
      }
      if((verase && c == (int)verase) || c == C('H') || c == '\x7f') {
        if(t->input.e != t->input.w &&
           t->input.buf[(t->input.e-1) % INPUT_BUF] != '\n') {
          int erase_n = console_utf8_erase_len(t);
          int j;
          if(erase_n > 0) {
            t->input.e -= erase_n;
            if(echo && (t->termios.c_lflag & ECHOE))
              for(j = 0; j < erase_n; j++)
                consputc(BACKSPACE);
          }
        }
        continue;
      }
    } else {
      if((verase && c == (int)verase) || c == C('H') || c == '\x7f') {
        if(t->input.e != t->input.r) {
          t->input.e--;
          t->input.w = t->input.e;
          wakeup(&t->input.r);
        }
        continue;
      }
    }

    /* Input CR/NL mapping controls. */
    if(c == '\r') {
      if(t->termios.c_iflag & IGNCR)
        continue;
      if(t->termios.c_iflag & ICRNL)
        c = '\n';
    } else if(c == '\n') {
      if(t->termios.c_iflag & INLCR)
        c = '\r';
    }

    /* Buffer the character */
    if(c != 0 && t->input.e - t->input.r < INPUT_BUF) {
      t->input.buf[t->input.e++ % INPUT_BUF] = c;
      console_echo_input_char(t, c, canonical, veof);
      if(!canonical                        ||
         c == '\n'                         ||
         (veol && c == (int)veol)          ||
         (veof && c == (int)veof)          ||
         t->input.e == t->input.r + INPUT_BUF) {
        t->input.w = t->input.e;
        wakeup(&t->input.r);
      }
    }
  }
  release(&cons.lock);
  if(doprocdump)
    procdump();
}

/* --------------------------------------------------------------------------
 * consoleread / consolewrite
 * -------------------------------------------------------------------------- */

int
consoleread(struct inode *ip, char *dst, uint off, int n)
{
  struct console_tty_state *t;
  int tty;
  uint target;
  uint now;
  uint deadline;
  int c;
  int got;
  int canonical;
  int vmin;
  int vtime;
  int timed_mode;
  uchar veof;

  (void)off;
  iunlock(ip);
  target = n;
  acquire(&cons.lock);
  tty = console_tty_index_from_inode(ip);
  t = console_tty_by_index(tty);

  /* SIGTTIN: background process reading controlling terminal */
  if(myproc()->tty >= 0 &&
     myproc()->pgid != 0 &&
      myproc()->pgid != t->fg_pgid) {
    release(&cons.lock);
    ilock(ip);
    proc_signal_pgid(myproc()->pgid, SIGTTIN);
    return -1;
  }

  canonical = (t->termios.c_lflag & ICANON) != 0;
  veof = t->termios.c_cc[VEOF];
  vmin = (int)t->termios.c_cc[VMIN];
  vtime = (int)t->termios.c_cc[VTIME];
  got = 0;
  timed_mode = 0;
  deadline = 0;

  if(!canonical) {
    if(vmin == 0 && vtime == 0) {
      while(n > 0 && t->input.r != t->input.w) {
        *dst++ = t->input.buf[t->input.r++ % INPUT_BUF];
        n--;
      }
      release(&cons.lock);
      ilock(ip);
      return target - n;
    }

    if(vtime > 0 && vmin == 0) {
      acquire(&tickslock);
      deadline = ticks + (uint)(vtime * 10);
      timed_mode = 1;
      release(&tickslock);
    }
  }

  while(n > 0) {
    while(t->input.r == t->input.w) {
      if(myproc()->killed) {
        release(&cons.lock);
        ilock(ip);
        return -1;
      }

      if(!canonical && vtime > 0) {
        acquire(&tickslock);

        if(vmin > 0 && got > 0 && !timed_mode) {
          deadline = ticks + (uint)(vtime * 10);
          timed_mode = 1;
        }

        now = ticks;
        if(timed_mode && (int)(now - deadline) >= 0) {
          release(&tickslock);
          release(&cons.lock);
          ilock(ip);
          return target - n;
        }

        release(&cons.lock);
        sleep(&ticks, &tickslock);
        release(&tickslock);
        acquire(&cons.lock);
        continue;
      }

      sleep(&t->input.r, &cons.lock);
    }

    c = t->input.buf[t->input.r++ % INPUT_BUF];

    if(canonical && veof && c == (int)veof) {
      if(n < (int)target)
        t->input.r--;
      break;
    }

    *dst++ = c;
    got++;
    --n;

    if(!canonical) {
      if(vtime > 0 && vmin > 0) {
        acquire(&tickslock);
        deadline = ticks + (uint)(vtime * 10);
        timed_mode = 1;
        release(&tickslock);
      }

      if(vmin == 0)
        break;
      if(got >= vmin)
        break;
      continue;
    }

    if(c == '\n' ||
       (t->termios.c_cc[VEOL] && c == (int)t->termios.c_cc[VEOL]))
      break;
  }
  release(&cons.lock);
  ilock(ip);
  return target - n;
}

int
consolewrite(struct inode *ip, char *buf, uint off, int n)
{
  struct console_tty_state *t;
  int tty;
  int i, c;
  int emit;
  int col;

  (void)off;
  iunlock(ip);
  acquire(&cons.lock);
  tty = console_tty_index_from_inode(ip);
  t = console_tty_by_index(tty);

  /* SIGTTOU: background write with TOSTOP set */
  if(myproc()->tty >= 0 &&
     myproc()->pgid != 0 &&
      myproc()->pgid != t->fg_pgid &&
      (t->termios.c_lflag & TOSTOP)) {
    release(&cons.lock);
    ilock(ip);
    proc_signal_pgid(myproc()->pgid, SIGTTOU);
    return -1;
  }

  t->output_busy++;

  for(i = 0; i < n; i++) {
    c = buf[i] & 0xff;
    emit = 1;
    if(t->termios.c_oflag & OPOST) {
      col = t->cursor % 80;

      if(c == '\r' && (t->termios.c_oflag & ONOCR) && col == 0)
        emit = 0;

      if(c == '\r' && (t->termios.c_oflag & OCRNL))
        c = '\n';

      if(c == '\n') {
        if(t->termios.c_oflag & ONLCR)
          consputc_ansi(t, '\r');
        else if(t->termios.c_oflag & ONLRET)
          consputc_ansi(t, '\r');
      }
    }

    if(emit)
      consputc_ansi(t, c);
  }

  t->output_busy--;
  if(t->output_busy <= 0) {
    t->output_busy = 0;
    if(t->pending_termios_valid) {
      t->termios = t->pending_termios;
      if(t->pending_termios_action == TCSAFLUSH)
        t->input.r = t->input.w = t->input.e;
      t->pending_termios_valid = 0;
      t->pending_termios_action = 0;
    }
  }

  release(&cons.lock);
  ilock(ip);
  return n;
}

/* --------------------------------------------------------------------------
 * consoleinit
 * -------------------------------------------------------------------------- */

void
consoleinit(void)
{
  int i;
  int j;
  ushort blank;

  initlock(&cons.lock, "console");
  cga_read_cursor();
  blank = (ushort)(' ' | (0x07 << 8));
  for(i = 0; i < CONSOLE_NTTY; i++) {
    cttys[i] = console_tty_default;
    cttys[i].cursor = 0;
    for(j = 0; j < 25 * 80; j++)
      cttys[i].screen[j] = blank;
  }
  memmove(cttys[0].screen, cga_hw, sizeof(cttys[0].screen));
  cttys[0].cursor = cga_cursor;
  console_active_tty = 0;
  devsw[CONSOLE].write = consolewrite;
  devsw[CONSOLE].read  = consoleread;
  cons.locking = 1;
  ioapicenable(IRQ_KBD, 0);
}

