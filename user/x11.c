#include "types.h"
#include "auxv6/user.h"
#include "socket.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "poll.h"
#include "sys/select.h"
#include "signal.h"
#include "time.h"
#include "errno.h"
#include <stdarg.h>
#include <fcntl.h>
#include "X11/Xlib.h"
#include "X11/Xutil.h"
#include "X11/Xresource.h"
#include "X11/keysym.h"
#include "X11/extensions/Xcomposite.h"
#include "X11/extensions/Xdamage.h"
#include "X11/extensions/Xfixes.h"
#include "X11/extensions/Xinerama.h"
#include "X11/extensions/Xrandr.h"
#include "X11/extensions/Xrender.h"
#include "X11/extensions/XRes.h"
#include "X11/extensions/XShm.h"
#include "X11/extensions/shape.h"
#include "X11/Xft/Xft.h"

#define X6_PORT 6006
#define X6_BUF_SIZE 4096
#define X11_RXBUF_SIZE 4096
#define X11_MAX_ATOMS 256
#define X11_MAX_GCS 128
#define X11_MAX_PIXMAPS 256
#define X11_MAX_FONTS 16
#define X11_MAX_PICTURES 256
#define X11_MAX_DAMAGE 256
#define X11_MAX_COMPOSITE_REDIRECTS 128
#define X11_MAX_SHAPE_WINDOWS 256
#define X11_MAX_RANDR_SELECTS 128
#define X11_MAX_SHM_IMAGES 256
#define X11_MAX_SELECTIONS 16
#define X11_MAX_EVENTS 128
#define X11_MAX_CONTEXT_ENTRIES 512
#define X11_X6_ANY_MODIFIER (1U << 15)
#define DoRed 1
#define DoGreen 2
#define DoBlue 4

#define X11_EXT_EVENT_BASE_COMPOSITE 80
#define X11_EXT_EVENT_BASE_DAMAGE 90
#define X11_EXT_EVENT_BASE_RANDR 100
#define X11_EXT_EVENT_BASE_SHAPE 110
#define X11_EXT_EVENT_BASE_XFIXES 120
#define X11_EXT_EVENT_BASE_XINERAMA 130
#define X11_EXT_EVENT_BASE_XSHM 140
#define X11_EXT_EVENT_BASE_RENDER 150

#define X11_EXT_OPCODE_COMPOSITE 130
#define X11_EXT_OPCODE_DAMAGE 131
#define X11_EXT_OPCODE_RANDR 132
#define X11_EXT_OPCODE_SHAPE 133
#define X11_EXT_OPCODE_XINERAMA 134
#define X11_EXT_OPCODE_XFIXES 135
#define X11_EXT_OPCODE_XSHM 136
#define X11_EXT_OPCODE_RENDER 137
#define X11_EXT_OPCODE_XRES 138

#define X11_EXT_ERROR_BASE_COMPOSITE 160
#define X11_EXT_ERROR_BASE_DAMAGE 170
#define X11_EXT_ERROR_BASE_RANDR 180
#define X11_EXT_ERROR_BASE_SHAPE 190
#define X11_EXT_ERROR_BASE_XFIXES 200
#define X11_EXT_ERROR_BASE_XINERAMA 210
#define X11_EXT_ERROR_BASE_XSHM 220

#ifndef XValue
#define XValue 0x0001
#endif
#ifndef YValue
#define YValue 0x0002
#endif
#ifndef WidthValue
#define WidthValue 0x0004
#endif
#ifndef HeightValue
#define HeightValue 0x0008
#endif

typedef struct {
  Atom atom;
  char name[64];
} atom_entry;

static Display *g_display;
static unsigned long g_next_atom = 128;
static atom_entry g_atoms[X11_MAX_ATOMS];
static int g_atom_count;
static int (*g_error_handler)(Display *, XErrorEvent *);
static unsigned long g_next_gc = 1;
static GC g_xft_gc;
static int g_x11_dbg_count;
static int g_x11_draw_tx_count;
static int g_x11_draw_call_count;
static int g_x11_draw_reply_seen;
static Drawable g_copy_area_last_src;
static Drawable g_copy_area_last_dest;
static int g_copy_area_last_src_x;
static int g_copy_area_last_src_y;
static unsigned int g_copy_area_last_w;
static unsigned int g_copy_area_last_h;
static int g_copy_area_last_dest_x;
static int g_copy_area_last_dest_y;
static uint g_copy_area_last_tick;
static uint g_copy_area_trace_seq;
static uint g_copy_area_drop_seq;
static unsigned long g_ext_event_serial = 1;
static Time g_ext_event_time = 1;
static unsigned long g_next_cursor_id = 0x10000;
static int g_x11_sigpipe_ignored;

static XEvent *
x11_event_queue(Display *display)
{
  if (!display || !display->event_queue)
    return 0;
  return (XEvent *)display->event_queue;
}

static void x11dbg(const char *fmt, ...);

#define X11_XRM_MAX_ENTRIES 256

struct x11_xrm_entry {
  char *name;
  char *value;
};

struct _XrmHashBucketRec {
  int count;
  struct x11_xrm_entry entries[X11_XRM_MAX_ENTRIES];
};

struct x11_context_entry {
  int in_use;
  XID rid;
  XContext context;
  const char *data;
};

struct _XRegion {
  XRectangle extents;
};

static char *g_xrm_root_string;
static struct x11_context_entry g_context_entries[X11_MAX_CONTEXT_ENTRIES];
static XContext g_next_context_id = 1;

#define X11_DRAW_BATCH_CAP 65536

static int
x11_is_chatty_draw_cmd(const char *cmd)
{
  if (!cmd)
    return 0;
  return strncmp(cmd, "DRAW_TEXT ", 10) == 0 ||
         strncmp(cmd, "DRAW_RECT ", 10) == 0 ||
         strncmp(cmd, "DRAW_LINE ", 10) == 0 ||
         strncmp(cmd, "COPY_AREA ", 10) == 0;
}

/* Flush all buffered draw commands to the server in one send() call.
 * Called from x11_cmd (before any synchronous request) and from XFlush. */
static int
x11_flush_draw_batch(Display *dpy)
{
  size_t off;
  int newlines;
  int i;
  int fd;
  int flags;
  int restore_flags;
  int dropped;

  if (!dpy || dpy->draw_batch_len <= 0)
    return 0;

  fd = dpy->fd;
  restore_flags = 0;
  dropped = 0;
  flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0 && (flags & O_NONBLOCK) == 0) {
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0)
      restore_flags = 1;
  }

  off = 0;
  while (off < (size_t)dpy->draw_batch_len) {
    int n = send(fd, dpy->draw_batch_buf + off,
                 (size_t)dpy->draw_batch_len - off);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        dropped = dpy->draw_batch_len - (int)off;
        break;
      }
      if (restore_flags)
        fcntl(fd, F_SETFL, flags);
      dpy->draw_batch_len = 0;
      return -1;
    }
    if (n == 0) {
      if (restore_flags)
        fcntl(fd, F_SETFL, flags);
      dpy->draw_batch_len = 0;
      return -1;
    }
    off += (size_t)n;
  }

  if (restore_flags)
    fcntl(fd, F_SETFL, flags);

  if (dropped > 0)
    x11dbg("x11:draw-batch dropped bytes=%d queued=%d", dropped, dpy->draw_batch_len);

  /* Count commands (newlines) for debug accounting only.
   * Draw operations are now fire-and-forget and do not require replies. */
  newlines = 0;
  for (i = 0; i < dpy->draw_batch_len; i++)
    if (dpy->draw_batch_buf[i] == '\n')
      newlines++;

  g_x11_draw_tx_count += newlines;

  (void)0; /* debug logging deliberately omitted: x11dbg not yet declared */

  dpy->draw_batch_len = 0;
  dpy->draw_batch_last_copy_area_off = -1;
  dpy->draw_batch_last_copy_area_len = 0;
  return 0;
}

/* Append a draw command (already formatted with trailing '\n') to the client-
 * side batch buffer.  Flushes automatically when the buffer nears capacity. */
static int
x11_batch_draw(Display *dpy, const char *cmd)
{
  int len;
  int is_copy_area;

  if (!dpy || !cmd)
    return -1;
  if (dpy->fd < 0)
    return -1;

  len = (int)strlen(cmd);
  is_copy_area = (strncmp(cmd, "COPY_AREA ", 10) == 0);

  /* Presentation blits are bursty under scroll storms. Keep only the latest
   * tail COPY_AREA in the pending draw batch so stale presents don't backlog. */
  if (is_copy_area &&
      dpy->draw_batch_last_copy_area_off >= 0 &&
      dpy->draw_batch_last_copy_area_off + dpy->draw_batch_last_copy_area_len == dpy->draw_batch_len) {
    dpy->draw_batch_len = dpy->draw_batch_last_copy_area_off;
    dpy->draw_batch_last_copy_area_off = -1;
    dpy->draw_batch_last_copy_area_len = 0;
  }

  /* Flush first if this command would overflow. */
  if (dpy->draw_batch_len + len >= dpy->draw_batch_cap - 16) {
    if (x11_flush_draw_batch(dpy) < 0)
      return -1;
  }

  memmove(dpy->draw_batch_buf + dpy->draw_batch_len, cmd, (size_t)len);
  if (is_copy_area) {
    dpy->draw_batch_last_copy_area_off = dpy->draw_batch_len;
    dpy->draw_batch_last_copy_area_len = len;
  }
  dpy->draw_batch_len += len;
  return 0;
}

static Time
x11_next_fake_time(void)
{
  return g_ext_event_time++;
}

static void
x11_stamp_synthetic_event(XEvent *ev)
{
  if (!ev)
    return;
  ev->xany.serial = g_ext_event_serial++;
  ev->xany.send_event = False;
}

static void
x11dbg(const char *fmt, ...)
{
  char msg[280];
  char line[320];
  int n;
  int fd;
  va_list ap;

  if (g_x11_dbg_count >= 5000)
    return;
  g_x11_dbg_count++;

  va_start(ap, fmt);
  n = vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  if (n < 0)
    return;
  n = snprintf(line, sizeof(line), "pid=%d %s", getpid(), msg);
  if (n < 0)
    return;
  if ((size_t)n >= sizeof(line))
    n = (int)sizeof(line) - 1;

  line[n++] = '\n';
  line[n] = '\0';

  fd = open("/tmp/x11-debug.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd >= 0) {
    write(fd, line, (size_t)n);
    close(fd);
  }
}

/* Critical-path logger that bypasses x11dbg volume cap. */
static void
x11crit(const char *fmt, ...)
{
  char msg[280];
  char line[320];
  int n;
  int fd;
  va_list ap;

  va_start(ap, fmt);
  n = vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  if (n < 0)
    return;
  n = snprintf(line, sizeof(line), "pid=%d %s", getpid(), msg);
  if (n < 0)
    return;
  if ((size_t)n >= sizeof(line))
    n = (int)sizeof(line) - 1;

  line[n++] = '\n';
  line[n] = '\0';

  fd = open("/tmp/x11-debug.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd >= 0) {
    write(fd, line, (size_t)n);
    close(fd);
  }
}

static void
x11consolecrit(const char *fmt, ...)
{
  char msg[280];
  char line[320];
  int n;
  int fd;
  va_list ap;

  va_start(ap, fmt);
  n = vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  if (n < 0)
    return;
  n = snprintf(line, sizeof(line), "x11trace: pid=%d %s", getpid(), msg);
  if (n < 0)
    return;
  if ((size_t)n >= sizeof(line))
    n = (int)sizeof(line) - 1;
  line[n++] = '\n';
  line[n] = '\0';

  fd = open("/dev/console", O_WRONLY);
  if (fd >= 0) {
    write(fd, line, (size_t)n);
    close(fd);
  }
}

struct x11_selection_state {
  int in_use;
  Atom selection;
  Window owner;
  Time time;
};

static struct x11_selection_state g_selections[X11_MAX_SELECTIONS];

struct x11_fc_pattern {
  char name[128];
  double pixel_size;
  int has_pixel_size;
  int slant;
  int has_slant;
  int weight;
  int has_weight;
};

struct x11_gc_state {
  int in_use;
  GC id;
  unsigned long fg;
  unsigned long bg;
  int function;
  int fill_style;
  Pixmap tile;
  int ts_x_origin;
  int ts_y_origin;
  Pixmap clip_mask;
  int clip_x_origin;
  int clip_y_origin;
  int dash_offset;
  char dashes;
};

struct x11_pixmap_state {
  int in_use;
  Pixmap id;
  unsigned int width;
  unsigned int height;
  unsigned int depth;
  unsigned int *pixels;
};

struct x11_font_state {
  int in_use;
  Font id;
  char name[128];
  int ascent;
  int descent;
  int height;
  int width;  /* For monospace fonts */
};

struct x11_picture_state {
  int in_use;
  Picture id;
  Drawable drawable;
  XRenderPictFormat format;
};

struct x11_damage_state {
  int in_use;
  Damage id;
  Drawable drawable;
  int level;
  int pending;
  int pending_x;
  int pending_y;
  unsigned int pending_w;
  unsigned int pending_h;
};

struct x11_composite_redirect_state {
  int in_use;
  Window w;
  int update;
};

struct x11_shape_state {
  int in_use;
  Window w;
  Bool bounding_shaped;
  int x_bounding;
  int y_bounding;
  unsigned int w_bounding;
  unsigned int h_bounding;
  Bool clip_shaped;
  int x_clip;
  int y_clip;
  unsigned int w_clip;
  unsigned int h_clip;
  unsigned long event_mask;
};

struct x11_randr_select_state {
  int in_use;
  Window w;
  int mask;
};

struct x11_shm_image_state {
  int in_use;
  XImage *image;
  void *shmseg;
};

struct _XOC {
  Font font_id;
  char *font_name;
  XFontStruct font;
  XFontStruct *font_list_entry[1];
  char *font_name_list_entry[1];
};

/* Color allocation cache */
#define X11_MAX_COLORS 256
struct x11_color_entry {
  int in_use;
  unsigned long pixel;
  unsigned short red;
  unsigned short green;
  unsigned short blue;
};
static struct x11_color_entry g_colors[X11_MAX_COLORS];

static struct x11_font_state g_fonts[X11_MAX_FONTS];
static unsigned long g_next_font = 1;

static struct x11_gc_state g_gcs[X11_MAX_GCS];
static struct x11_pixmap_state g_pixmaps[X11_MAX_PIXMAPS];
static unsigned long g_next_pixmap = 2;  // Start after window IDs
static struct x11_picture_state g_pictures[X11_MAX_PICTURES];
static unsigned long g_next_picture = 1;
static struct x11_damage_state g_damages[X11_MAX_DAMAGE];
static unsigned long g_next_damage = 1;
static struct x11_composite_redirect_state g_composite_redirects[X11_MAX_COMPOSITE_REDIRECTS];
static struct x11_shape_state g_shapes[X11_MAX_SHAPE_WINDOWS];
static struct x11_randr_select_state g_randr_selects[X11_MAX_RANDR_SELECTS];
static struct x11_shm_image_state g_shm_images[X11_MAX_SHM_IMAGES];
static struct x11_pixmap_state *x11_find_pixmap(Pixmap pm);
static int x11_event_push_back(Display *display, const XEvent *ev);
static int x11_cmd(Display *dpy, const char *cmd, char *resp, int maxlen);

static int
x11_tolower_ascii(int c)
{
  if (c >= 'A' && c <= 'Z')
    return c - 'A' + 'a';
  return c;
}

