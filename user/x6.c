#include "types.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "stat.h"
#include "sys/ioctl.h"
#include "graphics/drm_ioctls.h"
#include "graphics/input_events.h"
#include "graphics/user_font.h"
#include "errno.h"
#include "signal.h"
#include "socket.h"
#include "stdio.h"
#include "poll.h"
#include "termios.h"
#include <string.h>

#define XK_BackSpace 0xff08
#define XK_Return 0xff0d
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
#define X6_CLIENT_RXBUF 4096
#define X6_PROTO_VERSION 1
#define X6_PROC_PATH "/proc/server7"
#define X6_FBIOGET_VSCREENINFO 0x4600
#define X6_FBIOGET_FSCREENINFO 0x4602
#define X6_CONSOLE_MAJOR 1
#define X6_CONSOLE_MINOR_FB0 100
#define X6_CONSOLE_MINOR_MOUSE0 101
#define X6_CONSOLE_MINOR_KBD0 102

#define X6_MAX_WINDOWS 128
#define X6_MAX_EVENTS_PER_CLIENT 64
#define X6_MAX_CLIENTS 16
#define X6_MAX_KEY_GRABS 256
#define X6_MAX_BUTTON_GRABS 256
#define X6_MAX_PIXMAPS 32
#define X6_MAX_SELECTIONS 16
#define X6_ANY_MODIFIER (1U << 15)
#define X6_STATE_BUTTON1 (1U << 8)
#define X6_STATE_BUTTON2 (1U << 9)
#define X6_STATE_BUTTON3 (1U << 10)
#define X6_MASK_ENTER_WINDOW (1L << 4)
#define X6_MASK_LEAVE_WINDOW (1L << 5)
#define X6_MASK_PROPERTY_CHANGE (1L << 22)

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
#define X6_EVENT_CONFIGURE_NOTIFY 10
#define X6_EVENT_EXPOSE 11
#define X6_EVENT_CLIENT_MESSAGE 12
#define X6_EVENT_KEY_RELEASE 13
#define X6_EVENT_PROPERTY_NOTIFY 14
#define X6_EVENT_ENTER_NOTIFY 15
#define X6_EVENT_LEAVE_NOTIFY 16
#define X6_EVENT_SELECTION_CLEAR 17
#define X6_EVENT_SELECTION_REQUEST 18
#define X6_EVENT_SELECTION_NOTIFY 19
#define X6_EVENT_MAP_NOTIFY 20

struct x6_event {
  int type;
  uint wid;      // window ID
  int x, y, w, h; // geometry for configure requests
  int keycode;
  int button;
  uint state;
  uint data0;
  uint requestor;
  uint time;
  int mode;
  int detail;
  int focus;
  int same_screen;
  char atom[64];
  char target_atom[64];
  char property_atom[64];
};

struct x6_key_grab {
  int in_use;
  int owner_fd;
  uint wid;
  uint keycode;
  uint modifiers;
};

struct x6_button_grab {
  int in_use;
  int owner_fd;
  uint wid;
  uint button;
  uint modifiers;
};

struct x6_event_queue {
  struct x6_event events[X6_MAX_EVENTS_PER_CLIENT];
  int head;
  int tail;
};

// Property storage for windows (Phase 2.1d)
#define X6_MAX_PROPERTIES_PER_WINDOW 16
#define X6_MAX_PROP_NAME 64
#define X6_MAX_PROP_VALUE 3072

struct x6_property {
  char name[X6_MAX_PROP_NAME];
  char value[X6_MAX_PROP_VALUE];
};

struct x6_selection {
  int in_use;
  char name[64];
  uint owner;
  uint time;
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
  int border_width;
  uint border_pixel;
  int override_redirect;
  int cursor_set;
  uint cursor;
  int z;
  long event_mask;  /* X11 event mask registered via SELECT_EVENTS / XSelectInput */
  struct x6_property props[X6_MAX_PROPERTIES_PER_WINDOW];
  int prop_count;
};

struct x6_pixmap {
  int in_use;
  uint id;
  int width;
  int height;
  int depth;
  /* Pixel data: store as uint (assuming 32-bit color ARGB) */
  uint *pixels;
  int pixels_size;
};

struct x6_client {
  int in_use;
  int fd;
  struct x6_event_queue queue;
  char rxbuf[X6_CLIENT_RXBUF];
  int rxlen;
  int logged_first_cmd;
  int hello_done;
};

static volatile sig_atomic_t keep_running = 1;
static struct x6_window wins[X6_MAX_WINDOWS];
static struct x6_window *wins_by_id[256];  // Hash table for O(1) window lookup by ID (id % 256)
static struct x6_client clients[X6_MAX_CLIENTS];
static struct x6_key_grab key_grabs[X6_MAX_KEY_GRABS];
static struct x6_button_grab button_grabs[X6_MAX_BUTTON_GRABS];
static struct x6_pixmap pixmaps[X6_MAX_PIXMAPS];
static struct x6_selection selections[X6_MAX_SELECTIONS];
static uint x6_next_wid = 2;  // Server-assigned window IDs; 0=none, 1=root
static uint x6_next_pmid = 1001;  // Pixmap IDs start at 1001 (windows use < 1000)
static int x6_next_z = 1;

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
static uint canvas_pixels[X6_CANVAS_ROWS][X6_CANVAS_COLS];
static int canvas_ready;
static int x6_backend_pref = X6_BACKEND_AUTO;
static int x6_backend = X6_BACKEND_ANSI;
static int x6_backend_claimed = 0;
static int x6_mouse_fd = -1;
static int x6_kbd_fd = -1;
static int pointer_x;
static int pointer_y;
static uint pointer_state;
static uint keyboard_mod_state;
static uint x6_event_time;
static uint pointer_hover_wid;
static int pointer_grab_active;
static uint pointer_grab_window;
static uint root_cursor;

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
static uint *x6_fb_shadow;
static int x6_fb_shadow_w;
static int x6_fb_shadow_h;

struct x6_cursor_overlay {
  int drawn;
  int x;
  int y;
  int w;
  int h;
  uint saved[121];
};

static struct x6_cursor_overlay x6_cursor;

static int x6_event_queue_enqueue(struct x6_event_queue *q, struct x6_event *evt);
static struct x6_window *x6_pick_window_at(int px, int py);
static struct x6_window *find_window(uint id);
static struct x6_client *x6_find_client_by_fd(int fd);
static struct x6_event_queue *x6_queue_for_window(uint wid);
static int x6_find_key_grab_target(uint keycode, uint state, uint *target_wid);
static int x6_find_button_grab_target(uint button, uint mods, uint *target_wid);
static uint x6_pointer_event_state(void);
static void x6_enqueue_crossing_event(int type, uint wid);
static struct x6_selection *x6_find_selection(const char *name);
static struct x6_selection *x6_find_or_alloc_selection(const char *name);
static void x6_clear_key_grabs_for_fd(int owner_fd);
static void x6_clear_button_grabs_for_fd(int owner_fd);
static void x6_disconnect_client(struct x6_client *client);
static void x6_flush_client_events(struct x6_client *client);
static void x6_enqueue_property_notify(struct x6_window *win, const char *atom, int state);

static void
x6_raise_window(struct x6_window *w)
{
  if(!w)
    return;
  w->z = x6_next_z++;
  if(x6_next_z < 1)
    x6_next_z = 1;
}

static void
x6_lower_window(struct x6_window *w)
{
  int i;
  int minz;

  if(!w)
    return;

  minz = w->z;
  for(i = 0; i < X6_MAX_WINDOWS; i++) {
    if(!wins[i].in_use)
      continue;
    if(wins[i].z < minz)
      minz = wins[i].z;
  }
  w->z = minz - 1;
}

static uint
x6_fb_shadow_pixel(int x, int y)
{
  size_t idx;

  if(x6_fb_shadow == 0)
    return 0;
  if(x < 0 || y < 0 || x >= x6_fb_shadow_w || y >= x6_fb_shadow_h)
    return 0;

  idx = (size_t)y * (size_t)x6_fb_shadow_w + (size_t)x;
  return x6_fb_shadow[idx];
}

static int
x6_cursor_overlaps_rect(int x, int y, int w, int h)
{
  if(!x6_cursor.drawn)
    return 0;
  // AABB rectangle intersection test
  if(x + w <= x6_cursor.x || y + h <= x6_cursor.y)
    return 0;
  if(x6_cursor.x + x6_cursor.w <= x || x6_cursor.y + x6_cursor.h <= y)
    return 0;
  return 1;
}

static int
x6_cursor_shape_hit(int dx, int dy)
{
  if(dx < 0 || dy < 0 || dx >= 11 || dy >= 11)
    return 0;
  if(dx == 5 || dy == 5)
    return 2;
  if(dx == 4 || dx == 6 || dy == 4 || dy == 6)
    return 1;
  return 0;
}

static uint
x6_active_cursor(void)
{
  struct x6_window *hit;

  hit = x6_pick_window_at(pointer_x, pointer_y);
  if(hit && hit->cursor_set)
    return hit->cursor;
  return root_cursor;
}

static void
x6_cursor_hide(void)
{
  int ix;
  int iy;
  int idx;
  int py;
  int px;
  uint64_t off;

  if(x6_backend != X6_BACKEND_FB || x6_fb.fd < 0)
    return;
  if(!x6_cursor.drawn)
    return;

  idx = 0;
  for(iy = 0; iy < x6_cursor.h; iy++) {
    py = x6_cursor.y + iy;
    px = x6_cursor.x;
    
    // Ensure row buffer is large enough
    if(x6_cursor.w > x6_fb.rowcap) {
      uint *nbuf = (uint *)malloc((size_t)x6_cursor.w * sizeof(uint));
      if(!nbuf)
        return;
      if(x6_fb.rowbuf)
        free(x6_fb.rowbuf);
      x6_fb.rowbuf = nbuf;
      x6_fb.rowcap = x6_cursor.w;
    }
    
    // Fill row buffer with saved pixels
    for(ix = 0; ix < x6_cursor.w; ix++)
      x6_fb.rowbuf[ix] = x6_cursor.saved[idx + ix];
    
    // Write entire row in single syscall
    off = (uint64_t)py * (uint64_t)x6_fb.stride + (uint64_t)px * 4ULL;
    if(lseek(x6_fb.fd, (off_t)off, SEEK_SET) < 0)
      break;
    if(write(x6_fb.fd, x6_fb.rowbuf, x6_cursor.w * (int)sizeof(uint)) < 0)
      break;
    
    // Update shadow buffer
    if(x6_fb_shadow) {
      int j;
      size_t base = (size_t)py * (size_t)x6_fb_shadow_w + (size_t)px;
      for(j = 0; j < x6_cursor.w; j++)
        x6_fb_shadow[base + j] = x6_fb.rowbuf[j];
    }
    
    idx += x6_cursor.w;
  }

  x6_cursor.drawn = 0;
}

