#include "types.h"
#include "auxv6/user.h"
#include "socket.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "X11/Xlib.h"
#include "X11/Xutil.h"

#define X6_PORT 6006
#define X6_BUF_SIZE 1024
#define X11_MAX_ATOMS 256

typedef struct {
  Atom atom;
  char name[64];
} atom_entry;

static Display *g_display;
static unsigned long g_next_window = 2;
static unsigned long g_next_atom = 128;
static atom_entry g_atoms[X11_MAX_ATOMS];
static int g_atom_count;
static int (*g_error_handler)(Display *, XErrorEvent *);

static int
x11_read_line(int fd, char *line, int maxlen)
{
  int pos = 0;
  while (pos < maxlen - 1) {
    char ch;
    int n = recv(fd, &ch, 1);
    if (n <= 0)
      return -1;
    if (ch == '\n') {
      line[pos] = '\0';
      return pos;
    }
    line[pos++] = ch;
  }
  line[pos] = '\0';
  return pos;
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
  if (!dpy || dpy->fd < 0)
    return -1;
  if (x11_send(dpy->fd, cmd) < 0)
    return -1;
  if (!resp)
    return 0;
  return x11_read_line(dpy->fd, resp, maxlen);
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
  if (dpy->fd < 0)
    goto fail;

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = (ushort)X6_PORT;
  addr.sin_addr = INADDR_LOOPBACK;
  if (connect(dpy->fd, &addr, sizeof(addr)) < 0)
    goto fail;

  if (x11_read_line(dpy->fd, line, sizeof(line)) < 0)
    goto fail;
  if (x11_cmd(dpy, "HELLO x6/1\n", line, sizeof(line)) < 0)
    goto fail;
  if (strncmp(line, "OK proto=", 9) != 0)
    goto fail;

  dpy->screen = 0;
  dpy->root = 1;
  dpy->width = 1024;
  dpy->height = 768;
  dpy->depth = 32;
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
  (void)parent; (void)border_width; (void)depth; (void)class;
  (void)visual; (void)valuemask; (void)attributes;

  if (!display)
    return None;

  w = g_next_window++;
  snprintf(cmd, sizeof(cmd), "CREATE %u %d %d %d %d\n", (uint)w, x, y, (int)width, (int)height);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return None;
  return strncmp(line, "OK create", 9) == 0 ? w : None;
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
  snprintf(cmd, sizeof(cmd), "MAP %u\n", (uint)w);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  if (strncmp(line, "OK map", 6) == 0 || strncmp(line, "PENDING map", 11) == 0)
    return 0;
  return -1;
}

int XMapRaised(Display *display, Window w) { return XMapWindow(display, w); }

int
XUnmapWindow(Display *display, Window w)
{
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  snprintf(cmd, sizeof(cmd), "UNMAP %u\n", (uint)w);
  return x11_cmd(display, cmd, line, sizeof(line)) < 0 ? -1 : 0;
}

int
XMoveResizeWindow(Display *display, Window w, int x, int y, unsigned int width, unsigned int height)
{
  char cmd[X6_BUF_SIZE], line[X6_BUF_SIZE];
  snprintf(cmd, sizeof(cmd), "CONFIGURE %u %d %d %d %d\n", (uint)w, x, y, (int)width, (int)height);
  if (x11_cmd(display, cmd, line, sizeof(line)) < 0)
    return -1;
  if (strncmp(line, "OK configure", 12) == 0 || strncmp(line, "PENDING configure", 17) == 0)
    return 0;
  return -1;
}

int XMoveWindow(Display *display, Window w, int x, int y) { return XMoveResizeWindow(display, w, x, y, 1, 1); }
int XRaiseWindow(Display *display, Window w) { (void)display; (void)w; return 0; }
int XLowerWindow(Display *display, Window w) { (void)display; (void)w; return 0; }

int
XConfigureWindow(Display *display, Window w, unsigned int value_mask, XWindowChanges *values)
{
  int x = values ? values->x : 0;
  int y = values ? values->y : 0;
  unsigned int ww = values ? (unsigned int)values->width : 1;
  unsigned int hh = values ? (unsigned int)values->height : 1;
  (void)value_mask;
  return XMoveResizeWindow(display, w, x, y, ww, hh);
}