static int
x11_strieq(const char *a, const char *b)
{
  int ca;
  int cb;

  if (!a || !b)
    return 0;
  while (*a && *b) {
    ca = x11_tolower_ascii((unsigned char)*a);
    cb = x11_tolower_ascii((unsigned char)*b);
    if (ca != cb)
      return 0;
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

static void
x11_set_xcolor(XColor *c, unsigned short r, unsigned short g, unsigned short b)
{
  if (!c)
    return;
  c->red = r;
  c->green = g;
  c->blue = b;
  c->pixel = ((unsigned long)(r >> 8) << 16) |
             ((unsigned long)(g >> 8) << 8) |
             (unsigned long)(b >> 8);
  c->flags = DoRed | DoGreen | DoBlue;
}

static int
x11_named_color_level(int level)
{
  switch (level) {
  case 1: return 0xffff;
  case 2: return 0xeeee;
  case 3: return 0xcdcd;
  case 4: return 0x8b8b;
  default:
    return -1;
  }
}

static int
x11_parse_named_color(const char *spec, XColor *c)
{
  char base[32];
  int bi;
  int i;
  int level;
  int gray;
  unsigned short v;
  int rv;

  if (!spec || !c)
    return 0;

  bi = 0;
  i = 0;
  while (spec[i] && bi < (int)sizeof(base) - 1) {
    if (spec[i] >= '0' && spec[i] <= '9')
      break;
    base[bi++] = (char)x11_tolower_ascii((unsigned char)spec[i]);
    i++;
  }
  base[bi] = '\0';

  level = 0;
  if (spec[i] >= '0' && spec[i] <= '9')
    level = atoi(spec + i);

  if (x11_strieq(base, "black")) {
    x11_set_xcolor(c, 0, 0, 0);
    return 1;
  }
  if (x11_strieq(base, "white")) {
    x11_set_xcolor(c, 0xffff, 0xffff, 0xffff);
    return 1;
  }

  if (x11_strieq(base, "gray") || x11_strieq(base, "grey")) {
    gray = level;
    if (gray < 0)
      gray = 0;
    if (gray > 100)
      gray = 100;
    v = (unsigned short)((gray * 65535) / 100);
    x11_set_xcolor(c, v, v, v);
    return 1;
  }

  rv = (level > 0) ? x11_named_color_level(level) : 0xffff;
  if (rv < 0)
    return 0;

  if (x11_strieq(base, "red")) {
    x11_set_xcolor(c, (unsigned short)rv, 0, 0);
    return 1;
  }
  if (x11_strieq(base, "green")) {
    x11_set_xcolor(c, 0, (unsigned short)rv, 0);
    return 1;
  }
  if (x11_strieq(base, "blue")) {
    x11_set_xcolor(c, 0, 0, (unsigned short)rv);
    return 1;
  }
  if (x11_strieq(base, "yellow")) {
    x11_set_xcolor(c, (unsigned short)rv, (unsigned short)rv, 0);
    return 1;
  }
  if (x11_strieq(base, "magenta")) {
    x11_set_xcolor(c, (unsigned short)rv, 0, (unsigned short)rv);
    return 1;
  }
  if (x11_strieq(base, "cyan")) {
    x11_set_xcolor(c, 0, (unsigned short)rv, (unsigned short)rv);
    return 1;
  }

  return 0;
}

static GC
x11_xft_gc(Display *display, Drawable d)
{
  if (!display)
    return 0;
  if (g_xft_gc)
    return g_xft_gc;
  g_xft_gc = XCreateGC(display, d, 0, 0);
  return g_xft_gc;
}

static int
x11_encode_modifiers(unsigned int modifiers)
{
  if (modifiers == AnyModifier)
    return X11_X6_ANY_MODIFIER;
  return (int)(modifiers & 0xffffU);
}

static struct x11_selection_state *
x11_find_selection(Atom selection, int create)
{
  int i;
  struct x11_selection_state *free_slot;

  free_slot = 0;
  for (i = 0; i < X11_MAX_SELECTIONS; i++) {
    if (g_selections[i].in_use && g_selections[i].selection == selection)
      return &g_selections[i];
    if (!g_selections[i].in_use && !free_slot)
      free_slot = &g_selections[i];
  }
  if (!create || !free_slot)
    return 0;

  memset(free_slot, 0, sizeof(*free_slot));
  free_slot->in_use = 1;
  free_slot->selection = selection;
  return free_slot;
}

static int
x11_drawable_size(Display *display, Drawable d, int *w, int *h)
{
  struct x11_pixmap_state *pm;

  if (!display || !w || !h)
    return -1;

  pm = x11_find_pixmap((Pixmap)d);
  if (pm) {
    *w = (int)pm->width;
    *h = (int)pm->height;
    return 0;
  }

  *w = display->width > 0 ? display->width : 1024;
  *h = display->height > 0 ? display->height : 768;
  return 0;
}

static int
x11_clip_box(XftDraw *draw, int *x, int *y, unsigned int *w, unsigned int *h)
{
  int rx;
  int ry;
  int rw;
  int rh;
  int x2;
  int y2;

  if (!draw || !x || !y || !w || !h || !draw->has_clip)
    return 0;

  rx = draw->clip.x;
  ry = draw->clip.y;
  rw = draw->clip.width;
  rh = draw->clip.height;
  if (rw <= 0 || rh <= 0)
    return -1;

  x2 = *x + (int)(*w);
  y2 = *y + (int)(*h);
  if (*x < rx)
    *x = rx;
  if (*y < ry)
    *y = ry;
  if (x2 > rx + rw)
    x2 = rx + rw;
  if (y2 > ry + rh)
    y2 = ry + rh;

  if (x2 <= *x || y2 <= *y)
    return -1;

  *w = (unsigned int)(x2 - *x);
  *h = (unsigned int)(y2 - *y);
  return 0;
}

static int
x11_point_in_clip(XftDraw *draw, int x, int y)
{
  if (!draw || !draw->has_clip)
    return 1;
  if (x < draw->clip.x || y < draw->clip.y)
    return 0;
  if (x >= draw->clip.x + (int)draw->clip.width)
    return 0;
  if (y >= draw->clip.y + (int)draw->clip.height)
    return 0;
  return 1;
}

static struct x11_fc_pattern *
x11_fc_pattern_new(void)
{
  struct x11_fc_pattern *p;
  p = (struct x11_fc_pattern *)malloc(sizeof(*p));
  if (!p)
    return 0;
  memset(p, 0, sizeof(*p));
  return p;
}

static struct x11_gc_state *
x11_find_gc(GC gc)
{
  int i;
  for (i = 0; i < X11_MAX_GCS; i++) {
    if (g_gcs[i].in_use && g_gcs[i].id == gc)
      return &g_gcs[i];
  }
  return 0;
}

static struct x11_gc_state *
x11_alloc_gc(void)
{
  int i;
  for (i = 0; i < X11_MAX_GCS; i++) {
    if (!g_gcs[i].in_use) {
      g_gcs[i].in_use = 1;
      g_gcs[i].id = g_next_gc++;
      g_gcs[i].fg = 0xffffffUL;
      g_gcs[i].bg = 0x000000UL;
      g_gcs[i].function = 3;
      g_gcs[i].fill_style = FillSolid;
      return &g_gcs[i];
    }
  }
  return 0;
}

static struct x11_pixmap_state *
x11_find_pixmap(Pixmap pm)
{
  int i;
  for (i = 0; i < X11_MAX_PIXMAPS; i++) {
    if (g_pixmaps[i].in_use && g_pixmaps[i].id == pm)
      return &g_pixmaps[i];
  }
  return 0;
}

static struct x11_picture_state *
x11_find_picture(Picture pic)
{
  int i;
  for (i = 0; i < X11_MAX_PICTURES; i++) {
    if (g_pictures[i].in_use && g_pictures[i].id == pic)
      return &g_pictures[i];
  }
  return 0;
}

static struct x11_picture_state *
x11_alloc_picture(Drawable d, const XRenderPictFormat *format)
{
  int i;

  for (i = 0; i < X11_MAX_PICTURES; i++) {
    if (!g_pictures[i].in_use) {
      memset(&g_pictures[i], 0, sizeof(g_pictures[i]));
      g_pictures[i].in_use = 1;
      g_pictures[i].id = g_next_picture++;
      g_pictures[i].drawable = d;
      if (format)
        g_pictures[i].format = *format;
      return &g_pictures[i];
    }
  }
  return 0;
}

static struct x11_damage_state *
x11_find_damage(Damage id)
{
  int i;
  for (i = 0; i < X11_MAX_DAMAGE; i++) {
    if (g_damages[i].in_use && g_damages[i].id == id)
      return &g_damages[i];
  }
  return 0;
}

static struct x11_damage_state *
x11_alloc_damage(Drawable drawable, int level)
{
  int i;
  for (i = 0; i < X11_MAX_DAMAGE; i++) {
    if (!g_damages[i].in_use) {
      memset(&g_damages[i], 0, sizeof(g_damages[i]));
      g_damages[i].in_use = 1;
      g_damages[i].id = g_next_damage++;
      g_damages[i].drawable = drawable;
      g_damages[i].level = level;
      return &g_damages[i];
    }
  }
  return 0;
}

static int
x11_damage_count_for_drawable(Drawable drawable)
{
  int i;
  int count;

  count = 0;
  for (i = 0; i < X11_MAX_DAMAGE; i++) {
    if (!g_damages[i].in_use)
      continue;
    if (g_damages[i].drawable == drawable)
      count++;
  }
  return count;
}

static void
x11_damage_update_queued_event(Display *display,
                               Damage damage,
                               Drawable drawable,
                               int x0, int y0, int x1, int y1,
                               int ww, int wh)
{
  XEvent *events;
  int i;

  if (!display)
    return;
  events = x11_event_queue(display);
  if (!events)
    return;

  for (i = display->event_count - 1; i >= 0; i--) {
    XDamageNotifyEvent *dev;

    if (events[i].type != X11_EXT_EVENT_BASE_DAMAGE + XDamageNotify)
      continue;
    dev = (XDamageNotifyEvent *)&events[i];
    if (dev->damage != damage || dev->drawable != drawable)
      continue;
    dev->area.x = x0;
    dev->area.y = y0;
    dev->area.width = (unsigned short)((x1 > x0) ? (x1 - x0) : 0);
    dev->area.height = (unsigned short)((y1 > y0) ? (y1 - y0) : 0);
    dev->geometry.x = 0;
    dev->geometry.y = 0;
    dev->geometry.width = (unsigned short)((ww > 0) ? ww : 0);
    dev->geometry.height = (unsigned short)((wh > 0) ? wh : 0);
    return;
  }
}

static void
x11_damage_select_input(Display *display, Drawable drawable, int enable)
{
  char cmd[96];
  char line[128];

  if (!display)
    return;
  if (x11_find_pixmap((Pixmap)drawable))
    return;

  snprintf(cmd, sizeof(cmd), "DAMAGE_SELECT_INPUT %u %d\n", (uint)drawable,
           enable ? 1 : 0);
  x11_cmd(display, cmd, line, sizeof(line));
}

static struct x11_composite_redirect_state *
x11_find_composite_redirect(Window w)
{
  int i;
  for (i = 0; i < X11_MAX_COMPOSITE_REDIRECTS; i++) {
    if (g_composite_redirects[i].in_use && g_composite_redirects[i].w == w)
      return &g_composite_redirects[i];
  }
  return 0;
}

static struct x11_composite_redirect_state *
x11_alloc_composite_redirect(Window w, int update)
{
  int i;
  struct x11_composite_redirect_state *slot;

  slot = x11_find_composite_redirect(w);
  if (slot) {
    slot->update = update;
    return slot;
  }

  for (i = 0; i < X11_MAX_COMPOSITE_REDIRECTS; i++) {
    if (!g_composite_redirects[i].in_use) {
      g_composite_redirects[i].in_use = 1;
      g_composite_redirects[i].w = w;
      g_composite_redirects[i].update = update;
      return &g_composite_redirects[i];
    }
  }
  return 0;
}

static struct x11_shape_state *
x11_find_shape(Window w)
{
  int i;
  for (i = 0; i < X11_MAX_SHAPE_WINDOWS; i++) {
    if (g_shapes[i].in_use && g_shapes[i].w == w)
      return &g_shapes[i];
  }
  return 0;
}

static struct x11_shape_state *
x11_alloc_shape(Display *display, Window w)
{
  int i;
  int ww;
  int wh;

  struct x11_shape_state *s = x11_find_shape(w);
  if (s)
    return s;

  for (i = 0; i < X11_MAX_SHAPE_WINDOWS; i++) {
    if (!g_shapes[i].in_use) {
      memset(&g_shapes[i], 0, sizeof(g_shapes[i]));
      g_shapes[i].in_use = 1;
      g_shapes[i].w = w;
      ww = 0;
      wh = 0;
      if (display && x11_drawable_size(display, w, &ww, &wh) == 0) {
        g_shapes[i].w_bounding = (unsigned int)ww;
        g_shapes[i].h_bounding = (unsigned int)wh;
        g_shapes[i].w_clip = (unsigned int)ww;
        g_shapes[i].h_clip = (unsigned int)wh;
      }
      return &g_shapes[i];
    }
  }
  return 0;
}

static struct x11_shape_state *
x11_alloc_shape_no_probe(Window w)
{
  int i;
  struct x11_shape_state *s;

  s = x11_find_shape(w);
  if (s)
    return s;

  for (i = 0; i < X11_MAX_SHAPE_WINDOWS; i++) {
    if (!g_shapes[i].in_use) {
      memset(&g_shapes[i], 0, sizeof(g_shapes[i]));
      g_shapes[i].in_use = 1;
      g_shapes[i].w = w;
      return &g_shapes[i];
    }
  }
  return 0;
}

static struct x11_randr_select_state *
x11_find_randr_select(Window w)
{
  int i;
  for (i = 0; i < X11_MAX_RANDR_SELECTS; i++) {
    if (g_randr_selects[i].in_use && g_randr_selects[i].w == w)
      return &g_randr_selects[i];
  }
  return 0;
}

static struct x11_randr_select_state *
x11_alloc_randr_select(Window w)
{
  int i;
  struct x11_randr_select_state *s;

  s = x11_find_randr_select(w);
  if (s)
    return s;
  for (i = 0; i < X11_MAX_RANDR_SELECTS; i++) {
    if (!g_randr_selects[i].in_use) {
      g_randr_selects[i].in_use = 1;
      g_randr_selects[i].w = w;
      g_randr_selects[i].mask = 0;
      return &g_randr_selects[i];
    }
  }
  return 0;
}

static int
x11_randr_screen_change_selected(Display *display, Window w)
{
  int i;

  if (!display)
    return 0;
  for (i = 0; i < X11_MAX_RANDR_SELECTS; i++) {
    if (!g_randr_selects[i].in_use)
      continue;
    if (!(g_randr_selects[i].mask & RRScreenChangeNotifyMask))
      continue;
    if (g_randr_selects[i].w == w || g_randr_selects[i].w == display->root)
      return 1;
  }
  return 0;
}

static struct x11_shm_image_state *
x11_find_shm_image(XImage *image)
{
  int i;

  for (i = 0; i < X11_MAX_SHM_IMAGES; i++) {
    if (g_shm_images[i].in_use && g_shm_images[i].image == image)
      return &g_shm_images[i];
  }
  return 0;
}

static void
x11_clear_shm_image(XImage *image)
{
  struct x11_shm_image_state *s;

  s = x11_find_shm_image(image);
  if (!s)
    return;
  memset(s, 0, sizeof(*s));
}

static int
x11_track_shm_image(XImage *image, void *shmseg)
{
  int i;
  struct x11_shm_image_state *s;

  if (!image)
    return -1;

  s = x11_find_shm_image(image);
  if (s) {
    s->shmseg = shmseg;
    return 0;
  }

  for (i = 0; i < X11_MAX_SHM_IMAGES; i++) {
    if (!g_shm_images[i].in_use) {
      g_shm_images[i].in_use = 1;
      g_shm_images[i].image = image;
      g_shm_images[i].shmseg = shmseg;
      return 0;
    }
  }
  return -1;
}

static void
x11_emit_shape_notify(Display *display, struct x11_shape_state *s, int kind)
{
  XEvent ev;
  XShapeEvent *sev;

  if (!display || !s)
    return;
  if (!(s->event_mask & ShapeNotifyMask))
    return;

  memset(&ev, 0, sizeof(ev));
  sev = (XShapeEvent *)&ev;
  sev->type = X11_EXT_EVENT_BASE_SHAPE;
  sev->display = display;
  sev->window = s->w;
  sev->kind = kind;
  sev->time = x11_next_fake_time();
  if (kind == ShapeBounding) {
    sev->x = s->x_bounding;
    sev->y = s->y_bounding;
    sev->width = s->w_bounding;
    sev->height = s->h_bounding;
    sev->shaped = s->bounding_shaped;
  } else {
    sev->x = s->x_clip;
    sev->y = s->y_clip;
    sev->width = s->w_clip;
    sev->height = s->h_clip;
    sev->shaped = s->clip_shaped;
  }
  x11_stamp_synthetic_event(&ev);
  x11_event_push_back(display, &ev);
}

static void
x11_emit_randr_screen_change(Display *display, Window w)
{
  XEvent ev;
  XRRScreenChangeNotifyEvent *rev;

  if (!display)
    return;
  if (!x11_randr_screen_change_selected(display, w))
    return;
  memset(&ev, 0, sizeof(ev));
  rev = (XRRScreenChangeNotifyEvent *)&ev;
  rev->type = X11_EXT_EVENT_BASE_RANDR + RRScreenChangeNotify;
  rev->display = display;
  rev->window = w;
  rev->root = display->root;
  rev->timestamp = x11_next_fake_time();
  rev->config_timestamp = rev->timestamp;
  rev->rotation = 1;
  rev->width = (display->width > 0) ? display->width : 1024;
  rev->height = (display->height > 0) ? display->height : 768;
  rev->mwidth = rev->width;
  rev->mheight = rev->height;
  x11_stamp_synthetic_event(&ev);
  x11_event_push_back(display, &ev);
}

static int
x11_build_randr_screen_change_event(Display *display, Window w, XEvent *event)
{
  XRRScreenChangeNotifyEvent *rev;

  if (!display || !event)
    return -1;
  if (!x11_randr_screen_change_selected(display, w))
    return -1;

  memset(event, 0, sizeof(*event));
  rev = (XRRScreenChangeNotifyEvent *)event;
  rev->type = X11_EXT_EVENT_BASE_RANDR + RRScreenChangeNotify;
  rev->display = display;
  rev->window = w;
  rev->root = display->root;
  rev->timestamp = x11_next_fake_time();
  rev->config_timestamp = rev->timestamp;
  rev->rotation = 1;
  rev->width = (display->width > 0) ? display->width : 1024;
  rev->height = (display->height > 0) ? display->height : 768;
  rev->mwidth = rev->width;
  rev->mheight = rev->height;
  x11_stamp_synthetic_event(event);
  return 0;
}

static int
x11_build_damage_event_for_drawable_region(Display *display, Drawable d,
                                           int rx, int ry,
                                           unsigned int rw, unsigned int rh,
                                           int has_region,
                                           XEvent *event)
{
  int i;
  XDamageNotifyEvent *dev;
  int ww;
  int wh;
  int x0;
  int y0;
  int x1;
  int y1;

  if (!display || !event)
    return -1;

  for (i = 0; i < X11_MAX_DAMAGE; i++) {
    if (!g_damages[i].in_use)
      continue;
    if (g_damages[i].drawable != d)
      continue;

    ww = 0;
    wh = 0;
    if (x11_drawable_size(display, d, &ww, &wh) < 0) {
      ww = 0;
      wh = 0;
    }

    x0 = 0;
    y0 = 0;
    x1 = (ww > 0) ? ww : 0;
    y1 = (wh > 0) ? wh : 0;

    if (has_region) {
      x0 = rx;
      y0 = ry;
      x1 = rx + (int)rw;
      y1 = ry + (int)rh;
      if (x0 < 0)
        x0 = 0;
      if (y0 < 0)
        y0 = 0;
      if (ww > 0 && x1 > ww)
        x1 = ww;
      if (wh > 0 && y1 > wh)
        y1 = wh;
      if (x1 <= x0 || y1 <= y0) {
        x0 = 0;
        y0 = 0;
        x1 = (ww > 0) ? ww : 0;
        y1 = (wh > 0) ? wh : 0;
      }
    }

    memset(event, 0, sizeof(*event));
    dev = (XDamageNotifyEvent *)event;
    dev->type = X11_EXT_EVENT_BASE_DAMAGE + XDamageNotify;
    dev->display = display;
    dev->drawable = d;
    dev->damage = g_damages[i].id;
    dev->timestamp = x11_next_fake_time();
    dev->level = g_damages[i].level;
    dev->more = False;
    dev->area.x = x0;
    dev->area.y = y0;
    dev->area.width = (unsigned short)((x1 > x0) ? (x1 - x0) : 0);
    dev->area.height = (unsigned short)((y1 > y0) ? (y1 - y0) : 0);
    dev->geometry.x = 0;
    dev->geometry.y = 0;
    dev->geometry.width = (unsigned short)((ww > 0) ? ww : 0);
    dev->geometry.height = (unsigned short)((wh > 0) ? wh : 0);
    x11_stamp_synthetic_event(event);
    return 0;
  }

  return -1;
}

static void
x11_notify_drawable_damage_region(Display *display, Drawable d,
                                  int rx, int ry,
                                  unsigned int rw, unsigned int rh,
                                  int has_region)
{
  int i;
  XEvent ev;
  XDamageNotifyEvent *dev;
  int ww;
  int wh;
  int x0;
  int y0;
  int x1;
  int y1;
  int px0;
  int py0;
  int px1;
  int py1;

  if (!display)
    return;
  for (i = 0; i < X11_MAX_DAMAGE; i++) {
    if (!g_damages[i].in_use)
      continue;
    if (g_damages[i].drawable != d)
      continue;
    memset(&ev, 0, sizeof(ev));
    dev = (XDamageNotifyEvent *)&ev;
    dev->type = X11_EXT_EVENT_BASE_DAMAGE + XDamageNotify;
    dev->display = display;
    dev->drawable = d;
    dev->damage = g_damages[i].id;
    dev->timestamp = x11_next_fake_time();
    dev->level = g_damages[i].level;
    dev->more = False;
    ww = 0;
    wh = 0;
    if (x11_drawable_size(display, d, &ww, &wh) < 0) {
      ww = 0;
      wh = 0;
    }

    x0 = 0;
    y0 = 0;
    x1 = (ww > 0) ? ww : 0;
    y1 = (wh > 0) ? wh : 0;

    if (has_region) {
      x0 = rx;
      y0 = ry;
      x1 = rx + (int)rw;
      y1 = ry + (int)rh;
      if (x0 < 0)
        x0 = 0;
      if (y0 < 0)
        y0 = 0;
      if (ww > 0 && x1 > ww)
        x1 = ww;
      if (wh > 0 && y1 > wh)
        y1 = wh;
      if (x1 <= x0 || y1 <= y0) {
        x0 = 0;
        y0 = 0;
        x1 = (ww > 0) ? ww : 0;
        y1 = (wh > 0) ? wh : 0;
      }
    }

    dev->area.x = x0;
    dev->area.y = y0;
    dev->area.width = (unsigned short)((x1 > x0) ? (x1 - x0) : 0);
    dev->area.height = (unsigned short)((y1 > y0) ? (y1 - y0) : 0);
    dev->geometry.x = 0;
    dev->geometry.y = 0;
    dev->geometry.width = (unsigned short)((ww > 0) ? ww : 0);
    dev->geometry.height = (unsigned short)((wh > 0) ? wh : 0);

    if (g_damages[i].pending) {
      px0 = g_damages[i].pending_x;
      py0 = g_damages[i].pending_y;
      px1 = g_damages[i].pending_x + (int)g_damages[i].pending_w;
      py1 = g_damages[i].pending_y + (int)g_damages[i].pending_h;
      if (x0 < px0)
        px0 = x0;
      if (y0 < py0)
        py0 = y0;
      if (x1 > px1)
        px1 = x1;
      if (y1 > py1)
        py1 = y1;
      g_damages[i].pending_x = px0;
      g_damages[i].pending_y = py0;
      g_damages[i].pending_w = (unsigned int)((px1 > px0) ? (px1 - px0) : 0);
      g_damages[i].pending_h = (unsigned int)((py1 > py0) ? (py1 - py0) : 0);
      x11_damage_update_queued_event(display,
                                     g_damages[i].id,
                                     d,
                                     px0,
                                     py0,
                                     px1,
                                     py1,
                                     ww,
                                     wh);
      continue;
    }

    g_damages[i].pending = 1;
    g_damages[i].pending_x = x0;
    g_damages[i].pending_y = y0;
    g_damages[i].pending_w = (unsigned int)((x1 > x0) ? (x1 - x0) : 0);
    g_damages[i].pending_h = (unsigned int)((y1 > y0) ? (y1 - y0) : 0);
    x11_stamp_synthetic_event(&ev);
    x11_event_push_back(display, &ev);
  }
}

static void
x11_notify_drawable_damage(Display *display, Drawable d)
{
  x11_notify_drawable_damage_region(display, d, 0, 0, 0, 0, 0);
}

static void
x11_emit_shm_completion(Display *display, Drawable d, XImage *image)
{
  XEvent ev;
  XShmCompletionEvent *sev;
  struct x11_shm_image_state *state;

  if (!display)
    return;

  memset(&ev, 0, sizeof(ev));
  sev = (XShmCompletionEvent *)&ev;
  sev->type = X11_EXT_EVENT_BASE_XSHM + ShmCompletion;
  sev->display = display;
  sev->drawable = d;
  sev->major_code = 0;
  sev->minor_code = 0;
  sev->offset = 0;

  state = x11_find_shm_image(image);
  sev->shmseg = state ? state->shmseg : 0;

  x11_stamp_synthetic_event(&ev);
  x11_event_push_back(display, &ev);
}

static struct x11_pixmap_state *
x11_alloc_pixmap(unsigned int width, unsigned int height, unsigned int depth)
{
  int i;
  unsigned int *pixels;
  size_t count;
  if (width == 0 || height == 0)
    return 0;

  if (height != 0 && width > (0xffffffffU / height))
    return 0;
  count = (size_t)width * (size_t)height;
  if (count > (size_t)(8 * 1024 * 1024))
    return 0;
  pixels = (unsigned int *)malloc(count * sizeof(unsigned int));
  if (!pixels)
    return 0;
  memset(pixels, 0, count * sizeof(unsigned int));

  for (i = 0; i < X11_MAX_PIXMAPS; i++) {
    if (!g_pixmaps[i].in_use) {
      g_pixmaps[i].in_use = 1;
      g_pixmaps[i].id = g_next_pixmap++;
      g_pixmaps[i].width = width;
      g_pixmaps[i].height = height;
      g_pixmaps[i].depth = depth;
      g_pixmaps[i].pixels = pixels;
      return &g_pixmaps[i];
    }
  }
  free(pixels);
  return 0;
}

static struct x11_color_entry *
x11_alloc_color(unsigned short red, unsigned short green, unsigned short blue)
{
  int i;
  for (i = 0; i < X11_MAX_COLORS; i++) {
    if (!g_colors[i].in_use) {
      g_colors[i].in_use = 1;
      g_colors[i].red = red;
      g_colors[i].green = green;
      g_colors[i].blue = blue;
      /* Convert 16-bit color to 24-bit pixel value */
      g_colors[i].pixel = ((unsigned long)(red >> 8) << 16) | 
                          ((unsigned long)(green >> 8) << 8) | 
                          (unsigned long)(blue >> 8);
      return &g_colors[i];
    }
  }
  return 0;
}

static struct x11_font_state *
x11_find_font(Font fid)
{
  int i;
  for (i = 0; i < X11_MAX_FONTS; i++) {
    if (g_fonts[i].in_use && g_fonts[i].id == fid)
      return &g_fonts[i];
  }
  return 0;
}

/* Parse font name to extract metrics.
 * Handles formats like "montecarlo-8x16" or standard XLFD.
 * Returns width, height, ascent, descent from name. */
static int
x11_parse_font_metrics(const char *name, int *width, int *height, int *ascent, int *descent)
{
  int w, h;
  
  if (!name || !width || !height || !ascent || !descent)
    return -1;
  
  /* Try to parse "name-WxH" format (e.g., "montecarlo-8x16") */
  if (sscanf(name, "%*[^-]-%dx%d", &w, &h) == 2) {
    *width = w;
    *height = h;
    *ascent = (h * 3) / 4;  /* Approximate: 3/4 of height */
    *descent = h / 4;       /* Approximate: 1/4 of height */
    return 0;
  }
  
  /* Default fallback */
  *width = 8;
  *height = 16;
  *ascent = 12;
  *descent = 4;
  return 0;
}

static void
x11_fill_xft_font_metrics(XftFont *f, const char *name, double pixel_size)
{
  int width;
  int height;
  int ascent;
  int descent;

  if (!f)
    return;

  width = 8;
  height = 16;
  ascent = 12;
  descent = 4;

  if (name)
    x11_parse_font_metrics(name, &width, &height, &ascent, &descent);

  /* x6 currently rasterizes text with a fixed builtin bitmap font (8x16).
   * Keep Xft metrics aligned to that renderer even if a pixel size was
   * requested through fontconfig, otherwise st/dwm layout drifts and glyphs
   * overlap or clip. */
  (void)pixel_size;

  f->ascent = ascent;
  f->descent = descent;
  f->height = height;
  f->max_advance_width = width;
}

static struct x11_font_state *
x11_alloc_font(const char *name)
{
  int i, w, h, a, d;
  
  if (!name || x11_parse_font_metrics(name, &w, &h, &a, &d) < 0)
    return 0;
  
  for (i = 0; i < X11_MAX_FONTS; i++) {
    if (!g_fonts[i].in_use) {
      g_fonts[i].in_use = 1;
      g_fonts[i].id = g_next_font++;
      strncpy(g_fonts[i].name, name, sizeof(g_fonts[i].name) - 1);
      g_fonts[i].name[sizeof(g_fonts[i].name) - 1] = 0;
      g_fonts[i].width = w;
      g_fonts[i].height = h;
      g_fonts[i].ascent = a;
      g_fonts[i].descent = d;
      return &g_fonts[i];
    }
  }
  return 0;
}

static void
x11_fill_font_struct(XFontStruct *out, const struct x11_font_state *fs)
{
  if (!out || !fs)
    return;
  memset(out, 0, sizeof(*out));
  out->fid = fs->id;
  out->ascent = fs->ascent;
  out->descent = fs->descent;
  out->max_bounds.width = fs->width;
}

static const char *
x11_pick_fontset_name(const char *base_font_name_list)
{
  static char chosen[128];
  int i;

  if (!base_font_name_list || !*base_font_name_list)
    return "fixed";

  i = 0;
  while (base_font_name_list[i] &&
         base_font_name_list[i] != ',' &&
         base_font_name_list[i] != ';' &&
         i < (int)sizeof(chosen) - 1) {
    chosen[i] = base_font_name_list[i];
    i++;
  }
  while (i > 0 && (chosen[i - 1] == ' ' || chosen[i - 1] == '\t'))
    i--;
  chosen[i] = '\0';

  if (i == 0)
    return "fixed";
  return chosen;
}

static int x11_read_line(Display *display, char *line, int maxlen);
static int x11_parse_event_line(Display *display, const char *line, XEvent *event);
static long x11_event_mask_for_type(int type);
static Window x11_event_window(const XEvent *event);

static int
x11_event_push_back(Display *display, const XEvent *ev)
{
  XEvent *events;
  XEvent *last;
  Window ev_win;
  Window last_win;

  if (!display)
    return -1;
  events = x11_event_queue(display);
  if (!events)
    return -1;
  if (!ev)
    return -1;
  if (ev->type == MotionNotify && display->event_count > 0) {
    last = &events[display->event_count - 1];
    if (last->type == MotionNotify) {
      ev_win = x11_event_window(ev);
      last_win = x11_event_window(last);
      if (ev_win != 0 && ev_win == last_win) {
        *last = *ev;
        return 0;
      }
    }
  }
  if (display->event_count >= X11_MAX_EVENTS) {
    /* Drop oldest-preserve order behavior here was O(N) via memmove and can
     * dominate CPU under event storms. Keep queue length fixed and overwrite
     * the newest slot with the latest event instead. */
    events[X11_MAX_EVENTS - 1] = *ev;
    return 0;
  }
  events[display->event_count++] = *ev;
  if (ev->type == KeyPress || ev->type == KeyRelease) {
    x11crit("x11:key-queue push type=%d keycode=%u state=%u qlen=%d",
            ev->type, ev->xkey.keycode, ev->xkey.state, display->event_count);
  }
  return 0;
}

static int
x11_event_push_front(Display *display, const XEvent *ev)
{
  XEvent *events;

  if (!display)
    return -1;
  events = x11_event_queue(display);
  if (!events)
    return -1;
  if (!ev)
    return -1;
  if (display->event_count >= X11_MAX_EVENTS)
    display->event_count = X11_MAX_EVENTS - 1;
  if (display->event_count > 0)
    memmove(&events[1], &events[0], (size_t)display->event_count * sizeof(XEvent));
  events[0] = *ev;
  display->event_count++;
  return 0;
}

static int
x11_event_pop_front(Display *display, XEvent *ev)
{
  XEvent *events;

  if (!display || !ev || display->event_count <= 0)
    return -1;
  events = x11_event_queue(display);
  if (!events)
    return -1;
  *ev = events[0];
  if (display->event_count > 1)
    memmove(&events[0], &events[1], (size_t)(display->event_count - 1) * sizeof(XEvent));
  display->event_count--;
  if (ev->type == KeyPress || ev->type == KeyRelease) {
    x11crit("x11:key-queue pop type=%d keycode=%u state=%u qlen=%d",
            ev->type, ev->xkey.keycode, ev->xkey.state, display->event_count);
  }
  return 0;
}

static int
x11_event_peek_front(Display *display, XEvent *ev)
{
  XEvent *events;

  if (!display || !ev || display->event_count <= 0)
    return -1;
  events = x11_event_queue(display);
  if (!events)
    return -1;
  *ev = events[0];
  return 0;
}

static int
x11_event_pop_index(Display *display, int idx, XEvent *ev)
{
  XEvent *events;

  if (!display || idx < 0 || idx >= display->event_count || !ev)
    return -1;
  events = x11_event_queue(display);
  if (!events)
    return -1;
  *ev = events[idx];
  if (idx + 1 < display->event_count)
    memmove(&events[idx], &events[idx + 1], (size_t)(display->event_count - idx - 1) * sizeof(XEvent));
  display->event_count--;
  return 0;
}

static int
x11_event_find_and_pop_mask(Display *display, long mask, XEvent *ev)
{
  XEvent *events;
  int i;

  if (!display || !ev)
    return -1;
  events = x11_event_queue(display);
  if (!events)
    return -1;
  for (i = 0; i < display->event_count; i++) {
    if (x11_event_mask_for_type(events[i].type) & mask)
      return x11_event_pop_index(display, i, ev);
  }
  return -1;
}

static int
x11_event_find_and_pop_type(Display *display, int type, Window w, int check_window, XEvent *ev)
{
  XEvent *events;
  int i;

  if (!display || !ev)
    return -1;
  events = x11_event_queue(display);
  if (!events)
    return -1;
  for (i = 0; i < display->event_count; i++) {
    if (events[i].type != type)
      continue;
    if (check_window && x11_event_window(&events[i]) != w)
      continue;
    return x11_event_pop_index(display, i, ev);
  }
  return -1;
}

static int
x11_event_find_and_pop_window_mask(Display *display, Window w, long mask, XEvent *ev)
{
  XEvent *events;
  int i;

  if (!display || !ev)
    return -1;
  events = x11_event_queue(display);
  if (!events)
    return -1;
  for (i = 0; i < display->event_count; i++) {
    if (x11_event_window(&events[i]) != w)
      continue;
    if ((x11_event_mask_for_type(events[i].type) & mask) == 0)
      continue;
    return x11_event_pop_index(display, i, ev);
  }
  return -1;
}

static int
x11_event_find_and_pop_predicate(Display *display, XEvent *ev,
                                 Bool (*predicate)(Display *, XEvent *, XPointer),
                                 XPointer arg)
{
  int i;

  if (!display || !ev || !predicate)
    return -1;

  for (i = 0; i < display->event_count; i++) {
    XEvent candidate = x11_event_queue(display)[i];
    if (predicate(display, &candidate, arg))
      return x11_event_pop_index(display, i, ev);
  }
  return -1;
}

static int
x11_handle_unsolicited_line(Display *display, const char *line)
{
  if (!display || !line)
    return 0;

  if (strncmp(line, "EVENT ", 6) == 0) {
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    if (x11_parse_event_line(display, line, &ev) == 0) {
      x11_event_push_back(display, &ev);
      x11dbg("x11:event queued type=%d qlen=%d", ev.type, display->event_count);
    } else {
      x11dbg("x11:event dropped raw='%s'", line);
    }
    return 1;
  }

    if (strncmp(line, "OK draw", 7) == 0 ||
      strncmp(line, "OK text", 7) == 0 ||
      strncmp(line, "OK copy_area", 12) == 0) {
    if (display->pending_draw_replies > 0)
      display->pending_draw_replies--;
    g_x11_draw_reply_seen++;
    if ((g_x11_draw_reply_seen % 256) == 0 || display->pending_draw_replies == 0) {
      x11dbg("x11:draw-reply seen=%d pending=%d", g_x11_draw_reply_seen, display->pending_draw_replies);
    }
    return 1;
  }

  /* Draw commands can also fail (e.g. ERR not-found / ERR unknown). Consume
   * these while draining draw replies so pending accounting does not wedge. */
  if (strncmp(line, "ERR ", 4) == 0 && display->pending_draw_replies > 0) {
    display->pending_draw_replies--;
    g_x11_draw_reply_seen++;
    x11dbg("x11:draw-reply error='%s' pending=%d", line, display->pending_draw_replies);
    return 1;
  }

  return 0;
}

/* x11_drain_draw_replies is no longer called: draw commands are fire-and-
 * forget, and pending replies are consumed lazily by x11_handle_unsolicited_line
 * in the x11_cmd response loop.  Kept as a dead stub in case external callers
 * exist in the translation unit. */
static int __attribute__((unused))
x11_drain_draw_replies(Display *display)
{
  char line[X6_BUF_SIZE];

  if (!display)
    return -1;

  while (display->pending_draw_replies > 0) {
    if (x11_read_line(display, line, sizeof(line)) < 0)
      return -1;
    if (!x11_handle_unsolicited_line(display, line))
      return -1;
  }
  return 0;
}

static int
x11_sanitize_rect(Display *display, int *x, int *y, unsigned int *w, unsigned int *h)
{
  int maxw;
  int maxh;

  if (!display || !x || !y || !w || !h)
    return -1;

  maxw = display->width > 0 ? display->width : 1024;
  maxh = display->height > 0 ? display->height : 768;

  if (*w == 0 || *h == 0)
    return -1;

  if (*w > (unsigned int)maxw)
    *w = (unsigned int)maxw;
  if (*h > (unsigned int)maxh)
    *h = (unsigned int)maxh;

  if (*x < 0) {
    unsigned int drop = (unsigned int)(-*x);
    if (drop >= *w)
      return -1;
    *w -= drop;
    *x = 0;
  }
  if (*y < 0) {
    unsigned int drop = (unsigned int)(-*y);
    if (drop >= *h)
      return -1;
    *h -= drop;
    *y = 0;
  }

  if (*x >= maxw || *y >= maxh)
    return -1;

  if (*x + (int)(*w) > maxw)
    *w = (unsigned int)(maxw - *x);
  if (*y + (int)(*h) > maxh)
    *h = (unsigned int)(maxh - *y);

  if (*w == 0 || *h == 0)
    return -1;
  return 0;
}

static int
x11_parse_event_line(Display *display, const char *line, XEvent *event)
{
  if (!display || !line || !event)
    return -1;
  if (strncmp(line, "EVENT ", 6) != 0)
    return -1;

  x11dbg("x11:event-parse raw='%s'", line);

  memset(event, 0, sizeof(*event));
  if (strncmp(line + 6, "MapRequest", 10) == 0) {
    event->type = MapRequest;
    sscanf(line, "EVENT MapRequest wid=%u", &event->xmaprequest.window);
    event->xmaprequest.parent = display->root;
    return 0;
  }
  if (strncmp(line + 6, "MapNotify", 9) == 0) {
    event->type = MapNotify;
    sscanf(line, "EVENT MapNotify wid=%u", &event->xmap.window);
    event->xmap.event = event->xmap.window;
    event->xmap.override_redirect = False;
    return 0;
  }
  if (strncmp(line + 6, "ConfigureRequest", 16) == 0) {
    event->type = ConfigureRequest;
    sscanf(line, "EVENT ConfigureRequest wid=%u geom=%d,%d %dx%d",
           &event->xconfigurerequest.window,
           &event->xconfigurerequest.x,
           &event->xconfigurerequest.y,
           &event->xconfigurerequest.width,
           &event->xconfigurerequest.height);
    event->xconfigurerequest.parent = display->root;
    return 0;
  }
  if (strncmp(line + 6, "FocusIn", 7) == 0) {
    event->type = FocusIn;
    event->xfocus.mode = 0;
    event->xfocus.detail = 0;
    sscanf(line, "EVENT FocusIn wid=%u mode=%d detail=%d",
           &event->xfocus.window, &event->xfocus.mode, &event->xfocus.detail);
    return 0;
  }
  if (strncmp(line + 6, "FocusOut", 8) == 0) {
    event->type = FocusOut;
    event->xfocus.mode = 0;
    event->xfocus.detail = 0;
    sscanf(line, "EVENT FocusOut wid=%u mode=%d detail=%d",
           &event->xfocus.window, &event->xfocus.mode, &event->xfocus.detail);
    return 0;
  }
  if (strncmp(line + 6, "DestroyNotify", 13) == 0) {
    event->type = DestroyNotify;
    sscanf(line, "EVENT DestroyNotify wid=%u", &event->xdestroywindow.window);
    return 0;
  }
  if (strncmp(line + 6, "KeyPress", 8) == 0) {
    event->type = KeyPress;
    sscanf(line, "EVENT KeyPress wid=%u keycode=%u state=%u",
           &event->xkey.window, &event->xkey.keycode, &event->xkey.state);
    sscanf(line, "EVENT KeyPress wid=%*u keycode=%*u state=%*u time=%lu", &event->xkey.time);
    return 0;
  }
  if (strncmp(line + 6, "KeyRelease", 10) == 0) {
    event->type = KeyRelease;
    sscanf(line, "EVENT KeyRelease wid=%u keycode=%u state=%u",
           &event->xkey.window, &event->xkey.keycode, &event->xkey.state);
    sscanf(line, "EVENT KeyRelease wid=%*u keycode=%*u state=%*u time=%lu", &event->xkey.time);
    return 0;
  }
  if (strncmp(line + 6, "ButtonPress", 11) == 0) {
    event->type = ButtonPress;
    sscanf(line, "EVENT ButtonPress wid=%u button=%u state=%u x=%d y=%d",
           &event->xbutton.window, &event->xbutton.button, &event->xbutton.state,
           &event->xbutton.x, &event->xbutton.y);
    sscanf(line, "EVENT ButtonPress wid=%*u button=%*u state=%*u x=%*d y=%*d time=%lu", &event->xbutton.time);
    event->xbutton.x_root = event->xbutton.x;
    event->xbutton.y_root = event->xbutton.y;
    return 0;
  }
  if (strncmp(line + 6, "ButtonRelease", 13) == 0) {
    event->type = ButtonRelease;
    sscanf(line, "EVENT ButtonRelease wid=%u button=%u state=%u x=%d y=%d",
           &event->xbutton.window, &event->xbutton.button, &event->xbutton.state,
           &event->xbutton.x, &event->xbutton.y);
    sscanf(line, "EVENT ButtonRelease wid=%*u button=%*u state=%*u x=%*d y=%*d time=%lu", &event->xbutton.time);
    event->xbutton.x_root = event->xbutton.x;
    event->xbutton.y_root = event->xbutton.y;
    return 0;
  }
  if (strncmp(line + 6, "MotionNotify", 12) == 0) {
    event->type = MotionNotify;
    sscanf(line, "EVENT MotionNotify wid=%u x=%d y=%d state=%u",
           &event->xmotion.window, &event->xmotion.x, &event->xmotion.y, &event->xmotion.state);
    sscanf(line, "EVENT MotionNotify wid=%*u x=%*d y=%*d state=%*u time=%lu", &event->xmotion.time);
    event->xmotion.x_root = event->xmotion.x;
    event->xmotion.y_root = event->xmotion.y;
    return 0;
  }
      if (strncmp(line + 6, "EnterNotify", 11) == 0) {
        event->type = EnterNotify;
          event->xcrossing.mode = 0;
          event->xcrossing.detail = 0;
          event->xcrossing.focus = 0;
          event->xcrossing.same_screen = True;
          sscanf(line, "EVENT EnterNotify wid=%u x=%d y=%d state=%u mode=%d detail=%d focus=%d same=%d",
          &event->xcrossing.window, &event->xcrossing.x,
            &event->xcrossing.y, &event->xcrossing.state,
            &event->xcrossing.mode, &event->xcrossing.detail,
            &event->xcrossing.focus, &event->xcrossing.same_screen);
          sscanf(line, "EVENT EnterNotify wid=%*u x=%*d y=%*d state=%*u mode=%*d detail=%*d focus=%*d same=%*d time=%lu",
          &event->xcrossing.time);
        event->xcrossing.x_root = event->xcrossing.x;
        event->xcrossing.y_root = event->xcrossing.y;
        return 0;
      }
      if (strncmp(line + 6, "LeaveNotify", 11) == 0) {
        event->type = LeaveNotify;
          event->xcrossing.mode = 0;
          event->xcrossing.detail = 0;
          event->xcrossing.focus = 0;
          event->xcrossing.same_screen = True;
          sscanf(line, "EVENT LeaveNotify wid=%u x=%d y=%d state=%u mode=%d detail=%d focus=%d same=%d",
          &event->xcrossing.window, &event->xcrossing.x,
            &event->xcrossing.y, &event->xcrossing.state,
            &event->xcrossing.mode, &event->xcrossing.detail,
            &event->xcrossing.focus, &event->xcrossing.same_screen);
          sscanf(line, "EVENT LeaveNotify wid=%*u x=%*d y=%*d state=%*u mode=%*d detail=%*d focus=%*d same=%*d time=%lu",
          &event->xcrossing.time);
        event->xcrossing.x_root = event->xcrossing.x;
        event->xcrossing.y_root = event->xcrossing.y;
        return 0;
      }
  if (strncmp(line + 6, "ConfigureNotify", 15) == 0) {
    event->type = ConfigureNotify;
    sscanf(line, "EVENT ConfigureNotify wid=%u x=%d y=%d w=%d h=%d",
           &event->xconfigure.window,
           &event->xconfigure.x, &event->xconfigure.y,
           &event->xconfigure.width, &event->xconfigure.height);
    return 0;
  }
  if (strncmp(line + 6, "Expose", 6) == 0) {
    event->type = Expose;
    sscanf(line, "EVENT Expose wid=%u x=%d y=%d w=%d h=%d",
           &event->xexpose.window,
           &event->xexpose.x, &event->xexpose.y,
           &event->xexpose.width, &event->xexpose.height);
    return 0;
  }
  if (strncmp(line + 6, "DamageNotify", 12) == 0) {
    unsigned int wid;
    int dx;
    int dy;
    int dw;
    int dh;

    wid = 0;
    dx = 0;
    dy = 0;
    dw = 0;
    dh = 0;
    sscanf(line, "EVENT DamageNotify wid=%u x=%d y=%d w=%d h=%d",
           &wid, &dx, &dy, &dw, &dh);
    return x11_build_damage_event_for_drawable_region(
        display,
        (Drawable)wid,
        dx,
        dy,
        (unsigned int)(dw > 0 ? dw : 0),
        (unsigned int)(dh > 0 ? dh : 0),
        1,
        event);
  }
  if (strncmp(line + 6, "ShapeNotify", 11) == 0) {
    unsigned int wid;
    int kind;
    unsigned int shaped;
    int sx;
    int sy;
    int sw;
    int sh;
    struct x11_shape_state *s;
    XShapeEvent *sev;

    wid = 0;
    kind = ShapeBounding;
    shaped = 0;
    sx = 0;
    sy = 0;
    sw = 0;
    sh = 0;
    sscanf(line, "EVENT ShapeNotify wid=%u kind=%d shaped=%u x=%d y=%d w=%d h=%d",
           &wid, &kind, &shaped, &sx, &sy, &sw, &sh);

    s = x11_alloc_shape_no_probe((Window)wid);
    if (!s)
      return -1;

    if (kind == ShapeClip) {
      s->clip_shaped = shaped ? 1 : 0;
      s->x_clip = sx;
      s->y_clip = sy;
      s->w_clip = (unsigned int)(sw > 0 ? sw : 0);
      s->h_clip = (unsigned int)(sh > 0 ? sh : 0);
      if (!(s->event_mask & ShapeNotifyMask))
        return -1;
      memset(event, 0, sizeof(*event));
      sev = (XShapeEvent *)event;
      sev->type = X11_EXT_EVENT_BASE_SHAPE + ShapeNotify;
      sev->display = display;
      sev->window = s->w;
      sev->kind = ShapeClip;
      sev->x = s->x_clip;
      sev->y = s->y_clip;
      sev->width = s->w_clip;
      sev->height = s->h_clip;
      sev->time = x11_next_fake_time();
      sev->shaped = s->clip_shaped;
      x11_stamp_synthetic_event(event);
      return 0;
    } else {
      s->bounding_shaped = shaped ? 1 : 0;
      s->x_bounding = sx;
      s->y_bounding = sy;
      s->w_bounding = (unsigned int)(sw > 0 ? sw : 0);
      s->h_bounding = (unsigned int)(sh > 0 ? sh : 0);
      if (!(s->event_mask & ShapeNotifyMask))
        return -1;
      memset(event, 0, sizeof(*event));
      sev = (XShapeEvent *)event;
      sev->type = X11_EXT_EVENT_BASE_SHAPE + ShapeNotify;
      sev->display = display;
      sev->window = s->w;
      sev->kind = ShapeBounding;
      sev->x = s->x_bounding;
      sev->y = s->y_bounding;
      sev->width = s->w_bounding;
      sev->height = s->h_bounding;
      sev->time = x11_next_fake_time();
      sev->shaped = s->bounding_shaped;
      x11_stamp_synthetic_event(event);
      return 0;
    }
  }
  if (strncmp(line + 6, "RandRNotify", 11) == 0) {
    unsigned int wid;
    int rw;
    int rh;

    wid = 0;
    rw = 0;
    rh = 0;
    sscanf(line, "EVENT RandRNotify wid=%u width=%d height=%d",
           &wid, &rw, &rh);

    if (rw > 0)
      display->width = rw;
    if (rh > 0)
      display->height = rh;

    return x11_build_randr_screen_change_event(
        display,
        wid ? (Window)wid : display->root,
        event);
  }
  if (strncmp(line + 6, "ClientMessage", 13) == 0) {
    unsigned int mtype = 0;
    unsigned int d0 = 0;
    event->type = ClientMessage;
    sscanf(line, "EVENT ClientMessage wid=%u type=%u data0=%u",
           &event->xclient.window, &mtype, &d0);
    event->xclient.message_type = (Atom)mtype;
    event->xclient.format = 32;
    event->xclient.data.l[0] = (long)d0;
    return 0;
  }
  if (strncmp(line + 6, "PropertyNotify", 14) == 0) {
    char atom_name[64];
    unsigned int st = 0;
    event->type = PropertyNotify;
    atom_name[0] = '\0';
    sscanf(line, "EVENT PropertyNotify wid=%u atom=%63s state=%u",
           &event->xproperty.window, atom_name, &st);
    sscanf(line, "EVENT PropertyNotify wid=%*u atom=%*s state=%*u time=%lu",
           &event->xproperty.time);
    event->xproperty.state = (int)st;
    event->xproperty.atom = atom_name[0] ? XInternAtom(display, atom_name, False) : None;
    return 0;
  }
  if (strncmp(line + 6, "SelectionClear", 14) == 0) {
    char sel_name[64];
    sel_name[0] = '\0';
    event->type = SelectionClear;
    sscanf(line, "EVENT SelectionClear wid=%u selection=%63s time=%lu",
           &event->xselectionclear.window, sel_name, &event->xselectionclear.time);
    event->xselectionclear.selection = sel_name[0] ? XInternAtom(display, sel_name, False) : None;
    return 0;
  }
  if (strncmp(line + 6, "SelectionRequest", 16) == 0) {
    char sel_name[64];
    char target_name[64];
    char prop_name[64];
    sel_name[0] = target_name[0] = prop_name[0] = '\0';
    event->type = SelectionRequest;
    sscanf(line, "EVENT SelectionRequest owner=%u requestor=%u selection=%63s target=%63s property=%63s time=%lu",
           &event->xselectionrequest.owner, &event->xselectionrequest.requestor,
           sel_name, target_name, prop_name, &event->xselectionrequest.time);
    event->xselectionrequest.selection = sel_name[0] ? XInternAtom(display, sel_name, False) : None;
    event->xselectionrequest.target = target_name[0] ? XInternAtom(display, target_name, False) : None;
    event->xselectionrequest.property = (!strcmp(prop_name, "NONE") || prop_name[0] == '\0')
                                        ? None : XInternAtom(display, prop_name, False);
    return 0;
  }
  if (strncmp(line + 6, "SelectionNotify", 15) == 0) {
    char sel_name[64];
    char target_name[64];
    char prop_name[64];
    sel_name[0] = target_name[0] = prop_name[0] = '\0';
    event->type = SelectionNotify;
    sscanf(line, "EVENT SelectionNotify requestor=%u selection=%63s target=%63s property=%63s time=%lu",
           &event->xselection.requestor, sel_name, target_name, prop_name,
           &event->xselection.time);
    event->xselection.selection = sel_name[0] ? XInternAtom(display, sel_name, False) : None;
    event->xselection.target = target_name[0] ? XInternAtom(display, target_name, False) : None;
    event->xselection.property = (!strcmp(prop_name, "NONE") || prop_name[0] == '\0')
                                 ? None : XInternAtom(display, prop_name, False);
    return 0;
  }
  return -1;
}

static long
x11_event_mask_for_type(int type)
{
  switch (type) {
  case KeyPress:
    return KeyPressMask;
  case KeyRelease:
    return KeyReleaseMask;
  case ButtonPress:
    return ButtonPressMask;
  case ButtonRelease:
    return ButtonReleaseMask;
  case MotionNotify:
    return PointerMotionMask | ButtonMotionMask;
  case EnterNotify:
    return EnterWindowMask;
  case LeaveNotify:
    return LeaveWindowMask;
  case Expose:
    return ExposureMask;
  case MapRequest:
  case ConfigureRequest:
    return SubstructureRedirectMask;
  case MapNotify:
    return StructureNotifyMask;
  case FocusIn:
  case FocusOut:
    return FocusChangeMask;
  case DestroyNotify:
  case ConfigureNotify:
    return StructureNotifyMask;
  case PropertyNotify:
    return PropertyChangeMask;
  case X11_EXT_EVENT_BASE_SHAPE + ShapeNotify:
    return ShapeNotifyMask;
  case X11_EXT_EVENT_BASE_RANDR + RRScreenChangeNotify:
    return RRScreenChangeNotifyMask | RRCrtcChangeNotifyMask |
           RROutputChangeNotifyMask | RROutputPropertyNotifyMask;
  default:
    return 0;
  }
}

static int
x11_read_event_from_wire(Display *display, XEvent *event)
{
  char line[X6_BUF_SIZE];
  if (!display || !event)
    return -1;

  while (1) {
    if (x11_read_line(display, line, sizeof(line)) < 0)
      return -1;
    if (x11_handle_unsolicited_line(display, line)) {
      if (x11_event_pop_front(display, event) == 0)
        return 0;
      continue;
    }
    if (x11_parse_event_line(display, line, event) == 0)
      return 0;
  }
}

/* Nonblocking variant used by XPending: drain any unsolicited lines without
 * ever stalling waiting for a true EVENT line. */
static int
x11_read_event_from_wire_nonblocking(Display *display, XEvent *event)
{
  int fd;
  int flags;
  int changed;
  int rc;

  if (!display || !event)
    return -1;

  fd = display->fd;
  changed = 0;
  flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0 && (flags & O_NONBLOCK) == 0) {
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0)
      changed = 1;
  }

  rc = x11_read_event_from_wire(display, event);

  if (changed)
    fcntl(fd, F_SETFL, flags);

  return rc;
}