static void
x6_cursor_show(void)
{
  int ox;
  int oy;
  int x0;
  int y0;
  int x1;
  int y1;
  int ix;
  int iy;
  int idx;
  uint p;
  uint active;
  int py;
  uint64_t off;

  if(x6_backend != X6_BACKEND_FB || x6_fb.fd < 0)
    return;

  active = x6_active_cursor();
  if(active == 0)
    return;

  ox = pointer_x - 5;
  oy = pointer_y - 5;
  x0 = ox < 0 ? 0 : ox;
  y0 = oy < 0 ? 0 : oy;
  x1 = ox + 11;
  y1 = oy + 11;
  if(x1 > x6_fb.width) x1 = x6_fb.width;
  if(y1 > x6_fb.height) y1 = x6_fb.height;
  if(x1 <= x0 || y1 <= y0)
    return;

  x6_cursor.x = x0;
  x6_cursor.y = y0;
  x6_cursor.w = x1 - x0;
  x6_cursor.h = y1 - y0;

  // Ensure row buffer is large enough
  if(x6_cursor.w > x6_fb.rowcap) {
    uint *nbuf = (uint *)malloc((size_t)x6_cursor.w * sizeof(uint));
    if(!nbuf)
      return;
    if(x6_fb.rowbuf)
      free(x6_fb.rowbuf);
    x6_fb.rowbuf = nbuf;
    x6_fb.rowcap = x6_cursor.w;
  }

  idx = 0;
  for(iy = 0; iy < x6_cursor.h; iy++) {
    py = x6_cursor.y + iy;
    
    for(ix = 0; ix < x6_cursor.w; ix++) {
      int hit;
      int dx;
      int dy;
      int px;

      px = x6_cursor.x + ix;
      p = x6_fb_shadow_pixel(px, py);
      x6_cursor.saved[idx] = p;

      dx = px - ox;
      dy = py - oy;
      hit = x6_cursor_shape_hit(dx, dy);
      if(hit == 2)
        x6_fb.rowbuf[ix] = 0x00ffffffU;  // White
      else if(hit == 1)
        x6_fb.rowbuf[ix] = 0x00000000U;  // Black cursor outline
      else
        x6_fb.rowbuf[ix] = p;  // Unchanged pixel

      idx++;
    }
    
    // Write entire row in single syscall
    off = (uint64_t)py * (uint64_t)x6_fb.stride + (uint64_t)x6_cursor.x * 4ULL;
    if(lseek(x6_fb.fd, (off_t)off, SEEK_SET) < 0)
      break;
    if(write(x6_fb.fd, x6_fb.rowbuf, x6_cursor.w * (int)sizeof(uint)) < 0)
      break;
    
    // Update shadow buffer
    if(x6_fb_shadow) {
      int j;
      size_t base = (size_t)py * (size_t)x6_fb_shadow_w + (size_t)x6_cursor.x;
      for(j = 0; j < x6_cursor.w; j++)
        x6_fb_shadow[base + j] = x6_fb.rowbuf[j];
    }
  }

  x6_cursor.drawn = 1;
}

static void
x6_cursor_refresh(void)
{
  x6_cursor_hide();
  x6_cursor_show();
}

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

static struct x6_client *
x6_find_client_by_fd(int fd)
{
  int i;

  for(i = 0; i < X6_MAX_CLIENTS; i++) {
    if(clients[i].in_use && clients[i].fd == fd)
      return &clients[i];
  }
  return 0;
}

static struct x6_event_queue *
x6_queue_for_window(uint wid)
{
  struct x6_client *client;
  struct x6_window *win;

  if(wid == (uint)wm_redirect_root)
    return wm_event_queue;

  win = find_window(wid);
  if(win == 0)
    return 0;

  client = x6_find_client_by_fd(win->owner_fd);
  if(client == 0)
    return 0;
  return &client->queue;
}

static int
x6_find_key_grab_target(uint keycode, uint state, uint *target_wid)
{
  uint mods;
  int i;

  mods = state & 0xffU;
  for(i = 0; i < X6_MAX_KEY_GRABS; i++) {
    struct x6_key_grab *g;
    g = &key_grabs[i];
    if(!g->in_use)
      continue;
    if(g->keycode != 0 && g->keycode != keycode)
      continue;
    if(g->modifiers != X6_ANY_MODIFIER && g->modifiers != mods)
      continue;
    if(target_wid)
      *target_wid = g->wid;
    return 1;
  }
  return 0;
}

static int
x6_find_button_grab_target(uint button, uint mods, uint *target_wid)
{
  int i;

  for(i = 0; i < X6_MAX_BUTTON_GRABS; i++) {
    struct x6_button_grab *g;
    g = &button_grabs[i];
    if(!g->in_use)
      continue;
    if(g->button != 0 && g->button != button)
      continue;
    if(g->modifiers != X6_ANY_MODIFIER && g->modifiers != mods)
      continue;
    if(target_wid)
      *target_wid = g->wid;
    return 1;
  }
  return 0;
}

static uint
x6_pointer_event_state(void)
{
  uint state;

  state = keyboard_mod_state & 0xffU;
  if(pointer_state & 0x01U)
    state |= X6_STATE_BUTTON1;
  if(pointer_state & 0x02U)
    state |= X6_STATE_BUTTON2;
  if(pointer_state & 0x04U)
    state |= X6_STATE_BUTTON3;
  return state;
}

static void
x6_enqueue_crossing_event(int type, uint wid)
{
  struct x6_event evt;
  struct x6_event_queue *q;
  struct x6_window *w;

  if(wid == 0)
    return;

  w = find_window(wid);
  if(w) {
    if(type == X6_EVENT_ENTER_NOTIFY && (w->event_mask & X6_MASK_ENTER_WINDOW) == 0)
      return;
    if(type == X6_EVENT_LEAVE_NOTIFY && (w->event_mask & X6_MASK_LEAVE_WINDOW) == 0)
      return;
  }

  q = x6_queue_for_window(wid);
  if(!q)
    return;

  memset(&evt, 0, sizeof(evt));
  evt.type = type;
  evt.wid = wid;
  evt.x = pointer_x;
  evt.y = pointer_y;
  evt.state = x6_pointer_event_state();
  evt.mode = 0;
  evt.detail = 0;
  evt.focus = (focus_wid == wid) ? 1 : 0;
  evt.same_screen = 1;
  x6_event_queue_enqueue(q, &evt);
}

static struct x6_selection *
x6_find_selection(const char *name)
{
  int i;

  if(!name || !*name)
    return 0;
  for(i = 0; i < X6_MAX_SELECTIONS; i++) {
    if(selections[i].in_use && strcmp(selections[i].name, name) == 0)
      return &selections[i];
  }
  return 0;
}

static struct x6_selection *
x6_find_or_alloc_selection(const char *name)
{
  int i;
  struct x6_selection *s;

  s = x6_find_selection(name);
  if(s)
    return s;

  for(i = 0; i < X6_MAX_SELECTIONS; i++) {
    if(selections[i].in_use)
      continue;
    memset(&selections[i], 0, sizeof(selections[i]));
    selections[i].in_use = 1;
    strncpy(selections[i].name, name, sizeof(selections[i].name) - 1);
    selections[i].name[sizeof(selections[i].name) - 1] = '\0';
    return &selections[i];
  }
  return 0;
}

static void
x6_clear_key_grabs_for_fd(int owner_fd)
{
  int i;

  for(i = 0; i < X6_MAX_KEY_GRABS; i++) {
    if(key_grabs[i].in_use && key_grabs[i].owner_fd == owner_fd)
      memset(&key_grabs[i], 0, sizeof(key_grabs[i]));
  }
}

