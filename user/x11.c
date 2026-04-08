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
#include "X11/Xlib.h"
#include "X11/Xutil.h"
#include "X11/keysym.h"
#include "X11/Xft/Xft.h"

#define X6_PORT 6006
#define X6_BUF_SIZE 1024
#define X11_RXBUF_SIZE 4096
#define X11_MAX_ATOMS 256
#define X11_MAX_GCS 128
#define X11_MAX_PIXMAPS 256
#define X11_MAX_FONTS 16
#define DoRed 1
#define DoGreen 2
#define DoBlue 4

typedef struct {
  Atom atom;
  char name[64];
} atom_entry;

static Display *g_display;
static unsigned long g_next_atom = 128;
static atom_entry g_atoms[X11_MAX_ATOMS];
static int g_atom_count;
static int (*g_error_handler)(Display *, XErrorEvent *);
static int g_is_wm;
static XEvent g_pending_event;
static int g_has_pending_event;
static unsigned long g_next_gc = 1;
static char g_rxbuf[X11_RXBUF_SIZE];
static int g_rxlen;
static int g_pending_draw_replies;

struct x11_gc_state {
  int in_use;
  GC id;
  unsigned long fg;
};

struct x11_pixmap_state {
  int in_use;
  Pixmap id;
  unsigned int width;
  unsigned int height;
  unsigned int depth;
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

/* XFontStruct - minimal definition for auxv6 */
typedef struct {
  Font fid;
  int ascent;
  int descent;
  struct {
    int width;
  } max_bounds;
  void *per_char;  /* Not used in our impl but in X protocol */
} XFontStruct;

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

static struct x11_pixmap_state *
x11_alloc_pixmap(unsigned int width, unsigned int height, unsigned int depth)
{
  int i;
  if (width == 0 || height == 0)
    return 0;
  for (i = 0; i < X11_MAX_PIXMAPS; i++) {
    if (!g_pixmaps[i].in_use) {
      g_pixmaps[i].in_use = 1;
      g_pixmaps[i].id = g_next_pixmap++;
      g_pixmaps[i].width = width;
      g_pixmaps[i].height = height;
      g_pixmaps[i].depth = depth;
      return &g_pixmaps[i];
    }
  }
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

static int x11_read_line(int fd, char *line, int maxlen);
static int x11_parse_event_line(Display *display, const char *line, XEvent *event);

static int
x11_handle_unsolicited_line(Display *display, const char *line)
{
  if (!display || !line)
    return 0;

  if (strncmp(line, "EVENT ", 6) == 0) {
    if (!g_has_pending_event && x11_parse_event_line(display, line, &g_pending_event) == 0)
      g_has_pending_event = 1;
    return 1;
  }

  if ((strncmp(line, "OK draw", 7) == 0 || strncmp(line, "OK text", 7) == 0) &&
      g_pending_draw_replies > 0) {
    g_pending_draw_replies--;
    return 1;
  }

  return 0;
}

static int
x11_drain_draw_replies(Display *display)
{
  char line[X6_BUF_SIZE];

  if (!display)
    return -1;

  while (g_pending_draw_replies > 0) {
    if (x11_read_line(display->fd, line, sizeof(line)) < 0)
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

  memset(event, 0, sizeof(*event));
  if (strncmp(line + 6, "MapRequest", 10) == 0) {
    event->type = MapRequest;
    sscanf(line, "EVENT MapRequest wid=%u", &event->xmaprequest.window);
    event->xmaprequest.parent = display->root;
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
    sscanf(line, "EVENT FocusIn wid=%u", &event->xfocus.window);
    return 0;
  }
  if (strncmp(line + 6, "FocusOut", 8) == 0) {
    event->type = FocusOut;
    sscanf(line, "EVENT FocusOut wid=%u", &event->xfocus.window);
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
  case Expose:
    return ExposureMask;
  case MapRequest:
  case ConfigureRequest:
    return SubstructureRedirectMask;
  case FocusIn:
  case FocusOut:
    return FocusChangeMask;
  case DestroyNotify:
  case ConfigureNotify:
    return StructureNotifyMask;
  default:
    return 0;
  }
}

static int
x11_read_event(Display *display, XEvent *event)
{
  char line[X6_BUF_SIZE];
  if (!display || !event)
    return -1;

  while (1) {
    if (x11_read_line(display->fd, line, sizeof(line)) < 0)
      return -1;
    if (x11_handle_unsolicited_line(display, line)) {
      if (g_has_pending_event) {
        *event = g_pending_event;
        g_has_pending_event = 0;
        return 0;
      }
      continue;
    }
    if (x11_parse_event_line(display, line, event) == 0)
      return 0;
  }
}

static int
x11_read_line(int fd, char *line, int maxlen)
{
  int i;

  if (!line || maxlen <= 1)
    return -1;

  while (1) {
    for (i = 0; i < g_rxlen; i++) {
      if (g_rxbuf[i] == '\n' || g_rxbuf[i] == '\r') {
        int copy_len;
        int consume;

        copy_len = i;
        if (copy_len > maxlen - 1)
          copy_len = maxlen - 1;
        memmove(line, g_rxbuf, (size_t)copy_len);
        line[copy_len] = '\0';

        consume = i + 1;
        while (consume < g_rxlen && (g_rxbuf[consume] == '\n' || g_rxbuf[consume] == '\r'))
          consume++;
        memmove(g_rxbuf, g_rxbuf + consume, (size_t)(g_rxlen - consume));
        g_rxlen -= consume;
        return copy_len;
      }
    }

    if (g_rxlen >= (int)sizeof(g_rxbuf))
      return -1;

    {
      int n = recv(fd, g_rxbuf + g_rxlen, sizeof(g_rxbuf) - (size_t)g_rxlen);
      if (n <= 0)
        return -1;
      g_rxlen += n;
    }
  }
}

static int
x11_send(int fd, const char *cmd)
{
  if (!cmd)
    return -1;
  return send(fd, cmd, strlen(cmd)) < 0 ? -1 : 0;
}

static int
x11_cmd(Display *dpy, const char *cmd, char *resp, int maxlen)
{
  char line[X6_BUF_SIZE];
  if (!dpy || dpy->fd < 0)
    return -1;
  if (x11_drain_draw_replies(dpy) < 0)
    return -1;
  if (x11_send(dpy->fd, cmd) < 0)
    return -1;
  if (!resp)
    return 0;

  while (1) {
    if (x11_read_line(dpy->fd, line, sizeof(line)) < 0)
      return -1;

    if (x11_handle_unsolicited_line(dpy, line))
      continue;

    strncpy(resp, line, maxlen - 1);
    resp[maxlen - 1] = '\0';
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

  if (g_display)
    return g_display;

  dpy = malloc(sizeof(*dpy));
  if (!dpy)
    return 0;
  memset(dpy, 0, sizeof(*dpy));

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

  if (x11_read_line(dpy->fd, line, sizeof(line)) < 0) {
    goto fail;
  }
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
  g_rxlen = 0;
  g_pending_draw_replies = 0;
  g_display = dpy;
  return dpy;

fail:
  if (dpy->fd >= 0)
    close(dpy->fd);
  free(dpy);
  return 0;
}

int
XCloseDisplay(Display *display)
{
  if (!display)
    return 0;
  if (display->fd >= 0) {
    x11_send(display->fd, "QUIT\n");
    close(display->fd);
  }
  if (g_display == display)
    g_display = 0;
  g_rxlen = 0;
  g_pending_draw_replies = 0;
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
  (void)display;
  return 0;
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
  (void)parent; (void)depth; (void)class;
  (void)visual;

  if (!display)
    return None;

  snprintf(cmd, sizeof(cmd), "CREATE %d %d %d %d\n", x, y, (int)width, (int)height);
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
  return x11_cmd(display, cmd, line, sizeof(line)) < 0 ? -1 : 0;
}

int
XMapWindow(Display *display, Window w)
{
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];

  if (g_is_wm)
    snprintf(cmd, sizeof(cmd), "WM_MAP %u\n", (uint)w);
  else
    snprintf(cmd, sizeof(cmd), "MAP %u\n", (uint)w);

  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  if (strncmp(line, "OK map", 6) == 0 ||
      strncmp(line, "OK mapped", 9) == 0 ||
      strncmp(line, "PENDING map", 11) == 0)
    return 0;
  return -1;
}

int XMapRaised(Display *display, Window w) { return XMapWindow(display, w); }

int
XUnmapWindow(Display *display, Window w)
{
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];

  if (g_is_wm)
    snprintf(cmd, sizeof(cmd), "WM_UNMAP %u\n", (uint)w);
  else
    snprintf(cmd, sizeof(cmd), "UNMAP %u\n", (uint)w);

  return x11_cmd(display, cmd, line, sizeof(line)) < 0 ? -1 : 0;
}

int
XMoveResizeWindow(Display *display, Window w, int x, int y, unsigned int width, unsigned int height)
{
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];

  if (g_is_wm)
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
int XRaiseWindow(Display *display, Window w) { (void)display; (void)w; return 0; }
int XLowerWindow(Display *display, Window w) { (void)display; (void)w; return 0; }

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
      g_is_wm = 1;
    else if (strncmp(line, "ERR redirect-in-use", 19) == 0)
      g_is_wm = 0;
    else
      return -1;
  }

