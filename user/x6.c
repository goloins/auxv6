#include "types.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "stat.h"
#include "sys/ioctl.h"
#include "graphics/drm_ioctls.h"
#include "graphics/input_events.h"
#include "signal.h"
#include "socket.h"
#include "stdio.h"
#include "poll.h"
#include <string.h>

#define XK_BackSpace 0xff08
#define XK_Return 0xff0d
#define X6_MOD1_MASK (1U << 3)
#define X6_POLL_MS 50
#define X6_CANVAS_COLS 120
#define X6_CANVAS_ROWS 40
#define X6_CELL_W 8
#define X6_CELL_H 16
#define X6_BACKEND_AUTO 0
#define X6_BACKEND_ANSI 1
#define X6_BACKEND_FB 2

#define X6_DEFAULT_PORT 6006
#define X6_BACKLOG 16
#define X6_PROTO_VERSION 1
#define X6_PROC_PATH "/proc/server7"
#define X6_FBIOGET_VSCREENINFO 0x4600
#define X6_FBIOGET_FSCREENINFO 0x4602
#define X6_CONSOLE_MAJOR 1
#define X6_CONSOLE_MINOR_FB0 100
#define X6_CONSOLE_MINOR_MOUSE0 101

#define X6_MAX_WINDOWS 128
#define X6_MAX_EVENTS_PER_CLIENT 64

// Event types
#define X6_EVENT_MAP_REQUEST 1
#define X6_EVENT_CONFIGURE_REQUEST 2
#define X6_EVENT_FOCUS_IN 3
#define X6_EVENT_FOCUS_OUT 4
#define X6_EVENT_DESTROY_NOTIFY 5
#define X6_EVENT_KEY_PRESS 6
#define X6_EVENT_BUTTON_PRESS 7
#define X6_EVENT_BUTTON_RELEASE 8
#define X6_EVENT_MOTION_NOTIFY 9

struct x6_event {
  int type;
  uint wid;      // window ID
  int x, y, w, h; // geometry for configure requests
  int keycode;
  int button;
  uint state;
};

struct x6_event_queue {
  struct x6_event events[X6_MAX_EVENTS_PER_CLIENT];
  int head;
  int tail;
};

// Property storage for windows (Phase 2.1d)
#define X6_MAX_PROPERTIES_PER_WINDOW 16
#define X6_MAX_PROP_NAME 64
#define X6_MAX_PROP_VALUE 256

struct x6_property {
  char name[X6_MAX_PROP_NAME];
  char value[X6_MAX_PROP_VALUE];
};

struct x6_window {
  int in_use;
  uint id;
  int owner_fd;
  int x;
  int y;
  int w;
  int h;
  int mapped;
  struct x6_property props[X6_MAX_PROPERTIES_PER_WINDOW];
  int prop_count;
};

// Per-client context (for future expansion)
struct x6_client {
  int fd;
  struct x6_event_queue queue;
};

static volatile sig_atomic_t keep_running = 1;
static struct x6_window wins[X6_MAX_WINDOWS];

// Per-client context (simplified for MVP: one connection at a time)
static struct x6_event_queue *current_event_queue = 0;
static struct x6_event_queue *wm_event_queue = 0;
static int wm_client_fd = -1;

// WM state (Phase 2.1b: SubstructureRedirect semantics)
static int wm_has_redirect = 0;  // Does WM hold SubstructureRedirect on root?
static int wm_redirect_root = 1; // Root window ID is always 1

// Focus and keyboard state (Phase 2.1c)
static uint focus_wid = 0;          // Currently focused window (0 = no focus)
static uint keyboard_grab_owner = 0; // Who holds exclusive keyboard grab (0 = nobody, typically WM)
static int wm_has_kb_grab = 0;      // Does WM hold keyboard grab?
static int console_alt_prefix = 0;   // ESC-prefix to synthesize Mod1 for next keypress
static int console_esc_seq = 0;
static uint canvas_pixels[X6_CANVAS_ROWS][X6_CANVAS_COLS];
static int canvas_ready;
static uint x6_conn_seq;
static uint x6_draw_rect_count;
static int x6_backend_pref = X6_BACKEND_AUTO;
static int x6_backend = X6_BACKEND_ANSI;
static int x6_backend_claimed = 0;
static int x6_console_nonblock_ready = 0;
static int x6_console_input_enabled = 0;
static int x6_mouse_fd = -1;
static int pointer_x;
static int pointer_y;
static uint pointer_state;

struct x6_fb_state {
  int fd;
  int width;
  int height;
  int stride;
  int bpp;
  uint *rowbuf;
  int rowcap;
};

static struct x6_fb_state x6_fb = { -1, 0, 0, 0, 0, 0, 0 };

static int x6_event_queue_enqueue(struct x6_event_queue *q, struct x6_event *evt);

static int
x6_parse_backend(const char *s)
{
  if(!s)
    return -1;
  if(strcmp(s, "auto") == 0)
    return X6_BACKEND_AUTO;
  if(strcmp(s, "ansi") == 0)
    return X6_BACKEND_ANSI;
  if(strcmp(s, "fb") == 0 || strcmp(s, "framebuffer") == 0)
    return X6_BACKEND_FB;
  return -1;
}

static int
x6_clamp_int(int v, int lo, int hi)
{
  if(v < lo)
    return lo;
  if(v > hi)
    return hi;
  return v;
}

static struct x6_event_queue *
x6_target_event_queue(void)
{
  if(wm_event_queue)
    return wm_event_queue;
  return current_event_queue;
}

static struct x6_window *
x6_pick_window_at(int px, int py)
{
  int i;
  struct x6_window *best;

  best = 0;
  for(i = 0; i < X6_MAX_WINDOWS; i++) {
    struct x6_window *w;
    if(!wins[i].in_use || !wins[i].mapped)
      continue;
    w = &wins[i];
    if(px < w->x || py < w->y)
      continue;
    if(px >= w->x + w->w || py >= w->y + w->h)
      continue;
    best = w;
  }
  return best;
}