static int
x11_read_event(Display *display, XEvent *event)
{
  if (x11_event_pop_front(display, event) == 0)
    return 0;
  return x11_read_event_from_wire(display, event);
}

static int
x11_read_line(Display *display, char *line, int maxlen)
{
  int i;
  int fd;

  if (!display || !display->rxbuf || !line || maxlen <= 1)
    return -1;
  fd = display->fd;

  while (1) {
    for (i = 0; i < display->rxlen; i++) {
      if (display->rxbuf[i] == '\n' || display->rxbuf[i] == '\r') {
        int copy_len;
        int consume;

        copy_len = i;
        if (copy_len > maxlen - 1)
          copy_len = maxlen - 1;
        memmove(line, display->rxbuf, (size_t)copy_len);
        line[copy_len] = '\0';

        consume = i + 1;
        while (consume < display->rxlen && (display->rxbuf[consume] == '\n' || display->rxbuf[consume] == '\r'))
          consume++;
        memmove(display->rxbuf, display->rxbuf + consume, (size_t)(display->rxlen - consume));
        display->rxlen -= consume;
        if (strncmp(line, "OK text", 7) != 0 && strncmp(line, "OK draw", 7) != 0)
          x11dbg("x11:wire:rx '%s' buffered=%d", line, display->rxlen);
        return copy_len;
      }
    }

    if (display->rxlen >= X11_RXBUF_SIZE)
      return -1;

    {
      int n = recv(fd, display->rxbuf + display->rxlen, (size_t)(X11_RXBUF_SIZE - display->rxlen));
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0)
        return -1;
      display->rxlen += n;
    }
  }
}

static int
x11_send(int fd, const char *cmd)
{
  size_t len;
  size_t off;

  if (!cmd)
    return -1;
  if (x11_is_chatty_draw_cmd(cmd)) {
    if ((g_x11_draw_tx_count++ % 256) == 0)
      x11dbg("x11:wire:tx(sampled) '%s'", cmd);
  } else {
    x11dbg("x11:wire:tx '%s'", cmd);
  }

  len = strlen(cmd);
  off = 0;
  while (off < len) {
    int n = send(fd, cmd + off, len - off);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    if (n == 0)
      return -1;
    off += (size_t)n;
  }
  return 0;
}

static int
x11_cmd(Display *dpy, const char *cmd, char *resp, int maxlen)
{
  char line[X6_BUF_SIZE];
  if (!dpy || dpy->fd < 0)
    return -1;
  x11dbg("x11:cmd:begin '%s'", cmd ? cmd : "(null)");
  /* Flush any buffered draw commands before sending a synchronous request.
   * We do NOT drain pending_draw_replies here — the response loop below will
   * consume 'OK draw'/'OK text'/'OK copy_area' lines via
   * x11_handle_unsolicited_line, eliminating head-of-line blocking. */
  if (x11_flush_draw_batch(dpy) < 0)
    return -1;
  if (x11_send(dpy->fd, cmd) < 0)
    return -1;
  if (!resp)
    return 0;

  while (1) {
    if (x11_read_line(dpy, line, sizeof(line)) < 0)
      return -1;

    if (x11_handle_unsolicited_line(dpy, line))
      continue;

    strncpy(resp, line, maxlen - 1);
    resp[maxlen - 1] = '\0';
    x11dbg("x11:cmd:resp '%s'", resp);
    return (int)strlen(resp);
  }
}

static int
x11_parse_hello_dim(const char *line, const char *key, int fallback)
{
  const char *p;
  int v;

  if (!line || !key)
    return fallback;

  p = strstr(line, key);
  if (!p)
    return fallback;
  p += strlen(key);

  v = atoi(p);
  if (v <= 0)
    return fallback;
  return v;
}

static const char *
atom_to_name(Atom a)
{
  int i;
  switch (a) {
  case XA_PRIMARY: return "PRIMARY";
  case XA_SECONDARY: return "SECONDARY";
  case XA_ATOM: return "ATOM";
  case XA_STRING: return "STRING";
  case XA_VISUALID: return "VISUALID";
  case XA_WINDOW: return "WINDOW";
  case XA_WM_HINTS: return "WM_HINTS";
  case XA_WM_NAME: return "WM_NAME";
  case XA_WM_NORMAL_HINTS: return "WM_NORMAL_HINTS";
  case XA_WM_TRANSIENT_FOR: return "WM_TRANSIENT_FOR";
  default:
    break;
  }
  for (i = 0; i < g_atom_count; i++) {
    if (g_atoms[i].atom == a)
      return g_atoms[i].name;
  }
  return "UNKNOWN";
}

Display *
XOpenDisplay(char *display_name)
{
  struct sockaddr_in addr;
  char line[X6_BUF_SIZE];
  Display *dpy;
  (void)display_name;

  if (!g_x11_sigpipe_ignored) {
    signal(SIGPIPE, SIG_IGN);
    g_x11_sigpipe_ignored = 1;
  }

  if (g_display)
    return g_display;

  dpy = malloc(sizeof(*dpy));
  if (!dpy)
    return 0;
  memset(dpy, 0, sizeof(*dpy));
  dpy->fd = -1;
  dpy->event_queue = malloc((size_t)X11_MAX_EVENTS * sizeof(XEvent));
  dpy->rxbuf = malloc(X11_RXBUF_SIZE);
  if (!dpy->event_queue || !dpy->rxbuf)
    goto fail;

  dpy->fd = socket(AF_INET, SOCK_STREAM, 0);
  if (dpy->fd < 0) {
    goto fail;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = (ushort)X6_PORT;
  addr.sin_addr = INADDR_LOOPBACK;
  if (connect(dpy->fd, &addr, sizeof(addr)) < 0) {
    goto fail;
  }

  if (x11_read_line(dpy, line, sizeof(line)) < 0) {
    goto fail;
  }
  x11dbg("x11:open hello='%s'", line);
  if (x11_cmd(dpy, "HELLO x6/1\n", line, sizeof(line)) < 0) {
    goto fail;
  }
  if (strncmp(line, "OK proto=", 9) != 0) {
    goto fail;
  }

  dpy->screen = 0;
  dpy->root = 1;
  dpy->width = x11_parse_hello_dim(line, "width=", 1024);
  dpy->height = x11_parse_hello_dim(line, "height=", 768);
  dpy->depth = 32;
  dpy->is_wm = 0;
  dpy->event_count = 0;
  dpy->rxlen = 0;
  dpy->pending_draw_replies = 0;
  dpy->draw_batch_cap = X11_DRAW_BATCH_CAP;
  dpy->draw_batch_buf = malloc((size_t)dpy->draw_batch_cap);
  if (!dpy->draw_batch_buf)
    goto fail;
  dpy->draw_batch_len = 0;
  dpy->draw_batch_last_copy_area_off = -1;
  dpy->draw_batch_last_copy_area_len = 0;
  g_x11_dbg_count = 0;
  x11dbg("x11:open ready fd=%d root=%lu size=%dx%d", dpy->fd, dpy->root, dpy->width, dpy->height);
  g_display = dpy;
  return dpy;

fail:
  if (dpy->fd >= 0)
    close(dpy->fd);
  if (dpy->event_queue)
    free(dpy->event_queue);
  if (dpy->rxbuf)
    free(dpy->rxbuf);
  if (dpy->draw_batch_buf)
    free(dpy->draw_batch_buf);
  free(dpy);
  return 0;
}

int
XCloseDisplay(Display *display)
{
  char line[X6_BUF_SIZE];

  if (!display)
    return 0;
  if (display->fd >= 0) {
    if (x11_cmd(display, "DETACH\n", line, sizeof(line)) < 0)
      x11_send(display->fd, "DETACH\n");
    close(display->fd);
  }
  if (g_display == display)
    g_display = 0;
  g_xft_gc = 0;
  if (display->event_queue)
    free(display->event_queue);
  if (display->rxbuf)
    free(display->rxbuf);
  if (display->draw_batch_buf) {
    free(display->draw_batch_buf);
  }
  free(display);
  return 0;
}

int
XSync(Display *display, Bool discard)
{
  char line[X6_BUF_SIZE];
  (void)discard;
  return x11_cmd(display, "PING\n", line, sizeof(line)) < 0 ? -1 : 0;
}

int
XFlush(Display *display)
{
  if (!display)
    return 0;
  return x11_flush_draw_batch(display) < 0 ? -1 : 0;
}

Window
XCreateWindow(Display *display, Window parent, int x, int y,
              unsigned int width, unsigned int height,
              unsigned int border_width, int depth, unsigned int class,
              void *visual, unsigned long valuemask,
              XSetWindowAttributes *attributes)
{
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  Window w;
  char attrcmd[X6_BUF_SIZE];
  (void)depth; (void)class;
  (void)visual;

  if (!display)
    return None;

  snprintf(cmd, sizeof(cmd), "CREATE %u %d %d %d %d\n", (uint)(parent ? parent : display->root),
           x, y, (int)width, (int)height);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return None;
  if (sscanf(line, "OK create wid=%lu", &w) != 1)
    return None;

  if (border_width > 0) {
    snprintf(attrcmd, sizeof(attrcmd), "SET_BORDER_WIDTH %u %u\n", (uint)w, border_width);
    if (x11_cmd(display, attrcmd, line, sizeof(line)) < 0)
      return None;
  }

  if (valuemask && attributes) {
    if (XChangeWindowAttributes(display, w, valuemask, attributes) < 0)
      return None;
  }

  return w;
}

Window
XCreateSimpleWindow(Display *display, Window parent, int x, int y,
                    unsigned int width, unsigned int height,
                    unsigned int border_width, unsigned long border,
                    unsigned long background)
{
  (void)border;
  (void)background;
  return XCreateWindow(display, parent, x, y, width, height, border_width,
                       CopyFromParent, InputOutput, 0, 0, 0);
}

int
XDestroyWindow(Display *display, Window w)
{
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  snprintf(cmd, sizeof(cmd), "DESTROY %u\n", (uint)w);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  return strncmp(line, "OK destroy", 10) == 0 ? 0 : -1;
}

int
XMapWindow(Display *display, Window w)
{
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];

  if (display->is_wm)
    snprintf(cmd, sizeof(cmd), "WM_MAP %u\n", (uint)w);
  else
    snprintf(cmd, sizeof(cmd), "MAP %u\n", (uint)w);

  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  x11dbg("x11:map wid=%u resp='%s'", (uint)w, line);
  if (strncmp(line, "OK map", 6) == 0 ||
      strncmp(line, "OK mapped", 9) == 0 ||
      strncmp(line, "PENDING map", 11) == 0)
    return 0;
  return -1;
}

int
XMapRaised(Display *display, Window w)
{
  if (XMapWindow(display, w) != 0)
    return -1;
  return XRaiseWindow(display, w);
}

int
XUnmapWindow(Display *display, Window w)
{
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];

  if (display->is_wm)
    snprintf(cmd, sizeof(cmd), "WM_UNMAP %u\n", (uint)w);
  else
    snprintf(cmd, sizeof(cmd), "UNMAP %u\n", (uint)w);

  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  return strncmp(line, "OK unmap", 8) == 0 ||
         strncmp(line, "OK unmapped", 11) == 0 ? 0 : -1;
}

int
XMoveResizeWindow(Display *display, Window w, int x, int y, unsigned int width, unsigned int height)
{
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];

  if (display->is_wm)
    snprintf(cmd, sizeof(cmd), "WM_CONFIGURE %u %d %d %d %d\n", (uint)w, x, y, (int)width, (int)height);
  else
    snprintf(cmd, sizeof(cmd), "CONFIGURE %u %d %d %d %d\n", (uint)w, x, y, (int)width, (int)height);

  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  if (strncmp(line, "OK configure", 12) == 0 ||
      strncmp(line, "OK configured", 13) == 0 ||
      strncmp(line, "PENDING configure", 17) == 0)
    return 0;
  return -1;
}

int XMoveWindow(Display *display, Window w, int x, int y) {
  XWindowAttributes attrs;
  if (!display)
    return -1;
  if (!XGetWindowAttributes(display, w, &attrs))
    return -1;
  return XMoveResizeWindow(display, w, x, y, (unsigned int)attrs.width, (unsigned int)attrs.height);
}

int
XResizeWindow(Display *display, Window w, unsigned int width, unsigned int height)
{
  XWindowAttributes attrs;

  if (!display)
    return -1;
  if (!XGetWindowAttributes(display, w, &attrs))
    return -1;
  return XMoveResizeWindow(display, w, attrs.x, attrs.y, width, height);
}

int XRaiseWindow(Display *display, Window w) {
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  if (!display)
    return -1;
  snprintf(cmd, sizeof(cmd), "RAISE %u\n", (uint)w);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  return strncmp(line, "OK raised", 9) == 0 ? 0 : -1;
}
int XLowerWindow(Display *display, Window w) {
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  if (!display)
    return -1;
  snprintf(cmd, sizeof(cmd), "LOWER %u\n", (uint)w);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  return strncmp(line, "OK lowered", 10) == 0 ? 0 : -1;
}

int
XRestackWindows(Display *display, Window windows[], int nwindows)
{
  XWindowChanges wc;
  int i;

  if (!display)
    return 0;
  if (!windows || nwindows < 0)
    return 0;
  if (nwindows <= 1)
    return 1;

  /* Apply from bottom-to-top so list order (top-first) is preserved. */
  for (i = nwindows - 1; i >= 0; i--) {
    wc.sibling = (Window)0;
    wc.stack_mode = Above;
    if (XConfigureWindow(display, windows[i], CWStackMode, &wc) != 0)
      return 0;
  }

  return 1;
}

int
XConfigureWindow(Display *display, Window w, unsigned int value_mask, XWindowChanges *values)
{
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  int x = 0;
  int y = 0;
  unsigned int ww = 0;
  unsigned int hh = 0;

  if (!display)
    return -1;

  if (value_mask & CWBorderWidth) {
    int bw = values ? values->border_width : 0;
    if (bw < 0)
      bw = 0;
    snprintf(cmd, sizeof(cmd), "SET_BORDER_WIDTH %u %d\n", (uint)w, bw);
    if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
      return -1;
  }

  if (value_mask & CWStackMode) {
    Window sibling = 0;
    int stack_mode = Above;

    if (values) {
      stack_mode = values->stack_mode;
      if (value_mask & CWSibling)
        sibling = values->sibling;
    }

    snprintf(cmd, sizeof(cmd), "RESTACK %u %u %d\n", (uint)w, (uint)sibling, stack_mode);
    if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
      return -1;
    if (strncmp(line, "OK restack", 10) != 0)
      return -1;
  }

  if (!(value_mask & (CWX | CWY | CWWidth | CWHeight)))
    return 0;

  if (values && (value_mask & CWX))
    x = values->x;
  if (values && (value_mask & CWY))
    y = values->y;
  if (values && (value_mask & CWWidth))
    ww = (unsigned int)values->width;
  if (values && (value_mask & CWHeight))
    hh = (unsigned int)values->height;

  /* Preserve unspecified geometry fields using the current server-side attrs. */
  if (!(value_mask & (CWX | CWY | CWWidth | CWHeight)) ||
      !(value_mask & CWX) || !(value_mask & CWY) ||
      !(value_mask & CWWidth) || !(value_mask & CWHeight)) {
    XWindowAttributes attrs;
    if (XGetWindowAttributes(display, w, &attrs)) {
      if (!(value_mask & CWX)) x = attrs.x;
      if (!(value_mask & CWY)) y = attrs.y;
      if (!(value_mask & CWWidth)) ww = (unsigned int)attrs.width;
      if (!(value_mask & CWHeight)) hh = (unsigned int)attrs.height;
    }
  }
  return XMoveResizeWindow(display, w, x, y, ww, hh);
}

int
XGetWindowAttributes(Display *display, Window w, XWindowAttributes *attrs)
{
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  int x, y, ww, hh, bw, depth, mapped, override;
  long evmask;

  if (!display)
    return 0;
  if (!attrs)
    return 0;

  snprintf(cmd, sizeof(cmd), "GET_WINDOW_ATTR %u\n", (uint)w);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return 0;

  if (sscanf(line,
             "OK attr x=%d y=%d w=%d h=%d bw=%d depth=%d mapped=%d override=%d events=%ld",
             &x, &y, &ww, &hh, &bw, &depth, &mapped, &override, &evmask) != 9)
    return 0;

  memset(attrs, 0, sizeof(*attrs));
  attrs->x = x;
  attrs->y = y;
  attrs->width = ww;
  attrs->height = hh;
  attrs->border_width = bw;
  attrs->depth = depth;
  attrs->override_redirect = override ? True : False;
  attrs->map_state = mapped ? IsViewable : IsUnmapped;
  attrs->your_event_mask = evmask;
  return 1;
}

int
XSelectInput(Display *display, Window w, long event_mask)
{
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];

  if (!display)
    return -1;

  // Claim WM redirect role when selecting SubstructureRedirect on root.
  if (w == display->root && (event_mask & SubstructureRedirectMask)) {
    snprintf(cmd, sizeof(cmd), "REQUEST_REDIRECT %u\n", (uint)display->root);
    if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
      return -1;
    if (strncmp(line, "OK redirect_granted", 19) == 0)
      display->is_wm = 1;
    else if (strncmp(line, "ERR redirect-in-use", 19) == 0)
      display->is_wm = 0;
    else
      return -1;
  }

  // Register the event mask for this window so the server routes events here.
  snprintf(cmd, sizeof(cmd), "SELECT_EVENTS %u %ld\n", (uint)w, event_mask);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;

  x11dbg("x11:select-input wid=%u mask=%ld resp='%s'", (uint)w, event_mask, line);

  return 0;
}

static Window
x11_event_window(const XEvent *event)
{
  if (!event)
    return None;

  switch (event->type) {
  case KeyPress:
  case KeyRelease:
    return event->xkey.window;
  case ButtonPress:
  case ButtonRelease:
    return event->xbutton.window;
  case MotionNotify:
    return event->xmotion.window;
  case EnterNotify:
  case LeaveNotify:
    return event->xcrossing.window;
  case Expose:
    return event->xexpose.window;
  case ConfigureNotify:
    return event->xconfigure.window;
  case ConfigureRequest:
    return event->xconfigurerequest.window;
  case MapRequest:
    return event->xmaprequest.window;
  case MapNotify:
    return event->xmap.window;
  case DestroyNotify:
    return event->xdestroywindow.window;
  case FocusIn:
  case FocusOut:
    return event->xfocus.window;
  case PropertyNotify:
    return event->xproperty.window;
  case SelectionClear:
    return event->xselectionclear.window;
  case SelectionRequest:
    return event->xselectionrequest.owner;
  case SelectionNotify:
    return event->xselection.requestor;
  case ClientMessage:
    return event->xclient.window;
  default:
    return event->xany.window;
  }
}

int
XNextEvent(Display *display, XEvent *event)
{
  if (x11_event_pop_front(display, event) == 0) {
    if (event && (event->type == KeyPress || event->type == KeyRelease)) {
      x11dbg("x11:next-event(pop) type=%d keycode=%u state=%u qlen=%d",
             event->type, event->xkey.keycode, event->xkey.state, display->event_count);
    }
    return 0;
  }
  if (x11_read_event(display, event) < 0)
    return -1;
  x11dbg("x11:next-event type=%d window=%u", event->type, (uint)x11_event_window(event));
  return 0;
}

int
XPeekEvent(Display *display, XEvent *event)
{
  XEvent tmp;

  if (!display || !event)
    return -1;
  if (x11_event_peek_front(display, event) == 0)
    return 0;
  if (x11_read_event(display, &tmp) < 0)
    return -1;
  x11_event_push_front(display, &tmp);
  *event = tmp;
  return 0;
}

int
XPutBackEvent(Display *display, XEvent *event)
{
  if (!display)
    return 0;
  if (!event)
    return 0;
  x11_event_push_front(display, event);
  return 0;
}

int
XMaskEvent(Display *display, long event_mask, XEvent *event)
{
  if (!display || !event)
    return -1;

  if (x11_event_find_and_pop_mask(display, event_mask, event) == 0)
    return 0;

  while (1) {
    XEvent tmp;
    if (x11_read_event_from_wire(display, &tmp) < 0)
      return -1;
    if (x11_event_mask_for_type(tmp.type) & event_mask) {
      *event = tmp;
      return 0;
    }
    x11_event_push_back(display, &tmp);
  }
}

Bool XCheckMaskEvent(Display *display, long event_mask, XEvent *event) {
  if (!display)
    return False;
  if (!event)
    return False;
  return x11_event_find_and_pop_mask(display, event_mask, event) == 0 ? True : False;
}

Bool
XCheckIfEvent(Display *display, XEvent *event_return,
              Bool (*predicate)(Display *, XEvent *, XPointer),
              XPointer arg)
{
  if (!display || !event_return || !predicate)
    return False;

  if (x11_event_find_and_pop_predicate(display, event_return, predicate, arg) == 0)
    return True;

  if (!XPending(display))
    return False;

  if (x11_event_find_and_pop_predicate(display, event_return, predicate, arg) == 0)
    return True;

  return False;
}

Bool
XCheckTypedEvent(Display *display, int event_type, XEvent *event_return)
{
  if (!display || !event_return)
    return False;
  if (x11_event_find_and_pop_type(display, event_type, None, 0, event_return) == 0)
    return True;
  if (!XPending(display))
    return False;
  if (x11_event_find_and_pop_type(display, event_type, None, 0, event_return) == 0)
    return True;
  return False;
}

Bool
XCheckTypedWindowEvent(Display *display, Window w, int event_type, XEvent *event_return)
{
  if (!display || !event_return)
    return False;
  if (x11_event_find_and_pop_type(display, event_type, w, 1, event_return) == 0)
    return True;
  if (!XPending(display))
    return False;
  if (x11_event_find_and_pop_type(display, event_type, w, 1, event_return) == 0)
    return True;
  return False;
}

Bool
XCheckWindowEvent(Display *display, Window w, long event_mask, XEvent *event_return)
{
  if (!display || !event_return)
    return False;
  if (x11_event_find_and_pop_window_mask(display, w, event_mask, event_return) == 0)
    return True;
  if (!XPending(display))
    return False;
  if (x11_event_find_and_pop_window_mask(display, w, event_mask, event_return) == 0)
    return True;
  return False;
}

int
XWindowEvent(Display *display, Window w, long event_mask, XEvent *event_return)
{
  XEvent ev;

  if (!display || !event_return)
    return -1;

  if (x11_event_find_and_pop_window_mask(display, w, event_mask, event_return) == 0)
    return 0;

  while (1) {
    if (x11_read_event_from_wire(display, &ev) < 0)
      return -1;
    if (x11_event_window(&ev) == w &&
        (x11_event_mask_for_type(ev.type) & event_mask)) {
      *event_return = ev;
      return 0;
    }
    x11_event_push_back(display, &ev);
  }
}

Atom
XInternAtom(Display *display, char *atom_name, Bool only_if_exists)
{
  int i;
  (void)display;
  if (!atom_name)
    return None;

  if (!strcmp(atom_name, "WM_NAME")) return XA_WM_NAME;
  if (!strcmp(atom_name, "WM_NORMAL_HINTS")) return XA_WM_NORMAL_HINTS;
  if (!strcmp(atom_name, "WM_HINTS")) return XA_WM_HINTS;
  if (!strcmp(atom_name, "WM_TRANSIENT_FOR")) return XA_WM_TRANSIENT_FOR;
  if (!strcmp(atom_name, "ATOM")) return XA_ATOM;
  if (!strcmp(atom_name, "STRING")) return XA_STRING;
  if (!strcmp(atom_name, "WINDOW")) return XA_WINDOW;

  for (i = 0; i < g_atom_count; i++) {
    if (!strcmp(g_atoms[i].name, atom_name))
      return g_atoms[i].atom;
  }

  if (only_if_exists || g_atom_count >= X11_MAX_ATOMS)
    return None;

  g_atoms[g_atom_count].atom = g_next_atom++;
  strncpy(g_atoms[g_atom_count].name, atom_name, sizeof(g_atoms[g_atom_count].name) - 1);
  g_atoms[g_atom_count].name[sizeof(g_atoms[g_atom_count].name) - 1] = '\0';
  g_atom_count++;
  return g_atoms[g_atom_count - 1].atom;
}

char *
XGetAtomName(Display *display, Atom atom)
{
  (void)display;
  return (char *)atom_to_name(atom);
}

XContext
XUniqueContext(void)
{
  XContext id;

  id = g_next_context_id++;
  if (id == 0)
    id = g_next_context_id++;
  return id;
}

int
XSaveContext(Display *display, XID rid, XContext context, const char *data)
{
  int i;

  (void)display;

  for (i = 0; i < X11_MAX_CONTEXT_ENTRIES; i++) {
    if (!g_context_entries[i].in_use)
      continue;
    if (g_context_entries[i].rid == rid && g_context_entries[i].context == context) {
      g_context_entries[i].data = data;
      return 0;
    }
  }

  for (i = 0; i < X11_MAX_CONTEXT_ENTRIES; i++) {
    if (g_context_entries[i].in_use)
      continue;
    g_context_entries[i].in_use = 1;
    g_context_entries[i].rid = rid;
    g_context_entries[i].context = context;
    g_context_entries[i].data = data;
    return 0;
  }

  return 1;
}

int
XFindContext(Display *display, XID rid, XContext context, char **data_return)
{
  int i;

  (void)display;

  if (data_return)
    *data_return = 0;

  for (i = 0; i < X11_MAX_CONTEXT_ENTRIES; i++) {
    if (!g_context_entries[i].in_use)
      continue;
    if (g_context_entries[i].rid != rid || g_context_entries[i].context != context)
      continue;
    if (data_return)
      *data_return = (char *)g_context_entries[i].data;
    return 0;
  }

  return 1;
}

int
XDeleteContext(Display *display, XID rid, XContext context)
{
  int i;

  (void)display;

  for (i = 0; i < X11_MAX_CONTEXT_ENTRIES; i++) {
    if (!g_context_entries[i].in_use)
      continue;
    if (g_context_entries[i].rid != rid || g_context_entries[i].context != context)
      continue;
    memset(&g_context_entries[i], 0, sizeof(g_context_entries[i]));
    return 0;
  }

  return 1;
}