static void
x6_clear_button_grabs_for_fd(int owner_fd)
{
  int i;

  for(i = 0; i < X6_MAX_BUTTON_GRABS; i++) {
    if(button_grabs[i].in_use && button_grabs[i].owner_fd == owner_fd)
      memset(&button_grabs[i], 0, sizeof(button_grabs[i]));
  }
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
    if(!best || w->z >= best->z)
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
    int rh;
    int rw;
    uint p;
    int need_cursor_refresh;

    // Only hide/show cursor if drawing overlaps cursor area
    need_cursor_refresh = x6_cursor_overlaps_rect(x, y, w, h);
    if(need_cursor_refresh)
      x6_cursor_hide();

    x0 = x;
    y0 = y;
    x1 = x + w;
    y1 = y + h;
    if(x0 < 0) x0 = 0;
    if(y0 < 0) y0 = 0;
    if(x1 > x6_fb.width) x1 = x6_fb.width;
    if(y1 > x6_fb.height) y1 = x6_fb.height;
    if(x1 <= x0 || y1 <= y0) {
      if(need_cursor_refresh)
        x6_cursor_show();
      return;
    }

    rw = x1 - x0;
    rh = y1 - y0;

    /* Fast path: full-width contiguous fills can be written in one bulk write. */
    if(x0 == 0 && rw == x6_fb.width && x6_fb.stride == x6_fb.width * 4 && rh > 1) {
      int total;
      int wrote_ok;
      size_t start;
      uint64_t off;

      total = rw * rh;
      if(total > x6_fb.rowcap) {
        uint *nbuf;
        nbuf = (uint *)malloc((size_t)total * sizeof(uint));
        if(!nbuf) {
          if(need_cursor_refresh)
            x6_cursor_show();
          return;
        }
        if(x6_fb.rowbuf)
          free(x6_fb.rowbuf);
        x6_fb.rowbuf = nbuf;
        x6_fb.rowcap = total;
      }

      p = pixel & 0x00ffffffU;
      for(i = 0; i < total; i++)
        x6_fb.rowbuf[i] = p;

      wrote_ok = 0;
      off = (uint64_t)y0 * (uint64_t)x6_fb.stride;
      if(lseek(x6_fb.fd, (off_t)off, SEEK_SET) >= 0 &&
         write(x6_fb.fd, x6_fb.rowbuf, total * (int)sizeof(uint)) == total * (int)sizeof(uint))
        wrote_ok = 1;

      if(wrote_ok && x6_fb_shadow) {
        start = (size_t)y0 * (size_t)x6_fb_shadow_w;
        for(i = 0; i < total; i++)
          x6_fb_shadow[start + (size_t)i] = p;
      }

      if(need_cursor_refresh)
        x6_cursor_show();
      return;
    }

    /*
     * Partial-rect coalescing path: when rows are tightly packed and we have
     * shadow contents, compose full scanlines and stream writes sequentially.
     * This avoids per-row seek+write for tall non-full-width rectangles.
     */
    if(x6_fb_shadow && x6_fb.stride == x6_fb.width * 4 && rh > 1 && rw < x6_fb.width) {
      uint64_t off;

      if(x6_fb.width > x6_fb.rowcap) {
        uint *nbuf;
        nbuf = (uint *)malloc((size_t)x6_fb.width * sizeof(uint));
        if(!nbuf) {
          if(need_cursor_refresh)
            x6_cursor_show();
          return;
        }
        if(x6_fb.rowbuf)
          free(x6_fb.rowbuf);
        x6_fb.rowbuf = nbuf;
        x6_fb.rowcap = x6_fb.width;
      }

      p = pixel & 0x00ffffffU;
      off = (uint64_t)y0 * (uint64_t)x6_fb.stride;
      if(lseek(x6_fb.fd, (off_t)off, SEEK_SET) >= 0) {
        for(i = y0; i < y1; i++) {
          int j;
          size_t row_base;

          row_base = (size_t)i * (size_t)x6_fb_shadow_w;
          memmove(x6_fb.rowbuf,
                  x6_fb_shadow + row_base,
                  (size_t)x6_fb.width * sizeof(uint));
          for(j = 0; j < rw; j++)
            x6_fb.rowbuf[x0 + j] = p;

          if(write(x6_fb.fd,
                   x6_fb.rowbuf,
                   x6_fb.width * (int)sizeof(uint)) < 0)
            break;

          for(j = 0; j < rw; j++)
            x6_fb_shadow[row_base + (size_t)x0 + (size_t)j] = p;
        }
        if(need_cursor_refresh)
          x6_cursor_show();
        return;
      }
    }

    if(rw > x6_fb.rowcap) {
      uint *nbuf;
      nbuf = (uint *)malloc((size_t)rw * sizeof(uint));
      if(!nbuf) {
        if(need_cursor_refresh)
          x6_cursor_show();
        return;
      }
      if(x6_fb.rowbuf)
        free(x6_fb.rowbuf);
      x6_fb.rowbuf = nbuf;
      x6_fb.rowcap = rw;
    }

    p = pixel & 0x00ffffffU;
    for(i = 0; i < rw; i++)
      x6_fb.rowbuf[i] = p;

    for(i = y0; i < y1; i++) {
      int j;
      size_t base;
      uint64_t off;
      off = (uint64_t)i * (uint64_t)x6_fb.stride + (uint64_t)x0 * 4ULL;
      if(lseek(x6_fb.fd, (off_t)off, SEEK_SET) < 0)
        break;
      if(write(x6_fb.fd, x6_fb.rowbuf, rw * (int)sizeof(uint)) < 0)
        break;
      if(x6_fb_shadow == 0)
        continue;
      base = (size_t)i * (size_t)x6_fb_shadow_w + (size_t)x0;
      for(j = 0; j < rw; j++)
        x6_fb_shadow[base + (size_t)j] = p;
    }
    if(need_cursor_refresh)
      x6_cursor_show();
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

static void
x6_draw_text_pixels(int x, int baseline_y, uint color, const char *text, int len)
{
  const struct user_font *font;
  int i;
  int cx;

  if(!text || len <= 0)
    return;

  font = user_font_builtin_montecarlo();
  if(!font)
    return;

  if(x6_backend == X6_BACKEND_FB && x6_fb.fd >= 0) {
    int need_cursor_refresh;
    int text_w = user_font_text_width(font, text, len);
    int text_h = font->size;
    need_cursor_refresh = x6_cursor_overlaps_rect(x, baseline_y - text_h, text_w, text_h);
    if(need_cursor_refresh)
      x6_cursor_hide();
    
    // Use row-batched rendering: accumulate pixels per row, single syscall per row
    cx = x;
    for(i = 0; i < len; i++) {
      const struct user_glyph *g;
      int gx;
      int gy;
      int r;

      g = user_font_get_glyph(font, (uint)(uchar)text[i]);
      if(!g)
        continue;
      gx = cx + g->bearing_x;
      gy = baseline_y - g->bearing_y;

      // Process each row of the glyph
      for(r = 0; r < g->height; r++) {
        int c;
        int py;
        int row_min_x;
        int row_max_x;
        uchar rowbits;
        uint p;
        uint64_t off;

        py = gy + r;
        if(py < 0 || py >= x6_fb.height)
          continue;
        
        rowbits = g->bitmap ? g->bitmap[r] : 0;
        if(rowbits == 0)
          continue;  // Skip empty rows
        
        // Find min/max x for this row
        row_min_x = gx;
        row_max_x = gx;
        for(c = 0; c < g->width && c < 8; c++) {
          if((rowbits & (1U << (7 - c))) != 0) {
            int px = gx + c;
            if(px < row_min_x) row_min_x = px;
            if(px > row_max_x) row_max_x = px;
          }
        }
        
        // Clamp to screen bounds
        if(row_min_x < 0) row_min_x = 0;
        if(row_max_x >= x6_fb.width) row_max_x = x6_fb.width - 1;
        if(row_min_x > row_max_x)
          continue;
        
        // Fill row buffer with background then glyph pixels
        int row_width = row_max_x - row_min_x + 1;
        
        // Ensure row buffer is large enough
        if(row_width > x6_fb.rowcap) {
          uint *nbuf = (uint *)malloc((size_t)row_width * sizeof(uint));
          if(!nbuf)
            goto text_fallback;
          if(x6_fb.rowbuf)
            free(x6_fb.rowbuf);
          x6_fb.rowbuf = nbuf;
          x6_fb.rowcap = row_width;
        }
        
        // Prefill from the shadow framebuffer. Do not read back from /dev/fb0
        // here: the device is not a reliable read source, and text compositing
        // must treat the maintained shadow buffer as the source of truth.
        if(x6_fb_shadow) {
          int j;
          size_t base = (size_t)py * (size_t)x6_fb_shadow_w + (size_t)row_min_x;
          for(j = 0; j < row_width; j++)
            x6_fb.rowbuf[j] = x6_fb_shadow[base + j];
        } else {
          // No shadow buffer; fill with black background
          memset(x6_fb.rowbuf, 0, (size_t)row_width * sizeof(uint));
        }
        
        // Overlay glyph pixels on the buffer
        p = color & 0x00ffffffU;
        for(c = 0; c < g->width && c < 8; c++) {
          int px = gx + c;
          if(px >= 0 && px < x6_fb.width && (rowbits & (1U << (7 - c))) != 0) {
            x6_fb.rowbuf[px - row_min_x] = p;
          }
        }
        
        // Write back entire row in single syscall
        off = (uint64_t)py * (uint64_t)x6_fb.stride + (uint64_t)row_min_x * 4ULL;
        if(lseek(x6_fb.fd, (off_t)off, SEEK_SET) < 0)
          goto text_fallback;
        if(write(x6_fb.fd, x6_fb.rowbuf, row_width * (int)sizeof(uint)) < 0)
          goto text_fallback;
        
        // Update shadow buffer
        if(x6_fb_shadow) {
          int j;
          size_t base = (size_t)py * (size_t)x6_fb_shadow_w + (size_t)row_min_x;
          for(j = 0; j < row_width; j++)
            x6_fb_shadow[base + j] = x6_fb.rowbuf[j];
        }
      }

      cx += g->advance_x > 0 ? g->advance_x : g->width;
    }
    
    if(need_cursor_refresh)
      x6_cursor_show();
    return;
    
text_fallback:
    // Fallback to ANSI if row-batched rendering fails
    if(need_cursor_refresh)
      x6_cursor_show();
    goto ansi_fallback;
  }
  
ansi_fallback:

  x6_canvas_fill_pixels(x, baseline_y - font->ascent,
                        user_font_text_width(font, text, len),
                        font->size, color);
}

static void
x6_draw_text_pixmap(struct x6_pixmap *pm, int x, int baseline_y, uint color, const char *text, int len)
{
  const struct user_font *font;
  int i;
  int cx;

  if(!pm || !pm->pixels || !text || len <= 0)
    return;

  font = user_font_builtin_montecarlo();
  if(!font)
    return;

  cx = x;
  for(i = 0; i < len; i++) {
    const struct user_glyph *g;
    int gx;
    int gy;
    int r;
    uchar ch;

    ch = (uchar)text[i];
    g = user_font_get_glyph(font, (uint)ch);
    if(!g) {
      cx += font->size / 2;
      continue;
    }

    gx = cx + g->bearing_x;
    gy = baseline_y - g->bearing_y;

    for(r = 0; r < g->height; r++) {
      int c;
      int py;
      uchar rowbits;

      py = gy + r;
      if(py < 0 || py >= pm->height)
        continue;

      rowbits = g->bitmap ? g->bitmap[r] : 0;
      if(rowbits == 0)
        continue;

      for(c = 0; c < g->width && c < 8; c++) {
        int px;
        if((rowbits & (1U << (7 - c))) == 0)
          continue;
        px = gx + c;
        if(px < 0 || px >= pm->width)
          continue;
        pm->pixels[py * pm->width + px] = color & 0x00ffffffU;
      }
    }

    cx += g->advance_x > 0 ? g->advance_x : g->width;
  }
}

static int
x6_parse_draw_text(char *cmd, uint *id, int *x, int *y, uint *color, int *len, char **text)
{
  int n;
  char *p;

  if(!cmd || !id || !x || !y || !color || !len || !text)
    return -1;
  if(sscanf(cmd, "DRAW_TEXT %u %d %d %u %d%n", id, x, y, color, len, &n) != 5)
    return -1;
  if(*len < 0)
    return -1;

  p = cmd + n;
  while(*p == ' ')
    p++;
  *text = p;
  if((int)strlen(p) < *len)
    *len = (int)strlen(p);
  return 0;
}

static void __attribute__((unused))
x6_enqueue_keyevent(int type, uint keycode, uint state)
{
  struct x6_event evt;
  struct x6_event_queue *target_q;
  struct x6_window *hit;
  uint target;

  if(wm_has_kb_grab && keyboard_grab_owner != 0)
    target = keyboard_grab_owner;
  else if(x6_find_key_grab_target(keycode, state, &target))
    ;
  else if(focus_wid != 0 && focus_wid != (uint)wm_redirect_root)
    target = focus_wid;
  else {
    hit = x6_pick_window_at(pointer_x, pointer_y);
    if(hit && hit->mapped)
      target = hit->id;
    else
      target = wm_redirect_root;
  }

  target_q = x6_queue_for_window(target);
  if(target_q == 0)
    return;

  evt.type = type;
  evt.wid = target;
  evt.keycode = (int)keycode;
  evt.state = state;
  evt.x = pointer_x;
  evt.y = pointer_y;
  evt.w = evt.h = 0;
  evt.button = 0;
  evt.data0 = 0;
  evt.atom[0] = '\0';
  x6_event_queue_enqueue(target_q, &evt);
}

static void
x6_enqueue_pointer_event(int type, int button)
{
  struct x6_event evt;
  struct x6_event_queue *target_q;
  struct x6_window *hit;
  uint grab_wid;
  uint mods;

  hit = x6_pick_window_at(pointer_x, pointer_y);
  evt.type = type;
  mods = keyboard_mod_state & 0xffU;
  if(pointer_grab_active)
    evt.wid = pointer_grab_window ? pointer_grab_window : wm_redirect_root;
  else if(type == X6_EVENT_BUTTON_PRESS &&
          x6_find_button_grab_target((uint)button, mods, &grab_wid))
    evt.wid = grab_wid;
  else
    evt.wid = hit ? hit->id : wm_redirect_root;

  target_q = x6_queue_for_window(evt.wid);
  if(target_q == 0)
    return;

  evt.x = pointer_x;
  evt.y = pointer_y;
  evt.w = evt.h = 0;
  evt.state = x6_pointer_event_state();
  evt.keycode = 0;
  evt.button = button;
  evt.data0 = 0;
  evt.atom[0] = '\0';
  x6_event_queue_enqueue(target_q, &evt);
}

static void
x6_move_pointer(int dx, int dy)
{
  int sw;
  int sh;
  uint old_wid;
  uint new_wid;
  struct x6_window *hit;

  hit = x6_pick_window_at(pointer_x, pointer_y);
  old_wid = hit ? hit->id : 0;

  x6_cursor_hide();
  x6_screen_size(&sw, &sh);
  pointer_x = x6_clamp_int(pointer_x + dx, 0, sw - 1);
  pointer_y = x6_clamp_int(pointer_y + dy, 0, sh - 1);
  x6_cursor_show();  // Always refresh on pointer move

  if(!pointer_grab_active) {
    hit = x6_pick_window_at(pointer_x, pointer_y);
    new_wid = hit ? hit->id : 0;
    if(new_wid != old_wid) {
      x6_enqueue_crossing_event(X6_EVENT_LEAVE_NOTIFY, old_wid);
      x6_enqueue_crossing_event(X6_EVENT_ENTER_NOTIFY, new_wid);
    }
    pointer_hover_wid = new_wid;
  } else {
    pointer_hover_wid = 0;
  }

  x6_enqueue_pointer_event(X6_EVENT_MOTION_NOTIFY, 0);
}

static void __attribute__((unused))
x6_format_modifiers(uint state, char *buf, int buflen)
{
  int off;

  if(!buf || buflen <= 0)
    return;

  off = 0;
  buf[0] = 0;
  if(state & (1U << 0))
    off += snprintf(buf + off, buflen - off, "%sShift", off ? "+" : "");
  if(state & (1U << 2) && off < buflen)
    off += snprintf(buf + off, buflen - off, "%sCtrl", off ? "+" : "");
  if(state & (1U << 3) && off < buflen)
    off += snprintf(buf + off, buflen - off, "%sAlt", off ? "+" : "");
  if(state & (1U << 4) && off < buflen)
    off += snprintf(buf + off, buflen - off, "%sMod2", off ? "+" : "");
  if(state & (1U << 5) && off < buflen)
    off += snprintf(buf + off, buflen - off, "%sMod3", off ? "+" : "");
  if(state & (1U << 6) && off < buflen)
    off += snprintf(buf + off, buflen - off, "%sMod4", off ? "+" : "");
  if(state & (1U << 7) && off < buflen)
    off += snprintf(buf + off, buflen - off, "%sMod5", off ? "+" : "");
  if(off == 0)
    snprintf(buf, buflen, "none");
}

static void
x6_enqueue_property_notify(struct x6_window *win, const char *atom, int state)
{
  struct x6_event evt;
  struct x6_event_queue *q;

  if(!win || !atom)
    return;
  if((win->event_mask & X6_MASK_PROPERTY_CHANGE) == 0)
    return;

  q = x6_queue_for_window(win->id);
  if(!q)
    return;

  memset(&evt, 0, sizeof(evt));
  evt.type = X6_EVENT_PROPERTY_NOTIFY;
  evt.wid = win->id;
  evt.state = (uint)state;
  strncpy(evt.atom, atom, sizeof(evt.atom) - 1);
  evt.atom[sizeof(evt.atom) - 1] = '\0';
  x6_event_queue_enqueue(q, &evt);
}

static void
x6_mouse_setup(void)
{
  int flags;

  if(x6_mouse_fd >= 0)
    return;
  x6_mouse_fd = open("/dev/mouse0", O_RDONLY);
  if(x6_mouse_fd < 0) {
    mknod("/dev/mouse0", M_IFCHR | 0600, X6_CONSOLE_MAJOR, X6_CONSOLE_MINOR_MOUSE0);
    x6_mouse_fd = open("/dev/mouse0", O_RDONLY);
  }
  if(x6_mouse_fd >= 0) {
    flags = fcntl(x6_mouse_fd, F_GETFL, 0);
    if(flags >= 0)
      fcntl(x6_mouse_fd, F_SETFL, flags | O_NONBLOCK);
  }
}

static void
x6_keyboard_setup(void)
{
  int flags;

  if(x6_kbd_fd >= 0)
    return;
  x6_kbd_fd = open("/dev/kbd0", O_RDONLY);
  if(x6_kbd_fd < 0) {
    mknod("/dev/kbd0", M_IFCHR | 0600, X6_CONSOLE_MAJOR, X6_CONSOLE_MINOR_KBD0);
    x6_kbd_fd = open("/dev/kbd0", O_RDONLY);
  }
  if(x6_kbd_fd >= 0) {
    flags = fcntl(x6_kbd_fd, F_GETFL, 0);
    if(flags >= 0)
      fcntl(x6_kbd_fd, F_SETFL, flags | O_NONBLOCK);
  }
}

static void
x6_pump_keyboard(void)
{
  struct aux_kbd_event evbuf[16];
  int n;
  int i;

  if(x6_kbd_fd < 0)
    return;

  n = read(x6_kbd_fd, evbuf, sizeof(evbuf));
  if(n < 0) {
    return;
  }
  if(n == 0)
    return;
  if(n < (int)sizeof(struct aux_kbd_event))
    return;
  n /= (int)sizeof(struct aux_kbd_event);
  for(i = 0; i < n; i++) {
    keyboard_mod_state = (uint)evbuf[i].state;
    if(evbuf[i].value == AUX_KBD_VALUE_PRESS ||
       evbuf[i].value == AUX_KBD_VALUE_REPEAT) {
      x6_enqueue_keyevent(X6_EVENT_KEY_PRESS, (uint)evbuf[i].keycode, (uint)evbuf[i].state);
    } else if(evbuf[i].value == AUX_KBD_VALUE_RELEASE) {
      x6_enqueue_keyevent(X6_EVENT_KEY_RELEASE, (uint)evbuf[i].keycode, (uint)evbuf[i].state);
    }
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
  x6_fb_shadow_w = x6_fb.width;
  x6_fb_shadow_h = x6_fb.height;
  x6_fb_shadow = (uint *)malloc((size_t)x6_fb_shadow_w * (size_t)x6_fb_shadow_h * sizeof(uint));
  if(x6_fb_shadow)
    memset(x6_fb_shadow, 0, (size_t)x6_fb_shadow_w * (size_t)x6_fb_shadow_h * sizeof(uint));
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
  if(x6_fb_shadow)
    free(x6_fb_shadow);
  x6_fb_shadow = 0;
  x6_fb_shadow_w = 0;
  x6_fb_shadow_h = 0;
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
  int len;
  int off;

  if(cfd < 0 || s == 0)
    return;

  len = (int)strlen(s);
  off = 0;
  while(off < len) {
    int n;

    n = (int)send(cfd, s + off, (size_t)(len - off));
    if(n > 0) {
      off += n;
      continue;
    }

    if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      struct pollfd pfd;
      int pr;

      pfd.fd = cfd;
      pfd.events = POLLOUT;
      pfd.revents = 0;
      pr = poll(&pfd, 1, 200);
      if(pr <= 0)
      {
        dprintf(2, "x6: send poll timeout fd=%d off=%d len=%d errno=%d\n", cfd, off, len, errno);
        break;
      }
      continue;
    }

    dprintf(2, "x6: send failed fd=%d off=%d len=%d n=%d errno=%d\n", cfd, off, len, n, errno);
    break;
  }
}

static int
x6_client_fill_rxbuf(int cfd, char *buf, int *buflen, int bufcap)
{
  int space;
  int n;

  if(cfd < 0 || buf == 0 || buflen == 0 || bufcap <= 0)
    return -1;

  if(*buflen >= bufcap)
    return 0;

  space = bufcap - *buflen;
  n = recv(cfd, buf + *buflen, (size_t)space);
  if(n > 0) {
    *buflen += n;
    return 0;
  }
  if(n == 0)
    return -1;
  return -1;
}

static int
x6_client_next_line(char *buf, int *buflen, char *line, int linecap)
{
  int i;

  if(buf == 0 || buflen == 0 || line == 0 || linecap <= 1)
    return -1;

  for(i = 0; i < *buflen; i++) {
    if(buf[i] == '\n' || buf[i] == '\r') {
      int copy_len;
      int consume;

      copy_len = i;
      if(copy_len >= linecap)
        copy_len = linecap - 1;
      memmove(line, buf, (size_t)copy_len);
      line[copy_len] = 0;

      consume = i + 1;
      while(consume < *buflen && (buf[consume] == '\n' || buf[consume] == '\r'))
        consume++;
      memmove(buf, buf + consume, (size_t)(*buflen - consume));
      *buflen -= consume;
      return 1;
    }
  }

  return 0;
}

static struct x6_window *
find_window(uint id)
{
  struct x6_window *w;
  int idx;

  // O(1) hash table lookup
  idx = (int)(id % 256);
  w = wins_by_id[idx];
  if(w && w->in_use && w->id == id)
    return w;
  return 0;
}

static struct x6_window *
alloc_window(uint id)
{
  int i;
  int idx;

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
      wins[i].border_width = 0;
      wins[i].border_pixel = 0;
      wins[i].override_redirect = 0;
      wins[i].cursor_set = 0;
      wins[i].cursor = 0;
      wins[i].z = x6_next_z++;
      wins[i].event_mask = 0;
      wins[i].prop_count = 0;  // Initialize properties (Phase 2.1d)
      // Hash table insert for O(1) lookup
      idx = (int)(id % 256);
      wins_by_id[idx] = &wins[i];
      return &wins[i];
    }
  }
  return 0;
}