int
XGetWindowAttributes(Display *display, Window w, XWindowAttributes *attrs)
{
  (void)display; (void)w;
  if (!attrs)
    return 0;
  memset(attrs, 0, sizeof(*attrs));
  attrs->width = 800;
  attrs->height = 600;
  attrs->depth = 32;
  attrs->map_state = IsViewable;
  return 1;
}

int XSelectInput(Display *display, Window w, long event_mask) { (void)display; (void)w; (void)event_mask; return 0; }

int
XNextEvent(Display *display, XEvent *event)
{
  char line[X6_BUF_SIZE];
  if (!display || !event)
    return -1;
  while (1) {
    if (x11_read_line(display->fd, line, sizeof(line)) < 0)
      return -1;
    if (strncmp(line, "EVENT ", 6) != 0)
      continue;
    memset(event, 0, sizeof(*event));
    if (strncmp(line + 6, "MapRequest", 10) == 0) {
      event->type = MapRequest;
      sscanf(line, "EVENT MapRequest wid=%u", &event->xmaprequest.window);
      event->xmaprequest.parent = display->root;
      return 0;
    }
    if (strncmp(line + 6, "ConfigureRequest", 16) == 0) {
      event->type = ConfigureRequest;
      sscanf(line, "EVENT ConfigureRequest wid=%u x=%d y=%d w=%d h=%d",
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
  }
}

int XMaskEvent(Display *display, long event_mask, XEvent *event) { (void)event_mask; return XNextEvent(display, event); }
Bool XCheckMaskEvent(Display *display, long event_mask, XEvent *event) { (void)display; (void)event_mask; (void)event; return False; }
int XPending(Display *display) { (void)display; return 0; }

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

int XDeleteProperty(Display *display, Window w, Atom property) { (void)display; (void)w; (void)property; return 0; }

Status XSendEvent(Display *display, Window w, Bool propagate, long event_mask, XEvent *event_send) {
  (void)display;
  (void)w;
  (void)propagate;
  (void)event_mask;
  (void)event_send;
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
  (void)display;
  if (focus_return) *focus_return = None;
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
  (void)display; (void)grab_window; (void)owner_events; (void)event_mask; (void)pointer_mode; (void)keyboard_mode; (void)confine_to; (void)cursor; (void)time;
  return GrabSuccess;
}
int XUngrabPointer(Display *display, Time time) { (void)display; (void)time; return 0; }
int XWarpPointer(Display *display, Window src_w, Window dest_w, int src_x, int src_y, unsigned int src_width, unsigned int src_height, int dest_x, int dest_y) {
  (void)display; (void)src_w; (void)dest_w; (void)src_x; (void)src_y; (void)src_width; (void)src_height; (void)dest_x; (void)dest_y; return 0;
}

int XGrabKey(Display *display, int keycode, unsigned int modifiers, Window grab_window, Bool owner_events, int pointer_mode, int keyboard_mode) {
  (void)display; (void)keycode; (void)modifiers; (void)grab_window; (void)owner_events; (void)pointer_mode; (void)keyboard_mode; return 0;
}
int XUngrabKey(Display *display, int keycode, unsigned int modifiers, Window grab_window) {
  (void)display; (void)keycode; (void)modifiers; (void)grab_window; return 0;
}
int XGrabButton(Display *display, unsigned int button, unsigned int modifiers, Window grab_window, Bool owner_events, unsigned int event_mask, int pointer_mode, int keyboard_mode, Window confine_to, Cursor cursor) {
  (void)display; (void)button; (void)modifiers; (void)grab_window; (void)owner_events; (void)event_mask; (void)pointer_mode; (void)keyboard_mode; (void)confine_to; (void)cursor; return 0;
}
int XUngrabButton(Display *display, unsigned int button, unsigned int modifiers, Window grab_window) {
  (void)display; (void)button; (void)modifiers; (void)grab_window; return 0;
}
int XAllowEvents(Display *display, int event_mode, Time time) { (void)display; (void)event_mode; (void)time; return 0; }

int XSetWindowBorder(Display *display, Window w, unsigned long border_pixel) { (void)display; (void)w; (void)border_pixel; return 0; }
int XChangeWindowAttributes(Display *display, Window w, unsigned long valuemask, XSetWindowAttributes *attributes) { (void)display; (void)w; (void)valuemask; (void)attributes; return 0; }
int XDefineCursor(Display *display, Window w, Cursor cursor) { (void)display; (void)w; (void)cursor; return 0; }
Cursor XCreateFontCursor(Display *display, unsigned int shape) { (void)display; return (Cursor)(shape + 1); }
int XFreeCursor(Display *display, Cursor cursor) { (void)display; (void)cursor; return 0; }

Pixmap XCreatePixmap(Display *display, Drawable d, unsigned int width, unsigned int height, unsigned int depth) { (void)display; (void)d; (void)width; (void)height; (void)depth; return 1; }
int XFreePixmap(Display *display, Pixmap pixmap) { (void)display; (void)pixmap; return 0; }
GC XCreateGC(Display *display, Drawable d, unsigned long valuemask, void *values) { (void)display; (void)d; (void)valuemask; (void)values; return 1; }
int XFreeGC(Display *display, GC gc) { (void)display; (void)gc; return 0; }
int XSetForeground(Display *display, GC gc, unsigned long foreground) { (void)display; (void)gc; (void)foreground; return 0; }
int XSetLineAttributes(Display *display, GC gc, unsigned int line_width, int line_style, int cap_style, int join_style) {
  (void)display; (void)gc; (void)line_width; (void)line_style; (void)cap_style; (void)join_style; return 0;
}
int XFillRectangle(Display *display, Drawable d, GC gc, int x, int y, unsigned int width, unsigned int height) {
  (void)display; (void)d; (void)gc; (void)x; (void)y; (void)width; (void)height; return 0;
}
int XDrawRectangle(Display *display, Drawable d, GC gc, int x, int y, unsigned int width, unsigned int height) {
  (void)display; (void)d; (void)gc; (void)x; (void)y; (void)width; (void)height; return 0;
}
int XCopyArea(Display *display, Drawable src, Drawable dest, GC gc, int src_x, int src_y, unsigned int width, unsigned int height, int dest_x, int dest_y) {
  (void)display; (void)src; (void)dest; (void)gc; (void)src_x; (void)src_y; (void)width; (void)height; (void)dest_x; (void)dest_y; return 0;
}

int XGetTransientForHint(Display *display, Window w, Window *prop_window_return) { (void)display; (void)w; if (prop_window_return) *prop_window_return = None; return 0; }
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
  (void)display; (void)w;
  if (root_return) *root_return = 1;
  if (child_return) *child_return = None;
  if (root_x_return) *root_x_return = 0;
  if (root_y_return) *root_y_return = 0;
  if (win_x_return) *win_x_return = 0;
  if (win_y_return) *win_y_return = 0;
  if (mask_return) *mask_return = 0;
  return True;
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

KeyCode XKeysymToKeycode(Display *display, KeySym keysym) { (void)display; return (KeyCode)(keysym & 0xff); }
KeySym XKeycodeToKeysym(Display *display, KeyCode keycode, int index) { (void)display; (void)index; return (KeySym)keycode; }
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
    map[i] = (KeySym)(first_keycode + i);
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

int XSetWMNormalHints(Display *display, Window w, XSizeHints *hints) { (void)display; (void)w; (void)hints; return 0; }
int XSetTransientForHint(Display *display, Window w, Window prop_window) { (void)display; (void)w; (void)prop_window; return 0; }
int XStoreName(Display *display, Window w, const char *name) {
  return XChangeProperty(display, w, XA_WM_NAME, XA_STRING, 8, PropModeReplace, (unsigned char *)name, name ? (int)strlen(name) : 0);
}
int XGetClassHint(Display *display, Window w, XClassHint *class_hint_return) {
  (void)display; (void)w;
  if (!class_hint_return) return 0;
  class_hint_return->res_name = 0;
  class_hint_return->res_class = 0;
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
  (void)display; (void)w;
  if (protocols_return) *protocols_return = 0;
  if (count_return) *count_return = 0;
  return 0;
}
XWMHints *XGetWMHints(Display *display, Window w) {
  XWMHints *h = malloc(sizeof(*h));
  (void)display; (void)w;
  if (!h) return 0;
  memset(h, 0, sizeof(*h));
  return h;
}
int XSetWMHints(Display *display, Window w, XWMHints *wmhints) { (void)display; (void)w; (void)wmhints; return 0; }
int XGetWMNormalHints(Display *display, Window w, XSizeHints *hints_return, long *supplied_return) {
  (void)display; (void)w;
  if (hints_return) memset(hints_return, 0, sizeof(*hints_return));
  if (supplied_return) *supplied_return = 0;
  return 1;
}
int XSetClassHint(Display *display, Window w, XClassHint *class_hints) { (void)display; (void)w; (void)class_hints; return 0; }