static int
x11_prop_unit_bytes(int format)
{
  if (format == 8)
    return 1;
  if (format == 16)
    return 2;
  if (format == 32)
    return 4;
  return 0;
}

static int
x11_hex_nibble(int c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static char *
x11_prop_pack(Atom type, int format, const unsigned char *data, unsigned long nelements)
{
  static const char hexd[] = "0123456789abcdef";
  unsigned long nbytes;
  unsigned long i;
  char *out;
  int unit;
  int hdr;

  unit = x11_prop_unit_bytes(format);
  if (unit <= 0)
    return 0;

  nbytes = nelements * (unsigned long)unit;
  out = (char *)malloc((size_t)(nbytes * 2 + 96));
  if (!out)
    return 0;

  hdr = snprintf(out, 96, "@AUXP1@%lu@%d@%lu@", (unsigned long)type, format, nelements);
  if (hdr < 0) {
    free(out);
    return 0;
  }

  for (i = 0; i < nbytes; i++) {
    unsigned char b = data ? data[i] : 0;
    out[hdr + (int)(2 * i)] = hexd[(b >> 4) & 0x0f];
    out[hdr + (int)(2 * i) + 1] = hexd[b & 0x0f];
  }
  out[hdr + (int)(2 * nbytes)] = '\0';
  return out;
}

static int
x11_prop_unpack(const char *wire,
                Atom *type_out,
                int *format_out,
                unsigned long *nitems_out,
                unsigned char **bytes_out)
{
  unsigned long typev;
  int fmt;
  unsigned long nitems;
  const char *hex;
  unsigned long nbytes;
  unsigned long i;
  int unit;
  unsigned char *buf;

  if (!wire || !type_out || !format_out || !nitems_out || !bytes_out)
    return -1;

  if (strncmp(wire, "@AUXP1@", 7) != 0) {
    nbytes = strlen(wire);
    buf = (unsigned char *)malloc((size_t)nbytes + 1);
    if (!buf)
      return -1;
    if (nbytes > 0)
      memmove(buf, wire, (size_t)nbytes);
    buf[nbytes] = '\0';
    *type_out = XA_STRING;
    *format_out = 8;
    *nitems_out = nbytes;
    *bytes_out = buf;
    return 0;
  }

  if (sscanf(wire, "@AUXP1@%lu@%d@%lu@", &typev, &fmt, &nitems) != 3)
    return -1;

  unit = x11_prop_unit_bytes(fmt);
  if (unit <= 0)
    return -1;

  hex = wire;
  hex = strchr(hex + 7, '@');
  if (!hex) return -1;
  hex = strchr(hex + 1, '@');
  if (!hex) return -1;
  hex = strchr(hex + 1, '@');
  if (!hex) return -1;
  hex++;

  nbytes = nitems * (unsigned long)unit;
  if (strlen(hex) < nbytes * 2)
    return -1;

  buf = (unsigned char *)malloc((size_t)nbytes + 1);
  if (!buf)
    return -1;

  for (i = 0; i < nbytes; i++) {
    int hi = x11_hex_nibble(hex[2 * i]);
    int lo = x11_hex_nibble(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) {
      free(buf);
      return -1;
    }
    buf[i] = (unsigned char)((hi << 4) | lo);
  }
  buf[nbytes] = '\0';

  *type_out = (Atom)typev;
  *format_out = fmt;
  *nitems_out = nitems;
  *bytes_out = buf;
  return 0;
}

int
XChangeProperty(Display *display, Window w, Atom property, Atom type,
                int format, int mode, unsigned char *data, int nelements)
{
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  const char *name = atom_to_name(property);
  char *packed;
  unsigned long base_nitems;
  Atom base_type;
  int base_format;
  int unit;

  if (!display)
    return -1;

  if (mode == PropModeReplace || nelements <= 0) {
    packed = x11_prop_pack(type, format, data, nelements > 0 ? (unsigned long)nelements : 0UL);
    if (!packed)
      return -1;
  } else {
    unsigned char *old_bytes;
    unsigned char *merged;
    unsigned long merged_items;
    unsigned long old_bytes_len;
    unsigned long add_bytes_len;
    int new_off;

    old_bytes = 0;
    base_nitems = 0;
    base_type = type;
    base_format = format;

    if (XGetWindowProperty(display, w, property, 0, 1L << 20, False, type,
                           &base_type, &base_format, &base_nitems, 0, &old_bytes) != Success) {
      old_bytes = 0;
      base_nitems = 0;
      base_type = type;
      base_format = format;
    }

    unit = x11_prop_unit_bytes(format);
    if (unit <= 0 || base_format != format || base_type != type) {
      if (old_bytes) XFree(old_bytes);
      return -1;
    }

    old_bytes_len = base_nitems * (unsigned long)unit;
    add_bytes_len = (unsigned long)nelements * (unsigned long)unit;
    merged = (unsigned char *)malloc((size_t)(old_bytes_len + add_bytes_len));
    if (!merged) {
      if (old_bytes) XFree(old_bytes);
      return -1;
    }

    if (mode == PropModePrepend) {
      if (add_bytes_len > 0 && data)
        memmove(merged, data, (size_t)add_bytes_len);
      if (old_bytes_len > 0 && old_bytes)
        memmove(merged + add_bytes_len, old_bytes, (size_t)old_bytes_len);
    } else {
      if (old_bytes_len > 0 && old_bytes)
        memmove(merged, old_bytes, (size_t)old_bytes_len);
      if (add_bytes_len > 0 && data)
        memmove(merged + old_bytes_len, data, (size_t)add_bytes_len);
    }

    merged_items = (old_bytes_len + add_bytes_len) / (unsigned long)unit;
    packed = x11_prop_pack(type, format, merged, merged_items);
    free(merged);
    if (old_bytes) XFree(old_bytes);
    if (!packed)
      return -1;

    new_off = (int)strlen(packed);
    if (new_off + 64 >= (int)sizeof(cmd)) {
      free(packed);
      return -1;
    }
  }

  snprintf(cmd, sizeof(cmd), "SET_PROPERTY %u %s %s\n", (uint)w, name, packed);
  free(packed);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  return strncmp(line, "OK property_set", 15) == 0 ? 0 : -1;
}

int
XGetWindowProperty(Display *display, Window w, Atom property,
                   long long_offset, long long_length, Bool del,
                   Atom req_type, Atom *actual_type_return,
                   int *actual_format_return, unsigned long *nitems_return,
                   unsigned long *bytes_after_return,
                   unsigned char **prop_return)
{
  static Atom wm_class_atom;
  static Atom wm_name_atom;
  static Atom net_wm_name_atom;
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  unsigned char *all_bytes;
  unsigned long all_nitems;
  Atom all_type;
  int all_format;
  int unit;
  unsigned long start_item;
  unsigned long want_items;
  unsigned long avail_items;
  unsigned long copy_items;
  unsigned long copy_bytes;
  char *space;
  const char *name = atom_to_name(property);
  int track_title_prop;

  if (!display || !prop_return)
    return -1;

  if (wm_class_atom == 0)
    wm_class_atom = XInternAtom(display, "WM_CLASS", False);
  if (wm_name_atom == 0)
    wm_name_atom = XInternAtom(display, "WM_NAME", False);
  if (net_wm_name_atom == 0)
    net_wm_name_atom = XInternAtom(display, "_NET_WM_NAME", False);

  track_title_prop = (property == wm_class_atom ||
                      property == wm_name_atom ||
                      property == net_wm_name_atom);

  if (track_title_prop)
    x11consolecrit("x11:getprop begin wid=%u name=%s req_type=%lu", (uint)w, name, (unsigned long)req_type);

  snprintf(cmd, sizeof(cmd), "GET_PROPERTY %u %s\n", (uint)w, name);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;

  if (track_title_prop)
    x11consolecrit("x11:getprop resp wid=%u line='%s'", (uint)w, line);

  if (strncmp(line, "ERR no-such-property", 20) == 0) {
    *prop_return = 0;
    if (nitems_return) *nitems_return = 0;
    if (bytes_after_return) *bytes_after_return = 0;
    return Success;
  }

  if (strncmp(line, "VALUE ", 6) != 0)
    return -1;

  space = strchr(line + 6, ' ');
  if (!space)
    return -1;
  space++;

  all_bytes = 0;
  all_nitems = 0;
  all_type = XA_STRING;
  all_format = 8;
  if (x11_prop_unpack(space, &all_type, &all_format, &all_nitems, &all_bytes) < 0)
    return -1;

  if (track_title_prop)
    x11consolecrit("x11:getprop unpack wid=%u type=%lu fmt=%d nitems=%lu", (uint)w,
                   (unsigned long)all_type, all_format, all_nitems);

  if (req_type != 0 && req_type != all_type) {
    if (actual_type_return) *actual_type_return = all_type;
    if (actual_format_return) *actual_format_return = all_format;
    if (nitems_return) *nitems_return = 0;
    if (bytes_after_return) *bytes_after_return = all_nitems * (unsigned long)x11_prop_unit_bytes(all_format);
    if (del)
      XDeleteProperty(display, w, property);
    XFree(all_bytes);
    *prop_return = 0;
    return Success;
  }

  unit = x11_prop_unit_bytes(all_format);
  if (unit <= 0) {
    XFree(all_bytes);
    return -1;
  }

  start_item = (long_offset < 0) ? 0UL : (unsigned long)long_offset;
  if (start_item > all_nitems)
    start_item = all_nitems;

  avail_items = all_nitems - start_item;
  want_items = (long_length < 0) ? avail_items : (unsigned long)long_length;
  if (want_items > avail_items)
    want_items = avail_items;
  copy_items = want_items;
  copy_bytes = copy_items * (unsigned long)unit;

  if (!del && start_item == 0 && copy_items == all_nitems) {
    *prop_return = all_bytes;
    if (actual_type_return) *actual_type_return = all_type;
    if (actual_format_return) *actual_format_return = all_format;
    if (nitems_return) *nitems_return = copy_items;
    if (bytes_after_return) *bytes_after_return = 0;
    if (track_title_prop)
      x11consolecrit("x11:getprop fast-return wid=%u bytes=%lu ptr=%p", (uint)w,
                     copy_bytes, *prop_return);
    return Success;
  }

  *prop_return = (unsigned char *)malloc((size_t)copy_bytes + 1);
  if (!*prop_return) {
    XFree(all_bytes);
    return -1;
  }
  if (track_title_prop)
    x11consolecrit("x11:getprop copy-return wid=%u bytes=%lu src=%p dst=%p", (uint)w,
                   copy_bytes, all_bytes, *prop_return);
  if (copy_bytes > 0)
    memmove(*prop_return, all_bytes + start_item * (unsigned long)unit, (size_t)copy_bytes);
  (*prop_return)[copy_bytes] = '\0';

  if (actual_type_return) *actual_type_return = all_type;
  if (actual_format_return) *actual_format_return = all_format;
  if (nitems_return) *nitems_return = copy_items;
  if (bytes_after_return)
    *bytes_after_return = (all_nitems - (start_item + copy_items)) * (unsigned long)unit;

  if (del && (all_nitems - (start_item + copy_items) == 0))
    XDeleteProperty(display, w, property);
  if (track_title_prop)
    x11consolecrit("x11:getprop return wid=%u bytes=%lu ptr=%p", (uint)w,
                   copy_bytes, *prop_return);
  XFree(all_bytes);
  return Success;
}

int XDeleteProperty(Display *display, Window w, Atom property) {
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  const char *name = atom_to_name(property);
  if (!display)
    return -1;
  snprintf(cmd, sizeof(cmd), "DELETE_PROPERTY %u %s\n", (uint)w, name);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  return 0;
}

int XSetSelectionOwner(Display *display, Atom selection, Window owner, Time time) {
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  const char *sel_name;
  struct x11_selection_state *s;

  if (!display)
    return 0;
  sel_name = atom_to_name(selection);
  snprintf(cmd, sizeof(cmd), "SET_SELECTION_OWNER %s %u %u\n",
           sel_name, (uint)owner, (uint)time);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return 0;
  if (strncmp(line, "OK selection_owner_set", 22) != 0)
    return 0;

  s = x11_find_selection(selection, 1);
  if (s) {
    s->owner = owner;
    s->time = time;
  }
  return 1;
}

int XGetSelectionOwner(Display *display, Atom selection) {
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  const char *sel_name;
  unsigned int owner = 0;
  struct x11_selection_state *s;

  if (!display)
    return None;
  sel_name = atom_to_name(selection);
  snprintf(cmd, sizeof(cmd), "GET_SELECTION_OWNER %s\n", sel_name);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    goto fallback;
  if (sscanf(line, "OK selection_owner %u", &owner) == 1)
    return (Window)owner;

fallback:
  s = x11_find_selection(selection, 0);
  return s ? s->owner : None;
}

int XConvertSelection(Display *display, Atom selection, Atom target, Atom property, Window requestor, Time time) {
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  const char *sel_name;
  const char *target_name;
  const char *prop_name;

  if (!display)
    return 0;
  if (selection == None || target == None || requestor == None)
    return 0;

  sel_name = atom_to_name(selection);
  target_name = atom_to_name(target);
  prop_name = (property == None) ? "NONE" : atom_to_name(property);

  snprintf(cmd, sizeof(cmd), "CONVERT_SELECTION %s %s %s %u %u\n",
           sel_name, target_name, prop_name, (uint)requestor, (uint)time);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return 0;
  return strncmp(line, "OK selection_convert", 19) == 0 ? 1 : 0;
}

Status XSendEvent(Display *display, Window w, Bool propagate, long event_mask, XEvent *event_send) {
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  (void)w;
  (void)propagate;
  (void)event_mask;
  if (!display || !event_send)
    return 0;

  if (event_send->type == ConfigureNotify) {
    int ret;

    x11dbg("x11:sendevent cfg wid=%u xy=%d,%d wh=%dx%d begin",
           (uint)event_send->xconfigure.window,
           event_send->xconfigure.x,
           event_send->xconfigure.y,
           event_send->xconfigure.width,
           event_send->xconfigure.height);

    if (display->is_wm) {
      /* WM-originated ConfigureNotify should reflect actual WM geometry.
       * Route via WM_CONFIGURE so server-side state and client notify stay
       * coherent, matching normal X11 WM flow expectations. */
      snprintf(cmd, sizeof(cmd), "WM_CONFIGURE %u %d %d %d %d\n",
               (uint)event_send->xconfigure.window,
               event_send->xconfigure.x,
               event_send->xconfigure.y,
               event_send->xconfigure.width,
               event_send->xconfigure.height);
      ret = x11_cmd(display, cmd, line, sizeof(line));
      if (ret < 0)
        return 0;
      if (strncmp(line, "OK configured", 13) == 0 ||
          strncmp(line, "OK configure", 12) == 0)
        return 1;
      return 0;
    }

    snprintf(cmd, sizeof(cmd), "QUEUE_CONFIGURE_NOTIFY %u %d %d %d %d\n",
             (uint)event_send->xconfigure.window,
             event_send->xconfigure.x,
             event_send->xconfigure.y,
             event_send->xconfigure.width,
             event_send->xconfigure.height);
    if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
      return 0;
    if (strncmp(line, "OK cfg_queued", 13) == 0 ||
        strncmp(line, "OK queued", 9) == 0)
      return 1;
    x11dbg("x11:sendevent cfg wid=%u unexpected-resp='%s'", (uint)event_send->xconfigure.window, line);
    return 1;
  }

  if (event_send->type == ClientMessage) {
    snprintf(cmd, sizeof(cmd), "QUEUE_CLIENT_MESSAGE %u %u %u\n",
             (uint)event_send->xclient.window,
             (uint)event_send->xclient.message_type,
             (uint)event_send->xclient.data.l[0]);
    if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
      return 0;
    return 1;
  }

  if (event_send->type == SelectionNotify) {
    const char *sel_name = atom_to_name(event_send->xselection.selection);
    const char *target_name = atom_to_name(event_send->xselection.target);
    const char *prop_name = (event_send->xselection.property == None)
                            ? "NONE"
                            : atom_to_name(event_send->xselection.property);
    snprintf(cmd, sizeof(cmd), "QUEUE_SELECTION_NOTIFY %u %s %s %s %u\n",
             (uint)event_send->xselection.requestor,
             sel_name, target_name, prop_name,
             (uint)event_send->xselection.time);
    if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
      return 0;
    return 1;
  }

  return 1;
}

int
XSetInputFocus(Display *display, Window focus, int revert_to, Time time)
{
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  (void)revert_to;
  (void)time;
  snprintf(cmd, sizeof(cmd), "SET_FOCUS %u\n", (uint)focus);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  return strncmp(line, "OK focused", 10) == 0 ? 0 : -1;
}

int XGetInputFocus(Display *display, Window *focus_return, int *revert_to_return) {
  char line[X6_BUF_SIZE];
  unsigned int wid = 0;
  if (!display)
    return -1;
  if (x11_cmd(display, "GET_FOCUS\n", line, sizeof(line)) < 0)
    return -1;
  if (sscanf(line, "OK focus %u", &wid) != 1)
    return -1;
  if (focus_return) *focus_return = (Window)wid;
  if (revert_to_return) *revert_to_return = RevertToNone;
  return 0;
}

int XGrabKeyboard(Display *display, Window grab_window, Bool owner_events, int pointer_mode, int keyboard_mode, Time time) {
  char line[X6_BUF_SIZE];
  (void)grab_window; (void)owner_events; (void)pointer_mode; (void)keyboard_mode; (void)time;
  if (x11_cmd(display, "GRAB_KEYBOARD\n", line, sizeof(line)) < 0)
    return BadAccess;
  return strncmp(line, "OK grab_active", 14) == 0 ? GrabSuccess : BadAccess;
}

int XUngrabKeyboard(Display *display, Time time) {
  char line[X6_BUF_SIZE];
  (void)time;
  if (x11_cmd(display, "UNGRAB_KEYBOARD\n", line, sizeof(line)) < 0)
    return -1;
  return strncmp(line, "OK ungrab_done", 14) == 0 ? 0 : -1;
}

int XGrabPointer(Display *display, Window grab_window, Bool owner_events, unsigned int event_mask, int pointer_mode, int keyboard_mode, Window confine_to, Cursor cursor, Time time) {
  char cmd[X6_BUF_SIZE];
  char line[X6_BUF_SIZE];
  (void)grab_window; (void)owner_events; (void)event_mask; (void)pointer_mode; (void)keyboard_mode; (void)confine_to; (void)cursor; (void)time;
  snprintf(cmd, sizeof(cmd), "GRAB_POINTER %u\n", (uint)grab_window);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return BadAccess;
  return strncmp(line, "OK pointer_grabbed", 18) == 0 ? GrabSuccess : BadAccess;
}
int XUngrabPointer(Display *display, Time time) {
  char line[X6_BUF_SIZE];
  (void)time;
  if (x11_cmd(display, "UNGRAB_POINTER\n", line, sizeof(line)) < 0)
    return -1;
  return strncmp(line, "OK pointer_ungrabbed", 20) == 0 ? 0 : -1;
}
int XWarpPointer(Display *display, Window src_w, Window dest_w, int src_x, int src_y, unsigned int src_width, unsigned int src_height, int dest_x, int dest_y) {
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  (void)src_w; (void)dest_w; (void)src_x; (void)src_y; (void)src_width; (void)src_height;
  snprintf(cmd, sizeof(cmd), "WARP_POINTER %d %d\n", dest_x, dest_y);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  return strncmp(line, "OK pointer_warped", 16) == 0 ? 0 : -1;
}

int XGrabKey(Display *display, int keycode, unsigned int modifiers, Window grab_window, Bool owner_events, int pointer_mode, int keyboard_mode) {
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  int x6_mods;
  (void)owner_events; (void)pointer_mode; (void)keyboard_mode;
  if (!display)
    return -1;
  x6_mods = x11_encode_modifiers(modifiers);
  x11dbg("x11:grab-key req keycode=%d mods=0x%x x6mods=0x%x wid=%u",
         keycode, modifiers, x6_mods, (uint)grab_window);
  snprintf(cmd, sizeof(cmd), "GRAB_KEY %u %u %u\n", (uint)keycode, (uint)x6_mods, (uint)grab_window);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  x11dbg("x11:grab-key resp '%s'", line);
  return strncmp(line, "OK key_grabbed", 14) == 0 ? 0 : -1;
}
int XUngrabKey(Display *display, int keycode, unsigned int modifiers, Window grab_window) {
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  int x6_mods;
  if (!display)
    return -1;
  x6_mods = x11_encode_modifiers(modifiers);
  snprintf(cmd, sizeof(cmd), "UNGRAB_KEY %u %u %u\n", (uint)keycode, (uint)x6_mods, (uint)grab_window);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  return strncmp(line, "OK key_ungrabbed", 16) == 0 ? 0 : -1;
}
int XGrabButton(Display *display, unsigned int button, unsigned int modifiers, Window grab_window, Bool owner_events, unsigned int event_mask, int pointer_mode, int keyboard_mode, Window confine_to, Cursor cursor) {
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  int x6_mods;
  (void)owner_events;
  (void)event_mask;
  (void)pointer_mode;
  (void)keyboard_mode;
  (void)confine_to;
  (void)cursor;
  if (!display)
    return -1;
  x6_mods = x11_encode_modifiers(modifiers);
  snprintf(cmd, sizeof(cmd), "GRAB_BUTTON %u %u %u\n", (uint)button, (uint)x6_mods, (uint)grab_window);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  return strncmp(line, "OK button_grabbed", 17) == 0 ? 0 : -1;
}
int XUngrabButton(Display *display, unsigned int button, unsigned int modifiers, Window grab_window) {
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  int x6_mods;
  if (!display)
    return -1;
  x6_mods = x11_encode_modifiers(modifiers);
  snprintf(cmd, sizeof(cmd), "UNGRAB_BUTTON %u %u %u\n", (uint)button, (uint)x6_mods, (uint)grab_window);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  return strncmp(line, "OK button_ungrabbed", 19) == 0 ? 0 : -1;
}
int XAllowEvents(Display *display, int event_mode, Time time) {
  char line[X6_BUF_SIZE];
  (void)time;
  if (!display)
    return -1;
  if (event_mode == ReplayPointer)
    x11_cmd(display, "UNGRAB_POINTER\n", line, sizeof(line));
  return 0;
}

int XSetWindowBorder(Display *display, Window w, unsigned long border_pixel) {
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  if (!display)
    return -1;
  snprintf(cmd, sizeof(cmd), "SET_BORDER_COLOR %u %u\n", (uint)w, (uint)(border_pixel & 0x00ffffffUL));
  return x11_cmd(display, cmd, line, sizeof(line)) < 0 ? -1 : 0;
}
int XChangeWindowAttributes(Display *display, Window w, unsigned long valuemask, XSetWindowAttributes *attributes) {
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];

  if (!display)
    return -1;

  if ((valuemask & CWEventMask) && attributes) {
    snprintf(cmd, sizeof(cmd), "SELECT_EVENTS %u %ld\n", (uint)w, attributes->event_mask);
    if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
      return -1;
  }

  if ((valuemask & CWOverrideRedirect) && attributes) {
    snprintf(cmd, sizeof(cmd), "SET_OVERRIDE_REDIRECT %u %d\n", (uint)w,
             attributes->override_redirect ? 1 : 0);
    if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
      return -1;
  }

  if ((valuemask & CWCursor) && attributes) {
    if (attributes->cursor == None) {
      snprintf(cmd, sizeof(cmd), "UNSET_CURSOR %u\n", (uint)w);
      if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
        return -1;
    } else {
      snprintf(cmd, sizeof(cmd), "SET_CURSOR %u %u\n", (uint)w, (uint)attributes->cursor);
      if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
        return -1;
    }
  }

  return 0;
}
int XDefineCursor(Display *display, Window w, Cursor cursor) {
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  if (!display)
    return -1;
  snprintf(cmd, sizeof(cmd), "SET_CURSOR %u %u\n", (uint)w, (uint)cursor);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  return strncmp(line, "OK cursor_set", 13) == 0 ? 0 : -1;
}
int XUndefineCursor(Display *display, Window w) {
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  if (!display)
    return -1;
  snprintf(cmd, sizeof(cmd), "UNSET_CURSOR %u\n", (uint)w);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  return strncmp(line, "OK cursor_unset", 15) == 0 ? 0 : -1;
}
Cursor XCreateFontCursor(Display *display, unsigned int shape) { (void)display; return (Cursor)(shape + 1); }
Cursor XCreatePixmapCursor(Display *display, Pixmap source, Pixmap mask,
                           XColor *foreground_color, XColor *background_color,
                           unsigned int x, unsigned int y) {
  unsigned long id;

  (void)display;
  (void)source;
  (void)mask;
  (void)foreground_color;
  (void)background_color;
  (void)x;
  (void)y;

  id = g_next_cursor_id++;
  if (id == 0)
    id = g_next_cursor_id++;
  return (Cursor)id;
}
int XFreeCursor(Display *display, Cursor cursor) { (void)display; (void)cursor; return 0; }

Pixmap XCreatePixmap(Display *display, Drawable d, unsigned int width, unsigned int height, unsigned int depth) {
  struct x11_pixmap_state *pm;
  char cmd[256];
  char response[128];
  unsigned int pmid;
  
  if (!display || width == 0 || height == 0)
    return 0;
  
  /* Allocate client-side pixmap tracking struct first */
  pm = x11_alloc_pixmap(width, height, depth);
  if (!pm)
    return 0;
  
  /* Send CREATE_PIXMAP command to x6 server */
  snprintf(cmd, sizeof(cmd), "CREATE_PIXMAP %u %u %u %u\n", depth, width, height, depth);
  if (x11_cmd(display, cmd, response, sizeof(response)) < 0) {
    if (pm->pixels)
      free(pm->pixels);
    pm->pixels = 0;
    pm->in_use = 0;
    x11dbg("x11:create-pixmap failed cmd transport w=%u h=%u d=%u", width, height, depth);
    return 0;
  }

  if (sscanf(response, "OK create_pixmap pmid=%u", &pmid) != 1 || pmid == 0) {
    if (pm->pixels)
      free(pm->pixels);
    pm->pixels = 0;
    pm->in_use = 0;
    x11dbg("x11:create-pixmap bad response '%s'", response);
    return 0;
  }

  pm->id = (Pixmap)pmid;
  x11consolecrit("x11:create-pixmap drawable=%u server_id=%u size=%ux%u depth=%u",
                 (uint)d, pmid, width, height, depth);
  x11dbg("x11:create-pixmap ok local->server id=%u size=%ux%u depth=%u", pmid, width, height, depth);
  
  return pm->id;
}

int XFreePixmap(Display *display, Pixmap pixmap) {
  struct x11_pixmap_state *pm;
  char cmd[256];
  char response[128];
  
  pm = x11_find_pixmap(pixmap);
  if (!pm)
    return 0;

  x11consolecrit("x11:free-pixmap id=%u size=%ux%u", (uint)pixmap,
                 pm->width, pm->height);
  
  /* Send DESTROY_PIXMAP command to x6 server */
  snprintf(cmd, sizeof(cmd), "DESTROY_PIXMAP %u\n", pixmap);
  
  if (display) {
    x11_cmd(display, cmd, response, sizeof(response));
  }
  
  /* Mark as unused in client tracking */
  if (pm->pixels)
    free(pm->pixels);
  pm->pixels = 0;
  pm->in_use = 0;
  
  return 0;
}
GC XCreateGC(Display *display, Drawable d, unsigned long valuemask, void *values) {
  struct x11_gc_state *gs;
  XGCValues *v;
  (void)display;
  (void)d;
  gs = x11_alloc_gc();
  if (!gs)
    return 0;

  v = (XGCValues *)values;
  if (v) {
    if (valuemask & GCForeground)
      gs->fg = v->foreground;
    if (valuemask & GCBackground)
      gs->bg = v->background;
    if (valuemask & GCFunction)
      gs->function = v->function;
    if (valuemask & GCFillStyle)
      gs->fill_style = v->fill_style;
    if (valuemask & GCTile)
      gs->tile = v->tile;
    if (valuemask & GCTileStipXOrigin)
      gs->ts_x_origin = v->ts_x_origin;
    if (valuemask & GCTileStipYOrigin)
      gs->ts_y_origin = v->ts_y_origin;
    if (valuemask & GCClipMask)
      gs->clip_mask = v->clip_mask;
    if (valuemask & GCClipXOrigin)
      gs->clip_x_origin = v->clip_x_origin;
    if (valuemask & GCClipYOrigin)
      gs->clip_y_origin = v->clip_y_origin;
    if (valuemask & GCDashOffset)
      gs->dash_offset = v->dash_offset;
    if (valuemask & GCDashList)
      gs->dashes = v->dashes;
  }

  return gs->id;
}
int XFreeGC(Display *display, GC gc) {
  struct x11_gc_state *gs;
  (void)display;
  gs = x11_find_gc(gc);
  if (gs)
    gs->in_use = 0;
  return 0;
}
int XSetForeground(Display *display, GC gc, unsigned long foreground) {
  struct x11_gc_state *gs;
  (void)display;
  gs = x11_find_gc(gc);
  if (gs)
    gs->fg = foreground;
  return 0;
}
int XSetBackground(Display *display, GC gc, unsigned long background) {
  struct x11_gc_state *gs;
  (void)display;
  gs = x11_find_gc(gc);
  if (gs)
    gs->bg = background;
  return 0;
}

int XSetFillStyle(Display *display, GC gc, int fill_style) {
  struct x11_gc_state *gs;
  (void)display;
  gs = x11_find_gc(gc);
  if (gs)
    gs->fill_style = fill_style;
  return 0;
}

int XSetFunction(Display *display, GC gc, int function) {
  struct x11_gc_state *gs;
  (void)display;
  gs = x11_find_gc(gc);
  if (gs)
    gs->function = function;
  return 0;
}

int XSetTile(Display *display, GC gc, Pixmap tile) {
  struct x11_gc_state *gs;
  (void)display;
  gs = x11_find_gc(gc);
  if (gs)
    gs->tile = tile;
  return 0;
}

int XSetTSOrigin(Display *display, GC gc, int ts_x_origin, int ts_y_origin) {
  struct x11_gc_state *gs;
  (void)display;
  gs = x11_find_gc(gc);
  if (gs) {
    gs->ts_x_origin = ts_x_origin;
    gs->ts_y_origin = ts_y_origin;
  }
  return 0;
}

int XSetClipMask(Display *display, GC gc, Pixmap pixmap) {
  struct x11_gc_state *gs;
  (void)display;
  gs = x11_find_gc(gc);
  if (gs)
    gs->clip_mask = pixmap;
  return 0;
}

int XSetClipRectangles(Display *display, GC gc, int clip_x_origin, int clip_y_origin,
                       XRectangle *rectangles, int n, int ordering) {
  struct x11_gc_state *gs;
  (void)display;
  (void)rectangles;
  (void)n;
  (void)ordering;
  gs = x11_find_gc(gc);
  if (gs) {
    gs->clip_x_origin = clip_x_origin;
    gs->clip_y_origin = clip_y_origin;
  }
  return 0;
}

int XSetDashes(Display *display, GC gc, int dash_offset, const char *dash_list, int n) {
  struct x11_gc_state *gs;
  (void)display;
  gs = x11_find_gc(gc);
  if (!gs)
    return 0;
  gs->dash_offset = dash_offset;
  gs->dashes = (dash_list && n > 0) ? dash_list[0] : 0;
  return 0;
}

int XChangeGC(Display *display, GC gc, unsigned long valuemask, XGCValues *values) {
  struct x11_gc_state *gs;

  (void)display;
  gs = x11_find_gc(gc);
  if (!gs || !values)
    return 0;

  if (valuemask & GCForeground)
    gs->fg = values->foreground;
  if (valuemask & GCBackground)
    gs->bg = values->background;
  if (valuemask & GCFunction)
    gs->function = values->function;
  if (valuemask & GCFillStyle)
    gs->fill_style = values->fill_style;
  if (valuemask & GCTile)
    gs->tile = values->tile;
  if (valuemask & GCTileStipXOrigin)
    gs->ts_x_origin = values->ts_x_origin;
  if (valuemask & GCTileStipYOrigin)
    gs->ts_y_origin = values->ts_y_origin;
  if (valuemask & GCClipMask)
    gs->clip_mask = values->clip_mask;
  if (valuemask & GCClipXOrigin)
    gs->clip_x_origin = values->clip_x_origin;
  if (valuemask & GCClipYOrigin)
    gs->clip_y_origin = values->clip_y_origin;
  if (valuemask & GCDashOffset)
    gs->dash_offset = values->dash_offset;
  if (valuemask & GCDashList)
    gs->dashes = values->dashes;
  return 0;
}