static struct x6_pixmap *
find_pixmap(uint id)
{
  int i;
  for(i = 0; i < X6_MAX_PIXMAPS; i++) {
    if(pixmaps[i].in_use && pixmaps[i].id == id)
      return &pixmaps[i];
  }
  return 0;
}

static struct x6_pixmap *
alloc_pixmap(int width, int height, int depth)
{
  int i;
  struct x6_pixmap *pm;
  int size;
  
  if(width < 1 || height < 1 || depth < 1)
    return 0;
  
  /* Limit pixmap size to prevent excessive memory use
   * Allow up to ~4MB per pixmap (assuming 4 bytes per pixel)
   */
  if((long)width * height > 1000000)
    return 0;
  
  for(i = 0; i < X6_MAX_PIXMAPS; i++) {
    if(!pixmaps[i].in_use) {
      pm = &pixmaps[i];
      size = width * height;
      
      /* Allocate pixel buffer */
      pm->pixels = (uint*)malloc(size * sizeof(uint));
      if(!pm->pixels)
        return 0;
      
      pm->in_use = 1;
      pm->id = x6_next_pmid++;
      pm->width = width;
      pm->height = height;
      pm->depth = depth;
      pm->pixels_size = size;
      
      /* Initialize to black */
      memset(pm->pixels, 0, size * sizeof(uint));
      return pm;
    }
  }
  return 0;
}

static void
destroy_pixmap(uint id)
{
  struct x6_pixmap *pm;
  
  pm = find_pixmap(id);
  if(pm) {
    if(pm->pixels) {
      free(pm->pixels);
      pm->pixels = 0;
    }
    pm->in_use = 0;
  }
}