static void
x6_screen_size(int *w, int *h)
{
  if(w == 0 || h == 0)
    return;

  if(x6_backend == X6_BACKEND_FB && x6_fb.width > 0 && x6_fb.height > 0) {
    *w = x6_fb.width;
    *h = x6_fb.height;
    return;
  }

  *w = X6_CANVAS_COLS * X6_CELL_W;
  *h = X6_CANVAS_ROWS * X6_CELL_H;
}

static void
x6_color_to_rgb(uint pixel, int *r, int *g, int *b)
{
  if(r) *r = (int)((pixel >> 16) & 0xff);
  if(g) *g = (int)((pixel >> 8) & 0xff);
  if(b) *b = (int)(pixel & 0xff);
}

static void
x6_canvas_init(void)
{
  if(x6_backend != X6_BACKEND_ANSI)
    return;
  if(canvas_ready)
    return;
  memset(canvas_pixels, 0, sizeof(canvas_pixels));
  dprintf(1, "\033[2J\033[H\033[?25l");
  canvas_ready = 1;
}

static void
x6_canvas_flush_rows(int row0, int row1)
{
  int r, c;

  if(!canvas_ready)
    return;

  row0 = x6_clamp_int(row0, 0, X6_CANVAS_ROWS - 1);
  row1 = x6_clamp_int(row1, 0, X6_CANVAS_ROWS - 1);
  if(row1 < row0)
    return;

  for(r = row0; r <= row1; r++) {
    uint last = ~0U;
    dprintf(1, "\033[%d;1H", r + 1);
    for(c = 0; c < X6_CANVAS_COLS; c++) {
      uint px = canvas_pixels[r][c];
      if(px != last) {
        int rr, gg, bb;
        x6_color_to_rgb(px, &rr, &gg, &bb);
        dprintf(1, "\033[48;2;%d;%d;%dm", rr, gg, bb);
        last = px;
      }
      dprintf(1, " ");
    }
    dprintf(1, "\033[0m");
  }
}

static void
x6_canvas_fill_pixels(int x, int y, int w, int h, uint pixel)
{
  int c0, c1, r0, r1;
  int r, c;
  int x0, y0, x1, y1;

  if(w <= 0 || h <= 0)
    return;

  if(x6_backend == X6_BACKEND_FB && x6_fb.fd >= 0) {
    int i;
    int rw;
    uint p;

    x0 = x;
    y0 = y;
    x1 = x + w;
    y1 = y + h;
    if(x0 < 0) x0 = 0;
    if(y0 < 0) y0 = 0;
    if(x1 > x6_fb.width) x1 = x6_fb.width;
    if(y1 > x6_fb.height) y1 = x6_fb.height;
    if(x1 <= x0 || y1 <= y0)
      return;

    rw = x1 - x0;
    if(rw > x6_fb.rowcap) {
      uint *nbuf;
      nbuf = (uint *)malloc((size_t)rw * sizeof(uint));
      if(!nbuf)
        return;
      if(x6_fb.rowbuf)
        free(x6_fb.rowbuf);
      x6_fb.rowbuf = nbuf;
      x6_fb.rowcap = rw;
    }

    p = pixel & 0x00ffffffU;
    for(i = 0; i < rw; i++)
      x6_fb.rowbuf[i] = p;

    for(i = y0; i < y1; i++) {
      uint64_t off;
      off = (uint64_t)i * (uint64_t)x6_fb.stride + (uint64_t)x0 * 4ULL;
      if(lseek(x6_fb.fd, (off_t)off, SEEK_SET) < 0)
        break;
      if(write(x6_fb.fd, x6_fb.rowbuf, rw * (int)sizeof(uint)) < 0)
        break;
    }
    return;
  }

  x6_canvas_init();

  c0 = x / X6_CELL_W;
  c1 = (x + w + X6_CELL_W - 1) / X6_CELL_W;
  r0 = y / X6_CELL_H;
  r1 = (y + h + X6_CELL_H - 1) / X6_CELL_H;

  c0 = x6_clamp_int(c0, 0, X6_CANVAS_COLS);
  c1 = x6_clamp_int(c1, 0, X6_CANVAS_COLS);
  r0 = x6_clamp_int(r0, 0, X6_CANVAS_ROWS);
  r1 = x6_clamp_int(r1, 0, X6_CANVAS_ROWS);
  if(c1 <= c0 || r1 <= r0)
    return;

  for(r = r0; r < r1; r++)
    for(c = c0; c < c1; c++)
      canvas_pixels[r][c] = pixel;

  x6_canvas_flush_rows(r0, r1 - 1);
}

static void __attribute__((unused))
x6_enqueue_keypress(uint keycode, uint state)
{
  struct x6_event evt;
  struct x6_event_queue *target_q;
  uint target;

  target_q = x6_target_event_queue();
  if(target_q == 0)
    return;

  if(wm_has_kb_grab && keyboard_grab_owner != 0)
    target = keyboard_grab_owner;
  else if(focus_wid != 0)
    target = focus_wid;
  else
    target = wm_redirect_root;

  evt.type = X6_EVENT_KEY_PRESS;
  evt.wid = target;
  evt.keycode = (int)keycode;
  evt.state = state;
  evt.x = pointer_x;
  evt.y = pointer_y;
  evt.w = evt.h = 0;
  evt.button = 0;
  x6_event_queue_enqueue(target_q, &evt);
}

static void
x6_enqueue_pointer_event(int type, int button)
{
  struct x6_event evt;
  struct x6_event_queue *target_q;
  struct x6_window *hit;

  target_q = x6_target_event_queue();
  if(target_q == 0)
    return;

  hit = x6_pick_window_at(pointer_x, pointer_y);
  evt.type = type;
  evt.wid = hit ? hit->id : wm_redirect_root;
  evt.x = pointer_x;
  evt.y = pointer_y;
  evt.w = evt.h = 0;
  evt.state = pointer_state;
  evt.keycode = 0;
  evt.button = button;
  x6_event_queue_enqueue(target_q, &evt);
}