  // Register the event mask for this window so the server routes events here.
  snprintf(cmd, sizeof(cmd), "SELECT_EVENTS %u %ld\n", (uint)w, event_mask);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;

  return 0;
}

int
XNextEvent(Display *display, XEvent *event)
{
  if (g_has_pending_event) {
    *event = g_pending_event;
    g_has_pending_event = 0;
    return 0;
  }
  return x11_read_event(display, event);
}

int
XMaskEvent(Display *display, long event_mask, XEvent *event)
{
  XEvent tmp;

  if (!display || !event)
    return -1;

  if (g_has_pending_event) {
    if (x11_event_mask_for_type(g_pending_event.type) & event_mask) {
      *event = g_pending_event;
      g_has_pending_event = 0;
      return 0;
    }
  }

  while (1) {
    if (x11_read_event(display, &tmp) < 0)
      return -1;
    if (x11_event_mask_for_type(tmp.type) & event_mask) {
      *event = tmp;
      return 0;
    }
    if (!g_has_pending_event) {
      g_pending_event = tmp;
      g_has_pending_event = 1;
    }
  }
}

Bool XCheckMaskEvent(Display *display, long event_mask, XEvent *event) {
  (void)display;
  if (!event)
    return False;
  if (g_has_pending_event && (x11_event_mask_for_type(g_pending_event.type) & event_mask)) {
    *event = g_pending_event;
    g_has_pending_event = 0;
    return True;
  }
  return False;
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

int
XChangeProperty(Display *display, Window w, Atom property, Atom type,
                int format, int mode, unsigned char *data, int nelements)
{
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  const char *name = atom_to_name(property);
  (void)type;
  (void)mode;
  if (!display || format != 8 || !data)
    return -1;
  snprintf(cmd, sizeof(cmd), "SET_PROPERTY %u %s %.*s\n", (uint)w, name, nelements, (char *)data);
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
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  char *space;
  const char *name = atom_to_name(property);
  (void)long_offset;
  (void)long_length;
  (void)del;
  (void)req_type;

  if (!display || !prop_return)
    return -1;

  snprintf(cmd, sizeof(cmd), "GET_PROPERTY %u %s\n", (uint)w, name);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;

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

  *prop_return = (unsigned char *)malloc(strlen(space) + 1);
  if (!*prop_return)
    return -1;
  strcpy((char *)*prop_return, space);

  if (actual_type_return) *actual_type_return = XA_STRING;
  if (actual_format_return) *actual_format_return = 8;
  if (nitems_return) *nitems_return = strlen(space);
  if (bytes_after_return) *bytes_after_return = 0;
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
  (void)display;
  (void)selection;
  (void)owner;
  (void)time;
  return 1;  /* Simplified: just succeed */
}

int XGetSelectionOwner(Display *display, Atom selection) {
  (void)display;
  (void)selection;
  return 0;  /* No selection owner */
}

int XConvertSelection(Display *display, Atom selection, Atom target, Atom property, Window requestor, Time time) {
  (void)display;
  (void)selection;
  (void)target;
  (void)property;
  (void)requestor;
  (void)time;
  return 1;  /* Simplified: just succeed */
}

Status XSendEvent(Display *display, Window w, Bool propagate, long event_mask, XEvent *event_send) {
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  (void)w;
  (void)propagate;
  (void)event_mask;
  if (!display || !event_send)
    return 0;

  if (event_send->type == ConfigureNotify) {
    snprintf(cmd, sizeof(cmd), "QUEUE_CONFIGURE_NOTIFY %u %d %d %d %d\n",
             (uint)event_send->xconfigure.window,
             event_send->xconfigure.x,
             event_send->xconfigure.y,
             event_send->xconfigure.width,
             event_send->xconfigure.height);
    if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
      return 0;
    return Success;
  }

  if (event_send->type == ClientMessage) {
    snprintf(cmd, sizeof(cmd), "QUEUE_CLIENT_MESSAGE %u %u %u\n",
             (uint)event_send->xclient.window,
             (uint)event_send->xclient.message_type,
             (uint)event_send->xclient.data.l[0]);
    if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
      return 0;
    return Success;
  }

  return Success;
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
  return x11_cmd(display, "UNGRAB_KEYBOARD\n", line, sizeof(line)) < 0 ? -1 : 0;
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
  return x11_cmd(display, "UNGRAB_POINTER\n", line, sizeof(line)) < 0 ? -1 : 0;
}
int XWarpPointer(Display *display, Window src_w, Window dest_w, int src_x, int src_y, unsigned int src_width, unsigned int src_height, int dest_x, int dest_y) {
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  (void)src_w; (void)dest_w; (void)src_x; (void)src_y; (void)src_width; (void)src_height;
  snprintf(cmd, sizeof(cmd), "WARP_POINTER %d %d\n", dest_x, dest_y);
  return x11_cmd(display, cmd, line, sizeof(line)) < 0 ? -1 : 0;
}

int XGrabKey(Display *display, int keycode, unsigned int modifiers, Window grab_window, Bool owner_events, int pointer_mode, int keyboard_mode) {
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  (void)owner_events; (void)pointer_mode; (void)keyboard_mode;
  if (!display)
    return -1;
  snprintf(cmd, sizeof(cmd), "GRAB_KEY %u %u %u\n", (uint)keycode, (uint)modifiers, (uint)grab_window);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  return 0;
}
int XUngrabKey(Display *display, int keycode, unsigned int modifiers, Window grab_window) {
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  if (!display)
    return -1;
  snprintf(cmd, sizeof(cmd), "UNGRAB_KEY %u %u %u\n", (uint)keycode, (uint)modifiers, (uint)grab_window);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  return 0;
}
int XGrabButton(Display *display, unsigned int button, unsigned int modifiers, Window grab_window, Bool owner_events, unsigned int event_mask, int pointer_mode, int keyboard_mode, Window confine_to, Cursor cursor) {
  (void)display; (void)button; (void)modifiers; (void)grab_window; (void)owner_events; (void)event_mask; (void)pointer_mode; (void)keyboard_mode; (void)confine_to; (void)cursor; return 0;
}
int XUngrabButton(Display *display, unsigned int button, unsigned int modifiers, Window grab_window) {
  (void)display; (void)button; (void)modifiers; (void)grab_window; return 0;
}
int XAllowEvents(Display *display, int event_mode, Time time) { (void)display; (void)event_mode; (void)time; return 0; }

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
int XFreeCursor(Display *display, Cursor cursor) { (void)display; (void)cursor; return 0; }

Pixmap XCreatePixmap(Display *display, Drawable d, unsigned int width, unsigned int height, unsigned int depth) {
  struct x11_pixmap_state *pm;
  char cmd[256];
  char response[128];
  
  if (!display || width == 0 || height == 0)
    return 0;
  
  /* Allocate client-side pixmap tracking struct first */
  pm = x11_alloc_pixmap(width, height, depth);
  if (!pm)
    return 0;
  
  /* Send CREATE_PIXMAP command to x6 server */
  snprintf(cmd, sizeof(cmd), "CREATE_PIXMAP %u %u %u %u\n", depth, width, height, depth);
  
  /* For now, we don't synchronously wait for the response
   * The pixmap ID we return is our server-generated ID
   * In a full implementation, we'd match against x6's ID
   */
  if (x11_cmd(display, cmd, response, sizeof(response)) == 0) {
    /* Parse "OK create_pixmap pmid=XXXX" response if needed */
    /* For now, just return our allocated pixmap ID */
    (void)response;  /* Suppress unused warning */
  }
  
  return pm->id;
}

int XFreePixmap(Display *display, Pixmap pixmap) {
  struct x11_pixmap_state *pm;
  char cmd[256];
  char response[128];
  
  pm = x11_find_pixmap(pixmap);
  if (!pm)
    return 0;
  
  /* Send DESTROY_PIXMAP command to x6 server */
  snprintf(cmd, sizeof(cmd), "DESTROY_PIXMAP %u\n", pixmap);
  
  if (display) {
    x11_cmd(display, cmd, response, sizeof(response));
  }
  
  /* Mark as unused in client tracking */
  pm->in_use = 0;
  
  return 0;
}
GC XCreateGC(Display *display, Drawable d, unsigned long valuemask, void *values) {
  struct x11_gc_state *gs;
  (void)display;
  (void)d;
  (void)valuemask;
  (void)values;
  gs = x11_alloc_gc();
  if (!gs)
    return 0;
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
int XSetLineAttributes(Display *display, GC gc, unsigned int line_width, int line_style, int cap_style, int join_style) {
  (void)display; (void)gc; (void)line_width; (void)line_style; (void)cap_style; (void)join_style; return 0;
}

int XParseColor(Display *display, Colormap colormap, const char *spec, XColor *exact_def_return) {
  int r, g, b;
  
  (void)display;
  (void)colormap;
  
  if (!spec || !exact_def_return)
    return 0;
  
  /* Parse hex color "#RRGGBB" */
  if (spec[0] == '#' && strlen(spec) == 7) {
    if (sscanf(spec, "#%02x%02x%02x", &r, &g, &b) == 3) {
      exact_def_return->red = (unsigned short)(r << 8);
      exact_def_return->green = (unsigned short)(g << 8);
      exact_def_return->blue = (unsigned short)(b << 8);
      exact_def_return->pixel = ((r & 0xff) << 16) | ((g & 0xff) << 8) | (b & 0xff);
      exact_def_return->flags = DoRed | DoGreen | DoBlue;
      return 1;
    }
  }
  
  /* Parse common color names */
  if (!strcmp(spec, "black")) {
    exact_def_return->red = 0;
    exact_def_return->green = 0;
    exact_def_return->blue = 0;
    exact_def_return->pixel = 0;
    exact_def_return->flags = DoRed | DoGreen | DoBlue;
    return 1;
  } else if (!strcmp(spec, "white")) {
    exact_def_return->red = 0xffff;
    exact_def_return->green = 0xffff;
    exact_def_return->blue = 0xffff;
    exact_def_return->pixel = 0xffffff;
    exact_def_return->flags = DoRed | DoGreen | DoBlue;
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
  
  memset(fs_out, 0, sizeof(*fs_out));
  fs_out->fid = fs->id;
  fs_out->ascent = fs->ascent;
  fs_out->descent = fs->descent;
  fs_out->max_bounds.width = fs->width;
  
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
  
  memset(fs_out, 0, sizeof(*fs_out));
  fs_out->fid = fs->id;
  fs_out->ascent = fs->ascent;
  fs_out->descent = fs->descent;
  fs_out->max_bounds.width = fs->width;
  
  return fs_out;
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
  if (x11_send(display->fd, cmd) < 0)
    return -1;
  g_pending_draw_replies++;
  return 0;
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
  if (x11_send(display->fd, cmd) < 0)
    return -1;
  g_pending_draw_replies++;
  return 0;
}
int XCopyArea(Display *display, Drawable src, Drawable dest, GC gc, int src_x, int src_y, unsigned int width, unsigned int height, int dest_x, int dest_y) {
  struct x11_gc_state *gs;
  char cmd[256];
  char response[32];
  int ret;
  
  (void)display;
  
  gs = x11_find_gc(gc);
  if (!gs)
    return 0;
  
  /* Send COPY_AREA command to x6 server
   * Format: COPY_AREA <src> <dest> <src_x> <src_y> <width> <height> <dest_x> <dest_y>
   */
  snprintf(cmd, sizeof(cmd), "COPY_AREA %lu %lu %d %d %u %u %d %d\n",
           src, dest, src_x, src_y, width, height, dest_x, dest_y);
  
  ret = x11_cmd(display, cmd, response, sizeof(response));
  return ret;
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
  (void)display;
  if (root_return) *root_return = 1;
  if (parent_return) *parent_return = 1;
  if (children_return) *children_return = 0;
  if (nchildren_return) *nchildren_return = 0;
  (void)w;
  return 1;
}
Bool XQueryPointer(Display *display, Window w, Window *root_return, Window *child_return, int *root_x_return, int *root_y_return, int *win_x_return, int *win_y_return, unsigned int *mask_return) {
  char line[X6_BUF_SIZE];
  unsigned int child = 0;
  int px = 0;
  int py = 0;
  unsigned int state = 0;
  (void)w;

  if (x11_cmd(display, "QUERY_POINTER\n", line, sizeof(line)) < 0)
    return False;
  if (sscanf(line, "OK pointer root=1 child=%u x=%d y=%d state=%u", &child, &px, &py, &state) < 4)
    return False;

  if (root_return) *root_return = 1;
  if (child_return) *child_return = (Window)child;
  if (root_x_return) *root_x_return = px;
  if (root_y_return) *root_y_return = py;
  if (win_x_return) *win_x_return = px;
  if (win_y_return) *win_y_return = py;
  if (mask_return) *mask_return = state;
  return True;
}

int
XPending(Display *display)
{
  struct pollfd pfd;
  XEvent ev;

  if (!display)
    return 0;
  if (g_has_pending_event)
    return 1;

  /* Check if we already have bytes buffered from a previous recv */
  if (g_rxlen > 0) {
    if (x11_read_event(display, &ev) < 0)
      return 0;
    g_pending_event = ev;
    g_has_pending_event = 1;
    return 1;
  }

  pfd.fd = display->fd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  if (poll(&pfd, 1, 0) <= 0)
    return 0;
  if ((pfd.revents & POLLIN) == 0)
    return 0;
  if (x11_read_event(display, &ev) < 0)
    return 0;
  g_pending_event = ev;
  g_has_pending_event = 1;
  return 1;
}

int (*XSetErrorHandler(int (*handler)(Display *, XErrorEvent *)))(Display *, XErrorEvent *) {
  int (*old)(Display *, XErrorEvent *) = g_error_handler;
  g_error_handler = handler;
  return old;
}
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
KeySym XKeycodeToKeysym(Display *display, KeyCode keycode, int index) { (void)display; (void)index; return x11_keycode_to_keysym(keycode); }
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
               "flags=%d min=%dx%d max=%dx%d base=%dx%d inc=%dx%d",
               hints->flags,
               hints->min_width, hints->min_height,
               hints->max_width, hints->max_height,
               hints->base_width, hints->base_height,
               hints->width_inc, hints->height_inc);
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
  char *sep;
  if (!class_hint_return) return 0;
  class_hint_return->res_name = 0;
  class_hint_return->res_class = 0;
  if (!display)
    return 1;
  if (XGetWindowProperty(display, w, XInternAtom(display, "WM_CLASS", False),
                         0, 256, False, XA_STRING, &actual, &fmt, &nitems, 0, &prop) != Success)
    return 1;
  if (!prop)
    return 1;
  sep = strchr((char *)prop, ',');
  if (sep) {
    *sep = '\0';
    class_hint_return->res_name = strdup((char *)prop);
    class_hint_return->res_class = strdup(sep + 1);
  } else {
    class_hint_return->res_name = strdup((char *)prop);
    class_hint_return->res_class = 0;
  }
  XFree(prop);
  return 1;
}
int XGetTextProperty(Display *display, Window w, XTextProperty *text_prop_return, Atom property) {
  unsigned char *prop = 0;
  unsigned long nitems = 0;
  Atom actual = 0;
  int fmt = 0;
  if (!text_prop_return) return 0;
  if (XGetWindowProperty(display, w, property, 0, 1024, False, XA_STRING, &actual, &fmt, &nitems, 0, &prop) != Success)
    return 0;
  text_prop_return->value = (char *)prop;
  text_prop_return->encoding = actual;
  text_prop_return->format = fmt;
  text_prop_return->nitems = nitems;
  return 1;
}
int XmbTextPropertyToTextList(Display *display, XTextProperty *text_prop, char ***list_return, int *count_return) {
  (void)display;
  if (!text_prop || !list_return || !count_return || !text_prop->value) return 0;
  *list_return = malloc(sizeof(char *));
  if (!*list_return) return 0;
  (*list_return)[0] = strdup(text_prop->value);
  if (!(*list_return)[0]) {
    free(*list_return);
    return 0;
  }
  *count_return = 1;
  return Success;
}
void XFreeStringList(char **list) {
  if (!list) return;
  if (list[0]) free(list[0]);
  free(list);
}
int XGetWMProtocols(Display *display, Window w, Atom **protocols_return, int *count_return) {
  unsigned char *prop = 0;
  unsigned long nitems = 0;
  Atom actual = 0;
  int fmt = 0;
  int count = 0;
  char *tok;
  char *tmp;

  if (protocols_return)
    *protocols_return = 0;
  if (count_return)
    *count_return = 0;
  if (!display || !protocols_return || !count_return)
    return 0;

  if (XGetWindowProperty(display, w, XInternAtom(display, "WM_PROTOCOLS", False),
                         0, 256, False, XA_STRING, &actual, &fmt, &nitems, 0, &prop) != Success)
    return 0;
  if (!prop)
    return 0;

  tmp = strdup((char *)prop);
  XFree(prop);
  if (!tmp)
    return 0;

  for (tok = strtok(tmp, ","); tok; tok = strtok(0, ","))
    count++;
  free(tmp);
  if (count <= 0)
    return 0;

  *protocols_return = (Atom *)malloc((size_t)count * sizeof(Atom));
  if (!*protocols_return)
    return 0;

  if (XGetWindowProperty(display, w, XInternAtom(display, "WM_PROTOCOLS", False),
                         0, 256, False, XA_STRING, &actual, &fmt, &nitems, 0, &prop) != Success) {
    free(*protocols_return);
    *protocols_return = 0;
    return 0;
  }
  if (!prop) {
    free(*protocols_return);
    *protocols_return = 0;
    return 0;
  }

  tmp = strdup((char *)prop);
  XFree(prop);
  if (!tmp) {
    free(*protocols_return);
    *protocols_return = 0;
    return 0;
  }

  count = 0;
  for (tok = strtok(tmp, ","); tok; tok = strtok(0, ",")) {
    while (*tok == ' ' || *tok == '\t') tok++;
    (*protocols_return)[count++] = XInternAtom(display, tok, False);
  }
  free(tmp);

  *count_return = count;
  return count > 0;
}
XWMHints *XGetWMHints(Display *display, Window w) {
  XWMHints *h = malloc(sizeof(*h));
  unsigned char *prop = 0;
  unsigned long nitems = 0;
  Atom actual = 0;
  int fmt = 0;
  if (!h) return 0;
  memset(h, 0, sizeof(*h));
  h->input = 1;
  if (!display)
    return h;
  if (XGetWindowProperty(display, w, XA_WM_HINTS, 0, 128, False, XA_STRING,
                         &actual, &fmt, &nitems, 0, &prop) == Success && prop) {
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
  unsigned char *prop = 0;
  unsigned long nitems = 0;
  Atom actual = 0;
  int fmt = 0;
  if (hints_return) memset(hints_return, 0, sizeof(*hints_return));
  if (!display)
    return 1;
  if (hints_return &&
      XGetWindowProperty(display, w, XA_WM_NORMAL_HINTS, 0, 256, False, XA_STRING,
                         &actual, &fmt, &nitems, 0, &prop) == Success && prop) {
    sscanf((char *)prop,
           "flags=%d min=%dx%d max=%dx%d base=%dx%d inc=%dx%d",
           &hints_return->flags,
           &hints_return->min_width, &hints_return->min_height,
           &hints_return->max_width, &hints_return->max_height,
           &hints_return->base_width, &hints_return->base_height,
           &hints_return->width_inc, &hints_return->height_inc);
    XFree(prop);
  }
  if (supplied_return) *supplied_return = 0;
  return 1;
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
  (void)draw;
  (void)color;
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  /* Stub: rect drawing not needed for st */
}

void XftDrawSetClipRectangles(XftDraw *draw, int xOrigin, int yOrigin, XRectangle *rects, int nrects) {
  (void)draw;
  (void)xOrigin;
  (void)yOrigin;
  (void)rects;
  (void)nrects;
  /* Stub: clipping not needed for basic rendering */
}

void XftDrawSetClip(XftDraw *draw, void *clip) {
  (void)draw;
  (void)clip;
  /* Stub: clipping not needed for basic rendering */
}

void XftDrawGlyphFontSpec(XftDraw *draw, XftColor *color, XftGlyphFontSpec *glyphs, int nglyphs) {
  (void)draw;
  (void)color;
  (void)glyphs;
  (void)nglyphs;
  /* Stub: glyph-spec rendering not needed for basic st */
}

int XftColorAllocValue(Display *display, Visual *visual, Colormap colormap, XColor *color, XftColor *result) {
  if (!color || !result)
    return 0;
  (void)display;
  (void)visual;
  (void)colormap;
  result->pixel = color->pixel;
  result->color.red = color->red;
  result->color.green = color->green;
  result->color.blue = color->blue;
  result->color.alpha = 65535; /* fully opaque */
  return 1;
}

int XftColorAllocName(Display *display, Visual *visual, Colormap colormap, const char *name, XftColor *result) {
  XColor xc;
  if (!XParseColor(display, colormap, name, &xc))
    return 0;
  if (!XAllocColor(display, colormap, &xc))
    return 0;
  return XftColorAllocValue(display, visual, colormap, &xc, result);
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
  (void)display;
  (void)pattern;
  
  f = (XftFont *)malloc(sizeof(*f));
  if (f) {
    f->pattern = pattern;
    f->charset = 0;
    f->ascent = 12;
    f->descent = 4;
    f->height = 16;
    f->max_advance_width = 8;
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
  (void)xlfd;
  
  f = (XftFont *)malloc(sizeof(*f));
  if (f) {
    f->pattern = 0;
    f->charset = 0;
    f->ascent = 12;
    f->descent = 4;
    f->height = 16;
    f->max_advance_width = 8;
  }
  return f;
}

XftPattern *XftPatternCreate(void) {
  return (XftPattern *)malloc(1);
}

void XftPatternDestroy(XftPattern *p) {
  free(p);
}

void XftDefaultSubstitute(Display *display, int screen, XftPattern *pattern) {
  (void)display;
  (void)screen;
  (void)pattern;
  /* Stub - no substitution needed */
}

XftResult XftPatternGetInteger(XftPattern *p, const char *object, int id, int *i) {
  (void)p;
  (void)object;
  (void)id;
  (void)i;
  return XftResultNoMatch;
}

int XftCharExists(Display *display, XftFont *font, FcChar32 ucs4) {
  (void)display;
  (void)font;
  (void)ucs4;
  return 1;  /* Assume all chars exist */
}

unsigned int XftCharIndex(XftFont *font, FcChar32 ucs4) {
  (void)font;
  return (unsigned int)ucs4;  /* Simplified: treat codepoint as index */
}

XftPattern *XftFontMatch(Display *display, int screen, XftPattern *pattern, XftResult *result) {
  (void)display;
  (void)screen;
  (void)pattern;
  if (result)
    *result = XftResultMatch;
  return (XftPattern *)malloc(1);
}

void XftDrawStringUtf8(XftDraw *draw, XftColor *color, XftFont *font, int x, int y, const XftChar8 *string, int len) {
  (void)draw;
  (void)color;
  (void)font;
  (void)x;
  (void)y;
  (void)string;
  (void)len;
  /* Stub: text rendering to be implemented when pixmap text support is added */
}

void XftTextExtentsUtf8(Display *display, XftFont *font, const XftChar8 *string, int len, XGlyphInfo *extents) {
  (void)display;
  (void)font;
  (void)string;
  
  if (extents) {
    /* Stub: return reasonable defaults (8x16 font metrics) */
    extents->width = len * 8;
    extents->height = 16;
    extents->x = 0;
    extents->y = -12;
    extents->xOff = len * 8;
    extents->yOff = 0;
  }
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
  FcPattern *p = (FcPattern *)malloc(1);
  (void)name;
  return p;
}

void FcPatternDestroy(FcPattern *p) {
  free(p);
}

void FcFontSetDestroy(FcFontSet *ffs) {
  free(ffs);
}

FcPattern *FcPatternDuplicate(FcPattern *p) {
  FcPattern *dup = (FcPattern *)malloc(1);
  (void)p;
  return dup;
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
  (void)pattern;
  /* Stub */
}

FcPattern *FcFontSetMatch(void *config, FcFontSet **sets, int nsets, FcPattern *p, FcResult *result) {
  (void)config;
  (void)sets;
  (void)nsets;
  (void)p;
  if (result)
    *result = FcResultMatch;
  return (FcPattern *)malloc(1);
}

FcFontSet *FcFontSort(void *config, FcPattern **patterns, int npatterns, FcBool trim, FcCharSet **csp, FcResult *result) {
  FcFontSet *fs = (FcFontSet *)malloc(sizeof(*fs));
  (void)config;
  (void)patterns;
  (void)npatterns;
  (void)trim;
  (void)csp;
  if (result)
    *result = FcResultMatch;
  return fs;
}

FcPattern *FcFontMatch(void *config, FcPattern *p, FcResult *result) {
  (void)config;
  (void)p;
  if (result)
    *result = FcResultMatch;
  return (FcPattern *)malloc(1);
}

FcBool FcPatternDel(FcPattern *p, const char *object) {
  (void)p;
  (void)object;
  return 1;
}

FcBool FcPatternAddDouble(FcPattern *p, const char *object, double d) {
  (void)p;
  (void)object;
  (void)d;
  return 1;
}

FcBool FcPatternAddInteger(FcPattern *p, const char *object, int i) {
  (void)p;
  (void)object;
  (void)i;
  return 1;
}

FcResult FcPatternGetDouble(FcPattern *p, const char *object, int id, double *d) {
  (void)p;
  (void)object;
  (void)id;
  if (d)
    *d = 12.0;  /* Default font size */
  return FcResultMatch;
}

XftPattern *XftXlfdParse(const char *xlfd, int expand, FcBool ignore_scalable) {
  XftPattern *p = (XftPattern *)malloc(1);
  (void)xlfd;
  (void)expand;
  (void)ignore_scalable;
  return p;
}

int XDefaultDepth(Display *display, int screen) {
  (void)screen;
  if (!display)
    return 24;
  return display->depth;
}

int XReparentWindow(Display *display, Window w, Window parent, int x, int y) {
  (void)display;
  (void)w;
  (void)parent;
  (void)x;
  (void)y;
  return 0;
}

int XRegisterIMInstantiateCallback(Display *display, void *rdb, char *res_name, char *res_class, void (*callback)(Display *, void *, void *), void *client_data) {
  (void)display;
  (void)rdb;
  (void)res_name;
  (void)res_class;
  (void)callback;
  (void)client_data;
  return 1;
}

int XSetWMProtocols(Display *display, Window w, Atom *protocols, int count) {
  (void)display;
  (void)w;
  (void)protocols;
  (void)count;
  return 1;
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
  (void)parsestring;
  (void)x_return;
  (void)y_return;
  (void)width_return;
  (void)height_return;
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
  (void)display;
  (void)w;
  (void)text_prop;
  return 1;
}

int XSetTextProperty(Display *display, Window w, void *text_prop, Atom property) {
  (void)display;
  (void)w;
  (void)text_prop;
  (void)property;
  return 1;
}

int Xutf8TextListToTextProperty(Display *display, char **list, int count, XICCEncodingStyle style, void *text_prop_return) {
  (void)display;
  (void)list;
  (void)count;
  (void)style;
  (void)text_prop_return;
  return 0; /* Success */
}

int XSetICValues(XIC ic, ...) {
  (void)ic;
  return 0;
}

int XSetWMIconName(Display *display, Window w, void *text_prop) {
  (void)display;
  (void)w;
  (void)text_prop;
  return 1;
}

char *XSetLocaleModifiers(const char *modifier_list) {
  (void)modifier_list;
  return "";
}

XIC XCreateIC(XIM im, ...) {
  (void)im;
  return NULL;
}

int XUnregisterIMInstantiateCallback(Display *display, void *rdb, char *res_name, char *res_class, void (*callback)(Display *, void *, void *), void *client_data) {
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
  return NULL;
}

int XSetIMValues(XIM im, ...) {
  (void)im;
  return 0;
}

void *XVaCreateNestedList(int dummy, ...) {
  (void)dummy;
  return NULL;
}

void *XAllocSizeHints(void) {
  return malloc(sizeof(int) * 18);  /* Approximate size of XSizeHints */
}

int XSetWMProperties(Display *display, Window w, void *window_name, void *icon_name, char **argv, int argc, void *normal_hints, void *wm_hints, void *class_hints) {
  (void)display;
  (void)w;
  (void)window_name;
  (void)icon_name;
  (void)argv;
  (void)argc;
  (void)normal_hints;
  (void)wm_hints;
  (void)class_hints;
  return 1;
}

int XmbLookupString(XIC ic, XKeyEvent *event, char *buffer, int nbytes, KeySym *keysym, void *status) {
  (void)ic;
  (void)event;
  (void)buffer;
  (void)nbytes;
  (void)keysym;
  (void)status;
  return 0;
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