static void
destroy_window(uint id)
{
  struct x6_window *w;
  int idx;

  w = find_window(id);
  if(w == 0)
    return;
  // Clear hash table entry
  idx = (int)(id % 256);
  if(wins_by_id[idx] == w)
    wins_by_id[idx] = 0;
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

  if(sscanf(cmd, "CREATE %d %d %d %d", &x, &y, &w, &h) == 4) {
    id = x6_next_wid++;
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
    {
      char out[64];
      snprintf(out, sizeof(out), "OK create wid=%u\n", id);
      x6_send_line(cfd, out);
    }
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
    x6_raise_window(win);
    {
      struct x6_client *owner = x6_find_client_by_fd(win->owner_fd);
      if(owner && owner->in_use && owner->hello_done) {
        struct x6_event me;
        memset(&me, 0, sizeof(me));
        me.type = X6_EVENT_MAP_NOTIFY;
        me.wid = id;
        x6_event_queue_enqueue(&owner->queue, &me);
      }
    }
    x6_send_line(cfd, "OK map\n");
    return;
  }

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
    x6_raise_window(win);
    {
      struct x6_client *owner = x6_find_client_by_fd(win->owner_fd);
      if(owner && owner->in_use && owner->hello_done) {
        struct x6_event me;
        memset(&me, 0, sizeof(me));
        me.type = X6_EVENT_MAP_NOTIFY;
        me.wid = id;
        x6_event_queue_enqueue(&owner->queue, &me);
      }
    }
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
    x6_send_line(cfd, "OK unmap\n");
    return;
  }

  if(sscanf(cmd, "RAISE %u", &id) == 1) {
    win = find_window(id);
    if(win == 0) {
      x6_send_line(cfd, "ERR not-found\n");
      return;
    }
    x6_raise_window(win);
    x6_send_line(cfd, "OK raised\n");
    return;
  }

  if(sscanf(cmd, "LOWER %u", &id) == 1) {
    win = find_window(id);
    if(win == 0) {
      x6_send_line(cfd, "ERR not-found\n");
      return;
    }
    x6_lower_window(win);
    x6_send_line(cfd, "OK lowered\n");
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

  /* Pixmap management commands */
  if(sscanf(cmd, "CREATE_PIXMAP %u %d %d %d", &id, &w, &h, &x) == 4) {
    /* CREATE_PIXMAP <depth> <width> <height> <format> */
    struct x6_pixmap *pm;
    pm = alloc_pixmap(w, h, x);  /* x is actually depth/format */
    if(!pm) {
      x6_send_line(cfd, "ERR no-slots\n");
      return;
    }
    {
      char out[64];
      snprintf(out, sizeof(out), "OK create_pixmap pmid=%u\n", pm->id);
      x6_send_line(cfd, out);
    }
    return;
  }

  if(sscanf(cmd, "DESTROY_PIXMAP %u", &id) == 1) {
    destroy_pixmap(id);
    x6_send_line(cfd, "OK destroy_pixmap\n");
    return;
  }

  if(sscanf(cmd, "DRAW_RECT %u %d %d %u %u %u", &id, &x, &y, &uw, &uh, &color) == 6) {
    int ox = 0;
    int oy = 0;
    struct x6_pixmap *pm = 0;
    
    if(uw == 0 || uh == 0 || uw > 4096U || uh > 4096U) {
      x6_send_line(cfd, "OK draw\n");
      return;
    }
    w = (int)uw;
    h = (int)uh;
    
    /* Check if target is a pixmap */
    pm = find_pixmap((uint)id);
    if(pm) {
      /* Draw to pixmap buffer */
      int i, j, px, py;
      for(j = 0; j < h && (y + j) < pm->height; j++) {
        for(i = 0; i < w && (x + i) < pm->width; i++) {
          px = x + i;
          py = y + j;
          if(px >= 0 && px < pm->width && py >= 0 && py < pm->height)
            pm->pixels[py * pm->width + px] = color;
        }
      }
      x6_send_line(cfd, "OK draw\n");
      return;
    }
    
    /* Otherwise, treat as window */
    if(id != (uint)wm_redirect_root) {
      win = find_window(id);
      if(win == 0) {
        x6_send_line(cfd, "ERR not-found\n");
        return;
      }
      if(!win->mapped) {
        x6_send_line(cfd, "OK draw\n");
        return;
      }
      ox = win->x;
      oy = win->y;
    }
    x6_canvas_fill_pixels(ox + x, oy + y, w, h, color);
    x6_send_line(cfd, "OK draw\n");
    return;
  }

  {
    char *text;
    int tlen;
    if(x6_parse_draw_text(cmd, &id, &x, &y, &color, &tlen, &text) == 0) {
      int ox;
      int oy;
      struct x6_pixmap *pm;

      /* Check if target is a pixmap */
      pm = find_pixmap((uint)id);
      if(pm) {
        x6_draw_text_pixmap(pm, x, y, color, text, tlen);
        x6_send_line(cfd, "OK text\n");
        return;
      }

      ox = 0;
      oy = 0;
      if(id != (uint)wm_redirect_root) {
        win = find_window(id);
        if(win == 0) {
          x6_send_line(cfd, "ERR not-found\n");
          return;
        }
        if(!win->mapped) {
          x6_send_line(cfd, "OK text\n");
          return;
        }
        ox = win->x;
        oy = win->y;
      }

      if(tlen > 0)
        x6_draw_text_pixels(ox + x, oy + y, color, text, tlen);
      x6_send_line(cfd, "OK text\n");
      return;
    }
  }

  /* COPY_AREA command: Copy pixels from source drawable (pixmap or window) to destination drawable */
  {
    uint src_id;
    uint dst_id;
    int src_x;
    int src_y;
    uint width;
    uint height;
    int dest_x;
    int dest_y;

    if(sscanf(cmd, "COPY_AREA %u %u %d %d %u %u %d %d",
              &src_id, &dst_id, &src_x, &src_y,
              &width, &height, &dest_x, &dest_y) == 8) {
    struct x6_pixmap *src_pm = 0;
    struct x6_window *dest_win = 0;
    int i, j;
    uint pixel;
    
    /* Source should be a pixmap */
    src_pm = find_pixmap(src_id);
    if(!src_pm) {
      x6_send_line(cfd, "ERR source-not-found\n");
      return;
    }
    
    /* Destination should be a window */
    dest_win = find_window(dst_id);
    if(!dest_win) {
      x6_send_line(cfd, "ERR dest-not-found\n");
      return;
    }
    
    if(!dest_win->mapped) {
      /* Silently succeed even if dest not mapped (happens in redraw) */
      x6_send_line(cfd, "OK copy_area\n");
      return;
    }
    
    /* Copy pixels from pixmap to destination window. */
    if(x6_backend == X6_BACKEND_FB && x6_fb.fd >= 0) {
      int need_cursor_refresh;

      need_cursor_refresh = x6_cursor_overlaps_rect(dest_win->x + dest_x,
                                                    dest_win->y + dest_y,
                                                    (int)width, (int)height);
      if(need_cursor_refresh)
        x6_cursor_hide();

      for(j = 0; j < (int)height; j++) {
        int sy;
        int dy;
        int sx0;
        int dx0;
        int copy_w;
        uint64_t off;
        int k;

        sy = src_y + j;
        dy = dest_win->y + dest_y + j;
        if(sy < 0 || sy >= src_pm->height)
          continue;
        if(dy < 0 || dy >= x6_fb.height)
          continue;

        sx0 = src_x;
        dx0 = dest_win->x + dest_x;
        copy_w = (int)width;

        if(sx0 < 0) {
          dx0 -= sx0;
          copy_w += sx0;
          sx0 = 0;
        }
        if(dx0 < 0) {
          sx0 -= dx0;
          copy_w += dx0;
          dx0 = 0;
        }
        if(sx0 + copy_w > src_pm->width)
          copy_w = src_pm->width - sx0;
        if(dx0 + copy_w > x6_fb.width)
          copy_w = x6_fb.width - dx0;
        if(copy_w <= 0)
          continue;

        if(copy_w > x6_fb.rowcap) {
          uint *nbuf;
          nbuf = (uint *)malloc((size_t)copy_w * sizeof(uint));
          if(!nbuf)
            continue;
          if(x6_fb.rowbuf)
            free(x6_fb.rowbuf);
          x6_fb.rowbuf = nbuf;
          x6_fb.rowcap = copy_w;
        }

        for(k = 0; k < copy_w; k++) {
          pixel = src_pm->pixels[sy * src_pm->width + (sx0 + k)] & 0x00ffffffU;
          x6_fb.rowbuf[k] = pixel;
        }

        off = (uint64_t)dy * (uint64_t)x6_fb.stride + (uint64_t)dx0 * 4ULL;
        if(lseek(x6_fb.fd, (off_t)off, SEEK_SET) >= 0)
          write(x6_fb.fd, x6_fb.rowbuf, copy_w * (int)sizeof(uint));

        if(x6_fb_shadow) {
          size_t base;
          base = (size_t)dy * (size_t)x6_fb_shadow_w + (size_t)dx0;
          for(k = 0; k < copy_w; k++)
            x6_fb_shadow[base + (size_t)k] = x6_fb.rowbuf[k];
        }
      }

      if(need_cursor_refresh)
        x6_cursor_show();
    } else {
      int row0;
      int row1;
      row0 = X6_CANVAS_ROWS;
      row1 = -1;

      for(j = 0; j < (int)height; j++) {
        for(i = 0; i < (int)width; i++) {
          int sx;
          int sy;
          int px;
          int py;
          int cx;
          int cy;

          sx = src_x + i;
          sy = src_y + j;
          if(sx < 0 || sy < 0 || sx >= src_pm->width || sy >= src_pm->height)
            continue;

          px = dest_win->x + dest_x + i;
          py = dest_win->y + dest_y + j;
          if(px < 0 || py < 0)
            continue;

          cx = px / X6_CELL_W;
          cy = py / X6_CELL_H;
          if(cx < 0 || cy < 0 || cx >= X6_CANVAS_COLS || cy >= X6_CANVAS_ROWS)
            continue;

          pixel = src_pm->pixels[sy * src_pm->width + sx] & 0x00ffffffU;
          canvas_pixels[cy][cx] = pixel;
          if(cy < row0) row0 = cy;
          if(cy > row1) row1 = cy;
        }
      }

      if(row1 >= row0)
        x6_canvas_flush_rows(row0, row1);
    }
    
    x6_send_line(cfd, "OK copy_area\n");
    return;
  }
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
    // Notify the window owner of its new geometry
    {
      struct x6_client *owner = x6_find_client_by_fd(win->owner_fd);
      if(owner && owner->in_use && owner->hello_done) {
        struct x6_event cn;
        cn.type = X6_EVENT_CONFIGURE_NOTIFY;
        cn.wid = id;
        cn.x = x; cn.y = y; cn.w = w; cn.h = h;
        x6_event_queue_enqueue(&owner->queue, &cn);
      }
    }
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
    // Notify the window owner to redraw
    {
      struct x6_client *owner = x6_find_client_by_fd(win->owner_fd);
      if(owner && owner->in_use && owner->hello_done) {
        struct x6_event ex;
        ex.type = X6_EVENT_EXPOSE;
        ex.wid = id;
        ex.x = 0; ex.y = 0; ex.w = win->w; ex.h = win->h;
        x6_event_queue_enqueue(&owner->queue, &ex);
      }
    }
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

  if(sscanf(cmd, "QUEUE_CONFIGURE_NOTIFY %u %d %d %d %d", &id, &x, &y, &w, &h) == 5) {
    struct x6_client *owner;
    struct x6_event evt;

    win = find_window(id);
    if(win == 0) {
      x6_send_line(cfd, "ERR not-found\n");
      return;
    }
    owner = x6_find_client_by_fd(win->owner_fd);
    if(owner == 0 || !owner->in_use || !owner->hello_done) {
      x6_send_line(cfd, "ERR no-owner\n");
      return;
    }

    evt.type = X6_EVENT_CONFIGURE_NOTIFY;
    evt.wid = id;
    evt.x = x;
    evt.y = y;
    evt.w = w;
    evt.h = h;
    evt.keycode = 0;
    evt.button = 0;
    evt.state = 0;
    evt.data0 = 0;
    if(x6_event_queue_enqueue(&owner->queue, &evt) < 0) {
      x6_send_line(cfd, "ERR queue-full\n");
      return;
    }
    x6_send_line(cfd, "OK cfg_queued\n");
    return;
  }

  if(sscanf(cmd, "QUEUE_CLIENT_MESSAGE %u %u %u", &id, &color, &uw) == 3) {
    struct x6_client *owner;
    struct x6_event evt;

    win = find_window(id);
    if(win == 0) {
      x6_send_line(cfd, "ERR not-found\n");
      return;
    }
    owner = x6_find_client_by_fd(win->owner_fd);
    if(owner == 0 || !owner->in_use || !owner->hello_done) {
      x6_send_line(cfd, "ERR no-owner\n");
      return;
    }

    evt.type = X6_EVENT_CLIENT_MESSAGE;
    evt.wid = id;
    evt.x = evt.y = evt.w = evt.h = 0;
    evt.keycode = (int)color;
    evt.button = 0;
    evt.state = 0;
    evt.data0 = uw;
    if(x6_event_queue_enqueue(&owner->queue, &evt) < 0) {
      x6_send_line(cfd, "ERR queue-full\n");
      return;
    }
    x6_send_line(cfd, "OK clientmsg_queued\n");
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
    struct x6_event_queue *target_q;
    uint old_focus = focus_wid;

    // PointerRoot/root focus means "no explicit client focus".
    if(id == (uint)wm_redirect_root)
      focus_wid = 0;
    else
      focus_wid = id;

    if(old_focus != 0 && old_focus != focus_wid) {
      target_q = x6_queue_for_window(old_focus);
      if(target_q != 0) {
        memset(&evt, 0, sizeof(evt));
        evt.type = X6_EVENT_FOCUS_OUT;
        evt.wid = old_focus;
        evt.mode = 0;
        evt.detail = 0;
        x6_event_queue_enqueue(target_q, &evt);
      }
    }

    if(focus_wid != 0) {
      target_q = x6_queue_for_window(focus_wid);
      if(target_q != 0) {
        memset(&evt, 0, sizeof(evt));
        evt.type = X6_EVENT_FOCUS_IN;
        evt.wid = focus_wid;
        evt.mode = 0;
        evt.detail = 0;
        x6_event_queue_enqueue(target_q, &evt);
      }
    }

    x6_send_line(cfd, "OK focused\n");
    return;
  }

  if(strncmp(cmd, "GET_FOCUS", 9) == 0) {
    char out[64];
    snprintf(out, sizeof(out), "OK focus %u\n", focus_wid);
    x6_send_line(cfd, out);
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
    pointer_grab_active = 1;
    pointer_grab_window = wm_redirect_root;
    if(sscanf(cmd, "GRAB_POINTER %u", &id) == 1)
      pointer_grab_window = id;
    x6_send_line(cfd, "OK pointer_grabbed\n");
    return;
  }

  if(strncmp(cmd, "UNGRAB_POINTER", 14) == 0) {
    pointer_grab_active = 0;
    pointer_grab_window = 0;
    x6_send_line(cfd, "OK pointer_ungrabbed\n");
    return;
  }

  if(sscanf(cmd, "WARP_POINTER %d %d", &x, &y) == 2) {
    int sw;
    int sh;
    x6_cursor_hide();
    x6_screen_size(&sw, &sh);
    pointer_x = x6_clamp_int(x, 0, sw - 1);
    pointer_y = x6_clamp_int(y, 0, sh - 1);
    x6_cursor_show();
    x6_send_line(cfd, "OK pointer_warped\n");
    return;
  }

  if(sscanf(cmd, "SET_CURSOR %u %u", &id, &color) == 2) {
    if(id == (uint)wm_redirect_root) {
      root_cursor = color;
      x6_cursor_refresh();
      x6_send_line(cfd, "OK cursor_set\n");
      return;
    }
    win = find_window(id);
    if(win == 0) {
      x6_send_line(cfd, "ERR not-found\n");
      return;
    }
    win->cursor_set = 1;
    win->cursor = color;
    x6_cursor_refresh();
    x6_send_line(cfd, "OK cursor_set\n");
    return;
  }

  if(sscanf(cmd, "UNSET_CURSOR %u", &id) == 1) {
    if(id == (uint)wm_redirect_root) {
      root_cursor = 1;
      x6_cursor_refresh();
      x6_send_line(cfd, "OK cursor_unset\n");
      return;
    }
    win = find_window(id);
    if(win == 0) {
      x6_send_line(cfd, "ERR not-found\n");
      return;
    }
    win->cursor_set = 0;
    win->cursor = 0;
    x6_cursor_refresh();
    x6_send_line(cfd, "OK cursor_unset\n");
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

  if(sscanf(cmd, "GRAB_KEY %u %u %u", &id, &color, &uw) == 3) {
    int slot;
    uint keycode;
    uint modifiers;
    uint grab_wid;

    keycode = id;
    modifiers = color;
    grab_wid = uw;

    slot = -1;
    for(i = 0; i < X6_MAX_KEY_GRABS; i++) {
      if(key_grabs[i].in_use &&
         key_grabs[i].owner_fd == cfd &&
         key_grabs[i].wid == grab_wid &&
         key_grabs[i].keycode == keycode &&
         key_grabs[i].modifiers == modifiers) {
        x6_send_line(cfd, "OK key_grabbed\n");
        return;
      }
      if(slot < 0 && !key_grabs[i].in_use)
        slot = i;
    }

    if(slot < 0) {
      x6_send_line(cfd, "ERR no-grab-slots\n");
      return;
    }

    key_grabs[slot].in_use = 1;
    key_grabs[slot].owner_fd = cfd;
    key_grabs[slot].wid = grab_wid;
    key_grabs[slot].keycode = keycode;
    key_grabs[slot].modifiers = modifiers;
    x6_send_line(cfd, "OK key_grabbed\n");
    return;
  }

  if(sscanf(cmd, "UNGRAB_KEY %u %u %u", &id, &color, &uw) == 3) {
    uint keycode;
    uint modifiers;
    uint grab_wid;

    keycode = id;
    modifiers = color;
    grab_wid = uw;

    for(i = 0; i < X6_MAX_KEY_GRABS; i++) {
      if(!key_grabs[i].in_use)
        continue;
      if(key_grabs[i].owner_fd != cfd)
        continue;
      if((keycode == 0 || key_grabs[i].keycode == keycode) &&
         (modifiers == X6_ANY_MODIFIER || key_grabs[i].modifiers == modifiers) &&
         key_grabs[i].wid == grab_wid)
        memset(&key_grabs[i], 0, sizeof(key_grabs[i]));
    }

    x6_send_line(cfd, "OK key_ungrabbed\n");
    return;
  }

  if(sscanf(cmd, "GRAB_BUTTON %u %u %u", &id, &color, &uw) == 3) {
    int slot;
    uint button;
    uint modifiers;
    uint grab_wid;

    button = id;
    modifiers = color;
    grab_wid = uw;

    slot = -1;
    for(i = 0; i < X6_MAX_BUTTON_GRABS; i++) {
      if(button_grabs[i].in_use &&
         button_grabs[i].owner_fd == cfd &&
         button_grabs[i].wid == grab_wid &&
         button_grabs[i].button == button &&
         button_grabs[i].modifiers == modifiers) {
        x6_send_line(cfd, "OK button_grabbed\n");
        return;
      }
      if(slot < 0 && !button_grabs[i].in_use)
        slot = i;
    }

    if(slot < 0) {
      x6_send_line(cfd, "ERR no-grab-slots\n");
      return;
    }

    button_grabs[slot].in_use = 1;
    button_grabs[slot].owner_fd = cfd;
    button_grabs[slot].wid = grab_wid;
    button_grabs[slot].button = button;
    button_grabs[slot].modifiers = modifiers;
    x6_send_line(cfd, "OK button_grabbed\n");
    return;
  }

  if(sscanf(cmd, "UNGRAB_BUTTON %u %u %u", &id, &color, &uw) == 3) {
    uint button;
    uint modifiers;
    uint grab_wid;

    button = id;
    modifiers = color;
    grab_wid = uw;

    for(i = 0; i < X6_MAX_BUTTON_GRABS; i++) {
      if(!button_grabs[i].in_use)
        continue;
      if(button_grabs[i].owner_fd != cfd)
        continue;
      if((button == 0 || button_grabs[i].button == button) &&
         (modifiers == X6_ANY_MODIFIER || button_grabs[i].modifiers == modifiers) &&
         button_grabs[i].wid == grab_wid)
        memset(&button_grabs[i], 0, sizeof(button_grabs[i]));
    }

    x6_send_line(cfd, "OK button_ungrabbed\n");
    return;
  }

  if(strncmp(cmd, "SET_SELECTION_OWNER", 19) == 0) {
    char sel[64];
    uint owner;
    uint t;
    struct x6_selection *s;
    uint prev_owner;

    if(sscanf(cmd, "SET_SELECTION_OWNER %63s %u %u", sel, &owner, &t) != 3) {
      x6_send_line(cfd, "ERR bad-syntax\n");
      return;
    }

    if(owner != 0 && owner != (uint)wm_redirect_root && !find_window(owner)) {
      x6_send_line(cfd, "ERR no-such-window\n");
      return;
    }

    s = x6_find_or_alloc_selection(sel);
    if(!s) {
      x6_send_line(cfd, "ERR no-selection-slots\n");
      return;
    }

    prev_owner = s->owner;
    s->owner = owner;
    s->time = t;

    if(prev_owner != 0 && prev_owner != owner) {
      struct x6_event evt;
      struct x6_event_queue *q;

      q = x6_queue_for_window(prev_owner);
      if(q) {
        memset(&evt, 0, sizeof(evt));
        evt.type = X6_EVENT_SELECTION_CLEAR;
        evt.wid = prev_owner;
        evt.time = t;
        strncpy(evt.atom, s->name, sizeof(evt.atom) - 1);
        evt.atom[sizeof(evt.atom) - 1] = '\0';
        x6_event_queue_enqueue(q, &evt);
      }
    }

    x6_send_line(cfd, "OK selection_owner_set\n");
    return;
  }

  if(strncmp(cmd, "GET_SELECTION_OWNER", 19) == 0) {
    char sel[64];
    struct x6_selection *s;
    char out[96];

    if(sscanf(cmd, "GET_SELECTION_OWNER %63s", sel) != 1) {
      x6_send_line(cfd, "ERR bad-syntax\n");
      return;
    }

    s = x6_find_selection(sel);
    snprintf(out, sizeof(out), "OK selection_owner %u\n", s ? s->owner : 0U);
    x6_send_line(cfd, out);
    return;
  }

  if(strncmp(cmd, "CONVERT_SELECTION", 17) == 0) {
    char sel[64];
    char target[64];
    char property[64];
    uint requestor;
    uint t;
    struct x6_selection *s;
    struct x6_event evt;
    struct x6_event_queue *q;

    if(sscanf(cmd, "CONVERT_SELECTION %63s %63s %63s %u %u",
              sel, target, property, &requestor, &t) != 5) {
      x6_send_line(cfd, "ERR bad-syntax\n");
      return;
    }

    if(target[0] == '\0') {
      x6_send_line(cfd, "ERR bad-target\n");
      return;
    }
    if(strcmp(property, "NONE") != 0 && property[0] == '\0') {
      x6_send_line(cfd, "ERR bad-property\n");
      return;
    }
    if(requestor != (uint)wm_redirect_root && !find_window(requestor)) {
      x6_send_line(cfd, "ERR no-requestor\n");
      return;
    }

    s = x6_find_selection(sel);
    if(!s || s->owner == 0 || (t != 0 && s->time != 0 && t < s->time)) {
      q = x6_queue_for_window(requestor);
      if(q) {
        memset(&evt, 0, sizeof(evt));
        evt.type = X6_EVENT_SELECTION_NOTIFY;
        evt.wid = requestor;
        evt.requestor = requestor;
        evt.time = t;
        strncpy(evt.atom, sel, sizeof(evt.atom) - 1);
        strncpy(evt.target_atom, target, sizeof(evt.target_atom) - 1);
        strncpy(evt.property_atom, "NONE", sizeof(evt.property_atom) - 1);
        x6_event_queue_enqueue(q, &evt);
      }
      x6_send_line(cfd, "OK selection_convert_none\n");
      return;
    }

    q = x6_queue_for_window(s->owner);
    if(!q) {
      x6_send_line(cfd, "ERR owner-unreachable\n");
      return;
    }

    memset(&evt, 0, sizeof(evt));
    evt.type = X6_EVENT_SELECTION_REQUEST;
    evt.wid = s->owner;
    evt.requestor = requestor;
    evt.time = t;
    strncpy(evt.atom, sel, sizeof(evt.atom) - 1);
    strncpy(evt.target_atom, target, sizeof(evt.target_atom) - 1);
    strncpy(evt.property_atom, property, sizeof(evt.property_atom) - 1);
    x6_event_queue_enqueue(q, &evt);

    x6_send_line(cfd, "OK selection_convert_queued\n");
    return;
  }

  if(strncmp(cmd, "QUEUE_SELECTION_NOTIFY", 22) == 0) {
    char sel[64];
    char target[64];
    char property[64];
    uint requestor;
    uint t;
    struct x6_event evt;
    struct x6_event_queue *q;

    if(sscanf(cmd, "QUEUE_SELECTION_NOTIFY %u %63s %63s %63s %u",
              &requestor, sel, target, property, &t) != 5) {
      x6_send_line(cfd, "ERR bad-syntax\n");
      return;
    }

    if(target[0] == '\0') {
      x6_send_line(cfd, "ERR bad-target\n");
      return;
    }
    if(strcmp(property, "NONE") != 0 && property[0] == '\0') {
      x6_send_line(cfd, "ERR bad-property\n");
      return;
    }

    q = x6_queue_for_window(requestor);
    if(!q) {
      x6_send_line(cfd, "ERR no-requestor\n");
      return;
    }

    memset(&evt, 0, sizeof(evt));
    evt.type = X6_EVENT_SELECTION_NOTIFY;
    evt.wid = requestor;
    evt.requestor = requestor;
    evt.time = t;
    strncpy(evt.atom, sel, sizeof(evt.atom) - 1);
    strncpy(evt.target_atom, target, sizeof(evt.target_atom) - 1);
    strncpy(evt.property_atom, property, sizeof(evt.property_atom) - 1);
    x6_event_queue_enqueue(q, &evt);

    x6_send_line(cfd, "OK selection_notify_queued\n");
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
    char out[4096];
    snprintf(out, sizeof(out), "VALUE %s %s\n", atom, prop->value);
    x6_send_line(cfd, out);
    return;
  }

  if(strncmp(cmd, "SET_PROPERTY", 12) == 0) {
    uint wid;
    char atom[128];
    char value[4096];
    int n;
    char *p;
    struct x6_window *win;
    struct x6_property *prop;

    // Parse: SET_PROPERTY <wid> <atom> <value...>
    if(sscanf(cmd, "SET_PROPERTY %u %127s%n", &wid, atom, &n) != 2) {
      x6_send_line(cfd, "ERR bad-syntax\n");
      return;
    }

    p = cmd + n;
    while(*p == ' ')
      p++;
    strncpy(value, p, sizeof(value) - 1);
    value[sizeof(value) - 1] = '\0';

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

    x6_enqueue_property_notify(win, atom, 0);

    x6_send_line(cfd, "OK property_set\n");
    return;
  }

  if(strncmp(cmd, "DELETE_PROPERTY", 15) == 0) {
    uint wid;
    char atom[128];
    int pi;

    if(sscanf(cmd, "DELETE_PROPERTY %u %127s", &wid, atom) != 2) {
      x6_send_line(cfd, "ERR bad-syntax\n");
      return;
    }

    win = find_window(wid);
    if(!win) {
      x6_send_line(cfd, "ERR no-such-window\n");
      return;
    }

    for(pi = 0; pi < win->prop_count; pi++) {
      if(strcmp(win->props[pi].name, atom) != 0)
        continue;
      for(; pi + 1 < win->prop_count; pi++)
        win->props[pi] = win->props[pi + 1];
      win->prop_count--;
      x6_enqueue_property_notify(win, atom, 1);
      x6_send_line(cfd, "OK property_deleted\n");
      return;
    }

    x6_send_line(cfd, "OK property_deleted\n");
    return;
  }
  if(strncmp(cmd, "GET_WINDOW_ATTR", 15) == 0) {
    uint wid;
    struct x6_window *win;
    char out[256];

    if(sscanf(cmd, "GET_WINDOW_ATTR %u", &wid) != 1) {
      x6_send_line(cfd, "ERR bad-syntax\n");
      return;
    }

    if(wid == (uint)wm_redirect_root) {
      int sw, sh;
      x6_screen_size(&sw, &sh);
      snprintf(out, sizeof(out),
               "OK attr x=0 y=0 w=%d h=%d bw=0 depth=32 mapped=1 override=0 events=0\n",
               sw, sh);
      x6_send_line(cfd, out);
      return;
    }

    win = find_window(wid);
    if(!win) {
      x6_send_line(cfd, "ERR no-such-window\n");
      return;
    }

    snprintf(out, sizeof(out),
             "OK attr x=%d y=%d w=%d h=%d bw=%d depth=32 mapped=%d override=%d events=%ld\n",
             win->x, win->y, win->w, win->h,
             win->border_width, win->mapped ? 1 : 0,
             win->override_redirect ? 1 : 0, win->event_mask);
    x6_send_line(cfd, out);
    return;
  }
  if(strncmp(cmd, "SET_BORDER_WIDTH", 16) == 0) {
    uint wid;
    int bw;
    struct x6_window *win;

    if(sscanf(cmd, "SET_BORDER_WIDTH %u %d", &wid, &bw) != 2) {
      x6_send_line(cfd, "ERR bad-syntax\n");
      return;
    }
    win = find_window(wid);
    if(!win) {
      x6_send_line(cfd, "ERR no-such-window\n");
      return;
    }
    if(bw < 0)
      bw = 0;
    win->border_width = bw;
    x6_send_line(cfd, "OK border_width_set\n");
    return;
  }
  if(strncmp(cmd, "SET_BORDER_COLOR", 16) == 0) {
    uint wid;
    uint color;
    struct x6_window *win;

    if(sscanf(cmd, "SET_BORDER_COLOR %u %u", &wid, &color) != 2) {
      x6_send_line(cfd, "ERR bad-syntax\n");
      return;
    }
    win = find_window(wid);
    if(!win) {
      x6_send_line(cfd, "ERR no-such-window\n");
      return;
    }
    win->border_pixel = color;
    x6_send_line(cfd, "OK border_color_set\n");
    return;
  }
  if(strncmp(cmd, "SET_OVERRIDE_REDIRECT", 21) == 0) {
    uint wid;
    int flag;
    struct x6_window *win;

    if(sscanf(cmd, "SET_OVERRIDE_REDIRECT %u %d", &wid, &flag) != 2) {
      x6_send_line(cfd, "ERR bad-syntax\n");
      return;
    }
    win = find_window(wid);
    if(!win) {
      x6_send_line(cfd, "ERR no-such-window\n");
      return;
    }
    win->override_redirect = flag ? 1 : 0;
    x6_send_line(cfd, "OK override_set\n");
    return;
  }
  if(strncmp(cmd, "SELECT_EVENTS", 13) == 0) {
    uint wid;
    long mask;
    struct x6_window *win;
    if(sscanf(cmd, "SELECT_EVENTS %u %ld", &wid, &mask) != 2) {
      x6_send_line(cfd, "ERR bad-syntax\n");
      return;
    }
    win = find_window(wid);
    if(!win) {
      /* Allow root window subscription even if not in wins[] table. */
      x6_send_line(cfd, "OK events_set\n");
      return;
    }
    win->event_mask = mask;
    x6_send_line(cfd, "OK events_set\n");
    return;
  }

  x6_send_line(cfd, "ERR unknown\n");
}

static void
x6_flush_client_events(struct x6_client *client)
{
  char eventbuf[256];
  struct x6_event evt;

  if(client == 0 || !client->in_use || !client->hello_done)
    return;

  while(!x6_event_queue_empty(&client->queue)) {
    if(x6_event_queue_dequeue(&client->queue, &evt) < 0)
      break;
    if(evt.type == X6_EVENT_MAP_REQUEST) {
      snprintf(eventbuf, sizeof(eventbuf), "EVENT MapRequest wid=%u\n", evt.wid);
    } else if(evt.type == X6_EVENT_CONFIGURE_NOTIFY) {
      snprintf(eventbuf, sizeof(eventbuf),
               "EVENT ConfigureNotify wid=%u x=%d y=%d w=%d h=%d\n",
               evt.wid, evt.x, evt.y, evt.w, evt.h);
    } else if(evt.type == X6_EVENT_EXPOSE) {
      snprintf(eventbuf, sizeof(eventbuf),
               "EVENT Expose wid=%u x=%d y=%d w=%d h=%d\n",
               evt.wid, evt.x, evt.y, evt.w, evt.h);
    } else if(evt.type == X6_EVENT_CLIENT_MESSAGE) {
      snprintf(eventbuf, sizeof(eventbuf),
               "EVENT ClientMessage wid=%u type=%u data0=%u\n",
               evt.wid, (uint)evt.keycode, evt.data0);
    } else if(evt.type == X6_EVENT_CONFIGURE_REQUEST) {
      snprintf(eventbuf, sizeof(eventbuf),
               "EVENT ConfigureRequest wid=%u geom=%d,%d %dx%d\n",
               evt.wid, evt.x, evt.y, evt.w, evt.h);
    } else if(evt.type == X6_EVENT_FOCUS_IN) {
      snprintf(eventbuf, sizeof(eventbuf), "EVENT FocusIn wid=%u mode=%d detail=%d\n",
               evt.wid, evt.mode, evt.detail);
    } else if(evt.type == X6_EVENT_FOCUS_OUT) {
      snprintf(eventbuf, sizeof(eventbuf), "EVENT FocusOut wid=%u mode=%d detail=%d\n",
               evt.wid, evt.mode, evt.detail);
    } else if(evt.type == X6_EVENT_DESTROY_NOTIFY) {
      snprintf(eventbuf, sizeof(eventbuf), "EVENT DestroyNotify wid=%u\n", evt.wid);
    } else if(evt.type == X6_EVENT_KEY_PRESS) {
      snprintf(eventbuf, sizeof(eventbuf), "EVENT KeyPress wid=%u keycode=%d state=%u time=%u\n",
               evt.wid, evt.keycode, evt.state, ++x6_event_time);
    } else if(evt.type == X6_EVENT_KEY_RELEASE) {
      snprintf(eventbuf, sizeof(eventbuf), "EVENT KeyRelease wid=%u keycode=%d state=%u time=%u\n",
               evt.wid, evt.keycode, evt.state, ++x6_event_time);
    } else if(evt.type == X6_EVENT_BUTTON_PRESS) {
      snprintf(eventbuf, sizeof(eventbuf), "EVENT ButtonPress wid=%u button=%d state=%u x=%d y=%d time=%u\n",
               evt.wid, evt.button, evt.state, evt.x, evt.y, ++x6_event_time);
    } else if(evt.type == X6_EVENT_BUTTON_RELEASE) {
      snprintf(eventbuf, sizeof(eventbuf), "EVENT ButtonRelease wid=%u button=%d state=%u x=%d y=%d time=%u\n",
               evt.wid, evt.button, evt.state, evt.x, evt.y, ++x6_event_time);
    } else if(evt.type == X6_EVENT_MOTION_NOTIFY) {
      snprintf(eventbuf, sizeof(eventbuf), "EVENT MotionNotify wid=%u x=%d y=%d state=%u time=%u\n",
               evt.wid, evt.x, evt.y, evt.state, ++x6_event_time);
    } else if(evt.type == X6_EVENT_PROPERTY_NOTIFY) {
      snprintf(eventbuf, sizeof(eventbuf), "EVENT PropertyNotify wid=%u atom=%s state=%u time=%u\n",
               evt.wid, evt.atom[0] ? evt.atom : "", evt.state, ++x6_event_time);
    } else if(evt.type == X6_EVENT_ENTER_NOTIFY) {
      snprintf(eventbuf, sizeof(eventbuf),
               "EVENT EnterNotify wid=%u x=%d y=%d state=%u mode=%d detail=%d focus=%d same=%d time=%u\n",
               evt.wid, evt.x, evt.y, evt.state, evt.mode, evt.detail,
               evt.focus, evt.same_screen, ++x6_event_time);
    } else if(evt.type == X6_EVENT_LEAVE_NOTIFY) {
      snprintf(eventbuf, sizeof(eventbuf),
               "EVENT LeaveNotify wid=%u x=%d y=%d state=%u mode=%d detail=%d focus=%d same=%d time=%u\n",
               evt.wid, evt.x, evt.y, evt.state, evt.mode, evt.detail,
               evt.focus, evt.same_screen, ++x6_event_time);
    } else if(evt.type == X6_EVENT_SELECTION_CLEAR) {
      snprintf(eventbuf, sizeof(eventbuf), "EVENT SelectionClear wid=%u selection=%s time=%u\n",
               evt.wid, evt.atom, evt.time ? evt.time : ++x6_event_time);
    } else if(evt.type == X6_EVENT_SELECTION_REQUEST) {
      snprintf(eventbuf, sizeof(eventbuf),
               "EVENT SelectionRequest owner=%u requestor=%u selection=%s target=%s property=%s time=%u\n",
               evt.wid, evt.requestor, evt.atom, evt.target_atom, evt.property_atom,
               evt.time ? evt.time : ++x6_event_time);
    } else if(evt.type == X6_EVENT_SELECTION_NOTIFY) {
      snprintf(eventbuf, sizeof(eventbuf),
               "EVENT SelectionNotify requestor=%u selection=%s target=%s property=%s time=%u\n",
               evt.requestor ? evt.requestor : evt.wid,
               evt.atom, evt.target_atom, evt.property_atom,
               evt.time ? evt.time : ++x6_event_time);
    } else if(evt.type == X6_EVENT_MAP_NOTIFY) {
      snprintf(eventbuf, sizeof(eventbuf), "EVENT MapNotify wid=%u\n", evt.wid);
    } else {
      continue;
    }
    x6_send_line(client->fd, eventbuf);
  }
}

static void
x6_disconnect_client(struct x6_client *client)
{
  int si;

  if(client == 0 || !client->in_use)
    return;

  if(wm_client_fd == client->fd) {
    wm_event_queue = 0;
    wm_client_fd = -1;
    wm_has_redirect = 0;
    wm_has_kb_grab = 0;
    keyboard_grab_owner = 0;
  }

  x6_clear_key_grabs_for_fd(client->fd);
  x6_clear_button_grabs_for_fd(client->fd);

  for(si = 0; si < X6_MAX_SELECTIONS; si++) {
    struct x6_window *owner;
    if(!selections[si].in_use || selections[si].owner == 0)
      continue;
    owner = find_window(selections[si].owner);
    if(owner && owner->owner_fd == client->fd) {
      selections[si].owner = 0;
      selections[si].time = 0;
    }
  }

  close(client->fd);
  memset(client, 0, sizeof(*client));
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

  x6_keyboard_setup();
  x6_mouse_setup();
  pointer_x = x6_fb.width > 0 ? (x6_fb.width / 2) : 0;
  pointer_y = x6_fb.height > 0 ? (x6_fb.height / 2) : 0;
  pointer_state = 0;
  x6_event_time = 0;
  pointer_grab_active = 0;
  pointer_grab_window = 0;
  root_cursor = 1;
  memset(&x6_cursor, 0, sizeof(x6_cursor));

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

    x6_cursor_show();

  while(keep_running) {
    struct pollfd pfds[3 + X6_MAX_CLIENTS];
    int client_index[3 + X6_MAX_CLIENTS];
    int nfds;
    int i;
    int pr;
    int listen_index;
    int mouse_poll_index;
    int kbd_poll_index;

    for(i = 0; i < X6_MAX_CLIENTS; i++)
      x6_flush_client_events(&clients[i]);

    nfds = 0;
    listen_index = nfds;
    pfds[nfds].fd = fd;
    pfds[nfds].events = POLLIN;
    pfds[nfds].revents = 0;
    client_index[nfds] = -1;
    nfds++;

    mouse_poll_index = -1;
    if(x6_mouse_fd >= 0) {
      mouse_poll_index = nfds;
      pfds[nfds].fd = x6_mouse_fd;
      pfds[nfds].events = POLLIN;
      pfds[nfds].revents = 0;
      client_index[nfds] = -1;
      nfds++;
    }

    kbd_poll_index = -1;
    if(x6_kbd_fd >= 0) {
      kbd_poll_index = nfds;
      pfds[nfds].fd = x6_kbd_fd;
      pfds[nfds].events = POLLIN;
      pfds[nfds].revents = 0;
      client_index[nfds] = -1;
      nfds++;
    }

    for(i = 0; i < X6_MAX_CLIENTS; i++) {
      if(!clients[i].in_use)
        continue;
      pfds[nfds].fd = clients[i].fd;
      pfds[nfds].events = POLLIN;
      pfds[nfds].revents = 0;
      client_index[nfds] = i;
      nfds++;
    }

    pr = poll(pfds, (nfds_t)nfds, X6_POLL_MS);
    if(pr < 0)
      continue;

    if(kbd_poll_index >= 0 && (pfds[kbd_poll_index].revents & POLLIN))
      x6_pump_keyboard();
    if(mouse_poll_index >= 0 && (pfds[mouse_poll_index].revents & POLLIN))
      x6_pump_mouse();

    if(pfds[listen_index].revents & POLLIN) {
      int cfd;
      cfd = accept(fd);
      if(cfd >= 0) {
        for(i = 0; i < X6_MAX_CLIENTS; i++) {
          if(!clients[i].in_use) {
            memset(&clients[i], 0, sizeof(clients[i]));
            clients[i].in_use = 1;
            clients[i].fd = cfd;
            x6_event_queue_init(&clients[i].queue);
            x6_send_line(cfd, "X6/1 READY\n");
            cfd = -1;
            break;
          }
        }
        if(cfd >= 0) {
          x6_send_line(cfd, "ERR busy\n");
          close(cfd);
        }
      }
    }

    for(i = 0; i < nfds; i++) {
      char line[192];
      struct x6_client *client;
      int idx;
      int should_detach;

      idx = client_index[i];
      if(idx < 0)
        continue;
      client = &clients[idx];
      if(!client->in_use)
        continue;
      if((pfds[i].revents & (POLLIN | POLLHUP | POLLERR)) == 0)
        continue;
      if(x6_client_fill_rxbuf(client->fd, client->rxbuf, &client->rxlen, sizeof(client->rxbuf)) < 0) {
        x6_disconnect_client(client);
        continue;
      }

      should_detach = 0;
      while(client->in_use && x6_client_next_line(client->rxbuf, &client->rxlen, line, sizeof(line)) > 0) {
        if(line[0] == 0)
          continue;
        if(!client->logged_first_cmd)
          client->logged_first_cmd = 1;
        if(strncmp(line, "HELLO x6/1", 10) == 0)
          client->hello_done = 1;
        current_event_queue = &client->queue;
        handle_one_command(client->fd, line);
        current_event_queue = 0;
        if(strncmp(line, "QUIT", 4) == 0 || strncmp(line, "DETACH", 6) == 0) {
          should_detach = 1;
          break;
        }
      }
      if(should_detach)
        x6_disconnect_client(client);
    }
  }

  for(i = 0; i < X6_MAX_CLIENTS; i++)
    x6_disconnect_client(&clients[i]);

  close(fd);
  if(x6_kbd_fd >= 0)
    close(x6_kbd_fd);
  x6_kbd_fd = -1;
  x6_fb_shutdown();
  x6_release_display();
  dprintf(1, "x6: exiting\n");
  return 0;
}