int XSetLineAttributes(Display *display, GC gc, unsigned int line_width, int line_style, int cap_style, int join_style) {
  (void)display; (void)gc; (void)line_width; (void)line_style; (void)cap_style; (void)join_style; return 0;
}

Status XGetGCValues(Display *display, GC gc, unsigned long valuemask,
                    XGCValues *values_return) {
  struct x11_gc_state *gs;

  (void)display;
  if (!values_return)
    return 0;

  gs = x11_find_gc(gc);
  if (!gs)
    return 0;

  if (valuemask & GCForeground)
    values_return->foreground = gs->fg;
  if (valuemask & GCBackground)
    values_return->background = gs->bg;
  if (valuemask & GCFunction)
    values_return->function = gs->function;
  if (valuemask & GCFillStyle)
    values_return->fill_style = gs->fill_style;
  if (valuemask & GCTile)
    values_return->tile = gs->tile;
  if (valuemask & GCTileStipXOrigin)
    values_return->ts_x_origin = gs->ts_x_origin;
  if (valuemask & GCTileStipYOrigin)
    values_return->ts_y_origin = gs->ts_y_origin;
  if (valuemask & GCClipMask)
    values_return->clip_mask = gs->clip_mask;
  if (valuemask & GCClipXOrigin)
    values_return->clip_x_origin = gs->clip_x_origin;
  if (valuemask & GCClipYOrigin)
    values_return->clip_y_origin = gs->clip_y_origin;
  if (valuemask & GCDashOffset)
    values_return->dash_offset = gs->dash_offset;
  if (valuemask & GCDashList)
    values_return->dashes = gs->dashes;
  return 1;
}

static int
x11_image_bytes_per_line(unsigned int width, int bits_per_pixel, int bitmap_pad)
{
  int bits;
  int pad;
  int line_bits;

  if (bits_per_pixel <= 0)
    return 0;
  bits = (int)width * bits_per_pixel;
  pad = bitmap_pad > 0 ? bitmap_pad : 32;
  line_bits = ((bits + pad - 1) / pad) * pad;
  return line_bits / 8;
}

XImage *XCreateImage(Display *display, Visual *visual, unsigned int depth,
                     int format, int offset, char *data,
                     unsigned int width, unsigned int height,
                     int bitmap_pad, int bytes_per_line) {
  XImage *img;
  size_t sz;

  (void)display;
  (void)visual;

  if (width == 0 || height == 0)
    return 0;

  img = (XImage *)malloc(sizeof(*img));
  if (!img)
    return 0;
  memset(img, 0, sizeof(*img));

  img->width = (int)width;
  img->height = (int)height;
  img->xoffset = offset;
  img->format = format;
  img->depth = (int)depth;
  img->bitmap_pad = bitmap_pad > 0 ? bitmap_pad : 32;
  img->bitmap_unit = 8;
  img->byte_order = LSBFirst;
  img->bitmap_bit_order = LSBFirst;
  img->bits_per_pixel = (depth <= 1) ? 1 : ((depth <= 16) ? 16 : 32);
  img->bytes_per_line = bytes_per_line > 0 ? bytes_per_line :
                        x11_image_bytes_per_line(width, img->bits_per_pixel, img->bitmap_pad);
  img->red_mask = 0x00ff0000UL;
  img->green_mask = 0x0000ff00UL;
  img->blue_mask = 0x000000ffUL;

  if (img->bytes_per_line <= 0) {
    free(img);
    return 0;
  }

  if (data) {
    img->data = data;
    img->obdata = 0;
    return img;
  }

  sz = (size_t)img->bytes_per_line * (size_t)height;
  img->data = (char *)malloc(sz);
  if (!img->data) {
    free(img);
    return 0;
  }
  memset(img->data, 0, sz);
  img->obdata = (XPointer)1;
  return img;
}

int XInitImage(XImage *image) {
  if (!image || !image->data)
    return 0;
  return 1;
}

int XDestroyImage(XImage *ximage) {
  if (!ximage)
    return 0;
  x11_clear_shm_image(ximage);
  if (ximage->data && ximage->obdata)
    free(ximage->data);
  free(ximage);
  return 1;
}

unsigned long XGetPixel(XImage *ximage, int x, int y) {
  unsigned char *p;

  if (!ximage || !ximage->data)
    return 0;
  if (x < 0 || y < 0 || x >= ximage->width || y >= ximage->height)
    return 0;

  p = (unsigned char *)ximage->data + (size_t)y * (size_t)ximage->bytes_per_line;
  if (ximage->bits_per_pixel == 1) {
    int byte_idx = x >> 3;
    int bit_idx = x & 7;
    return (p[byte_idx] >> bit_idx) & 1U;
  }
  if (ximage->bits_per_pixel <= 8)
    return p[x];
  if (ximage->bits_per_pixel <= 16) {
    p += (size_t)x * 2U;
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8);
  }
  if (ximage->bits_per_pixel <= 24) {
    p += (size_t)x * 3U;
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16);
  }
  p += (size_t)x * 4U;
  return (unsigned long)p[0] |
         ((unsigned long)p[1] << 8) |
         ((unsigned long)p[2] << 16) |
         ((unsigned long)p[3] << 24);
}

int XPutPixel(XImage *ximage, int x, int y, unsigned long pixel) {
  unsigned char *p;

  if (!ximage || !ximage->data)
    return 0;
  if (x < 0 || y < 0 || x >= ximage->width || y >= ximage->height)
    return 0;

  p = (unsigned char *)ximage->data + (size_t)y * (size_t)ximage->bytes_per_line;
  if (ximage->bits_per_pixel == 1) {
    int byte_idx = x >> 3;
    int bit_idx = x & 7;
    unsigned char mask = (unsigned char)(1U << bit_idx);
    if (pixel & 1U)
      p[byte_idx] |= mask;
    else
      p[byte_idx] &= (unsigned char)~mask;
    return 1;
  }
  if (ximage->bits_per_pixel <= 8) {
    p[x] = (unsigned char)(pixel & 0xffU);
    return 1;
  }
  if (ximage->bits_per_pixel <= 16) {
    p += (size_t)x * 2U;
    p[0] = (unsigned char)(pixel & 0xffU);
    p[1] = (unsigned char)((pixel >> 8) & 0xffU);
    return 1;
  }
  if (ximage->bits_per_pixel <= 24) {
    p += (size_t)x * 3U;
    p[0] = (unsigned char)(pixel & 0xffU);
    p[1] = (unsigned char)((pixel >> 8) & 0xffU);
    p[2] = (unsigned char)((pixel >> 16) & 0xffU);
    return 1;
  }
  p += (size_t)x * 4U;
  p[0] = (unsigned char)(pixel & 0xffU);
  p[1] = (unsigned char)((pixel >> 8) & 0xffU);
  p[2] = (unsigned char)((pixel >> 16) & 0xffU);
  p[3] = (unsigned char)((pixel >> 24) & 0xffU);
  return 1;
}

XImage *XSubImage(XImage *ximage, int x, int y, unsigned int subimage_width,
                  unsigned int subimage_height) {
  XImage *sub;
  unsigned int i;
  unsigned int j;

  if (!ximage)
    return 0;
  if (x < 0 || y < 0)
    return 0;
  if ((unsigned int)x >= (unsigned int)ximage->width || (unsigned int)y >= (unsigned int)ximage->height)
    return 0;

  if ((unsigned int)x + subimage_width > (unsigned int)ximage->width)
    subimage_width = (unsigned int)ximage->width - (unsigned int)x;
  if ((unsigned int)y + subimage_height > (unsigned int)ximage->height)
    subimage_height = (unsigned int)ximage->height - (unsigned int)y;
  if (subimage_width == 0 || subimage_height == 0)
    return 0;

  sub = XCreateImage(0, 0, (unsigned int)ximage->depth, ximage->format,
                     0, 0, subimage_width, subimage_height,
                     ximage->bitmap_pad, 0);
  if (!sub)
    return 0;

  for (j = 0; j < subimage_height; j++) {
    for (i = 0; i < subimage_width; i++) {
      unsigned long px = XGetPixel(ximage, x + (int)i, y + (int)j);
      XPutPixel(sub, (int)i, (int)j, px);
    }
  }
  return sub;
}

XImage *XGetImage(Display *display, Drawable d, int x, int y,
                  unsigned int width, unsigned int height,
                  unsigned long plane_mask, int format) {
  XImage *img;
  struct x11_pixmap_state *pm;
  int i;
  int j;

  (void)plane_mask;
  if (!display || width == 0 || height == 0)
    return 0;

  img = XCreateImage(display, 0, (unsigned int)display->depth, format,
                     0, 0, width, height, 32, 0);
  if (!img)
    return 0;

  pm = x11_find_pixmap((Pixmap)d);
  if (!pm || !pm->pixels)
    return img;

  for (j = 0; j < (int)height; j++) {
    for (i = 0; i < (int)width; i++) {
      int sx = x + i;
      int sy = y + j;
      unsigned long px = 0;
      if (sx >= 0 && sy >= 0 && (unsigned int)sx < pm->width && (unsigned int)sy < pm->height)
        px = pm->pixels[(size_t)sy * (size_t)pm->width + (size_t)sx];
      XPutPixel(img, i, j, px);
    }
  }
  return img;
}

int XPutImage(Display *display, Drawable d, GC gc, XImage *image,
              int src_x, int src_y, int dest_x, int dest_y,
              unsigned int width, unsigned int height) {
  struct x11_pixmap_state *pm;
  int i;
  int j;
  (void)gc;

  if (!display || !image || width == 0 || height == 0)
    return 0;

  pm = x11_find_pixmap((Pixmap)d);
  if (pm && pm->pixels) {
    for (j = 0; j < (int)height; j++) {
      for (i = 0; i < (int)width; i++) {
        int sx = src_x + i;
        int sy = src_y + j;
        int dx = dest_x + i;
        int dy = dest_y + j;
        unsigned long px;
        if (sx < 0 || sy < 0 || sx >= image->width || sy >= image->height)
          continue;
        if (dx < 0 || dy < 0 || (unsigned int)dx >= pm->width || (unsigned int)dy >= pm->height)
          continue;
        px = XGetPixel(image, sx, sy);
        pm->pixels[(size_t)dy * (size_t)pm->width + (size_t)dx] = (unsigned int)px;
      }
    }
    x11_notify_drawable_damage(display, d);
    return 0;
  }

  /* Window/root drawables: best-effort path not wired yet in x6 protocol. */
  return 0;
}

int XParseColor(Display *display, Colormap colormap, const char *spec, XColor *exact_def_return) {
  int r;
  int g;
  int b;
  int v;
  unsigned short rs;
  unsigned short gs;
  unsigned short bs;
  
  (void)display;
  (void)colormap;
  
  if (!spec || !exact_def_return)
    return 0;
  
  if (spec[0] == '#') {
    if (strlen(spec) == 7 && sscanf(spec, "#%02x%02x%02x", &r, &g, &b) == 3) {
      x11_set_xcolor(exact_def_return, (unsigned short)((r << 8) | r),
                     (unsigned short)((g << 8) | g),
                     (unsigned short)((b << 8) | b));
      return 1;
    }
    if (strlen(spec) == 4 && sscanf(spec, "#%1x%1x%1x", &r, &g, &b) == 3) {
      x11_set_xcolor(exact_def_return,
                     (unsigned short)(r * 0x1111),
                     (unsigned short)(g * 0x1111),
                     (unsigned short)(b * 0x1111));
      return 1;
    }
    if (strlen(spec) == 13 && sscanf(spec, "#%4x%4x%4x", &r, &g, &b) == 3) {
      x11_set_xcolor(exact_def_return, (unsigned short)r,
                     (unsigned short)g, (unsigned short)b);
      return 1;
    }
  }

  if (x11_parse_named_color(spec, exact_def_return))
    return 1;

  if ((sscanf(spec, "%d %d %d", &r, &g, &b) == 3) ||
      (sscanf(spec, "%d,%d,%d", &r, &g, &b) == 3)) {
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    x11_set_xcolor(exact_def_return, (unsigned short)((r << 8) | r),
                   (unsigned short)((g << 8) | g),
                   (unsigned short)((b << 8) | b));
    return 1;
  }

  v = atoi(spec);
  if (v >= 0 && v <= 100 && strchr(spec, ' ') == 0 && strchr(spec, ',') == 0) {
    rs = gs = bs = (unsigned short)((v * 65535) / 100);
    x11_set_xcolor(exact_def_return, rs, gs, bs);
    return 1;
  }
  
  return 0;
}

int XAllocColor(Display *display, Colormap colormap, XColor *screen_in_out) {
  struct x11_color_entry *ce;
  unsigned long pixel;
  
  (void)display;
  (void)colormap;
  
  if (!screen_in_out)
    return 0;
  
  /* Allocate and store the color */
  ce = x11_alloc_color(screen_in_out->red, screen_in_out->green, screen_in_out->blue);
  if (!ce)
    return 0;
  
  /* Return the allocated pixel value */
  pixel = ce->pixel;
  screen_in_out->pixel = pixel & 0xffffff; /* 24-bit RGB */
  screen_in_out->flags = DoRed | DoGreen | DoBlue;
  
  return 1;
}

Colormap XCreateColormap(Display *display, Window w, Visual *visual, int alloc) {
  (void)display;
  (void)w;
  (void)visual;
  (void)alloc;
  
  /* Return dummy colormap ID (we don't track colormaps) */
  return 1;
}

int XFreeColormap(Display *display, Colormap colormap) {
  (void)display;
  (void)colormap;
  return 0;
}

Font XLoadFont(Display *display, const char *name) {
  struct x11_font_state *fs;
  (void)display;
  
  if (!name)
    return 0;
  
  fs = x11_alloc_font(name);
  if (!fs)
    return 0;
  
  return fs->id;
}

XFontStruct *XLoadQueryFont(Display *display, const char *name) {
  XFontStruct *fs_out;
  struct x11_font_state *fs;
  
  (void)display;
  
  if (!name)
    return 0;
  
  fs = x11_alloc_font(name);
  if (!fs)
    return 0;
  
  /* Allocate XFontStruct and populate it */
  fs_out = (XFontStruct *)malloc(sizeof(*fs_out));
  if (!fs_out) {
    fs->in_use = 0;
    return 0;
  }
  x11_fill_font_struct(fs_out, fs);
  
  return fs_out;
}

XFontStruct *XQueryFont(Display *display, XID fid) {
  XFontStruct *fs_out;
  struct x11_font_state *fs;
  
  (void)display;
  
  fs = x11_find_font((Font)fid);
  if (!fs)
    return 0;
  
  fs_out = (XFontStruct *)malloc(sizeof(*fs_out));
  if (!fs_out)
    return 0;
  x11_fill_font_struct(fs_out, fs);
  
  return fs_out;
}

int XFreeFont(Display *display, XFontStruct *font_struct) {
  struct x11_font_state *fs;

  (void)display;
  if (!font_struct)
    return 0;

  fs = x11_find_font(font_struct->fid);
  if (fs)
    fs->in_use = 0;
  free(font_struct);
  return 0;
}

XFontSet XCreateFontSet(Display *display, const char *base_font_name_list,
                        char ***missing_charset_list_return,
                        int *missing_charset_count_return,
                        char **def_string_return) {
  struct x11_font_state *fs;
  struct _XOC *oc;
  const char *picked;

  (void)display;

  if (missing_charset_list_return)
    *missing_charset_list_return = 0;
  if (missing_charset_count_return)
    *missing_charset_count_return = 0;
  if (def_string_return)
    *def_string_return = 0;

  picked = x11_pick_fontset_name(base_font_name_list);
  fs = x11_alloc_font(picked);
  if (!fs)
    return 0;

  oc = (struct _XOC *)malloc(sizeof(*oc));
  if (!oc) {
    fs->in_use = 0;
    return 0;
  }
  memset(oc, 0, sizeof(*oc));

  oc->font_id = fs->id;
  oc->font_name = strdup(picked);
  if (!oc->font_name) {
    fs->in_use = 0;
    free(oc);
    return 0;
  }

  x11_fill_font_struct(&oc->font, fs);
  oc->font_list_entry[0] = &oc->font;
  oc->font_name_list_entry[0] = oc->font_name;
  return (XFontSet)oc;
}

int XFontsOfFontSet(XFontSet font_set, XFontStruct ***font_struct_list_return,
                    char ***font_name_list_return) {
  struct _XOC *oc;

  oc = (struct _XOC *)font_set;
  if (!oc)
    return 0;

  if (font_struct_list_return)
    *font_struct_list_return = oc->font_list_entry;
  if (font_name_list_return)
    *font_name_list_return = oc->font_name_list_entry;
  return 1;
}

void XFreeFontSet(Display *display, XFontSet font_set) {
  struct _XOC *oc;
  struct x11_font_state *fs;

  (void)display;
  oc = (struct _XOC *)font_set;
  if (!oc)
    return;

  fs = x11_find_font(oc->font_id);
  if (fs)
    fs->in_use = 0;
  free(oc->font_name);
  free(oc);
}

int XUnloadFont(Display *display, Font font) {
  struct x11_font_state *fs;
  (void)display;
  
  fs = x11_find_font(font);
  if (fs)
    fs->in_use = 0;
  return 0;
}

int XFreeFontInfo(char **names, XFontStruct *info, int count) {
  int i;
  
  if (info) {
    for (i = 0; i < count; i++) {
      free(info[i].per_char);
    }
    free(info);
  }
  
  if (names) {
    for (i = 0; i < count; i++)
      free(names[i]);
    free(names);
  }
  
  return 0;
}

int XTextWidth(XFontStruct *font_struct, const char *string, int count) {
  struct x11_font_state *fs;
  
  if (!font_struct || !string || count <= 0)
    return 0;
  
  /* font_struct->fid is the Font ID we allocated */
  fs = x11_find_font(font_struct->fid);
  if (!fs)
    return 0;
  
  /* For monospace fonts, width = char_count * char_width */
  return count * fs->width;
}

int XSetFont(Display *display, GC gc, Font font) {
  struct x11_gc_state *gs;
  (void)display;
  (void)font;
  
  gs = x11_find_gc(gc);
  if (!gs)
    return 0;
  
  /* Accept font changes, though we don't track them in GC yet */
  return 0;
}

int XFillRectangle(Display *display, Drawable d, GC gc, int x, int y, unsigned int width, unsigned int height) {
  char cmd[X6_BUF_SIZE];
  struct x11_gc_state *gs;
  unsigned int color;
  if (!display)
    return -1;
  if (x11_sanitize_rect(display, &x, &y, &width, &height) < 0)
    return 0;
  gs = x11_find_gc(gc);
  color = (unsigned int)(gs ? (gs->fg & 0x00ffffffUL) : 0x00ffffffU);
  snprintf(cmd, sizeof(cmd), "DRAW_RECT %u %d %d %u %u %u\n", (uint)d, x, y, width, height, color);
  return x11_batch_draw(display, cmd);
}
int XDrawRectangle(Display *display, Drawable d, GC gc, int x, int y, unsigned int width, unsigned int height) {
  if (width == 0 || height == 0)
    return 0;
  XFillRectangle(display, d, gc, x, y, width, 1);
  if (height > 1)
    XFillRectangle(display, d, gc, x, y + (int)height - 1, width, 1);
  if (height > 2) {
    XFillRectangle(display, d, gc, x, y + 1, 1, height - 2);
    if (width > 1)
      XFillRectangle(display, d, gc, x + (int)width - 1, y + 1, 1, height - 2);
  }
  return 0;
}
int XDrawString(Display *display, Drawable d, GC gc, int x, int y, const char *string, int length) {
  char cmd[X6_BUF_SIZE];
  struct x11_gc_state *gs;
  unsigned int color;
  int n;
  int i;

  if (!display || !string || length <= 0)
    return 0;

  n = length;
  if (n > 700)
    n = 700;
  for (i = 0; i < n; i++) {
    if (string[i] == '\n' || string[i] == '\r')
      n = i;
  }

  gs = x11_find_gc(gc);
  color = (unsigned int)(gs ? (gs->fg & 0x00ffffffUL) : 0x00ffffffU);
  snprintf(cmd, sizeof(cmd), "DRAW_TEXT %u %d %d %u %d %.*s\n", (uint)d, x, y, color, n, n, string);
  g_x11_draw_call_count++;
  return x11_batch_draw(display, cmd);
}

int XDrawLine(Display *display, Drawable d, GC gc, int x1, int y1, int x2, int y2) {
  char cmd[X6_BUF_SIZE];
  struct x11_gc_state *gs;
  unsigned int color;

  if (!display)
    return -1;
  gs = x11_find_gc(gc);
  color = (unsigned int)(gs ? (gs->fg & 0x00ffffffUL) : 0x00ffffffU);
  snprintf(cmd, sizeof(cmd), "DRAW_LINE %u %d %d %d %d %u\n",
           (uint)d, x1, y1, x2, y2, color);
  return x11_batch_draw(display, cmd);
}

int XDrawLines(Display *display, Drawable d, GC gc, XPoint *points, int npoints, int mode) {
  int i;
  int cx;
  int cy;

  if (!points || npoints <= 0)
    return 0;
  cx = points[0].x;
  cy = points[0].y;
  for (i = 1; i < npoints; i++) {
    int nx = points[i].x;
    int ny = points[i].y;
    if (mode != 0) {
      nx += cx;
      ny += cy;
    }
    XDrawLine(display, d, gc, cx, cy, nx, ny);
    cx = nx;
    cy = ny;
  }
  return 0;
}

int XDrawSegments(Display *display, Drawable d, GC gc, void *segments, int nsegments) {
  struct x11_segment {
    short x1, y1, x2, y2;
  };
  struct x11_segment *s;
  int i;

  if (!segments || nsegments <= 0)
    return 0;
  s = (struct x11_segment *)segments;
  for (i = 0; i < nsegments; i++)
    XDrawLine(display, d, gc, s[i].x1, s[i].y1, s[i].x2, s[i].y2);
  return 0;
}

int XDrawPoint(Display *display, Drawable d, GC gc, int x, int y) {
  return XFillRectangle(display, d, gc, x, y, 1, 1);
}

static int
x11_norm_angle64(int a)
{
  int full = 360 * 64;
  a %= full;
  if (a < 0)
    a += full;
  return a;
}

/* Fast monotonic pseudo-angle in X11 units (1/64 degree) without trig.
 * Input vector uses X11 arc-space coordinates: +x right, +y up. */
static int
x11_vector_angle64(int vx, int vy)
{
  int ax;
  int ay;
  int t;

  if (vx == 0 && vy == 0)
    return 0;

  ax = vx >= 0 ? vx : -vx;
  ay = vy >= 0 ? vy : -vy;
  t = (ay << 10) / (ax + ay + 1);

  if (vx >= 0 && vy >= 0)
    return ((t * (90 * 64)) >> 10);
  if (vx < 0 && vy >= 0)
    return (90 * 64) + (((1024 - t) * (90 * 64)) >> 10);
  if (vx < 0 && vy < 0)
    return (180 * 64) + ((t * (90 * 64)) >> 10);
  return (270 * 64) + (((1024 - t) * (90 * 64)) >> 10);
}

static int
x11_angle_in_arc64(int angle, int start, int extent)
{
  int full = 360 * 64;
  int end;
  int s;
  int e;
  int a;

  if (extent == 0)
    return 0;
  if (extent >= full || extent <= -full)
    return 1;

  a = x11_norm_angle64(angle);
  s = x11_norm_angle64(start);
  end = start + extent;
  e = x11_norm_angle64(end);

  if (extent > 0) {
    if (s <= e)
      return a >= s && a <= e;
    return a >= s || a <= e;
  }

  if (e <= s)
    return a >= e && a <= s;
  return a >= e || a <= s;
}

int XDrawArc(Display *display, Drawable d, GC gc, int x, int y,
             unsigned int width, unsigned int height, int angle1, int angle2) {
  int rx;
  int ry;
  int cx;
  int cy;
  int px;
  int py;
  int start;
  int extent;
  long rx2;
  long ry2;
  long tworx2;
  long twory2;
  long dx;
  long dy;
  long p;

  (void)angle1;
  (void)angle2;
  if (width == 0 || height == 0)
    return 0;

  start = x11_norm_angle64(angle1);
  extent = angle2;
  if (extent == 0)
    return 0;

  rx = (int)width / 2;
  ry = (int)height / 2;
  if (rx <= 0 || ry <= 0)
    return XDrawRectangle(display, d, gc, x, y, width, height);

  cx = x + rx;
  cy = y + ry;
  px = 0;
  py = ry;

  rx2 = (long)rx * (long)rx;
  ry2 = (long)ry * (long)ry;
  tworx2 = 2L * rx2;
  twory2 = 2L * ry2;
  dx = 0;
  dy = tworx2 * (long)py;

  p = ry2 - (rx2 * (long)ry) + (rx2 / 4L);
  while (dx < dy) {
    if (x11_angle_in_arc64(x11_vector_angle64(px, -py), start, extent))
      XDrawPoint(display, d, gc, cx + px, cy + py);
    if (x11_angle_in_arc64(x11_vector_angle64(-px, -py), start, extent))
      XDrawPoint(display, d, gc, cx - px, cy + py);
    if (x11_angle_in_arc64(x11_vector_angle64(px, py), start, extent))
      XDrawPoint(display, d, gc, cx + px, cy - py);
    if (x11_angle_in_arc64(x11_vector_angle64(-px, py), start, extent))
      XDrawPoint(display, d, gc, cx - px, cy - py);

    px++;
    dx += twory2;
    if (p < 0) {
      p += ry2 + dx;
    } else {
      py--;
      dy -= tworx2;
      p += ry2 + dx - dy;
    }
  }

  p = ry2 * (long)(px + 1) * (long)(px + 1)
      + rx2 * (long)(py - 1) * (long)(py - 1)
      - rx2 * ry2;
  while (py >= 0) {
    if (x11_angle_in_arc64(x11_vector_angle64(px, -py), start, extent))
      XDrawPoint(display, d, gc, cx + px, cy + py);
    if (x11_angle_in_arc64(x11_vector_angle64(-px, -py), start, extent))
      XDrawPoint(display, d, gc, cx - px, cy + py);
    if (x11_angle_in_arc64(x11_vector_angle64(px, py), start, extent))
      XDrawPoint(display, d, gc, cx + px, cy - py);
    if (x11_angle_in_arc64(x11_vector_angle64(-px, py), start, extent))
      XDrawPoint(display, d, gc, cx - px, cy - py);

    py--;
    dy -= tworx2;
    if (p > 0) {
      p += rx2 - dy;
    } else {
      px++;
      dx += twory2;
      p += rx2 - dy + dx;
    }
  }
  return 0;
}

int XDrawRectangles(Display *display, Drawable d, GC gc, XRectangle *rectangles, int nrectangles) {
  int i;

  if (!rectangles || nrectangles <= 0)
    return 0;
  for (i = 0; i < nrectangles; i++) {
    XDrawRectangle(display, d, gc,
                   rectangles[i].x, rectangles[i].y,
                   rectangles[i].width, rectangles[i].height);
  }
  return 0;
}

int XFillArc(Display *display, Drawable d, GC gc, int x, int y,
             unsigned int width, unsigned int height, int angle1, int angle2) {
  int rx;
  int ry;
  int cx;
  int cy;
  int px;
  int py;
  int start;
  int extent;
  int xi;
  int run_start;
  long rx2;
  long ry2;
  long tworx2;
  long twory2;
  long dx;
  long dy;
  long p;

  (void)angle1;
  (void)angle2;
  if (width == 0 || height == 0)
    return 0;

  start = x11_norm_angle64(angle1);
  extent = angle2;
  if (extent == 0)
    return 0;

  rx = (int)width / 2;
  ry = (int)height / 2;
  if (rx <= 0 || ry <= 0)
    return XFillRectangle(display, d, gc, x, y, width, height);

  cx = x + rx;
  cy = y + ry;
  px = 0;
  py = ry;

  rx2 = (long)rx * (long)rx;
  ry2 = (long)ry * (long)ry;
  tworx2 = 2L * rx2;
  twory2 = 2L * ry2;
  dx = 0;
  dy = tworx2 * (long)py;

  p = ry2 - (rx2 * (long)ry) + (rx2 / 4L);
  while (dx < dy) {
    run_start = 999999;
    for (xi = -px; xi <= px; xi++) {
      int inside = x11_angle_in_arc64(x11_vector_angle64(xi, -py), start, extent);
      if (inside && run_start == 999999)
        run_start = xi;
      if ((!inside || xi == px) && run_start != 999999) {
        int xend = inside && xi == px ? xi : xi - 1;
        XDrawLine(display, d, gc, cx + run_start, cy + py, cx + xend, cy + py);
        run_start = 999999;
      }
    }
    if (py != 0) {
      run_start = 999999;
      for (xi = -px; xi <= px; xi++) {
        int inside = x11_angle_in_arc64(x11_vector_angle64(xi, py), start, extent);
        if (inside && run_start == 999999)
          run_start = xi;
        if ((!inside || xi == px) && run_start != 999999) {
          int xend = inside && xi == px ? xi : xi - 1;
          XDrawLine(display, d, gc, cx + run_start, cy - py, cx + xend, cy - py);
          run_start = 999999;
        }
      }
    }

    px++;
    dx += twory2;
    if (p < 0) {
      p += ry2 + dx;
    } else {
      py--;
      dy -= tworx2;
      p += ry2 + dx - dy;
    }
  }

  p = ry2 * (long)(px + 1) * (long)(px + 1)
      + rx2 * (long)(py - 1) * (long)(py - 1)
      - rx2 * ry2;
  while (py >= 0) {
    run_start = 999999;
    for (xi = -px; xi <= px; xi++) {
      int inside = x11_angle_in_arc64(x11_vector_angle64(xi, -py), start, extent);
      if (inside && run_start == 999999)
        run_start = xi;
      if ((!inside || xi == px) && run_start != 999999) {
        int xend = inside && xi == px ? xi : xi - 1;
        XDrawLine(display, d, gc, cx + run_start, cy + py, cx + xend, cy + py);
        run_start = 999999;
      }
    }
    if (py != 0) {
      run_start = 999999;
      for (xi = -px; xi <= px; xi++) {
        int inside = x11_angle_in_arc64(x11_vector_angle64(xi, py), start, extent);
        if (inside && run_start == 999999)
          run_start = xi;
        if ((!inside || xi == px) && run_start != 999999) {
          int xend = inside && xi == px ? xi : xi - 1;
          XDrawLine(display, d, gc, cx + run_start, cy - py, cx + xend, cy - py);
          run_start = 999999;
        }
      }
    }

    py--;
    dy -= tworx2;
    if (p > 0) {
      p += rx2 - dy;
    } else {
      px++;
      dx += twory2;
      p += rx2 - dy + dx;
    }
  }
  return 0;
}

int XFillArcs(Display *display, Drawable d, GC gc, void *arcs, int narcs) {
  struct x11_arc {
    short x, y;
    unsigned short width, height;
    short angle1, angle2;
  };
  struct x11_arc *a;
  int i;

  if (!arcs || narcs <= 0)
    return 0;
  a = (struct x11_arc *)arcs;
  for (i = 0; i < narcs; i++)
    XFillArc(display, d, gc, a[i].x, a[i].y, a[i].width, a[i].height, a[i].angle1, a[i].angle2);
  return 0;
}

int XFillPolygon(Display *display, Drawable d, GC gc, XPoint *points, int npoints, int shape, int mode) {
  int i;
  int miny;
  int maxy;
  int cx;
  int cy;
  int *px;
  int *py;
  int *ix;
  int ninter;
  int y;

  (void)shape;
  if (!points || npoints <= 0)
    return 0;

  px = (int *)malloc((size_t)npoints * sizeof(int));
  py = (int *)malloc((size_t)npoints * sizeof(int));
  ix = (int *)malloc((size_t)npoints * sizeof(int));
  if (!px || !py || !ix) {
    if (px) free(px);
    if (py) free(py);
    if (ix) free(ix);
    return 0;
  }

  cx = points[0].x;
  cy = points[0].y;
  px[0] = cx;
  py[0] = cy;
  miny = maxy = cy;

  for (i = 1; i < npoints; i++) {
    int vx = points[i].x;
    int vy = points[i].y;
    if (mode != 0) {
      vx += cx;
      vy += cy;
      cx = vx;
      cy = vy;
    }
    px[i] = vx;
    py[i] = vy;
    if (vy < miny) miny = vy;
    if (vy > maxy) maxy = vy;
  }

  for (y = miny; y <= maxy; y++) {
    ninter = 0;
    for (i = 0; i < npoints; i++) {
      int j = (i + 1) % npoints;
      int y1 = py[i];
      int y2 = py[j];
      int x1 = px[i];
      int x2 = px[j];

      if (y1 == y2)
        continue;
      if ((y < y1 && y < y2) || (y >= y1 && y >= y2))
        continue;

      ix[ninter++] = x1 + (int)(((long)(y - y1) * (long)(x2 - x1)) / (long)(y2 - y1));
    }

    if (ninter < 2)
      continue;

    for (i = 0; i < ninter - 1; i++) {
      int j;
      for (j = i + 1; j < ninter; j++) {
        if (ix[j] < ix[i]) {
          int t = ix[i];
          ix[i] = ix[j];
          ix[j] = t;
        }
      }
    }

    for (i = 0; i + 1 < ninter; i += 2) {
      int x1 = ix[i];
      int x2 = ix[i + 1];
      if (x2 < x1) {
        int t = x1;
        x1 = x2;
        x2 = t;
      }
      if (x2 >= x1)
        XFillRectangle(display, d, gc, x1, y, (unsigned int)(x2 - x1 + 1), 1);
    }
  }

  free(px);
  free(py);
  free(ix);
  return 0;
}