static void
x6_move_pointer(int dx, int dy)
{
  int sw;
  int sh;

  x6_screen_size(&sw, &sh);
  pointer_x = x6_clamp_int(pointer_x + dx, 0, sw - 1);
  pointer_y = x6_clamp_int(pointer_y + dy, 0, sh - 1);
  x6_enqueue_pointer_event(X6_EVENT_MOTION_NOTIFY, 0);
}

static void __attribute__((unused))
x6_pump_console_input(void)
{
  char buf[32];
  int n;
  int i;

  n = read(0, buf, sizeof(buf));
  if(n <= 0)
    return;

  for(i = 0; i < n; i++) {
    uchar ch;
    uint keycode;
    uint state;

    ch = (uchar)buf[i];
    if(console_esc_seq == 1) {
      if(ch == '[') {
        console_esc_seq = 2;
        continue;
      }
      console_alt_prefix = 1;
      console_esc_seq = 0;
    }
    if(console_esc_seq == 2) {
      if(ch == 'A') {
        x6_move_pointer(0, -8);
        console_esc_seq = 0;
        continue;
      }
      if(ch == 'B') {
        x6_move_pointer(0, 8);
        console_esc_seq = 0;
        continue;
      }
      if(ch == 'C') {
        x6_move_pointer(8, 0);
        console_esc_seq = 0;
        continue;
      }
      if(ch == 'D') {
        x6_move_pointer(-8, 0);
        console_esc_seq = 0;
        continue;
      }
      console_esc_seq = 0;
    }
    if(ch == 27) {
      console_esc_seq = 1;
      continue;
    }

    state = console_alt_prefix ? X6_MOD1_MASK : 0;
    console_alt_prefix = 0;

    if(ch == '\r' || ch == '\n')
      keycode = 13;
    else if(ch == 8 || ch == 127)
      keycode = 8;
    else if(ch == ' ')
      keycode = 32;
    else if(ch == 'm') {
      pointer_state |= 0x0100U;
      x6_enqueue_pointer_event(X6_EVENT_BUTTON_PRESS, 1);
      continue;
    } else if(ch == 'n') {
      pointer_state &= ~0x0100U;
      x6_enqueue_pointer_event(X6_EVENT_BUTTON_RELEASE, 1);
      continue;
    }
    else
      keycode = (uint)ch;

    x6_enqueue_keypress(keycode, state);
  }
}

static void
x6_console_input_setup(void)
{
  int flags;

  if(x6_console_nonblock_ready)
    return;

  flags = fcntl(0, F_GETFL, 0);
  if(flags >= 0 && fcntl(0, F_SETFL, flags | O_NONBLOCK) >= 0)
    x6_console_input_enabled = 1;

  x6_console_nonblock_ready = 1;
}

static int
x6_console_input_pending(void)
{
  int n;

  if(!x6_console_input_enabled)
    return 0;
  n = 0;
  if(ioctl(0, 0x541B, &n) < 0)
    return 0;
  return n > 0 ? n : 0;
}

static void
x6_mouse_setup(void)
{
  if(x6_mouse_fd >= 0)
    return;
  x6_mouse_fd = open("/dev/mouse0", O_RDONLY);
  if(x6_mouse_fd < 0) {
    mknod("/dev/mouse0", M_IFCHR | 0600, X6_CONSOLE_MAJOR, X6_CONSOLE_MINOR_MOUSE0);
    x6_mouse_fd = open("/dev/mouse0", O_RDONLY);
  }
}

static void
x6_pump_mouse(void)
{
  struct aux_mouse_event evt;
  int n;
  int bit;

  if(x6_mouse_fd < 0)
    return;

  n = read(x6_mouse_fd, &evt, sizeof(evt));
  if(n != (int)sizeof(evt))
    return;

  if(evt.dx != 0 || evt.dy != 0)
    x6_move_pointer((int)evt.dx, -(int)evt.dy);

  for(bit = 0; bit < 3; bit++) {
    uint mask;

    mask = (uint)(1U << bit);
    if((evt.changed & (uchar)mask) == 0)
      continue;
    if(evt.buttons & mask) {
      pointer_state |= mask;
      x6_enqueue_pointer_event(X6_EVENT_BUTTON_PRESS, bit + 1);
    } else {
      pointer_state &= ~mask;
      x6_enqueue_pointer_event(X6_EVENT_BUTTON_RELEASE, bit + 1);
    }
  }
}

static void
usage(void)
{
  dprintf(2, "usage: x6 [-f] [-p port] [-B auto|ansi|fb]\n");
  dprintf(2, "       -f   run in foreground (no daemonize)\n");
  dprintf(2, "       -p   listen port (default %d)\n", X6_DEFAULT_PORT);
  dprintf(2, "       -B   display backend selection\n");
  exit(1);
}

static int
daemonize_self(void)
{
  int pid;
  int fd;

  pid = fork();
  if(pid < 0)
    return -1;
  if(pid > 0)
    exit(0);

  if(setsid() < 0)
    return -1;

  pid = fork();
  if(pid < 0)
    return -1;
  if(pid > 0)
    exit(0);

  chdir("/");

  close(0);
  close(1);
  close(2);

  fd = open("/dev/console", O_RDWR);
  if(fd < 0)
    return 0;

  if(fd != 0) {
    dup2(fd, 0);
    close(fd);
  }
  dup(0);
  dup(0);

  return 0;
}

static void
on_term(int signo)
{
  if(signo == SIGTERM || signo == SIGINT)
    keep_running = 0;
}

static int
parse_port(const char *s)
{
  int p;

  if(s == 0)
    return -1;
  p = atoi(s);
  if(p < 1 || p > 65535)
    return -1;
  return p;
}

static int
x6_proc_write(const char *cmd)
{
  int fd;
  int n;

  fd = open(X6_PROC_PATH, O_RDWR);
  if(fd < 0)
    return -1;

  n = strlen(cmd);
  if(write(fd, (char *)cmd, n) != n) {
    close(fd);
    return -1;
  }

  close(fd);
  return 0;
}

static int
x6_claim_display(void)
{
  if(x6_proc_write("claim\n") < 0) {
    dprintf(2, "x6: warning: display claim unavailable; continuing without server7 claim\n");
    x6_backend_claimed = 0;
    return 0;
  }
  x6_backend_claimed = 1;
  return 0;
}