int XFillRectangles(Display *display, Drawable d, GC gc, XRectangle *rectangles, int nrectangles) {
  int i;

  if (!rectangles || nrectangles <= 0)
    return 0;
  for (i = 0; i < nrectangles; i++) {
    XFillRectangle(display, d, gc,
                   rectangles[i].x, rectangles[i].y,
                   rectangles[i].width, rectangles[i].height);
  }
  return 0;
}

int XClearArea(Display *display, Window w, int x, int y,
               unsigned int width, unsigned int height,
               Bool exposures) {
  char cmd[X6_BUF_SIZE];
  int rw;
  int rh;

  (void)exposures;

  if (!display)
    return -1;

  rw = 0;
  rh = 0;
  if ((width == 0 || height == 0) && x11_drawable_size(display, w, &rw, &rh) == 0) {
    if (width == 0)
      width = (rw > 0) ? (unsigned int)rw : 1;
    if (height == 0)
      height = (rh > 0) ? (unsigned int)rh : 1;
  }
  if (width == 0)
    width = 1;
  if (height == 0)
    height = 1;

  if (x11_sanitize_rect(display, &x, &y, &width, &height) < 0)
    return 0;

  snprintf(cmd, sizeof(cmd), "DRAW_RECT %u %d %d %u %u %u\n", (uint)w, x, y, width, height, 0x000000U);
  return x11_batch_draw(display, cmd);
}

int XClearWindow(Display *display, Window w) {
  return XClearArea(display, w, 0, 0, 0, 0, False);
}

int XCopyArea(Display *display, Drawable src, Drawable dest, GC gc, int src_x, int src_y, unsigned int width, unsigned int height, int dest_x, int dest_y) {
  struct x11_gc_state *gs;
  char cmd[256];
  unsigned long area;
  uint now;
  int trace_console;

  if (!display)
    return -1;

  gs = x11_find_gc(gc);
  if (!gs)
    return 0;

  area = (unsigned long)width * (unsigned long)height;
  now = (uint)uptime();

  /* Large presents under scroll storms can vary slightly frame-to-frame
   * (cursor and small dirty-region shifts). Rate-cap same src/dst traffic
   * first, then keep the stricter identical-geometry drop as a fallback. */
  if (area >= 300000UL &&
      src == g_copy_area_last_src &&
      dest == g_copy_area_last_dest &&
      (now - g_copy_area_last_tick) < 4U) {
    g_copy_area_drop_seq++;
    if ((g_copy_area_drop_seq & 127U) == 1U) {
      x11dbg("x11:copy-area ratecap drops=%u wh=%u,%u dt=%u",
             g_copy_area_drop_seq, width, height, (uint)(now - g_copy_area_last_tick));
    }
    return 0;
  }

  /* Drop redundant large presents that arrive too quickly with identical
   * geometry. This keeps text flood workloads from saturating x6. */
  if (area >= 300000UL &&
      src == g_copy_area_last_src &&
      dest == g_copy_area_last_dest &&
      src_x == g_copy_area_last_src_x &&
      src_y == g_copy_area_last_src_y &&
      width == g_copy_area_last_w &&
      height == g_copy_area_last_h &&
      dest_x == g_copy_area_last_dest_x &&
      dest_y == g_copy_area_last_dest_y &&
      (now - g_copy_area_last_tick) < 2U) {
    g_copy_area_drop_seq++;
    if ((g_copy_area_drop_seq & 127U) == 1U) {
      x11dbg("x11:copy-area throttle drops=%u wh=%u,%u dt=%u",
             g_copy_area_drop_seq, width, height, (uint)(now - g_copy_area_last_tick));
    }
    return 0;
  }

  g_copy_area_last_src = src;
  g_copy_area_last_dest = dest;
  g_copy_area_last_src_x = src_x;
  g_copy_area_last_src_y = src_y;
  g_copy_area_last_w = width;
  g_copy_area_last_h = height;
  g_copy_area_last_dest_x = dest_x;
  g_copy_area_last_dest_y = dest_y;
  g_copy_area_last_tick = now;

  trace_console = 0;
  if ((uint)dest != 2U || dest_y != 0 || height > 32U) {
    if (area >= 300000UL) {
      if ((g_copy_area_trace_seq++ & 127U) == 0U)
        trace_console = 1;
    } else {
      trace_console = 1;
    }
  }
  
  /* Send COPY_AREA command to x6 server
   * Format: COPY_AREA <src> <dest> <src_x> <src_y> <width> <height> <dest_x> <dest_y>
   */
  snprintf(cmd, sizeof(cmd), "COPY_AREA %lu %lu %d %d %u %u %d %d\n",
           src, dest, src_x, src_y, width, height, dest_x, dest_y);
  if (trace_console) {
    x11consolecrit("x11:copy-area src=%u dst=%u src_xy=%d,%d wh=%u,%u dst_xy=%d,%d",
                   (uint)src, (uint)dest, src_x, src_y, width, height, dest_x, dest_y);
  }
  if (area < 300000UL || (g_copy_area_trace_seq & 127U) == 1U) {
    x11dbg("x11:copy-area src=%lu dst=%lu src_xy=%d,%d wh=%u,%u dst_xy=%d,%d",
           src, dest, src_x, src_y, width, height, dest_x, dest_y);
  }

  if (!display)
    return -1;
  if (x11_batch_draw(display, cmd) < 0) {
    x11consolecrit("x11:copy-area resp src=%u dst=%u ret=-1 line='send-failed'",
                   (uint)src, (uint)dest);
    return -1;
  }
  return 0;
}

int XCopyPlane(Display *display, Drawable src, Drawable dest, GC gc,
               int src_x, int src_y, unsigned int width, unsigned int height,
               int dest_x, int dest_y, unsigned long plane) {
  (void)plane;
  return XCopyArea(display, src, dest, gc, src_x, src_y, width, height, dest_x, dest_y);
}

int XGetTransientForHint(Display *display, Window w, Window *prop_window_return) {
  unsigned char *prop = 0;
  unsigned long nitems = 0;
  Atom actual = 0;
  int fmt = 0;
  unsigned int pw = 0;

  if (prop_window_return)
    *prop_window_return = None;
  if (!display)
    return 0;
  if (XGetWindowProperty(display, w, XA_WM_TRANSIENT_FOR, 0, 64, False,
                         XA_STRING, &actual, &fmt, &nitems, 0, &prop) != Success)
    return 0;
  if (!prop)
    return 0;
  if (sscanf((char *)prop, "%u", &pw) == 1) {
    if (prop_window_return)
      *prop_window_return = (Window)pw;
    XFree(prop);
    return 1;
  }
  XFree(prop);
  return 0;
}
int XQueryTree(Display *display, Window w, Window *root_return, Window *parent_return, Window **children_return, unsigned int *nchildren_return) {
  char cmd[X6_BUF_SIZE];
  char line[X6_BUF_SIZE];
  unsigned int root = 0;
  unsigned int parent = 0;
  unsigned int nchild = 0;
  char children[256];
  unsigned int parsed;

  if (children_return)
    *children_return = 0;
  if (nchildren_return)
    *nchildren_return = 0;
  if (!display)
    return 0;

  children[0] = 0;
  snprintf(cmd, sizeof(cmd), "QUERY_TREE %u\n", (uint)w);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return 0;

  parsed = (unsigned int)sscanf(line, "OK tree root=%u parent=%u n=%u children=%255s",
                                &root, &parent, &nchild, children);
  if (parsed < 3)
    return 0;

  if (root_return)
    *root_return = (Window)root;
  if (parent_return)
    *parent_return = (Window)parent;

  if (nchild == 0 || !children_return) {
    if (nchildren_return)
      *nchildren_return = nchild;
    return 1;
  }

  {
    Window *arr;
    unsigned int count;
    const char *p;

    arr = (Window *)calloc((size_t)nchild, sizeof(Window));
    if (!arr)
      return 0;

    count = 0;
    p = children;
    while (*p && count < nchild) {
      unsigned int cid;
      int step;
      if (*p == ',') {
        p++;
        continue;
      }
      if (*p == '-')
        break;
      step = 0;
      if (sscanf(p, "%u%n", &cid, &step) != 1 || step <= 0)
        break;
      arr[count++] = (Window)cid;
      p += step;
      if (*p == ',')
        p++;
    }

    if (nchildren_return)
      *nchildren_return = count;
    if (count == 0) {
      free(arr);
      *children_return = 0;
    } else {
      *children_return = arr;
    }
  }
  return 1;
}
Bool XQueryPointer(Display *display, Window w, Window *root_return, Window *child_return, int *root_x_return, int *root_y_return, int *win_x_return, int *win_y_return, unsigned int *mask_return) {
  char cmd[X6_BUF_SIZE];
  char line[X6_BUF_SIZE];
  unsigned int root = 0;
  unsigned int child = 0;
  int px = 0;
  int py = 0;
  int wx = 0;
  int wy = 0;
  unsigned int state = 0;

  snprintf(cmd, sizeof(cmd), "QUERY_POINTER %u\n", (uint)w);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return False;
  if (sscanf(line, "OK pointer root=%u child=%u x=%d y=%d wx=%d wy=%d state=%u",
             &root, &child, &px, &py, &wx, &wy, &state) < 7) {
    if (sscanf(line, "OK pointer root=%u child=%u x=%d y=%d state=%u",
               &root, &child, &px, &py, &state) < 5)
      return False;
    wx = px;
    wy = py;
  }

  if (root_return) *root_return = (Window)root;
  if (child_return) *child_return = (Window)child;
  if (root_x_return) *root_x_return = px;
  if (root_y_return) *root_y_return = py;
  if (win_x_return) *win_x_return = wx;
  if (win_y_return) *win_y_return = wy;
  if (mask_return) *mask_return = state;
  return True;
}

int XGetGeometry(Display *display, Drawable d, Window *root_return,
                 int *x_return, int *y_return,
                 unsigned int *width_return, unsigned int *height_return,
                 unsigned int *border_width_return,
                 unsigned int *depth_return) {
  struct x11_pixmap_state *pm;

  if (!display)
    return 0;

  pm = x11_find_pixmap((Pixmap)d);
  if (pm) {
    if (root_return) *root_return = display->root;
    if (x_return) *x_return = 0;
    if (y_return) *y_return = 0;
    if (width_return) *width_return = pm->width;
    if (height_return) *height_return = pm->height;
    if (border_width_return) *border_width_return = 0;
    if (depth_return) *depth_return = pm->depth ? pm->depth : (unsigned int)display->depth;
    return 1;
  }

  {
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(display, (Window)d, &attrs))
      return 0;
    if (root_return) *root_return = display->root;
    if (x_return) *x_return = attrs.x;
    if (y_return) *y_return = attrs.y;
    if (width_return) *width_return = (unsigned int)attrs.width;
    if (height_return) *height_return = (unsigned int)attrs.height;
    if (border_width_return) *border_width_return = (unsigned int)attrs.border_width;
    if (depth_return) *depth_return = (unsigned int)(attrs.depth > 0 ? attrs.depth : display->depth);
    return 1;
  }
}

static int
x11_window_origin(Display *display, Window w, int *x_out, int *y_out)
{
  XWindowAttributes attrs;

  if (!display || !x_out || !y_out)
    return -1;
  if (w == display->root) {
    *x_out = 0;
    *y_out = 0;
    return 0;
  }
  if (!XGetWindowAttributes(display, w, &attrs))
    return -1;
  *x_out = attrs.x;
  *y_out = attrs.y;
  return 0;
}

int XTranslateCoordinates(Display *display, Window src_w, Window dest_w,
                          int src_x, int src_y,
                          int *dest_x_return, int *dest_y_return,
                          Window *child_return) {
  int src_ox;
  int src_oy;
  int dst_ox;
  int dst_oy;
  int root_x;
  int root_y;
  char cmd[X6_BUF_SIZE];
  char line[X6_BUF_SIZE];
  unsigned int child = 0;

  if (!display)
    return 0;
  if (x11_window_origin(display, src_w, &src_ox, &src_oy) < 0)
    return 0;
  if (x11_window_origin(display, dest_w, &dst_ox, &dst_oy) < 0)
    return 0;

  root_x = src_ox + src_x;
  root_y = src_oy + src_y;

  if (dest_x_return)
    *dest_x_return = root_x - dst_ox;
  if (dest_y_return)
    *dest_y_return = root_y - dst_oy;

  if (child_return) {
    *child_return = None;
    if (dest_w == display->root) {
      snprintf(cmd, sizeof(cmd), "QUERY_WINDOW_AT %d %d\n", root_x, root_y);
      if (x11_cmd(display, cmd, line, sizeof(line)) >= 0) {
        if (sscanf(line, "OK window_at child=%u", &child) == 1 && child != 0)
          *child_return = (Window)child;
      }
    } else {
      snprintf(cmd, sizeof(cmd), "QUERY_CHILD_AT %u %d %d\n", (uint)dest_w, root_x, root_y);
      if (x11_cmd(display, cmd, line, sizeof(line)) >= 0) {
        if (sscanf(line, "OK child_at child=%u", &child) == 1 && child != 0)
          *child_return = (Window)child;
      }
    }
  }
  return 1;
}

int
XPending(Display *display)
{
  struct pollfd pfd;

  if (!display)
    return 0;
  if (display->event_count > 0)
  {
    x11dbg("x11:pending immediate queued=%d", display->event_count);
    return 1;
  }

  /* Check if we already have bytes buffered from a previous recv */
  if (display->rxlen > 0) {
    XEvent ev;
    if (x11_read_event_from_wire_nonblocking(display, &ev) == 0)
      x11_event_push_back(display, &ev);
    x11dbg("x11:pending rxbuf-hit queued=%d", display->event_count);
    return display->event_count > 0 ? 1 : 0;
  }

  pfd.fd = display->fd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  if (poll(&pfd, 1, 0) <= 0)
    return 0;
  if ((pfd.revents & POLLIN) == 0)
    return 0;
  {
    XEvent ev;
    if (x11_read_event_from_wire_nonblocking(display, &ev) == 0)
      x11_event_push_back(display, &ev);
  }
  x11dbg("x11:pending poll-hit queued=%d revents=%d", display->event_count, pfd.revents);
  return display->event_count > 0 ? 1 : 0;
}

int
XEventsQueued(Display *display, int mode)
{
  int before;

  (void)mode;
  if (!display)
    return 0;

  before = display->event_count;
  if (before > 0)
    return before;

  (void)XPending(display);
  return display->event_count;
}

int (*XSetErrorHandler(int (*handler)(Display *, XErrorEvent *)))(Display *, XErrorEvent *) {
  int (*old)(Display *, XErrorEvent *) = g_error_handler;
  g_error_handler = handler;
  return old;
}
int XBell(Display *display, int percent) { (void)display; (void)percent; return 0; }
int XAddToSaveSet(Display *display, Window w) { (void)display; (void)w; return 0; }
int XRemoveFromSaveSet(Display *display, Window w) { (void)display; (void)w; return 0; }
void XSetCloseDownMode(Display *display, int close_mode) { (void)display; (void)close_mode; }
void XGrabServer(Display *display) { (void)display; }
void XUngrabServer(Display *display) { (void)display; }
int XKillClient(Display *display, XID resource) { (void)display; (void)resource; return 0; }

void XFree(void *data) { if (data) free(data); }

/* Map x6 console keycode (= ASCII byte) to the proper X11 keysym.
 * Printable ASCII 32-126: keysym == codepoint (identical in X11).
 * Control characters must be translated to their 0xff00 keysym equivalents
 * so that dwm's XK_* comparisons work correctly. */
static KeySym
x11_keycode_to_keysym(KeyCode keycode)
{
  switch ((unsigned int)keycode) {
    case 8:   return XK_BackSpace;
    case 9:   return XK_Tab;
    case 10:  return XK_Return;
    case 13:  return XK_Return;
    case 27:  return XK_Escape;
    case 127: return XK_Delete;
    default:  return (KeySym)keycode;
  }
}

int XLookupString(XKeyEvent *event_struct, char *buffer_return, int bytes_buffer,
                  KeySym *keysym_return, void *status_in_out) {
  KeySym ks;
  char ch;
  (void)status_in_out;

  if (!event_struct)
    return 0;

  ks = x11_keycode_to_keysym((KeyCode)event_struct->keycode);
  if (keysym_return)
    *keysym_return = ks;

  if (!buffer_return || bytes_buffer <= 0)
    return 0;

  ch = 0;
  if (ks == XK_Return)
    ch = '\r';
  else if (ks == XK_BackSpace)
    ch = '\b';
  else if (ks == XK_Tab)
    ch = '\t';
  else if (ks == XK_Escape)
    ch = '\033';
  else if (ks >= 32 && ks < 127)
    ch = (char)ks;
  else
    return 0;

  if ((event_struct->state & ShiftMask) && ch >= 'a' && ch <= 'z')
    ch = (char)(ch - 'a' + 'A');

  buffer_return[0] = ch;
  return 1;
}

KeyCode XKeysymToKeycode(Display *display, KeySym keysym) {
  (void)display;
  switch ((unsigned long)keysym) {
    case XK_BackSpace: return 8;
    case XK_Tab:       return 9;
    case XK_Return:    return 10;
    case XK_Escape:    return 27;
    case XK_Delete:    return 127;
    default:           return (KeyCode)(keysym & 0xff);
  }
}
KeySym XKeycodeToKeysym(Display *display, KeyCode keycode, int index) {
  KeySym ks;
  (void)display;
  (void)index;
  ks = x11_keycode_to_keysym(keycode);
  if (keycode == 10)
    x11dbg("x11:keycode->keysym keycode=%u -> keysym=0x%lx", (uint)keycode, (unsigned long)ks);
  return ks;
}
void XDisplayKeycodes(Display *display, int *min_keycodes_return, int *max_keycodes_return) {
  (void)display;
  if (min_keycodes_return) *min_keycodes_return = 8;
  if (max_keycodes_return) *max_keycodes_return = 255;
}
KeySym *XGetKeyboardMapping(Display *display, KeyCode first_keycode, int keycode_count, int *keysyms_per_keycode_return) {
  int i;
  KeySym *map;
  (void)display;
  if (keysyms_per_keycode_return) *keysyms_per_keycode_return = 1;
  map = malloc(sizeof(KeySym) * keycode_count);
  if (!map) return 0;
  for (i = 0; i < keycode_count; i++)
    map[i] = x11_keycode_to_keysym((KeyCode)(first_keycode + i));
  return map;
}
XModifierKeymap *XGetModifierMapping(Display *display) {
  XModifierKeymap *m;
  (void)display;
  m = malloc(sizeof(*m));
  if (!m) return 0;
  m->max_keypermod = 1;
  m->modifiermap = malloc(8 * sizeof(KeyCode));
  if (!m->modifiermap) {
    free(m);
    return 0;
  }
  memset(m->modifiermap, 0, 8 * sizeof(KeyCode));
  return m;
}
int XFreeModifiermap(XModifierKeymap *modmap) {
  if (!modmap) return 0;
  free(modmap->modifiermap);
  free(modmap);
  return 0;
}
int XRefreshKeyboardMapping(XMappingEvent *event_map) { (void)event_map; return 0; }

int XSupportsLocale(void) { return 1; }

int XSetWMNormalHints(Display *display, Window w, XSizeHints *hints) {
  char buf[256];
  int n;
  if (!display || !hints)
    return 0;
  n = snprintf(buf, sizeof(buf),
               "flags=%ld size=%dx%d min=%dx%d max=%dx%d base=%dx%d inc=%dx%d pos=%d,%d gravity=%d aspect=%d/%d:%d/%d",
               hints->flags,
               hints->width, hints->height,
               hints->min_width, hints->min_height,
               hints->max_width, hints->max_height,
               hints->base_width, hints->base_height,
               hints->width_inc, hints->height_inc,
               hints->x, hints->y, hints->win_gravity,
               hints->min_aspect.x, hints->min_aspect.y,
               hints->max_aspect.x, hints->max_aspect.y);
  if (n < 0)
    return 0;
  return XChangeProperty(display, w, XA_WM_NORMAL_HINTS, XA_STRING, 8,
                         PropModeReplace, (unsigned char *)buf, (int)strlen(buf));
}
int XSetTransientForHint(Display *display, Window w, Window prop_window) {
  char buf[64];
  int n;
  if (!display)
    return 0;
  n = snprintf(buf, sizeof(buf), "%u", (uint)prop_window);
  if (n < 0)
    return 0;
  return XChangeProperty(display, w, XA_WM_TRANSIENT_FOR, XA_STRING, 8,
                         PropModeReplace, (unsigned char *)buf, (int)strlen(buf));
}
int XStoreName(Display *display, Window w, const char *name) {
  return XChangeProperty(display, w, XA_WM_NAME, XA_STRING, 8, PropModeReplace, (unsigned char *)name, name ? (int)strlen(name) : 0);
}
int XGetClassHint(Display *display, Window w, XClassHint *class_hint_return) {
  unsigned char *prop = 0;
  unsigned long nitems = 0;
  Atom actual = 0;
  int fmt = 0;
  const char *raw;
  unsigned long name_len;
  unsigned long class_len;
  unsigned long i;
  if (!class_hint_return) return 0;
  class_hint_return->res_name = 0;
  class_hint_return->res_class = 0;
  x11consolecrit("x11:getclasshint enter wid=%u", (uint)w);
  if (!display)
    return 1;
  if (XGetWindowProperty(display, w, XInternAtom(display, "WM_CLASS", False),
                         0, 256, False, XA_STRING, &actual, &fmt, &nitems, 0, &prop) != Success)
    return 1;
  x11consolecrit("x11:getclasshint fetched wid=%u actual=%lu fmt=%d nitems=%lu prop=%p",
                 (uint)w, (unsigned long)actual, fmt, nitems, prop);
  if (!prop)
    return 1;

  raw = (const char *)prop;
  name_len = 0;
  class_len = 0;

  /* Support both auxv6's comma-separated form and ICCCM's NUL-separated form. */
  for (i = 0; i < nitems; i++) {
    if (raw[i] == '\0' || raw[i] == ',')
      break;
  }
  name_len = i;
  x11consolecrit("x11:getclasshint split wid=%u name_len=%lu", (uint)w, name_len);

  if (i < nitems) {
    unsigned long class_start = i + 1;
    while (class_start < nitems && raw[class_start] == '\0')
      class_start++;
    if (class_start < nitems)
      class_len = nitems - class_start;
    x11consolecrit("x11:getclasshint class-range wid=%u class_start=%lu class_len=%lu", (uint)w,
                   class_start, class_len);
    if (class_len > 0)
      class_hint_return->res_class = strndup(raw + class_start, class_len);
  }

  if (name_len > 0)
    class_hint_return->res_name = strndup(raw, name_len);

  x11consolecrit("x11:getclasshint done wid=%u name=%p class=%p", (uint)w,
                 class_hint_return->res_name, class_hint_return->res_class);

  XFree(prop);
  return 1;
}
int XGetTextProperty(Display *display, Window w, XTextProperty *text_prop_return, Atom property) {
  unsigned char *prop = 0;
  unsigned long nitems = 0;
  Atom actual = 0;
  int fmt = 0;
  if (!text_prop_return) return 0;
  x11consolecrit("x11:gettextprop enter wid=%u atom=%lu", (uint)w, (unsigned long)property);
  if (XGetWindowProperty(display, w, property, 0, 1024, False, 0, &actual, &fmt, &nitems, 0, &prop) != Success)
    return 0;
  if (!prop) {
    x11consolecrit("x11:gettextprop empty wid=%u atom=%lu", (uint)w, (unsigned long)property);
    return 0;
  }
  text_prop_return->value = (char *)prop;
  text_prop_return->encoding = actual;
  text_prop_return->format = fmt;
  text_prop_return->nitems = nitems;
  x11consolecrit("x11:gettextprop done wid=%u atom=%lu actual=%lu fmt=%d nitems=%lu value=%p",
                 (uint)w, (unsigned long)property, (unsigned long)actual, fmt, nitems, prop);
  return 1;
}
int XmbTextPropertyToTextList(Display *display, XTextProperty *text_prop, char ***list_return, int *count_return) {
  (void)display;
  if (!text_prop || !list_return || !count_return || !text_prop->value) return 0;
  *list_return = calloc(2, sizeof(char *));
  if (!*list_return) return 0;
  (*list_return)[0] = strdup(text_prop->value);
  if (!(*list_return)[0]) {
    free(*list_return);
    return 0;
  }
  (*list_return)[1] = 0;
  *count_return = 1;
  return Success;
}
int XTextPropertyToStringList(void *text_prop, char ***list_return,
                              int *count_return) {
  XTextProperty *tp;
  unsigned long i;
  int count;

  if (list_return)
    *list_return = 0;
  if (count_return)
    *count_return = 0;

  if (!text_prop || !list_return || !count_return)
    return 0;

  tp = (XTextProperty *)text_prop;
  if (!tp->value || tp->nitems == 0)
    return 0;

  count = 0;
  for (i = 0; i < tp->nitems; i++) {
    if (tp->value[i] == '\0')
      count++;
  }
  if (tp->value[tp->nitems - 1] != '\0')
    count++;

  if (count <= 0)
    return 0;

  *list_return = (char **)calloc((size_t)count + 1, sizeof(char *));
  if (!*list_return)
    return 0;

  {
    unsigned long start = 0;
    int out = 0;
    for (i = 0; i < tp->nitems && out < count; i++) {
      if (tp->value[i] == '\0') {
        (*list_return)[out] = (char *)malloc((size_t)(i - start) + 1);
        if (!(*list_return)[out])
          break;
        memmove((*list_return)[out], tp->value + start, (size_t)(i - start));
        (*list_return)[out][i - start] = '\0';
        out++;
        start = i + 1;
      }
    }
    if (start < tp->nitems && out < count) {
      (*list_return)[out] = (char *)malloc((size_t)(tp->nitems - start) + 1);
      if ((*list_return)[out]) {
        memmove((*list_return)[out], tp->value + start, (size_t)(tp->nitems - start));
        (*list_return)[out][tp->nitems - start] = '\0';
        out++;
      }
    }

    if (out == 0) {
      free(*list_return);
      *list_return = 0;
      return 0;
    }

    if (out < count) {
      int j;
      for (j = 0; j < out; j++)
        free((*list_return)[j]);
      free(*list_return);
      *list_return = 0;
      return 0;
    }

    *count_return = out;
  }

  return Success;
}
void XFreeStringList(char **list) {
  int i;
  if (!list) return;
  for (i = 0; list[i]; i++)
    free(list[i]);
  free(list);
}