static void
x6_release_display(void)
{
  if(x6_backend_claimed)
    x6_proc_write("release\n");
}

static int
x6_fb_try_init(void)
{
  struct fb_var_screeninfo vinfo;
  struct fb_fix_screeninfo finfo;

  x6_fb.fd = open("/dev/fb0", O_RDWR);
  if(x6_fb.fd < 0) {
    // Self-heal if devman has not created /dev/fb0 yet.
    mknod("/dev/fb0", M_IFCHR | 0600, X6_CONSOLE_MAJOR, X6_CONSOLE_MINOR_FB0);
    x6_fb.fd = open("/dev/fb0", O_RDWR);
  }
  if(x6_fb.fd < 0) {
    dprintf(2, "x6: fb open failed for /dev/fb0\n");
    return -1;
  }

  if(ioctl(x6_fb.fd, X6_FBIOGET_VSCREENINFO, &vinfo) < 0) {
    dprintf(2, "x6: fb ioctl FBIOGET_VSCREENINFO failed\n");
    goto fail;
  }
  if(ioctl(x6_fb.fd, X6_FBIOGET_FSCREENINFO, &finfo) < 0) {
    dprintf(2, "x6: fb ioctl FBIOGET_FSCREENINFO failed\n");
    goto fail;
  }

  if(vinfo.xres == 0 || vinfo.yres == 0 || vinfo.bits_per_pixel != 32) {
    dprintf(2, "x6: fb geometry unsupported xres=%u yres=%u bpp=%u\n",
            vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
    goto fail;
  }

  x6_fb.width = (int)vinfo.xres;
  x6_fb.height = (int)vinfo.yres;
  x6_fb.stride = (int)finfo.line_length;
  x6_fb.bpp = (int)vinfo.bits_per_pixel;
  x6_backend = X6_BACKEND_FB;
  dprintf(1, "x6: framebuffer backend active %dx%d stride=%d bpp=%d\n",
          x6_fb.width, x6_fb.height, x6_fb.stride, x6_fb.bpp);
  return 0;

fail:
  close(x6_fb.fd);
  x6_fb.fd = -1;
  return -1;
}

static void
x6_fb_shutdown(void)
{
  if(x6_fb.fd >= 0)
    close(x6_fb.fd);
  x6_fb.fd = -1;
  if(x6_fb.rowbuf)
    free(x6_fb.rowbuf);
  x6_fb.rowbuf = 0;
  x6_fb.rowcap = 0;
}

static int
x6_init_backend(void)
{
  if(x6_backend_pref == X6_BACKEND_ANSI) {
    x6_backend = X6_BACKEND_ANSI;
    dprintf(1, "x6: backend=ansi (forced)\n");
    return 0;
  }

  if(x6_backend_pref == X6_BACKEND_FB) {
    if(x6_fb_try_init() < 0) {
      dprintf(2, "x6: framebuffer backend requested but unavailable\n");
      return -1;
    }
    return 0;
  }

  if(x6_fb_try_init() == 0)
    return 0;

  x6_backend = X6_BACKEND_ANSI;
  dprintf(1, "x6: backend=ansi (framebuffer unavailable)\n");
  return 0;
}

static void
x6_send_line(int cfd, const char *s)
{
  if(cfd < 0 || s == 0)
    return;
  send(cfd, (void *)s, strlen(s));
}

static int
x6_recv_line(int cfd, char *buf, int buflen)
{
  int pos;

  if(cfd < 0 || buf == 0 || buflen <= 1)
    return -1;

  pos = 0;
  while(pos < buflen - 1) {
    char ch;
    int n;
    n = recv(cfd, &ch, 1);
    if(n <= 0)
      return -1;
    if(ch == '\n' || ch == '\r') {
      buf[pos] = 0;
      return 0;
    }
    buf[pos++] = ch;
  }

  buf[pos] = 0;
  return 0;
}

static struct x6_window *
find_window(uint id)
{
  int i;

  for(i = 0; i < X6_MAX_WINDOWS; i++) {
    if(wins[i].in_use && wins[i].id == id)
      return &wins[i];
  }
  return 0;
}

static struct x6_window *
alloc_window(uint id)
{
  int i;

  for(i = 0; i < X6_MAX_WINDOWS; i++) {
    if(!wins[i].in_use) {
      wins[i].in_use = 1;
      wins[i].id = id;
      wins[i].owner_fd = -1;
      wins[i].x = 0;
      wins[i].y = 0;
      wins[i].w = 1;
      wins[i].h = 1;
      wins[i].mapped = 0;
      wins[i].prop_count = 0;  // Initialize properties (Phase 2.1d)
      return &wins[i];
    }
  }
  return 0;
}

static void
destroy_window(uint id)
{
  struct x6_window *w;

  w = find_window(id);
  if(w == 0)
    return;
  memset(w, 0, sizeof(*w));
}

static void
x6_event_queue_init(struct x6_event_queue *q)
{
  q->head = 0;
  q->tail = 0;
}

static int
x6_event_queue_empty(struct x6_event_queue *q)
{
  return q->head == q->tail;
}

static int
x6_event_queue_enqueue(struct x6_event_queue *q, struct x6_event *evt)
{
  int next_tail;

  next_tail = (q->tail + 1) % X6_MAX_EVENTS_PER_CLIENT;
  if(next_tail == q->head)
    return -1; // Queue full, drop oldest
  q->events[q->tail] = *evt;
  q->tail = next_tail;
  return 0;
}

static int
x6_event_queue_dequeue(struct x6_event_queue *q, struct x6_event *evt)
{
  if(q->head == q->tail)
    return -1; // Empty
  *evt = q->events[q->head];
  q->head = (q->head + 1) % X6_MAX_EVENTS_PER_CLIENT;
  return 0;
}

// Find a property in a window's property array by name
// Returns pointer to property or NULL if not found
static struct x6_property *
x6_find_property(struct x6_window *win, const char *atom)
{
  int i;
  if(!win) return 0;
  for(i = 0; i < win->prop_count; i++) {
    if(strcmp(win->props[i].name, atom) == 0) {
      return &win->props[i];
    }
  }
  return 0;
}

static void
handle_one_command(int cfd, char *cmd)
{
  uint id;
  uint color;
  uint uw, uh;
  int x, y, w, h;
  struct x6_window *win;
  int i;
  int listed;

  if(strncmp(cmd, "HELLO x6/1", 10) == 0) {
    char out[128];
    int sw;
    int sh;
    x6_screen_size(&sw, &sh);
    snprintf(out, sizeof(out),
             "OK proto=%d transport=tcp-loopback screen=0 root=1 visual=truecolor depth=32 width=%d height=%d\n",
             X6_PROTO_VERSION, sw, sh);
    x6_send_line(cfd, out);
    return;
  }

  if(strncmp(cmd, "PING", 4) == 0) {
    x6_send_line(cfd, "PONG\n");
    return;
  }

  if(strncmp(cmd, "DETACH", 6) == 0) {
    x6_send_line(cfd, "BYE\n");
    return;
  }

  if(strncmp(cmd, "QUIT", 4) == 0) {
    x6_send_line(cfd, "BYE\n");
    keep_running = 0;
    return;
  }

  if(sscanf(cmd, "CREATE %u %d %d %d %d", &id, &x, &y, &w, &h) == 5) {
    if(find_window(id) != 0) {
      x6_send_line(cfd, "ERR exists\n");
      return;
    }
    win = alloc_window(id);
    if(win == 0) {
      x6_send_line(cfd, "ERR no-slots\n");
      return;
    }
    if(w < 1)
      w = 1;
    if(h < 1)
      h = 1;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->owner_fd = cfd;
    x6_send_line(cfd, "OK create\n");
    return;
  }

  if(sscanf(cmd, "MAP %u", &id) == 1) {
    win = find_window(id);
    if(win == 0) {
      x6_send_line(cfd, "ERR not-found\n");
      return;
    }
    
    // Phase 2.1b: If WM holds SubstructureRedirect, queue MapRequest for WM approval
    if(wm_has_redirect && cfd != wm_client_fd && win->owner_fd != wm_client_fd) {
      struct x6_event evt;
      evt.type = X6_EVENT_MAP_REQUEST;
      evt.wid = id;
      if(wm_event_queue != 0) {
        x6_event_queue_enqueue(wm_event_queue, &evt);
      }
      x6_send_line(cfd, "PENDING map\n");  // Client is notified of pending state
      return;
    }
    
    // Otherwise, map directly
    win->mapped = 1;
    x6_send_line(cfd, "OK map\n");
    return;
  }

  if(sscanf(cmd, "UNMAP %u", &id) == 1) {
    win = find_window(id);
    if(win == 0) {
      x6_send_line(cfd, "ERR not-found\n");
      return;
    }
    win->mapped = 0;
    x6_send_line(cfd, "OK unmap\n");
    return;
  }

  if(sscanf(cmd, "CONFIGURE %u %d %d %d %d", &id, &x, &y, &w, &h) == 5) {
    win = find_window(id);
    if(win == 0) {
      x6_send_line(cfd, "ERR not-found\n");
      return;
    }
    
    // Phase 2.1b: If WM holds SubstructureRedirect, queue ConfigureRequest for WM approval
    if(wm_has_redirect && cfd != wm_client_fd && win->owner_fd != wm_client_fd) {
      struct x6_event evt;
      evt.type = X6_EVENT_CONFIGURE_REQUEST;
      evt.wid = id;
      evt.x = x;
      evt.y = y;
      evt.w = (w < 1) ? 1 : w;
      evt.h = (h < 1) ? 1 : h;
      if(wm_event_queue != 0) {
        x6_event_queue_enqueue(wm_event_queue, &evt);
      }
      x6_send_line(cfd, "PENDING configure\n");  // Client is notified of pending state
      return;
    }
    
    // Otherwise, configure directly
    if(w < 1)
      w = 1;
    if(h < 1)
      h = 1;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    x6_send_line(cfd, "OK configure\n");
    return;
  }

  if(sscanf(cmd, "DESTROY %u", &id) == 1) {
    destroy_window(id);
    x6_send_line(cfd, "OK destroy\n");
    return;
  }

  if(sscanf(cmd, "DRAW_RECT %u %d %d %u %u %u", &id, &x, &y, &uw, &uh, &color) == 6) {
    int ox = 0;
    int oy = 0;
    if(uw == 0 || uh == 0 || uw > 4096U || uh > 4096U) {
      x6_send_line(cfd, "OK draw\n");
      return;
    }
    w = (int)uw;
    h = (int)uh;
    if(id != (uint)wm_redirect_root) {
      win = find_window(id);
      if(win == 0) {
        x6_send_line(cfd, "ERR not-found\n");
        return;
      }
      ox = win->x;
      oy = win->y;
    }
    x6_canvas_fill_pixels(ox + x, oy + y, w, h, color);
    x6_draw_rect_count++;
    if(x6_draw_rect_count <= 5) {
      dprintf(1, "x6: draw#%u wid=%u xy=%d,%d wh=%d,%d color=%u\n",
              x6_draw_rect_count, id, ox + x, oy + y, w, h, color);
    }
    x6_send_line(cfd, "OK draw\n");
    return;
  }

  // Phase 2.1b: REQUEST_REDIRECT for WM to claim SubstructureRedirect on root
  if(sscanf(cmd, "REQUEST_REDIRECT %u", &id) == 1) {
    if(id != wm_redirect_root) {
      x6_send_line(cfd, "ERR invalid-window\n");
      return;
    }
    if(wm_has_redirect) {
      x6_send_line(cfd, "ERR redirect-in-use\n");
      return;
    }
    wm_has_redirect = 1;
    wm_event_queue = current_event_queue;
    wm_client_fd = cfd;
    x6_send_line(cfd, "OK redirect_granted\n");
    return;
  }

  // Phase 2.1b: WM-specific CONFIGURE response to honor child ConfigureRequest
  // Format: WM_CONFIGURE <wid> <x> <y> <w> <h>
  // Different from client CONFIGURE which is denied if WM holds redirect
  if(sscanf(cmd, "WM_CONFIGURE %u %d %d %d %d", &id, &x, &y, &w, &h) == 5) {
    if(!wm_has_redirect) {
      x6_send_line(cfd, "ERR not-wm\n");
      return;
    }
    win = find_window(id);
    if(win == 0) {
      x6_send_line(cfd, "ERR not-found\n");
      return;
    }
    if(w < 1) w = 1;
    if(h < 1) h = 1;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    x6_send_line(cfd, "OK configured\n");
    return;
  }

  // WM-specific MAP response to honor child MapRequest
  if(sscanf(cmd, "WM_MAP %u", &id) == 1) {
    if(!wm_has_redirect) {
      x6_send_line(cfd, "ERR not-wm\n");
      return;
    }
    win = find_window(id);
    if(win == 0) {
      x6_send_line(cfd, "ERR not-found\n");
      return;
    }
    win->mapped = 1;
    x6_send_line(cfd, "OK mapped\n");
    return;
  }

  // WM-specific UNMAP response
  if(sscanf(cmd, "WM_UNMAP %u", &id) == 1) {
    if(!wm_has_redirect) {
      x6_send_line(cfd, "ERR not-wm\n");
      return;
    }
    win = find_window(id);
    if(win == 0) {
      x6_send_line(cfd, "ERR not-found\n");
      return;
    }
    win->mapped = 0;
    x6_send_line(cfd, "OK unmapped\n");
    return;
  }

  if(sscanf(cmd, "QUEUE_EVENT %d %u", &x, &id) == 2) {
    // Test command: manually queue an event for testing infrastructure (Phase 2.1a)
    if(current_event_queue == 0) {
      x6_send_line(cfd, "ERR not-ready\n");
      return;
    }
    struct x6_event evt;
    evt.type = x;  // x is reused as event type here
    evt.wid = id;
    evt.x = evt.y = evt.w = evt.h = 0;
    if(x6_event_queue_enqueue(current_event_queue, &evt) < 0) {
      x6_send_line(cfd, "ERR queue-full\n");
      return;
    }
    x6_send_line(cfd, "OK queued\n");
    return;
  }

  // Synthetic input injection helpers for MVP bring-up.
  if(sscanf(cmd, "INJECT_KEY %u %d %d", &id, &x, &y) == 3) {
    struct x6_event evt;
    if(current_event_queue == 0) {
      x6_send_line(cfd, "ERR not-ready\n");
      return;
    }
    evt.type = X6_EVENT_KEY_PRESS;
    evt.wid = id;
    evt.keycode = x;
    evt.state = (uint)y;
    evt.x = evt.y = evt.w = evt.h = 0;
    evt.button = 0;
    if(x6_event_queue_enqueue(current_event_queue, &evt) < 0) {
      x6_send_line(cfd, "ERR queue-full\n");
      return;
    }
    x6_send_line(cfd, "OK key_injected\n");
    return;
  }

  if(sscanf(cmd, "INJECT_BUTTON %u %d %d %d %d", &id, &x, &y, &w, &h) == 5) {
    struct x6_event evt;
    if(current_event_queue == 0) {
      x6_send_line(cfd, "ERR not-ready\n");
      return;
    }
    // x=button, y=state, w=px, h=py
    evt.type = X6_EVENT_BUTTON_PRESS;
    evt.wid = id;
    evt.button = x;
    evt.state = (uint)y;
    evt.x = w;
    evt.y = h;
    evt.keycode = 0;
    evt.w = evt.h = 0;
    if(x6_event_queue_enqueue(current_event_queue, &evt) < 0) {
      x6_send_line(cfd, "ERR queue-full\n");
      return;
    }
    x6_send_line(cfd, "OK button_injected\n");
    return;
  }

  if(sscanf(cmd, "INJECT_MOTION %u %d %d %d", &id, &x, &y, &w) == 4) {
    struct x6_event evt;
    if(current_event_queue == 0) {
      x6_send_line(cfd, "ERR not-ready\n");
      return;
    }
    // x=px, y=py, w=state
    evt.type = X6_EVENT_MOTION_NOTIFY;
    evt.wid = id;
    evt.x = x;
    evt.y = y;
    evt.state = (uint)w;
    evt.keycode = 0;
    evt.button = 0;
    evt.w = evt.h = 0;
    if(x6_event_queue_enqueue(current_event_queue, &evt) < 0) {
      x6_send_line(cfd, "ERR queue-full\n");
      return;
    }
    x6_send_line(cfd, "OK motion_injected\n");
    return;
  }

  if(strncmp(cmd, "LIST", 4) == 0) {
    char out[128];
    listed = 0;
    for(i = 0; i < X6_MAX_WINDOWS; i++) {
      if(!wins[i].in_use)
        continue;
      snprintf(out, sizeof(out), "WIN id=%u map=%d geom=%d,%d %dx%d\n",
               wins[i].id,
               wins[i].mapped,
               wins[i].x,
               wins[i].y,
               wins[i].w,
               wins[i].h);
      x6_send_line(cfd, out);
      listed++;
    }
    snprintf(out, sizeof(out), "OK list count=%d\n", listed);
    x6_send_line(cfd, out);
    return;
  }

  // Phase 2.1c: Focus and keyboard grab
  if(sscanf(cmd, "SET_FOCUS %u", &id) == 1) {
    struct x6_event evt;
    uint old_focus = focus_wid;
    
    // Allow both WM and clients to set focus
    focus_wid = id;
    
    // Queue FocusOut for old focus window FIRST
    if(current_event_queue != 0 && old_focus != 0 && old_focus != focus_wid) {
      evt.type = X6_EVENT_FOCUS_OUT;
      evt.wid = old_focus;
      x6_event_queue_enqueue(current_event_queue, &evt);
    }
    
    // Queue FocusIn for new focus window AFTER
    if(current_event_queue != 0 && focus_wid != 0) {
      evt.type = X6_EVENT_FOCUS_IN;
      evt.wid = focus_wid;
      x6_event_queue_enqueue(current_event_queue, &evt);
    }
    
    x6_send_line(cfd, "OK focused\n");
    return;
  }

  if(strncmp(cmd, "GRAB_KEYBOARD", 13) == 0) {
    // Only WM (client with redirect) can grab keyboard
    if(!wm_has_redirect) {
      x6_send_line(cfd, "ERR permission-denied\n");
      return;
    }
    if(wm_has_kb_grab) {
      x6_send_line(cfd, "ERR already-grabbed\n");
      return;
    }
    wm_has_kb_grab = 1;
    keyboard_grab_owner = wm_redirect_root;
    x6_send_line(cfd, "OK grab_active\n");
    return;
  }

  if(strncmp(cmd, "GRAB_POINTER", 12) == 0) {
    x6_send_line(cfd, "OK pointer_grabbed\n");
    return;
  }

  if(strncmp(cmd, "UNGRAB_POINTER", 14) == 0) {
    x6_send_line(cfd, "OK pointer_ungrabbed\n");
    return;
  }

  if(sscanf(cmd, "WARP_POINTER %d %d", &x, &y) == 2) {
    int sw;
    int sh;
    x6_screen_size(&sw, &sh);
    pointer_x = x6_clamp_int(x, 0, sw - 1);
    pointer_y = x6_clamp_int(y, 0, sh - 1);
    x6_send_line(cfd, "OK pointer_warped\n");
    return;
  }

  if(strncmp(cmd, "QUERY_POINTER", 13) == 0) {
    char out[128];
    struct x6_window *hit;
    hit = x6_pick_window_at(pointer_x, pointer_y);
    snprintf(out, sizeof(out), "OK pointer root=1 child=%u x=%d y=%d state=%u\n",
             hit ? hit->id : 0, pointer_x, pointer_y, pointer_state);
    x6_send_line(cfd, out);
    return;
  }

  if(strncmp(cmd, "UNGRAB_KEYBOARD", 15) == 0) {
    // Only WM can release
    if(!wm_has_redirect) {
      x6_send_line(cfd, "ERR permission-denied\n");
      return;
    }
    if(!wm_has_kb_grab) {
      x6_send_line(cfd, "ERR not-grabbed\n");
      return;
    }
    wm_has_kb_grab = 0;
    keyboard_grab_owner = 0;
    x6_send_line(cfd, "OK ungrab_done\n");
    return;
  }
  if(strncmp(cmd, "GET_PROPERTY", 12) == 0) {
    uint wid;
    char atom[128];
    struct x6_window *win;
    struct x6_property *prop;

    // Parse: GET_PROPERTY <wid> <atom>
    if(sscanf(cmd, "GET_PROPERTY %u %127s", &wid, atom) != 2) {
      x6_send_line(cfd, "ERR bad-syntax\n");
      return;
    }

    win = find_window(wid);
    if(!win) {
      x6_send_line(cfd, "ERR no-such-window\n");
      return;
    }

    prop = x6_find_property(win, atom);
    if(!prop) {
      x6_send_line(cfd, "ERR no-such-property\n");
      return;
    }

    // Return property value as VALUE <atom> <value>
    char out[512];
    snprintf(out, sizeof(out), "VALUE %s %s\n", atom, prop->value);
    x6_send_line(cfd, out);
    return;
  }

  if(strncmp(cmd, "SET_PROPERTY", 12) == 0) {
    uint wid;
    char atom[128];
    char value[512];
    struct x6_window *win;
    struct x6_property *prop;

    // Parse: SET_PROPERTY <wid> <atom> <value>
    if(sscanf(cmd, "SET_PROPERTY %u %127s %511s", &wid, atom, value) != 3) {
      x6_send_line(cfd, "ERR bad-syntax\n");
      return;
    }

    win = find_window(wid);
    if(!win) {
      x6_send_line(cfd, "ERR no-such-window\n");
      return;
    }

    // Find existing property or add new one
    prop = x6_find_property(win, atom);
    if(prop) {
      // Update existing property
      strncpy(prop->value, value, X6_MAX_PROP_VALUE - 1);
      prop->value[X6_MAX_PROP_VALUE - 1] = '\0';
    } else {
      // Add new property if we have space
      if(win->prop_count >= X6_MAX_PROPERTIES_PER_WINDOW) {
        x6_send_line(cfd, "ERR no-property-space\n");
        return;
      }
      prop = &win->props[win->prop_count];
      strncpy(prop->name, atom, X6_MAX_PROP_NAME - 1);
      prop->name[X6_MAX_PROP_NAME - 1] = '\0';
      strncpy(prop->value, value, X6_MAX_PROP_VALUE - 1);
      prop->value[X6_MAX_PROP_VALUE - 1] = '\0';
      win->prop_count++;
    }

    x6_send_line(cfd, "OK property_set\n");
    return;
  }
  x6_send_line(cfd, "ERR unknown\n");
}

static void
handle_client(int cfd)
{
  char line[192];
  char eventbuf[256];
  struct x6_event_queue q;
  struct x6_event evt;
  int logged_first_cmd;

  logged_first_cmd = 0;

  x6_event_queue_init(&q);
  current_event_queue = &q;

  x6_send_line(cfd, "X6/1 READY\n");
  
  while(keep_running) {
    struct pollfd pfds[3];
    int nfds;
    int pr;
    int mouse_poll_index;

    // Drain any queued events and send them to client
    while(!x6_event_queue_empty(&q)) {
      if(x6_event_queue_dequeue(&q, &evt) == 0) {
        if(evt.type == X6_EVENT_MAP_REQUEST) {
          snprintf(eventbuf, sizeof(eventbuf), "EVENT MapRequest wid=%u\n", evt.wid);
        } else if(evt.type == X6_EVENT_CONFIGURE_REQUEST) {
          snprintf(eventbuf, sizeof(eventbuf), 
                   "EVENT ConfigureRequest wid=%u geom=%d,%d %dx%d\n",
                   evt.wid, evt.x, evt.y, evt.w, evt.h);
        } else if(evt.type == X6_EVENT_FOCUS_IN) {
          snprintf(eventbuf, sizeof(eventbuf), "EVENT FocusIn wid=%u\n", evt.wid);
        } else if(evt.type == X6_EVENT_FOCUS_OUT) {
          snprintf(eventbuf, sizeof(eventbuf), "EVENT FocusOut wid=%u\n", evt.wid);
        } else if(evt.type == X6_EVENT_DESTROY_NOTIFY) {
          snprintf(eventbuf, sizeof(eventbuf), "EVENT DestroyNotify wid=%u\n", evt.wid);
        } else if(evt.type == X6_EVENT_KEY_PRESS) {
          snprintf(eventbuf, sizeof(eventbuf), "EVENT KeyPress wid=%u keycode=%d state=%u\n",
                   evt.wid, evt.keycode, evt.state);
        } else if(evt.type == X6_EVENT_BUTTON_PRESS) {
          snprintf(eventbuf, sizeof(eventbuf), "EVENT ButtonPress wid=%u button=%d state=%u x=%d y=%d\n",
                   evt.wid, evt.button, evt.state, evt.x, evt.y);
        } else if(evt.type == X6_EVENT_BUTTON_RELEASE) {
          snprintf(eventbuf, sizeof(eventbuf), "EVENT ButtonRelease wid=%u button=%d state=%u x=%d y=%d\n",
                   evt.wid, evt.button, evt.state, evt.x, evt.y);
        } else if(evt.type == X6_EVENT_MOTION_NOTIFY) {
          snprintf(eventbuf, sizeof(eventbuf), "EVENT MotionNotify wid=%u x=%d y=%d state=%u\n",
                   evt.wid, evt.x, evt.y, evt.state);
        } else {
          continue;
        }
        x6_send_line(cfd, eventbuf);
      }
    }

    nfds = 0;
    pfds[nfds].fd = cfd;
    pfds[nfds].events = POLLIN;
    pfds[nfds].revents = 0;
    nfds++;
    mouse_poll_index = -1;
    if(x6_mouse_fd >= 0) {
      mouse_poll_index = nfds;
      pfds[nfds].fd = x6_mouse_fd;
      pfds[nfds].events = POLLIN;
      pfds[nfds].revents = 0;
      nfds++;
    }

    pr = poll(pfds, (nfds_t)nfds, 50);
    if(pr < 0)
      break;
    if(pr == 0)
      continue;

    if(x6_console_input_pending() > 0)
      x6_pump_console_input();
    if(mouse_poll_index >= 0 && (pfds[mouse_poll_index].revents & POLLIN))
      x6_pump_mouse();

    if((pfds[0].revents & POLLIN) == 0)
      continue;

    if(x6_recv_line(cfd, line, sizeof(line)) < 0)
      break;

    if(line[0] == 0)
      continue;
    if(!logged_first_cmd) {
      dprintf(1, "x6: first-cmd: %s\n", line);
      logged_first_cmd = 1;
    }
    handle_one_command(cfd, line);
    if(strncmp(line, "QUIT", 4) == 0 || strncmp(line, "DETACH", 6) == 0)
      break;
  }

  if(wm_event_queue == &q) {
    wm_event_queue = 0;
    wm_client_fd = -1;
    wm_has_redirect = 0;
    wm_has_kb_grab = 0;
    keyboard_grab_owner = 0;
  }

  current_event_queue = 0;
}

int
main(int argc, char **argv)
{
  int i;
  int foreground;
  int port;
  int fd;
  struct sockaddr_in src;
  struct sigaction sa;

  foreground = 0;
  port = X6_DEFAULT_PORT;

  for(i = 1; i < argc; i++) {
    if(strcmp(argv[i], "-f") == 0) {
      foreground = 1;
      continue;
    }
    if(strcmp(argv[i], "-p") == 0) {
      if(i + 1 >= argc)
        usage();
      port = parse_port(argv[++i]);
      if(port < 0)
        usage();
      continue;
    }
    if(strcmp(argv[i], "-B") == 0) {
      int b;
      if(i + 1 >= argc)
        usage();
      b = x6_parse_backend(argv[++i]);
      if(b < 0)
        usage();
      x6_backend_pref = b;
      continue;
    }
    usage();
  }

  if(!foreground) {
    if(daemonize_self() < 0) {
      dprintf(2, "x6: daemonize failed\n");
      exit(1);
    }
  }

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_term;
  sigaction(SIGTERM, &sa, 0);
  sigaction(SIGINT, &sa, 0);

  if(x6_claim_display() < 0) {
    dprintf(2, "x6: display claim failed via %s\n", X6_PROC_PATH);
    exit(1);
  }

  if(x6_init_backend() < 0) {
    x6_release_display();
    exit(1);
  }

  x6_console_input_setup();
  x6_mouse_setup();
  pointer_x = x6_fb.width > 0 ? (x6_fb.width / 2) : 0;
  pointer_y = x6_fb.height > 0 ? (x6_fb.height / 2) : 0;
  pointer_state = 0;

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) {
    x6_release_display();
    dprintf(2, "x6: socket failed\n");
    exit(1);
  }

  memset(&src, 0, sizeof(src));
  src.sin_family = AF_INET;
  src.sin_port = (ushort)port;
  src.sin_addr = INADDR_LOOPBACK;

  if(bind(fd, &src, sizeof(src)) < 0) {
    dprintf(2, "x6: bind failed on 127.0.0.1:%d\n", port);
    close(fd);
    x6_release_display();
    exit(1);
  }

  if(listen(fd, X6_BACKLOG) < 0) {
    dprintf(2, "x6: listen failed\n");
    close(fd);
    x6_release_display();
    exit(1);
  }

  dprintf(1, "x6: phase1 skeleton active proto=%d on 127.0.0.1:%d\n",
          X6_PROTO_VERSION, port);

  while(keep_running) {
    int cfd;

    cfd = accept(fd);
    if(cfd < 0)
      continue;

    x6_conn_seq++;
    dprintf(1, "x6: client#%u connected\n", x6_conn_seq);

    handle_client(cfd);
    close(cfd);
  }

  close(fd);
  x6_fb_shutdown();
  x6_release_display();
  dprintf(1, "x6: exiting\n");
  return 0;
}