static int
x11_is_space(char c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

static int
x11_is_hex_digit(char c)
{
  return (c >= '0' && c <= '9') ||
         (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

static int
x11_hex_digit_value(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F')
    return 10 + (c - 'A');
  return -1;
}

static int
x11_parse_int_token(const char **pp, unsigned int *value_out)
{
  const char *p;
  unsigned int v;
  int seen;

  if (!pp || !*pp || !value_out)
    return -1;

  p = *pp;
  while (*p && x11_is_space(*p))
    p++;

  v = 0;
  seen = 0;
  while (*p >= '0' && *p <= '9') {
    v = v * 10U + (unsigned int)(*p - '0');
    p++;
    seen = 1;
  }

  if (!seen)
    return -1;

  *value_out = v;
  *pp = p;
  return 0;
}

int XReadBitmapFileData(const char *filename,
                        unsigned int *width_return,
                        unsigned int *height_return,
                        unsigned char **data_return,
                        int *x_hot_return,
                        int *y_hot_return) {
  int fd;
  char *buf;
  int nread;
  int total;
  const char *p;
  unsigned int width;
  unsigned int height;
  unsigned int xhot;
  unsigned int yhot;
  int has_xhot;
  int has_yhot;
  int max_bytes;
  unsigned char *bytes;
  int count;

  if (width_return)
    *width_return = 0;
  if (height_return)
    *height_return = 0;
  if (data_return)
    *data_return = 0;
  if (x_hot_return)
    *x_hot_return = -1;
  if (y_hot_return)
    *y_hot_return = -1;

  if (!filename || !width_return || !height_return || !data_return)
    return 1;

  fd = open(filename, O_RDONLY);
  if (fd < 0)
    return 1;

  buf = (char *)malloc(65537);
  if (!buf) {
    close(fd);
    return 3;
  }

  total = 0;
  while (total < 65536) {
    nread = read(fd, buf + total, 65536 - total);
    if (nread < 0) {
      close(fd);
      free(buf);
      return 2;
    }
    if (nread == 0)
      break;
    total += nread;
  }
  close(fd);
  if (total <= 0) {
    free(buf);
    return 2;
  }
  buf[total] = '\0';

  width = 0;
  height = 0;
  xhot = 0;
  yhot = 0;
  has_xhot = 0;
  has_yhot = 0;

  p = buf;
  while (*p) {
    if (strncmp(p, "#define", 7) == 0 && x11_is_space(p[7])) {
      const char *q = p + 7;
      char name[128];
      int name_len = 0;
      unsigned int v;

      while (*q && x11_is_space(*q))
        q++;
      while (*q && !x11_is_space(*q) && name_len < (int)sizeof(name) - 1)
        name[name_len++] = *q++;
      name[name_len] = '\0';
      if (x11_parse_int_token(&q, &v) == 0) {
        if (name_len >= 6 && strcmp(name + name_len - 6, "_width") == 0)
          width = v;
        else if (name_len >= 7 && strcmp(name + name_len - 7, "_height") == 0)
          height = v;
        else if (name_len >= 5 && strcmp(name + name_len - 5, "_x_hot") == 0) {
          xhot = v;
          has_xhot = 1;
        } else if (name_len >= 5 && strcmp(name + name_len - 5, "_y_hot") == 0) {
          yhot = v;
          has_yhot = 1;
        }
      }
    }

    while (*p && *p != '\n')
      p++;
    if (*p == '\n')
      p++;
  }

  if (width == 0 || height == 0) {
    free(buf);
    return 2;
  }

  max_bytes = (int)(((width + 7U) / 8U) * height);
  if (max_bytes <= 0) {
    free(buf);
    return 2;
  }

  bytes = (unsigned char *)malloc((size_t)max_bytes);
  if (!bytes) {
    free(buf);
    return 3;
  }

  count = 0;
  p = buf;
  while (*p && count < max_bytes) {
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && x11_is_hex_digit(p[2])) {
      unsigned int v = 0;
      int d;
      p += 2;
      while ((d = x11_hex_digit_value(*p)) >= 0) {
        v = (v << 4) | (unsigned int)d;
        p++;
      }
      bytes[count++] = (unsigned char)(v & 0xffU);
      continue;
    }
    p++;
  }

  free(buf);

  if (count < max_bytes) {
    free(bytes);
    return 2;
  }

  *width_return = width;
  *height_return = height;
  *data_return = bytes;
  if (x_hot_return && has_xhot)
    *x_hot_return = (int)xhot;
  if (y_hot_return && has_yhot)
    *y_hot_return = (int)yhot;
  return 0;
}
int XGetWMProtocols(Display *display, Window w, Atom **protocols_return, int *count_return) {
  unsigned char *prop = 0;
  unsigned long nitems = 0;
  Atom actual = 0;
  int fmt = 0;
  unsigned long i;

  if (protocols_return)
    *protocols_return = 0;
  if (count_return)
    *count_return = 0;
  if (!display || !protocols_return || !count_return)
    return 0;

  if (XGetWindowProperty(display, w, XInternAtom(display, "WM_PROTOCOLS", False),
                         0, 256, False, XA_ATOM, &actual, &fmt, &nitems, 0, &prop) != Success)
    return 0;
  if (!prop || actual != XA_ATOM || fmt != 32 || nitems == 0) {
    if (prop)
      XFree(prop);
    return 0;
  }

  *protocols_return = (Atom *)malloc((size_t)nitems * sizeof(Atom));
  if (!*protocols_return) {
    XFree(prop);
    return 0;
  }

  for (i = 0; i < nitems; i++) {
    unsigned long v = ((unsigned long *)prop)[i];
    (*protocols_return)[i] = (Atom)v;
  }
  XFree(prop);

  *count_return = (int)nitems;
  return *count_return > 0;
}
XWMHints *XGetWMHints(Display *display, Window w) {
  XWMHints *h = malloc(sizeof(*h));
  char cmd[X6_BUF_SIZE];
  char line[X6_BUF_SIZE];
  unsigned char *prop = 0;
  unsigned long nitems = 0;
  Atom actual = 0;
  int fmt = 0;
  if (!h) return 0;
  memset(h, 0, sizeof(*h));
  h->input = 1;
  if (!display)
    return h;
  snprintf(cmd, sizeof(cmd), "GET_PROPERTY %u %s\n", (uint)w, atom_to_name(XA_WM_HINTS));
  if (x11_cmd(display, cmd, line, sizeof(line)) == 0 && strncmp(line, "VALUE ", 6) == 0) {
    char *space = strchr(line + 6, ' ');
    if (space)
      x11_prop_unpack(space + 1, &actual, &fmt, &nitems, &prop);
  }
  if (prop) {
    long flags = 0;
    int input = 1;
    int istate = 0;
    if (sscanf((char *)prop, "flags=%ld input=%d initial=%d", &flags, &input, &istate) == 3) {
      h->flags = flags;
      h->input = input;
      h->initial_state = istate;
    }
    XFree(prop);
  }
  return h;
}
int XSetWMHints(Display *display, Window w, XWMHints *wmhints) {
  char buf[128];
  if (!display || !wmhints)
    return 0;
  snprintf(buf, sizeof(buf), "flags=%ld input=%d initial=%d",
           wmhints->flags, wmhints->input, wmhints->initial_state);
  return XChangeProperty(display, w, XA_WM_HINTS, XA_STRING, 8,
                         PropModeReplace, (unsigned char *)buf, (int)strlen(buf));
}
int XGetWMNormalHints(Display *display, Window w, XSizeHints *hints_return, long *supplied_return) {
  char cmd[X6_BUF_SIZE];
  char line[X6_BUF_SIZE];
  unsigned char *prop = 0;
  unsigned long nitems = 0;
  Atom actual = 0;
  int fmt = 0;
  long flags = 0;
  int matched;
  if (hints_return) memset(hints_return, 0, sizeof(*hints_return));
  if (!display || !hints_return)
    return 0;
  x11consolecrit("x11:getwmnormalhints enter wid=%u", (uint)w);
  snprintf(cmd, sizeof(cmd), "GET_PROPERTY %u %s\n", (uint)w, atom_to_name(XA_WM_NORMAL_HINTS));
  if (x11_cmd(display, cmd, line, sizeof(line)) == 0 && strncmp(line, "VALUE ", 6) == 0) {
    char *space = strchr(line + 6, ' ');
    if (space)
      x11_prop_unpack(space + 1, &actual, &fmt, &nitems, &prop);
  }
  if (prop) {
    matched = sscanf((char *)prop,
           "flags=%ld size=%dx%d min=%dx%d max=%dx%d base=%dx%d inc=%dx%d pos=%d,%d gravity=%d aspect=%d/%d:%d/%d",
           &flags,
           &hints_return->width, &hints_return->height,
           &hints_return->min_width, &hints_return->min_height,
           &hints_return->max_width, &hints_return->max_height,
           &hints_return->base_width, &hints_return->base_height,
           &hints_return->width_inc, &hints_return->height_inc,
           &hints_return->x, &hints_return->y,
           &hints_return->win_gravity,
           &hints_return->min_aspect.x, &hints_return->min_aspect.y,
           &hints_return->max_aspect.x, &hints_return->max_aspect.y);
    if (matched < 9) {
      matched = sscanf((char *)prop,
             "flags=%ld min=%dx%d max=%dx%d base=%dx%d inc=%dx%d",
             &flags,
             &hints_return->min_width, &hints_return->min_height,
             &hints_return->max_width, &hints_return->max_height,
             &hints_return->base_width, &hints_return->base_height,
             &hints_return->width_inc, &hints_return->height_inc);
    }
    hints_return->flags = flags;
    x11consolecrit("x11:getwmnormalhints done wid=%u matched=%d flags=%ld min=%dx%d max=%dx%d base=%dx%d inc=%dx%d",
                   (uint)w, matched, hints_return->flags,
                   hints_return->min_width, hints_return->min_height,
                   hints_return->max_width, hints_return->max_height,
                   hints_return->base_width, hints_return->base_height,
                   hints_return->width_inc, hints_return->height_inc);
    XFree(prop);
    if (supplied_return) *supplied_return = hints_return->flags;
    return matched >= 9 ? 1 : 0;
  }
  x11consolecrit("x11:getwmnormalhints empty wid=%u", (uint)w);
  if (supplied_return) *supplied_return = 0;
  return 0;
}
int XSetClassHint(Display *display, Window w, XClassHint *class_hints) {
  char buf[256];
  const char *name;
  const char *klass;
  if (!display || !class_hints)
    return 0;
  name = class_hints->res_name ? class_hints->res_name : "";
  klass = class_hints->res_class ? class_hints->res_class : "";
  snprintf(buf, sizeof(buf), "%s,%s", name, klass);
  return XChangeProperty(display, w, XInternAtom(display, "WM_CLASS", False), XA_STRING, 8,
                         PropModeReplace, (unsigned char *)buf, (int)strlen(buf));
}

/* ------- Xft function implementations for st compatibility ------- */

XftDraw *XftDrawCreate(Display *display, Drawable drawable, Visual *visual, Colormap colormap) {
  XftDraw *d = (XftDraw *)malloc(sizeof(*d));
  if (d) {
    d->drawable = drawable;
    d->display = display;
    d->has_clip = 0;
    memset(&d->clip, 0, sizeof(d->clip));
  }
  (void)visual;
  (void)colormap;
  return d;
}

void XftDrawChange(XftDraw *draw, Drawable drawable) {
  if (draw)
    draw->drawable = drawable;
}

void XftDrawDestroy(XftDraw *draw) {
  free(draw);
}

void XftDrawRect(XftDraw *draw, XftColor *color, int x, int y, unsigned int width, unsigned int height) {
  GC gc;
  int dw;
  int dh;
  if (!draw || !draw->display || width == 0 || height == 0)
    return;
  if (x11_drawable_size(draw->display, draw->drawable, &dw, &dh) == 0) {
    (void)dw;
    (void)dh;
    if (x11_sanitize_rect(draw->display, &x, &y, &width, &height) < 0)
      return;
  }
  if (x11_clip_box(draw, &x, &y, &width, &height) < 0)
    return;
  gc = x11_xft_gc(draw->display, draw->drawable);
  if (!gc)
    return;
  XSetForeground(draw->display, gc, color ? color->pixel : 0x00ffffffUL);
  XFillRectangle(draw->display, draw->drawable, gc, x, y, width, height);
}

void XftDrawSetClipRectangles(XftDraw *draw, int xOrigin, int yOrigin, XRectangle *rects, int nrects) {
  int i;
  int minx;
  int miny;
  int maxx;
  int maxy;

  if (!draw)
    return;
  if (!rects || nrects <= 0) {
    draw->has_clip = 0;
    memset(&draw->clip, 0, sizeof(draw->clip));
    return;
  }

  minx = xOrigin + rects[0].x;
  miny = yOrigin + rects[0].y;
  maxx = minx + rects[0].width;
  maxy = miny + rects[0].height;
  for (i = 1; i < nrects; i++) {
    int rx;
    int ry;
    int rx2;
    int ry2;
    rx = xOrigin + rects[i].x;
    ry = yOrigin + rects[i].y;
    rx2 = rx + rects[i].width;
    ry2 = ry + rects[i].height;
    if (rx < minx) minx = rx;
    if (ry < miny) miny = ry;
    if (rx2 > maxx) maxx = rx2;
    if (ry2 > maxy) maxy = ry2;
  }

  if (maxx <= minx || maxy <= miny) {
    draw->has_clip = 1;
    draw->clip.x = 0;
    draw->clip.y = 0;
    draw->clip.width = 0;
    draw->clip.height = 0;
    return;
  }

  draw->has_clip = 1;
  draw->clip.x = (short)minx;
  draw->clip.y = (short)miny;
  draw->clip.width = (unsigned short)(maxx - minx);
  draw->clip.height = (unsigned short)(maxy - miny);
}

void XftDrawSetClip(XftDraw *draw, void *clip) {
  (void)clip;
  if (!draw)
    return;
  draw->has_clip = 0;
  memset(&draw->clip, 0, sizeof(draw->clip));
}

void XftDrawGlyphFontSpec(XftDraw *draw, XftColor *color, XftGlyphFontSpec *glyphs, int nglyphs) {
  GC gc;
  int i;
  int run_start;
  int run_len;
  int run_y;
  int run_x;
  int run_advance;
  XftFont *run_font;
  char textbuf[512];

  if (!draw || !draw->display || !glyphs || nglyphs <= 0)
    return;

  gc = x11_xft_gc(draw->display, draw->drawable);
  if (!gc)
    return;
  XSetForeground(draw->display, gc, color ? color->pixel : 0x00ffffffUL);

  run_start = -1;
  run_len = 0;
  run_y = 0;
  run_x = 0;
  run_advance = 0;
  run_font = 0;

  for (i = 0; i < nglyphs; i++) {
    char ch;
    unsigned int g = glyphs[i].glyph;
    int glyph_advance;
    int contiguous;

    if (!x11_point_in_clip(draw, glyphs[i].x, glyphs[i].y))
      contiguous = 0;
    else
      contiguous = 1;

    if (g >= 32 && g < 127)
      ch = (char)g;
    else
      ch = '?';

    glyph_advance = (glyphs[i].font && glyphs[i].font->max_advance_width > 0)
                    ? glyphs[i].font->max_advance_width
                    : 8;

    if (run_start >= 0 && contiguous) {
      int expected_x = run_x + run_len * run_advance;
      if (glyphs[i].y != run_y || glyphs[i].x != expected_x ||
          glyphs[i].font != run_font || glyph_advance != run_advance ||
          run_len >= (int)(sizeof(textbuf) - 1)) {
        XDrawString(draw->display, draw->drawable, gc,
                    run_x, run_y, textbuf, run_len);
        run_start = -1;
        run_len = 0;
      }
    }

    if (!contiguous)
      continue;

    if (run_start < 0) {
      run_start = i;
      run_len = 0;
      run_x = glyphs[i].x;
      run_y = glyphs[i].y;
      run_font = glyphs[i].font;
      run_advance = glyph_advance;
    }

    textbuf[run_len++] = ch;
  }

  if (run_start >= 0 && run_len > 0)
    XDrawString(draw->display, draw->drawable, gc, run_x, run_y, textbuf, run_len);
}

int XftColorAllocValue(Display *display, Visual *visual, Colormap colormap, XRenderColor *color, XftColor *result) {
  if (!color || !result)
    return 0;
  (void)display;
  (void)visual;
  (void)colormap;
  result->pixel = ((unsigned long)(color->red >> 8) << 16) |
                  ((unsigned long)(color->green >> 8) << 8) |
                  (unsigned long)(color->blue >> 8);
  result->color.red = color->red;
  result->color.green = color->green;
  result->color.blue = color->blue;
  result->color.alpha = color->alpha;
  return 1;
}

int XftColorAllocName(Display *display, Visual *visual, Colormap colormap, const char *name, XftColor *result) {
  XColor xc;
  XRenderColor rc;
  if (!XParseColor(display, colormap, name, &xc))
    return 0;
  if (!XAllocColor(display, colormap, &xc))
    return 0;
  rc.red = xc.red;
  rc.green = xc.green;
  rc.blue = xc.blue;
  rc.alpha = 65535;
  return XftColorAllocValue(display, visual, colormap, &rc, result);
}

void XftColorFree(Display *display, Visual *visual, Colormap colormap, XftColor *color) {
  (void)display;
  (void)visual;
  (void)colormap;
  (void)color;
  /* Pixel is already allocated; XAllocColor handles caching */
}

XftFont *XftFontOpenPattern(Display *display, XftPattern *pattern) {
  XftFont *f;
  struct x11_fc_pattern *fp;
  (void)display;
  
  f = (XftFont *)malloc(sizeof(*f));
  if (f) {
    fp = (struct x11_fc_pattern *)pattern;
    f->pattern = pattern;
    f->charset = 0;
    x11_fill_xft_font_metrics(f,
                              fp ? fp->name : 0,
                              (fp && fp->has_pixel_size) ? fp->pixel_size : 0.0);
  }
  return f;
}

void XftFontClose(Display *display, XftFont *font) {
  (void)display;
  free(font);
}

XftFont *XftFontOpenName(Display *display, int screen, const char *xlfd) {
  XftFont *f;
  (void)screen;
  (void)display;
  
  f = (XftFont *)malloc(sizeof(*f));
  if (f) {
    f->pattern = 0;
    f->charset = 0;
    x11_fill_xft_font_metrics(f, xlfd, 0.0);
  }
  return f;
}

XftPattern *XftPatternCreate(void) {
  return (XftPattern *)x11_fc_pattern_new();
}

void XftPatternDestroy(XftPattern *p) {
  free(p);
}

void XftDefaultSubstitute(Display *display, int screen, XftPattern *pattern) {
  (void)display;
  (void)screen;
  FcDefaultSubstitute((FcPattern *)pattern);
}

XftResult XftPatternGetInteger(XftPattern *p, const char *object, int id, int *i) {
  struct x11_fc_pattern *fp;
  (void)id;
  fp = (struct x11_fc_pattern *)p;
  if (!fp || !object || !i)
    return XftResultNoMatch;
  if (!strcmp(object, FC_SLANT) && fp->has_slant) {
    *i = fp->slant;
    return XftResultMatch;
  }
  if (!strcmp(object, FC_WEIGHT) && fp->has_weight) {
    *i = fp->weight;
    return XftResultMatch;
  }
  return XftResultNoMatch;
}

int XftCharExists(Display *display, XftFont *font, FcChar32 ucs4) {
  (void)display;
  (void)font;
  (void)ucs4;
  return 1;  /* Assume all chars exist */
}

unsigned int XftCharIndex(Display *display, XftFont *font, FcChar32 ucs4) {
  (void)display;
  (void)font;
  return (unsigned int)ucs4;  /* Simplified: treat codepoint as index */
}

XftPattern *XftFontMatch(Display *display, int screen, XftPattern *pattern, XftResult *result) {
  (void)display;
  (void)screen;
  if (result)
    *result = XftResultMatch;
  return (XftPattern *)FcPatternDuplicate((FcPattern *)pattern);
}

void XftDrawStringUtf8(XftDraw *draw, XftColor *color, XftFont *font, int x, int y, const XftChar8 *string, int len) {
  GC gc;
  int cw;
  int asc;
  int h;
  int bx;
  int by;
  unsigned int bw;
  unsigned int bh;
  if (!draw || !draw->display || !string || len <= 0)
    return;
  if (draw->has_clip) {
    cw = (font && font->max_advance_width > 0) ? font->max_advance_width : 8;
    asc = (font && font->ascent > 0) ? font->ascent : 12;
    h = (font && font->height > 0) ? font->height : 16;
    bx = x;
    by = y - asc;
    bw = (unsigned int)(len * cw);
    bh = (unsigned int)h;
    if (x11_clip_box(draw, &bx, &by, &bw, &bh) < 0)
      return;
  }
  gc = x11_xft_gc(draw->display, draw->drawable);
  if (!gc)
    return;
  XSetForeground(draw->display, gc, color ? color->pixel : 0x00ffffffUL);
  XDrawString(draw->display, draw->drawable, gc, x, y, (const char *)string, len);
}

void XftTextExtentsUtf8(Display *display, XftFont *font, const FcChar8 *string, int len, XGlyphInfo *extents) {
  (void)display;
  (void)string;
  
  if (extents) {
    int cw = font ? font->max_advance_width : 8;
    int ch = font ? font->height : 16;
    int asc = font ? font->ascent : 12;
    if (cw <= 0)
      cw = 8;
    if (ch <= 0)
      ch = 16;
    if (asc <= 0)
      asc = (ch * 3) / 4;
    extents->width = len * cw;
    extents->height = ch;
    extents->x = 0;
    extents->y = -asc;
    extents->xOff = len * cw;
    extents->yOff = 0;
  }
}

XRenderPictFormat *XRenderFindVisualFormat(Display *display, Visual *visual) {
  static XRenderPictFormat fmt;
  (void)visual;

  memset(&fmt, 0, sizeof(fmt));
  fmt.type = 0;
  fmt.depth = display ? display->depth : 24;
  fmt.colormap = 1;
  return &fmt;
}

XRenderPictFormat *XRenderFindStandardFormat(Display *display, int format) {
  static XRenderPictFormat argb32;
  static XRenderPictFormat rgb24;

  (void)display;

  memset(&argb32, 0, sizeof(argb32));
  argb32.type = PictStandardARGB32;
  argb32.depth = 32;
  argb32.colormap = 1;

  memset(&rgb24, 0, sizeof(rgb24));
  rgb24.type = PictStandardRGB24;
  rgb24.depth = 24;
  rgb24.colormap = 1;

  if (format == PictStandardARGB32)
    return &argb32;
  if (format == PictStandardRGB24)
    return &rgb24;
  return 0;
}

Picture XRenderCreatePicture(Display *display, Drawable drawable,
                             XRenderPictFormat *format,
                             unsigned long valuemask,
                             XRenderPictureAttributes *attributes) {
  struct x11_picture_state *ps;

  (void)display;
  (void)valuemask;
  (void)attributes;

  ps = x11_alloc_picture(drawable, format);
  if (!ps)
    return 0;
  return ps->id;
}

void XRenderFreePicture(Display *display, Picture picture) {
  struct x11_picture_state *ps;

  (void)display;
  ps = x11_find_picture(picture);
  if (!ps)
    return;
  memset(ps, 0, sizeof(*ps));
}

void XRenderComposite(Display *display, int op,
                      Picture src, Picture mask, Picture dst,
                      int src_x, int src_y,
                      int mask_x, int mask_y,
                      int dst_x, int dst_y,
                      unsigned int width, unsigned int height) {
  struct x11_picture_state *srcp;
  struct x11_picture_state *dstp;
  GC gc;

  (void)mask;
  (void)mask_x;
  (void)mask_y;

  if (!display || width == 0 || height == 0)
    return;

  srcp = x11_find_picture(src);
  dstp = x11_find_picture(dst);
  if (!srcp || !dstp)
    return;

  gc = x11_xft_gc(display, dstp->drawable);
  if (!gc)
    return;

  if (op == PictOpSrc || op == PictOpOver) {
    XCopyArea(display, srcp->drawable, dstp->drawable, gc,
              src_x, src_y, width, height, dst_x, dst_y);
    x11_notify_drawable_damage(display, dstp->drawable);
  }
}

void XRenderFillRectangle(Display *display, int op, Picture dst,
                          const XRenderColor *color,
                          int x, int y,
                          unsigned int width, unsigned int height) {
  struct x11_picture_state *dstp;
  GC gc;
  unsigned long pixel;

  (void)op;

  if (!display || !color || width == 0 || height == 0)
    return;

  dstp = x11_find_picture(dst);
  if (!dstp)
    return;

  gc = x11_xft_gc(display, dstp->drawable);
  if (!gc)
    return;

  pixel = ((unsigned long)(color->red >> 8) << 16) |
          ((unsigned long)(color->green >> 8) << 8) |
          (unsigned long)(color->blue >> 8);
  XSetForeground(display, gc, pixel);
  XFillRectangle(display, dstp->drawable, gc, x, y, width, height);
  x11_notify_drawable_damage(display, dstp->drawable);
}

void XRenderSetPictureFilter(Display *display, Picture picture,
                             const char *filter,
                             XFixed *params, int nparams) {
  (void)display;
  (void)picture;
  (void)filter;
  (void)params;
  (void)nparams;
}

void XRenderSetPictureTransform(Display *display, Picture picture,
                                const XTransform *transform) {
  (void)display;
  (void)picture;
  (void)transform;
}

Status XCompositeQueryExtension(Display *display,
                                int *event_base_return,
                                int *error_base_return) {
  (void)display;
  if (event_base_return)
    *event_base_return = X11_EXT_EVENT_BASE_COMPOSITE;
  if (error_base_return)
    *error_base_return = X11_EXT_ERROR_BASE_COMPOSITE;
  return 1;
}

Bool XQueryExtension(Display *display, const char *name,
                     int *major_opcode_return,
                     int *first_event_return,
                     int *first_error_return) {
  (void)display;

  if (!name || !*name)
    return False;

  if (strcmp(name, "Composite") == 0) {
    if (major_opcode_return) *major_opcode_return = X11_EXT_OPCODE_COMPOSITE;
    if (first_event_return) *first_event_return = X11_EXT_EVENT_BASE_COMPOSITE;
    if (first_error_return) *first_error_return = X11_EXT_ERROR_BASE_COMPOSITE;
    return True;
  }
  if (strcmp(name, "DAMAGE") == 0 || strcmp(name, "Damage") == 0) {
    if (major_opcode_return) *major_opcode_return = X11_EXT_OPCODE_DAMAGE;
    if (first_event_return) *first_event_return = X11_EXT_EVENT_BASE_DAMAGE;
    if (first_error_return) *first_error_return = X11_EXT_ERROR_BASE_DAMAGE;
    return True;
  }
  if (strcmp(name, "RANDR") == 0) {
    if (major_opcode_return) *major_opcode_return = X11_EXT_OPCODE_RANDR;
    if (first_event_return) *first_event_return = X11_EXT_EVENT_BASE_RANDR;
    if (first_error_return) *first_error_return = X11_EXT_ERROR_BASE_RANDR;
    return True;
  }
  if (strcmp(name, "SHAPE") == 0) {
    if (major_opcode_return) *major_opcode_return = X11_EXT_OPCODE_SHAPE;
    if (first_event_return) *first_event_return = X11_EXT_EVENT_BASE_SHAPE;
    if (first_error_return) *first_error_return = X11_EXT_ERROR_BASE_SHAPE;
    return True;
  }
  if (strcmp(name, "XINERAMA") == 0) {
    if (major_opcode_return) *major_opcode_return = X11_EXT_OPCODE_XINERAMA;
    if (first_event_return) *first_event_return = X11_EXT_EVENT_BASE_XINERAMA;
    if (first_error_return) *first_error_return = X11_EXT_ERROR_BASE_XINERAMA;
    return True;
  }
  if (strcmp(name, "XFIXES") == 0) {
    if (major_opcode_return) *major_opcode_return = X11_EXT_OPCODE_XFIXES;
    if (first_event_return) *first_event_return = X11_EXT_EVENT_BASE_XFIXES;
    if (first_error_return) *first_error_return = X11_EXT_ERROR_BASE_XFIXES;
    return True;
  }
  if (strcmp(name, "MIT-SHM") == 0) {
    if (major_opcode_return) *major_opcode_return = X11_EXT_OPCODE_XSHM;
    if (first_event_return) *first_event_return = X11_EXT_EVENT_BASE_XSHM;
    if (first_error_return) *first_error_return = X11_EXT_ERROR_BASE_XSHM;
    return True;
  }
  if (strcmp(name, "RENDER") == 0) {
    if (major_opcode_return) *major_opcode_return = X11_EXT_OPCODE_RENDER;
    if (first_event_return) *first_event_return = X11_EXT_EVENT_BASE_RENDER;
    if (first_error_return) *first_error_return = 0;
    return True;
  }
  if (strcmp(name, "X-Resource") == 0 || strcmp(name, "XRes") == 0) {
    if (major_opcode_return) *major_opcode_return = X11_EXT_OPCODE_XRES;
    if (first_event_return) *first_event_return = 0;
    if (first_error_return) *first_error_return = 0;
    return True;
  }

  if (major_opcode_return) *major_opcode_return = 0;
  if (first_event_return) *first_event_return = 0;
  if (first_error_return) *first_error_return = 0;
  return False;
}

Status XCompositeQueryVersion(Display *display,
                              int *major_version_return,
                              int *minor_version_return) {
  (void)display;
  if (major_version_return)
    *major_version_return = 0;
  if (minor_version_return)
    *minor_version_return = 4;
  return 1;
}

void XCompositeRedirectWindow(Display *display, Window w, int update) {
  (void)display;
  (void)x11_alloc_composite_redirect(w, update);
}

void XCompositeUnredirectWindow(Display *display, Window w, int update) {
  struct x11_composite_redirect_state *slot;

  (void)display;
  (void)update;
  slot = x11_find_composite_redirect(w);
  if (!slot)
    return;
  memset(slot, 0, sizeof(*slot));
}

Pixmap XCompositeNameWindowPixmap(Display *display, Window w) {
  struct x11_pixmap_state *pm;
  int ww;
  int wh;

  if (!display)
    return 0;
  if (x11_drawable_size(display, w, &ww, &wh) < 0)
    return 0;
  if (ww <= 0 || wh <= 0)
    return 0;

  pm = x11_alloc_pixmap((unsigned int)ww, (unsigned int)wh,
                        (unsigned int)(display->depth > 0 ? display->depth : 24));
  if (!pm)
    return 0;
  return pm->id;
}

Status XDamageQueryExtension(Display *display,
                             int *event_base_return,
                             int *error_base_return) {
  (void)display;
  if (event_base_return)
    *event_base_return = X11_EXT_EVENT_BASE_DAMAGE;
  if (error_base_return)
    *error_base_return = X11_EXT_ERROR_BASE_DAMAGE;
  return 1;
}

Status XDamageQueryVersion(Display *display,
                           int *major_version_return,
                           int *minor_version_return) {
  (void)display;
  if (major_version_return)
    *major_version_return = 1;
  if (minor_version_return)
    *minor_version_return = 1;
  return 1;
}

Damage XDamageCreate(Display *display, Drawable drawable, int level) {
  struct x11_damage_state *dmg;
  int existing;

  existing = x11_damage_count_for_drawable(drawable);

  dmg = x11_alloc_damage(drawable, level);
  if (!dmg)
    return 0;
  if (existing == 0)
    x11_damage_select_input(display, drawable, 1);
  x11_notify_drawable_damage(display, drawable);
  return dmg->id;
}

void XDamageDestroy(Display *display, Damage damage) {
  struct x11_damage_state *dmg;
  Drawable drawable;

  dmg = x11_find_damage(damage);
  if (!dmg)
    return;
  drawable = dmg->drawable;
  memset(dmg, 0, sizeof(*dmg));
  if (x11_damage_count_for_drawable(drawable) == 0)
    x11_damage_select_input(display, drawable, 0);
}

void XDamageSubtract(Display *display, Damage damage,
                     Region repair, Region parts) {
  struct x11_damage_state *dmg;

  (void)display;
  (void)repair;
  (void)parts;
  dmg = x11_find_damage(damage);
  if (!dmg)
    return;
  dmg->pending = 0;
  dmg->pending_x = 0;
  dmg->pending_y = 0;
  dmg->pending_w = 0;
  dmg->pending_h = 0;
}

Status XRRQueryExtension(Display *display, int *event_base_return,
                         int *error_base_return) {
  (void)display;
  if (event_base_return)
    *event_base_return = X11_EXT_EVENT_BASE_RANDR;
  if (error_base_return)
    *error_base_return = X11_EXT_ERROR_BASE_RANDR;
  return 1;
}

Status XRRQueryVersion(Display *display, int *major_version_return,
                       int *minor_version_return) {
  (void)display;
  if (major_version_return)
    *major_version_return = 1;
  if (minor_version_return)
    *minor_version_return = 5;
  return 1;
}

XRRScreenResources *XRRGetScreenResources(Display *display, Window window) {
  XRRScreenResources *res;

  (void)window;
  if (!display)
    return 0;

  res = (XRRScreenResources *)malloc(sizeof(*res));
  if (!res)
    return 0;
  memset(res, 0, sizeof(*res));

  res->ncrtc = 1;
  res->noutput = 1;
  res->nmode = 1;
  res->crtcs = (RRCrtc *)malloc(sizeof(RRCrtc));
  res->outputs = (RROutput *)malloc(sizeof(RROutput));
  res->modes = (RRMode *)malloc(sizeof(RRMode));
  if (!res->crtcs || !res->outputs || !res->modes) {
    free(res->crtcs);
    free(res->outputs);
    free(res->modes);
    free(res);
    return 0;
  }

  res->crtcs[0] = 1;
  res->outputs[0] = 1;
  res->modes[0] = 1;
  return res;
}

void XRRFreeScreenResources(XRRScreenResources *resources) {
  if (!resources)
    return;
  free(resources->crtcs);
  free(resources->outputs);
  free(resources->modes);
  free(resources);
}

XRROutputInfo *XRRGetOutputInfo(Display *display,
                                XRRScreenResources *resources,
                                RROutput output) {
  XRROutputInfo *info;
  const char *name = "Virtual-1";

  (void)display;
  (void)resources;
  (void)output;

  info = (XRROutputInfo *)malloc(sizeof(*info));
  if (!info)
    return 0;
  memset(info, 0, sizeof(*info));

  info->nameLen = (int)strlen(name);
  info->name = strdup(name);
  if (!info->name) {
    free(info);
    return 0;
  }
  info->crtc = 1;
  info->ncrtc = 1;
  info->crtcs = (RRCrtc *)malloc(sizeof(RRCrtc));
  info->nmode = 1;
  info->npreferred = 1;
  info->modes = (RRMode *)malloc(sizeof(RRMode));
  if (!info->crtcs || !info->modes) {
    free(info->name);
    free(info->crtcs);
    free(info->modes);
    free(info);
    return 0;
  }
  info->crtcs[0] = 1;
  info->modes[0] = 1;
  return info;
}

void XRRFreeOutputInfo(XRROutputInfo *output_info) {
  if (!output_info)
    return;
  free(output_info->name);
  free(output_info->crtcs);
  free(output_info->clones);
  free(output_info->modes);
  free(output_info);
}

XRRCrtcInfo *XRRGetCrtcInfo(Display *display,
                            XRRScreenResources *resources,
                            RRCrtc crtc) {
  XRRCrtcInfo *info;

  (void)resources;
  (void)crtc;
  if (!display)
    return 0;

  info = (XRRCrtcInfo *)malloc(sizeof(*info));
  if (!info)
    return 0;
  memset(info, 0, sizeof(*info));

  info->x = 0;
  info->y = 0;
  info->width = (unsigned int)(display->width > 0 ? display->width : 1024);
  info->height = (unsigned int)(display->height > 0 ? display->height : 768);
  info->mode = 1;
  info->noutput = 1;
  info->outputs = (RROutput *)malloc(sizeof(RROutput));
  if (!info->outputs) {
    free(info);
    return 0;
  }
  info->outputs[0] = 1;
  return info;
}

void XRRFreeCrtcInfo(XRRCrtcInfo *crtc_info) {
  if (!crtc_info)
    return;
  free(crtc_info->outputs);
  free(crtc_info->possible);
  free(crtc_info);
}

void XRRSelectInput(Display *display, Window window,
                    int mask) {
  struct x11_randr_select_state *s;
  char cmd[X6_BUF_SIZE];
  char line[X6_BUF_SIZE];

  s = x11_alloc_randr_select(window);
  if (!s)
    return;
  s->mask = mask;

  if (display) {
    snprintf(cmd, sizeof(cmd), "RANDR_SELECT_INPUT %u %d\n", (uint)window, mask);
    x11_cmd(display, cmd, line, sizeof(line));
  }

  if (display && (mask & RRScreenChangeNotifyMask))
    x11_emit_randr_screen_change(display, window);
}

int XRRUpdateConfiguration(XEvent *event) {
  if (!event)
    return 0;
  return event->type == X11_EXT_EVENT_BASE_RANDR + RRScreenChangeNotify ? 1 : 0;
}

Status XShapeQueryExtension(Display *display,
                            int *event_base_return,
                            int *error_base_return) {
  (void)display;
  if (event_base_return)
    *event_base_return = X11_EXT_EVENT_BASE_SHAPE;
  if (error_base_return)
    *error_base_return = X11_EXT_ERROR_BASE_SHAPE;
  return 1;
}

Status XShapeQueryVersion(Display *display,
                          int *major_version_return,
                          int *minor_version_return) {
  (void)display;
  if (major_version_return)
    *major_version_return = 1;
  if (minor_version_return)
    *minor_version_return = 1;
  return 1;
}

void XShapeCombineMask(Display *display, Window dest, int dest_kind,
                       int x_off, int y_off, Pixmap src, int op) {
  struct x11_shape_state *s;

  (void)op;
  s = x11_alloc_shape(display, dest);
  if (!s)
    return;

  if (dest_kind == ShapeBounding) {
    s->bounding_shaped = src ? 1 : 0;
    s->x_bounding = x_off;
    s->y_bounding = y_off;
    x11_emit_shape_notify(display, s, ShapeBounding);
  } else if (dest_kind == ShapeClip) {
    s->clip_shaped = src ? 1 : 0;
    s->x_clip = x_off;
    s->y_clip = y_off;
    x11_emit_shape_notify(display, s, ShapeClip);
  }
}

void XShapeCombineShape(Display *display, Window dest, int dest_kind,
                        int x_off, int y_off,
                        Window src, int src_kind, int op) {
  struct x11_shape_state *s;
  struct x11_shape_state *ssrc;

  (void)op;
  s = x11_alloc_shape(display, dest);
  if (!s)
    return;

  ssrc = x11_find_shape(src);
  if (dest_kind == ShapeBounding) {
    s->bounding_shaped = ssrc ? ssrc->bounding_shaped : 1;
    s->x_bounding = x_off;
    s->y_bounding = y_off;
    if (ssrc && src_kind == ShapeBounding) {
      s->w_bounding = ssrc->w_bounding;
      s->h_bounding = ssrc->h_bounding;
    }
    x11_emit_shape_notify(display, s, ShapeBounding);
  } else if (dest_kind == ShapeClip) {
    s->clip_shaped = ssrc ? ssrc->clip_shaped : 1;
    s->x_clip = x_off;
    s->y_clip = y_off;
    if (ssrc && src_kind == ShapeClip) {
      s->w_clip = ssrc->w_clip;
      s->h_clip = ssrc->h_clip;
    }
    x11_emit_shape_notify(display, s, ShapeClip);
  }
}

void XShapeCombineRectangles(Display *display, Window dest, int dest_kind,
                             int x_off, int y_off,
                             XRectangle *rectangles, int n_rectangles,
                             int op, int ordering) {
  struct x11_shape_state *s;
  int i;
  int minx;
  int miny;
  int maxx;
  int maxy;

  (void)op;
  (void)ordering;

  s = x11_alloc_shape(display, dest);
  if (!s)
    return;

  if (!rectangles || n_rectangles <= 0) {
    if (dest_kind == ShapeBounding)
      s->bounding_shaped = 0;
    else if (dest_kind == ShapeClip)
      s->clip_shaped = 0;
    if (dest_kind == ShapeBounding)
      x11_emit_shape_notify(display, s, ShapeBounding);
    else if (dest_kind == ShapeClip)
      x11_emit_shape_notify(display, s, ShapeClip);
    return;
  }

  minx = x_off + rectangles[0].x;
  miny = y_off + rectangles[0].y;
  maxx = minx + rectangles[0].width;
  maxy = miny + rectangles[0].height;
  for (i = 1; i < n_rectangles; i++) {
    int rx = x_off + rectangles[i].x;
    int ry = y_off + rectangles[i].y;
    int rx2 = rx + rectangles[i].width;
    int ry2 = ry + rectangles[i].height;
    if (rx < minx) minx = rx;
    if (ry < miny) miny = ry;
    if (rx2 > maxx) maxx = rx2;
    if (ry2 > maxy) maxy = ry2;
  }

  if (dest_kind == ShapeBounding) {
    s->bounding_shaped = 1;
    s->x_bounding = minx;
    s->y_bounding = miny;
    s->w_bounding = (unsigned int)((maxx > minx) ? (maxx - minx) : 0);
    s->h_bounding = (unsigned int)((maxy > miny) ? (maxy - miny) : 0);
    x11_emit_shape_notify(display, s, ShapeBounding);
  } else if (dest_kind == ShapeClip) {
    s->clip_shaped = 1;
    s->x_clip = minx;
    s->y_clip = miny;
    s->w_clip = (unsigned int)((maxx > minx) ? (maxx - minx) : 0);
    s->h_clip = (unsigned int)((maxy > miny) ? (maxy - miny) : 0);
    x11_emit_shape_notify(display, s, ShapeClip);
  }
}

Status XShapeQueryExtents(Display *display, Window window,
                          Bool *bounding_shaped,
                          int *x_bounding, int *y_bounding,
                          unsigned int *w_bounding,
                          unsigned int *h_bounding,
                          Bool *clip_shaped,
                          int *x_clip, int *y_clip,
                          unsigned int *w_clip,
                          unsigned int *h_clip) {
  struct x11_shape_state *s;
  int ww;
  int wh;

  s = x11_find_shape(window);
  ww = 0;
  wh = 0;
  if (!s && display && x11_drawable_size(display, window, &ww, &wh) == 0) {
    if (w_bounding) *w_bounding = (unsigned int)ww;
    if (h_bounding) *h_bounding = (unsigned int)wh;
    if (w_clip) *w_clip = (unsigned int)ww;
    if (h_clip) *h_clip = (unsigned int)wh;
  }

  if (bounding_shaped) *bounding_shaped = s ? s->bounding_shaped : 0;
  if (x_bounding) *x_bounding = s ? s->x_bounding : 0;
  if (y_bounding) *y_bounding = s ? s->y_bounding : 0;
  if (w_bounding && s) *w_bounding = s->w_bounding;
  if (h_bounding && s) *h_bounding = s->h_bounding;

  if (clip_shaped) *clip_shaped = s ? s->clip_shaped : 0;
  if (x_clip) *x_clip = s ? s->x_clip : 0;
  if (y_clip) *y_clip = s ? s->y_clip : 0;
  if (w_clip && s) *w_clip = s->w_clip;
  if (h_clip && s) *h_clip = s->h_clip;
  return 1;
}

void XShapeSelectInput(Display *display, Window window,
                       unsigned long mask) {
  struct x11_shape_state *s;
  char cmd[X6_BUF_SIZE];
  char line[X6_BUF_SIZE];

  s = x11_alloc_shape(display, window);
  if (!s)
    return;
  s->event_mask = mask;

  if (display) {
    snprintf(cmd, sizeof(cmd), "SHAPE_SELECT_INPUT %u %lu\n", (uint)window, mask);
    x11_cmd(display, cmd, line, sizeof(line));
  }
}

Status XineramaQueryExtension(Display *display,
                              int *event_base_return,
                              int *error_base_return) {
  (void)display;
  if (event_base_return)
    *event_base_return = X11_EXT_EVENT_BASE_XINERAMA;
  if (error_base_return)
    *error_base_return = X11_EXT_ERROR_BASE_XINERAMA;
  return 1;
}

Status XineramaQueryVersion(Display *display,
                            int *major_version_return,
                            int *minor_version_return) {
  (void)display;
  if (major_version_return)
    *major_version_return = 1;
  if (minor_version_return)
    *minor_version_return = 1;
  return 1;
}

Bool XineramaIsActive(Display *display) {
  (void)display;
  return True;
}

XineramaScreenInfo *XineramaQueryScreens(Display *display, int *number) {
  XineramaScreenInfo *info;
  int w;
  int h;

  w = (display && display->width > 0) ? display->width : 1024;
  h = (display && display->height > 0) ? display->height : 768;

  info = (XineramaScreenInfo *)malloc(sizeof(*info));
  if (!info) {
    if (number)
      *number = 0;
    return 0;
  }
  memset(info, 0, sizeof(*info));
  info->screen_number = 0;
  info->x_org = 0;
  info->y_org = 0;
  info->width = (short)w;
  info->height = (short)h;
  if (number)
    *number = 1;
  return info;
}

Bool XFixesQueryExtension(Display *display, int *event_base_return,
                          int *error_base_return) {
  (void)display;
  if (event_base_return)
    *event_base_return = X11_EXT_EVENT_BASE_XFIXES;
  if (error_base_return)
    *error_base_return = X11_EXT_ERROR_BASE_XFIXES;
  return True;
}

Status XFixesQueryVersion(Display *display, int *major_version_return,
                          int *minor_version_return) {
  (void)display;
  if (major_version_return)
    *major_version_return = 5;
  if (minor_version_return)
    *minor_version_return = 0;
  return 1;
}

Bool XShmQueryExtension(Display *display) {
  (void)display;
  return True;
}

int XShmGetEventBase(Display *display) {
  (void)display;
  return X11_EXT_EVENT_BASE_XSHM;
}

XImage *XShmCreateImage(Display *display, Visual *visual, unsigned int depth,
                        int format, char *data,
                        XShmSegmentInfo *shminfo,
                        unsigned int width, unsigned int height) {
  char *buf;
  XImage *img;

  buf = data;
  if (!buf && shminfo)
    buf = shminfo->shmaddr;
  img = XCreateImage(display, visual, depth, format, 0, buf,
                     width, height, 32, 0);
  if (img)
    x11_track_shm_image(img, shminfo ? shminfo->shmseg : 0);
  return img;
}

Bool XShmAttach(Display *display, XShmSegmentInfo *shminfo) {
  (void)display;
  if (!shminfo)
    return False;
  return True;
}

Bool XShmDetach(Display *display, XShmSegmentInfo *shminfo) {
  (void)display;
  (void)shminfo;
  return True;
}

Bool XShmPutImage(Display *display, Drawable d, GC gc, XImage *image,
                  int src_x, int src_y, int dst_x, int dst_y,
                  unsigned int src_width, unsigned int src_height,
                  Bool send_event) {
  int rc;

  rc = XPutImage(display, d, gc, image,
                 src_x, src_y, dst_x, dst_y,
                 src_width, src_height);
  if (rc == 0 && send_event)
    x11_emit_shm_completion(display, d, image);
  return rc == 0;
}

Bool XShmGetImage(Display *display, Drawable d, XImage *image,
                  int x, int y, unsigned long plane_mask) {
  XImage *tmp;
  size_t nbytes;

  if (!display || !image || image->width <= 0 || image->height <= 0)
    return False;

  tmp = XGetImage(display, d, x, y,
                  (unsigned int)image->width,
                  (unsigned int)image->height,
                  plane_mask, image->format);
  if (!tmp)
    return False;
  if (!image->data || !tmp->data) {
    XDestroyImage(tmp);
    return False;
  }

  nbytes = (size_t)tmp->bytes_per_line * (size_t)tmp->height;
  memmove(image->data, tmp->data, nbytes);
  XDestroyImage(tmp);
  return True;
}

Status XResQueryClientIds(Display *display, long num_specs,
                          XResClientIdSpec *client_specs,
                          long *num_ids, XResClientIdValue **client_ids) {
  XResClientIdValue *out;
  long pid;

  (void)display;

  if (!num_ids || !client_ids)
    return 0;
  *num_ids = 0;
  *client_ids = 0;

  if (num_specs < 0)
    return 0;
  if (num_specs == 0)
    return 1;

  out = (XResClientIdValue *)malloc((size_t)num_specs * sizeof(*out));
  if (!out)
    return 0;
  memset(out, 0, (size_t)num_specs * sizeof(*out));

  pid = (long)getpid();
  out[0].spec = client_specs ? client_specs[0].spec : 0;
  out[0].length = (long)sizeof(long);
  out[0].value = (unsigned char *)malloc(sizeof(long));
  if (!out[0].value) {
    free(out);
    return 0;
  }
  memmove(out[0].value, &pid, sizeof(long));

  *num_ids = 1;
  *client_ids = out;
  return 1;
}

Status XResGetClientPid(Display *display, XID resource_base,
                        long *pid_return) {
  (void)display;
  (void)resource_base;
  if (!pid_return)
    return 0;
  *pid_return = (long)getpid();
  return 1;
}

void XResClientIdsDestroy(long num_ids, XResClientIdValue *client_ids) {
  long i;

  if (!client_ids)
    return;
  for (i = 0; i < num_ids; i++)
    free(client_ids[i].value);
  free(client_ids);
}

FcCharSet *FcCharSetCreate(void) {
  return (FcCharSet *)malloc(1);
}

void FcCharSetDestroy(FcCharSet *fcs) {
  free(fcs);
}

FcBool FcCharSetAddChar(FcCharSet *fcs, FcChar32 ucs4) {
  (void)fcs;
  (void)ucs4;
  return 1;
}

FcPattern *FcNameParse(const FcChar8 *name) {
  struct x11_fc_pattern *p;
  p = x11_fc_pattern_new();
  if (!p)
    return 0;
  if (name) {
    strncpy(p->name, (const char *)name, sizeof(p->name) - 1);
    p->name[sizeof(p->name) - 1] = '\0';
  }
  return (FcPattern *)p;
}

void FcPatternDestroy(FcPattern *p) {
  free(p);
}

void FcFontSetDestroy(FcFontSet *ffs) {
  free(ffs);
}

FcPattern *FcPatternDuplicate(FcPattern *p) {
  struct x11_fc_pattern *src;
  struct x11_fc_pattern *dup;
  src = (struct x11_fc_pattern *)p;
  dup = x11_fc_pattern_new();
  if (!dup)
    return 0;
  if (src)
    *dup = *src;
  return (FcPattern *)dup;
}

FcBool FcPatternAddCharSet(FcPattern *p, const char *object, FcCharSet *charset) {
  (void)p;
  (void)object;
  (void)charset;
  return 1;
}

FcBool FcPatternAddBool(FcPattern *p, const char *object, FcBool b) {
  (void)p;
  (void)object;
  (void)b;
  return 1;
}

FcBool FcConfigSubstitute(void *config, FcPattern *p, FcMatchKind kind) {
  (void)config;
  (void)p;
  (void)kind;
  return 1;
}

void FcDefaultSubstitute(FcPattern *pattern) {
  struct x11_fc_pattern *p;
  p = (struct x11_fc_pattern *)pattern;
  if (!p)
    return;
  if (!p->has_pixel_size) {
    p->pixel_size = 12.0;
    p->has_pixel_size = 1;
  }
}

FcPattern *FcFontSetMatch(void *config, FcFontSet **sets, int nsets, FcPattern *p, FcResult *result) {
  (void)config;
  (void)sets;
  (void)nsets;
  if (result)
    *result = FcResultMatch;
  return FcPatternDuplicate(p);
}

FcFontSet *FcFontSort(void *config, FcPattern *pattern, FcBool trim, FcCharSet **csp, FcResult *result) {
  FcFontSet *fs = (FcFontSet *)malloc(sizeof(*fs));
  (void)config;
  (void)pattern;
  (void)trim;
  (void)csp;
  if (result)
    *result = FcResultMatch;
  return fs;
}

FcPattern *FcFontMatch(void *config, FcPattern *p, FcResult *result) {
  (void)config;
  if (result)
    *result = FcResultMatch;
  return FcPatternDuplicate(p);
}

FcBool FcPatternDel(FcPattern *p, const char *object) {
  struct x11_fc_pattern *fp;
  fp = (struct x11_fc_pattern *)p;
  if (!fp || !object)
    return 0;
  if (!strcmp(object, FC_PIXEL_SIZE) || !strcmp(object, FC_SIZE))
    fp->has_pixel_size = 0;
  if (!strcmp(object, FC_SLANT))
    fp->has_slant = 0;
  if (!strcmp(object, FC_WEIGHT))
    fp->has_weight = 0;
  return 1;
}

FcBool FcPatternAddDouble(FcPattern *p, const char *object, double d) {
  struct x11_fc_pattern *fp;
  fp = (struct x11_fc_pattern *)p;
  if (!fp || !object)
    return 0;
  if (!strcmp(object, FC_PIXEL_SIZE) || !strcmp(object, FC_SIZE)) {
    fp->pixel_size = d;
    fp->has_pixel_size = 1;
  }
  return 1;
}

FcBool FcPatternAddInteger(FcPattern *p, const char *object, int i) {
  struct x11_fc_pattern *fp;
  fp = (struct x11_fc_pattern *)p;
  if (!fp || !object)
    return 0;
  if (!strcmp(object, FC_SLANT)) {
    fp->slant = i;
    fp->has_slant = 1;
  } else if (!strcmp(object, FC_WEIGHT)) {
    fp->weight = i;
    fp->has_weight = 1;
  }
  return 1;
}

FcResult FcPatternGetDouble(FcPattern *p, const char *object, int id, double *d) {
  struct x11_fc_pattern *fp;
  (void)id;
  fp = (struct x11_fc_pattern *)p;
  if (!fp || !object || !d)
    return FcResultNoMatch;
  if ((!strcmp(object, FC_PIXEL_SIZE) || !strcmp(object, FC_SIZE)) && fp->has_pixel_size) {
    *d = fp->pixel_size;
    return FcResultMatch;
  }
  return FcResultNoMatch;
}

XftPattern *XftXlfdParse(const char *xlfd, int expand, FcBool ignore_scalable) {
  XftPattern *p;
  (void)expand;
  (void)ignore_scalable;
  p = (XftPattern *)FcNameParse((const FcChar8 *)xlfd);
  return p;
}

int XDefaultDepth(Display *display, int screen) {
  (void)screen;
  if (!display)
    return 24;
  return display->depth;
}

unsigned long XBlackPixel(Display *display, int screen_number) {
  (void)display;
  (void)screen_number;
  return 0x000000UL;
}

unsigned long XWhitePixel(Display *display, int screen_number) {
  (void)display;
  (void)screen_number;
  return 0x00ffffffUL;
}

Region XCreateRegion(void) {
  Region r;

  r = (Region)malloc(sizeof(*r));
  if (!r)
    return 0;
  memset(r, 0, sizeof(*r));
  return r;
}

int XUnionRectWithRegion(XRectangle *rectangle, Region src_region,
                         Region dest_region) {
  int x1;
  int y1;
  int x2;
  int y2;

  if (!dest_region)
    return 0;

  if (!rectangle && !src_region)
    return 0;

  if (!rectangle) {
    *dest_region = *src_region;
    return 1;
  }
  if (!src_region) {
    dest_region->extents = *rectangle;
    return 1;
  }

  x1 = rectangle->x < src_region->extents.x ? rectangle->x : src_region->extents.x;
  y1 = rectangle->y < src_region->extents.y ? rectangle->y : src_region->extents.y;
  x2 = (rectangle->x + (int)rectangle->width) > (src_region->extents.x + (int)src_region->extents.width)
         ? (rectangle->x + (int)rectangle->width)
         : (src_region->extents.x + (int)src_region->extents.width);
  y2 = (rectangle->y + (int)rectangle->height) > (src_region->extents.y + (int)src_region->extents.height)
         ? (rectangle->y + (int)rectangle->height)
         : (src_region->extents.y + (int)src_region->extents.height);

  dest_region->extents.x = (short)x1;
  dest_region->extents.y = (short)y1;
  dest_region->extents.width = (unsigned short)(x2 - x1);
  dest_region->extents.height = (unsigned short)(y2 - y1);
  return 1;
}

int XReparentWindow(Display *display, Window w, Window parent, int x, int y) {
  char cmd[X6_BUF_SIZE];
  char line[X6_BUF_SIZE];

  if (!display)
    return -1;

  snprintf(cmd, sizeof(cmd), "REPARENT %u %u %d %d\n",
           (uint)w,
           (uint)(parent ? parent : display->root),
           x, y);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  return strncmp(line, "OK reparent", 11) == 0 ? 0 : -1;
}

int XRegisterIMInstantiateCallback(Display *display, void *rdb, char *res_name, char *res_class, void (*callback)(Display *, XPointer, XPointer), void *client_data) {
  (void)display;
  (void)rdb;
  (void)res_name;
  (void)res_class;
  (void)callback;
  (void)client_data;
  return 1;
}

int XSetWMProtocols(Display *display, Window w, Atom *protocols, int count) {
  Atom wm_protocols;
  if (!display)
    return 0;
  wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
  if (!protocols || count <= 0)
    return XDeleteProperty(display, w, wm_protocols) == 0;
  return XChangeProperty(display, w, wm_protocols, XA_ATOM, 32,
                         PropModeReplace, (unsigned char *)protocols, count) == 0;
}

int XConnectionNumber(Display *display) {
  if (!display)
    return -1;
  return display->fd;
}

Bool XFilterEvent(XEvent *event, Window w) {
  (void)event;
  (void)w;
  return False;
}

int XParseGeometry(const char *parsestring, int *x_return, int *y_return, unsigned int *width_return, unsigned int *height_return) {
  int x;
  int y;
  int w;
  int h;

  if (!parsestring)
    return 0;

  x = 0;
  y = 0;
  w = 0;
  h = 0;
  if (sscanf(parsestring, "%dx%d+%d+%d", &w, &h, &x, &y) == 4) {
    if (x_return) *x_return = x;
    if (y_return) *y_return = y;
    if (width_return) *width_return = (unsigned int)w;
    if (height_return) *height_return = (unsigned int)h;
    return XValue | YValue | WidthValue | HeightValue;
  }
  if (sscanf(parsestring, "%dx%d", &w, &h) == 2) {
    if (width_return) *width_return = (unsigned int)w;
    if (height_return) *height_return = (unsigned int)h;
    return WidthValue | HeightValue;
  }
  return 0;
}

/* POSIX select wrapper for st */
int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
            const struct timespec *timeout, const sigset_t *sigmask) {
  struct timeval tv, *tvp = NULL;
  
  (void)sigmask; /* ignore signal mask for this stub */
  
  if (timeout) {
    tv.tv_sec = timeout->tv_sec;
    tv.tv_usec = timeout->tv_nsec / 1000;
    tvp = &tv;
  }
  
  return select(nfds, readfds, writefds, exceptfds, tvp);
}

/* Additional X11 functions for st */
int XSetWMName(Display *display, Window w, void *text_prop) {
  XTextProperty *tp;
  tp = (XTextProperty *)text_prop;
  if (!tp)
    return 0;
  return XChangeProperty(display, w, XA_WM_NAME,
                         tp->encoding ? tp->encoding : XA_STRING,
                         tp->format ? tp->format : 8,
                         PropModeReplace,
                         (unsigned char *)tp->value,
                         (int)tp->nitems) == 0;
}

int XSetTextProperty(Display *display, Window w, void *text_prop, Atom property) {
  XTextProperty *tp;
  tp = (XTextProperty *)text_prop;
  if (!tp)
    return 0;
  return XChangeProperty(display, w, property,
                         tp->encoding ? tp->encoding : XA_STRING,
                         tp->format ? tp->format : 8,
                         PropModeReplace,
                         (unsigned char *)tp->value,
                         (int)tp->nitems) == 0;
}

int Xutf8TextListToTextProperty(Display *display, char **list, int count, XICCEncodingStyle style, void *text_prop_return) {
  (void)style;
  if (!list || count <= 0 || !list[0] || !text_prop_return)
    return 1;
  {
    XTextProperty *tp = (XTextProperty *)text_prop_return;
    tp->value = strdup(list[0]);
    if (!tp->value)
      return 1;
    tp->encoding = display ? XInternAtom(display, "UTF8_STRING", False) : XA_STRING;
    tp->format = 8;
    tp->nitems = strlen(tp->value);
  }
  return 0;
}

int XSetICValues(XIC ic, ...) {
  (void)ic;
  return 0;
}

int XSetWMIconName(Display *display, Window w, void *text_prop) {
  return XSetTextProperty(display, w, text_prop,
                          XInternAtom(display, "WM_ICON_NAME", False));
}

char *XSetLocaleModifiers(const char *modifier_list) {
  static char mods[64];
  if (!modifier_list)
    return "";
  strncpy(mods, modifier_list, sizeof(mods) - 1);
  mods[sizeof(mods) - 1] = '\0';
  return mods;
}

XIC XCreateIC(XIM im, ...) {
  (void)im;
  return (XIC)malloc(1);
}

int XUnregisterIMInstantiateCallback(Display *display, void *rdb, char *res_name, char *res_class, void (*callback)(Display *, XPointer, XPointer), void *client_data) {
  (void)display;
  (void)rdb;
  (void)res_name;
  (void)res_class;
  (void)callback;
  (void)client_data;
  return 1;
}

XIM XOpenIM(Display *display, void *rdb, char *res_name, char *res_class) {
  (void)display;
  (void)rdb;
  (void)res_name;
  (void)res_class;
  return (XIM)malloc(1);
}

int XSetIMValues(XIM im, ...) {
  (void)im;
  return 0;
}

void *XVaCreateNestedList(int dummy, ...) {
  (void)dummy;
  return malloc(1);
}

void *XAllocSizeHints(void) {
  return malloc(sizeof(int) * 18);  /* Approximate size of XSizeHints */
}

int XSetWMProperties(Display *display, Window w, void *window_name, void *icon_name, char **argv, int argc, void *normal_hints, void *wm_hints, void *class_hints) {
  int i;
  int ok;

  if (!display)
    return 0;

  ok = 1;
  if (window_name)
    ok &= XSetWMName(display, w, window_name);
  if (icon_name)
    ok &= XSetWMIconName(display, w, icon_name);
  if (normal_hints)
    ok &= XSetWMNormalHints(display, w, (XSizeHints *)normal_hints);
  if (wm_hints)
    ok &= XSetWMHints(display, w, (XWMHints *)wm_hints);
  if (class_hints)
    ok &= XSetClassHint(display, w, (XClassHint *)class_hints);

  if (argv && argc > 0) {
    int len = 0;
    char *buf;
    Atom wm_command;
    for (i = 0; i < argc; i++)
      len += (int)strlen(argv[i]) + 1;
    buf = (char *)malloc((size_t)len + 1);
    if (buf) {
      int off = 0;
      for (i = 0; i < argc; i++) {
        int n = (int)strlen(argv[i]);
        memmove(buf + off, argv[i], (size_t)n);
        off += n;
        buf[off++] = '\0';
      }
      buf[off] = '\0';
      wm_command = XInternAtom(display, "WM_COMMAND", False);
      ok &= XChangeProperty(display, w, wm_command, XA_STRING, 8,
                            PropModeReplace, (unsigned char *)buf, off) == 0;
      free(buf);
    }
  }

  return ok;
}

int XmbLookupString(XIC ic, XKeyEvent *event, char *buffer, int nbytes, KeySym *keysym, void *status) {
  (void)ic;
  return XLookupString(event, buffer, nbytes, keysym, status);
}

int XSetICFocus(XIC ic) {
  (void)ic;
  return 0;
}

int XUnsetICFocus(XIC ic) {
  (void)ic;
  return 0;
}

int XDefaultScreen(Display *display) {
  if (!display)
    return 0;
  return display->screen;
}

Visual *XDefaultVisual(Display *display, int screen) {
  (void)screen;
  static Visual v = {
    .visualid = 0,
    .class = 1,
    .red_mask = 0xFF0000,
    .green_mask = 0x00FF00,
    .blue_mask = 0x0000FF,
    .bits_per_rgb = 8,
    .map_entries = 256
  };
  (void)display;
  return &v;
}

FcBool FcInit(void) {
  return 1;
}

Colormap XDefaultColormap(Display *display, int screen) {
  (void)screen;
  if (!display)
    return 1;
  return 1;
}

Window XRootWindow(Display *display, int screen) {
  (void)screen;
  if (!display)
    return 1;
  return display->root;
}

int XRecolorCursor(Display *display, Cursor cursor, XColor *foreground, XColor *background) {
  (void)display;
  (void)cursor;
  (void)foreground;
  (void)background;
  return 0;
}

static char *
x11_xrm_strdup_range(const char *start, int n)
{
  char *s;

  if(!start || n < 0)
    return 0;
  s = (char*)malloc((size_t)n + 1);
  if(!s)
    return 0;
  if(n > 0)
    memmove(s, start, (size_t)n);
  s[n] = '\0';
  return s;
}

static void
x11_xrm_trim_span(const char **pstart, int *plen)
{
  const char *s;
  int n;

  if(!pstart || !plen)
    return;
  s = *pstart;
  n = *plen;
  while(n > 0 && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) {
    s++;
    n--;
  }
  while(n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
    n--;
  *pstart = s;
  *plen = n;
}

static int
x11_xrm_find_entry(XrmDatabase db, const char *name)
{
  int i;

  if(!db || !name)
    return -1;
  for(i = 0; i < db->count; i++) {
    if(db->entries[i].name && strcmp(db->entries[i].name, name) == 0)
      return i;
  }
  return -1;
}

static int
x11_xrm_put_entry(XrmDatabase db, const char *name, const char *value)
{
  int idx;
  char *ncopy;
  char *vcopy;

  if(!db || !name || !value)
    return -1;

  idx = x11_xrm_find_entry(db, name);
  if(idx >= 0) {
    vcopy = strdup(value);
    if(!vcopy)
      return -1;
    if(db->entries[idx].value)
      free(db->entries[idx].value);
    db->entries[idx].value = vcopy;
    return 0;
  }

  if(db->count >= X11_XRM_MAX_ENTRIES)
    return -1;

  ncopy = strdup(name);
  vcopy = strdup(value);
  if(!ncopy || !vcopy) {
    if(ncopy)
      free(ncopy);
    if(vcopy)
      free(vcopy);
    return -1;
  }

  db->entries[db->count].name = ncopy;
  db->entries[db->count].value = vcopy;
  db->count++;
  return 0;
}

void
XrmInitialize(void)
{
}

char *
XResourceManagerString(Display *display)
{
  Atom prop_atom;
  Atom actual;
  int format;
  unsigned long nitems;
  unsigned long bytes_after;
  unsigned char *prop;

  if(!display)
    return 0;

  prop_atom = XInternAtom(display, "RESOURCE_MANAGER", False);
  prop = 0;
  if(XGetWindowProperty(display, display->root, prop_atom,
                        0, 1L << 20, False, XA_STRING,
                        &actual, &format, &nitems, &bytes_after,
                        &prop) != Success)
    return g_xrm_root_string;

  if(actual == XA_STRING && format == 8 && prop) {
    char *tmp = strdup((const char*)prop);
    if(tmp) {
      if(g_xrm_root_string)
        free(g_xrm_root_string);
      g_xrm_root_string = tmp;
    }
  }
  if(prop)
    XFree(prop);
  return g_xrm_root_string;
}

XrmDatabase
XrmGetStringDatabase(const char *data)
{
  XrmDatabase db;
  const char *p;

  db = (XrmDatabase)malloc(sizeof(*db));
  if(!db)
    return 0;
  memset(db, 0, sizeof(*db));

  if(!data)
    return db;

  p = data;
  while(*p) {
    const char *line = p;
    const char *sep = 0;
    int len;
    int i;

    while(*p && *p != '\n')
      p++;
    len = (int)(p - line);
    if(*p == '\n')
      p++;

    x11_xrm_trim_span(&line, &len);
    if(len <= 0)
      continue;
    if(line[0] == '!' || line[0] == '#')
      continue;

    for(i = 0; i < len; i++) {
      if(line[i] == ':' || line[i] == '=') {
        sep = line + i;
        break;
      }
    }
    if(!sep)
      continue;

    {
      const char *k = line;
      int klen = (int)(sep - line);
      const char *v = sep + 1;
      int vlen = len - klen - 1;
      char *ks;
      char *vs;

      x11_xrm_trim_span(&k, &klen);
      x11_xrm_trim_span(&v, &vlen);
      if(klen <= 0)
        continue;

      ks = x11_xrm_strdup_range(k, klen);
      vs = x11_xrm_strdup_range(v, vlen > 0 ? vlen : 0);
      if(!ks || !vs) {
        if(ks)
          free(ks);
        if(vs)
          free(vs);
        continue;
      }
      x11_xrm_put_entry(db, ks, vs);
      free(ks);
      free(vs);
    }
  }

  return db;
}

void
XrmDestroyDatabase(XrmDatabase database)
{
  int i;

  if(!database)
    return;
  for(i = 0; i < database->count; i++) {
    if(database->entries[i].name)
      free(database->entries[i].name);
    if(database->entries[i].value)
      free(database->entries[i].value);
  }
  free(database);
}

Bool
XrmGetResource(XrmDatabase database, const char *str_name,
               const char *str_class, char **str_type_return,
               XrmValue *value_return)
{
  int idx;

  if(!database || !value_return)
    return False;

  idx = -1;
  if(str_name)
    idx = x11_xrm_find_entry(database, str_name);
  if(idx < 0 && str_class)
    idx = x11_xrm_find_entry(database, str_class);
  if(idx < 0)
    return False;

  value_return->addr = database->entries[idx].value;
  value_return->size = database->entries[idx].value
                         ? (unsigned int)strlen(database->entries[idx].value) + 1
                         : 0;
  if(str_type_return)
    *str_type_return = "String";
  return True;
}

void
XrmPutStringResource(XrmDatabase *database, const char *specifier,
                     const char *value)
{
  if(!database || !specifier || !value)
    return;
  if(!*database) {
    *database = (XrmDatabase)malloc(sizeof(**database));
    if(!*database)
      return;
    memset(*database, 0, sizeof(**database));
  }
  x11_xrm_put_entry(*database, specifier, value);
}

void
XrmMergeDatabases(XrmDatabase source_db, XrmDatabase *target_db)
{
  int i;

  if(!source_db || !target_db)
    return;
  if(!*target_db) {
    *target_db = source_db;
    return;
  }
  for(i = 0; i < source_db->count; i++) {
    if(source_db->entries[i].name && source_db->entries[i].value)
      x11_xrm_put_entry(*target_db, source_db->entries[i].name,
                        source_db->entries[i].value);
  }
  XrmDestroyDatabase(source_db);
}


