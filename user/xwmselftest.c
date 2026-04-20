#include "types.h"
#include "auxv6/user.h"
#include "X11/Xlib.h"
#include "X11/Xproto.h"
#include "X11/extensions/shape.h"
#include "X11/extensions/Xrandr.h"
#include "X11/extensions/Xdamage.h"
#include "stdio.h"

#ifndef TopIf
#define TopIf 2
#endif
#ifndef BottomIf
#define BottomIf 3
#endif
#ifndef Opposite
#define Opposite 4
#endif

static int g_pass;
static int g_fail;
static int g_xerr_count;
static unsigned char g_xerr_code;
static unsigned char g_xerr_request;
static XID g_xerr_resource;

static void
reset_error_capture(void)
{
  g_xerr_count = 0;
  g_xerr_code = 0;
  g_xerr_request = 0;
  g_xerr_resource = (XID)0;
}

static int
selftest_error_handler(Display *display, XErrorEvent *event)
{
  (void)display;
  if(!event)
    return 0;
  g_xerr_count++;
  g_xerr_code = event->error_code;
  g_xerr_request = event->request_code;
  g_xerr_resource = event->resourceid;
  return 0;
}

static int
selftest_read_line(int fd, char *buf, int cap)
{
  int off;

  if(fd < 0 || !buf || cap < 2)
    return -1;

  off = 0;
  while(off < cap - 1) {
    char ch;
    int n;

    n = read(fd, &ch, 1);
    if(n <= 0)
      return -1;
    if(ch == '\r')
      continue;
    if(ch == '\n')
      break;
    buf[off++] = ch;
  }
  buf[off] = 0;
  return off;
}

static int
selftest_cmd(Display *dpy, const char *cmd, char *line, int line_cap)
{
  int len;
  int wrote;

  if(!dpy || !cmd || !line || line_cap < 2)
    return -1;

  len = strlen(cmd);
  wrote = 0;
  while(wrote < len) {
    int n;
    n = write(dpy->fd, cmd + wrote, len - wrote);
    if(n <= 0)
      return -1;
    wrote += n;
  }

  if(selftest_read_line(dpy->fd, line, line_cap) < 0)
    return -1;
  return 0;
}

static int
selftest_inject_key(Display *dpy, Window w, int keycode, unsigned int state)
{
  char cmd[96];
  char line[96];

  snprintf(cmd, sizeof(cmd), "INJECT_KEY %u %d %u\n", (uint)w, keycode, state);
  if(selftest_cmd(dpy, cmd, line, sizeof(line)) < 0)
    return 0;
  return strncmp(line, "OK key_injected", 15) == 0;
}

static int
selftest_inject_key_release(Display *dpy, Window w, int keycode, unsigned int state)
{
  char cmd[96];
  char line[96];

  snprintf(cmd, sizeof(cmd), "INJECT_KEY_RELEASE %u %d %u\n", (uint)w, keycode, state);
  if(selftest_cmd(dpy, cmd, line, sizeof(line)) < 0)
    return 0;
  return strncmp(line, "OK key_release_injected", 23) == 0;
}

static int
selftest_inject_button(Display *dpy, Window w, int button,
                       unsigned int state, int x, int y)
{
  char cmd[96];
  char line[96];

  snprintf(cmd, sizeof(cmd), "INJECT_BUTTON %u %d %u %d %d\n",
           (uint)w, button, state, x, y);
  if(selftest_cmd(dpy, cmd, line, sizeof(line)) < 0)
    return 0;
  return strncmp(line, "OK button_injected", 18) == 0;
}

static int
selftest_inject_button_release(Display *dpy, Window w, int button,
                               unsigned int state, int x, int y)
{
  char cmd[96];
  char line[96];

  snprintf(cmd, sizeof(cmd), "INJECT_BUTTON_RELEASE %u %d %u %d %d\n",
           (uint)w, button, state, x, y);
  if(selftest_cmd(dpy, cmd, line, sizeof(line)) < 0)
    return 0;
  return strncmp(line, "OK button_release_injected", 26) == 0;
}

static int
selftest_inject_motion(Display *dpy, Window w, int x, int y, unsigned int state)
{
  char cmd[96];
  char line[96];

  snprintf(cmd, sizeof(cmd), "INJECT_MOTION %u %d %d %u\n", (uint)w, x, y, state);
  if(selftest_cmd(dpy, cmd, line, sizeof(line)) < 0)
    return 0;
  return strncmp(line, "OK motion_injected", 18) == 0;
}

static void
report(const char *name, int ok, const char *detail)
{
  if(ok) {
    g_pass++;
    printf("xwmselftest: PASS %-24s %s\n", name, detail ? detail : "");
  } else {
    g_fail++;
    printf("xwmselftest: FAIL %-24s %s\n", name, detail ? detail : "");
  }
}

static int
check_tree(Display *dpy, Window w,
           Window exp_root, Window exp_parent,
           Window *exp_children, unsigned int exp_n,
           const char *name)
{
  Window root;
  Window parent;
  Window *children;
  unsigned int nchild;
  unsigned int i;

  root = (Window)0;
  parent = (Window)0;
  children = (Window *)0;
  nchild = 0;

  if(!XQueryTree(dpy, w, &root, &parent, &children, &nchild)) {
    report(name, 0, "XQueryTree returned False");
    return 0;
  }

  if(root != exp_root || parent != exp_parent || nchild != exp_n) {
    char detail[192];
    snprintf(detail, sizeof(detail),
             "got root=%lu parent=%lu n=%u expected root=%lu parent=%lu n=%u",
             (ulong)root, (ulong)parent, nchild,
             (ulong)exp_root, (ulong)exp_parent, exp_n);
    report(name, 0, detail);
    if(children)
      XFree(children);
    return 0;
  }

  for(i = 0; i < exp_n; i++) {
    if(!children || children[i] != exp_children[i]) {
      char detail[192];
      snprintf(detail, sizeof(detail),
               "child[%u]=%lu expected=%lu",
               i,
               (ulong)(children ? children[i] : 0),
               (ulong)exp_children[i]);
      report(name, 0, detail);
      if(children)
        XFree(children);
      return 0;
    }
  }

  {
    char detail[96];
    snprintf(detail, sizeof(detail),
             "root=%lu parent=%lu n=%u", (ulong)root, (ulong)parent, nchild);
    report(name, 1, detail);
  }
  if(children)
    XFree(children);
  return 1;
}

static int
check_translate(Display *dpy, Window src, Window dst,
                int sx, int sy,
                int ex, int ey,
                Window expected_child,
                const char *name)
{
  int dx;
  int dy;
  Window child;
  int ok;

  dx = -9999;
  dy = -9999;
  child = (Window)0;

  if(!XTranslateCoordinates(dpy, src, dst, sx, sy, &dx, &dy, &child)) {
    report(name, 0, "XTranslateCoordinates returned False");
    return 0;
  }

  ok = (dx == ex && dy == ey && child == expected_child);
  if(!ok) {
    char detail[160];
    snprintf(detail, sizeof(detail),
             "got xy=%d,%d child=%lu expected xy=%d,%d child=%lu",
             dx, dy, (ulong)child, ex, ey, (ulong)expected_child);
    report(name, 0, detail);
    return 0;
  }

  {
    char detail[128];
    snprintf(detail, sizeof(detail),
             "xy=%d,%d child=%lu", dx, dy, (ulong)child);
    report(name, 1, detail);
  }
  return 1;
}

static int
check_tree_missing(Display *dpy, Window w, const char *name)
{
  Window root;
  Window parent;
  Window *children;
  unsigned int nchild;

  root = (Window)0;
  parent = (Window)0;
  children = (Window *)0;
  nchild = 0;

  if(XQueryTree(dpy, w, &root, &parent, &children, &nchild)) {
    if(children)
      XFree(children);
    report(name, 0, "window still queryable after destroy");
    return 0;
  }
  report(name, 1, "query correctly failed (destroyed)");
  return 1;
}

static int
check_translate_missing(Display *dpy, Window src, Window dst, const char *name)
{
  int dx;
  int dy;
  Window child;

  dx = 0;
  dy = 0;
  child = (Window)0;

  if(XTranslateCoordinates(dpy, src, dst, 0, 0, &dx, &dy, &child)) {
    report(name, 0, "unexpected success for missing window");
    return 0;
  }
  report(name, 1, "translate correctly failed");
  return 1;
}

static int
check_attrs(Display *dpy, Window w,
            int ex, int ey, int ew, int eh,
            int ebw, int emapped,
            const char *name)
{
  XWindowAttributes attrs;
  int mapped;

  if(!XGetWindowAttributes(dpy, w, &attrs)) {
    report(name, 0, "XGetWindowAttributes returned False");
    return 0;
  }

  mapped = (attrs.map_state == IsViewable) ? 1 : 0;
  if(attrs.x != ex || attrs.y != ey ||
     attrs.width != ew || attrs.height != eh ||
     attrs.border_width != ebw || mapped != emapped) {
    char detail[192];
    snprintf(detail, sizeof(detail),
             "got x=%d y=%d w=%d h=%d bw=%d mapped=%d expected x=%d y=%d w=%d h=%d bw=%d mapped=%d",
             attrs.x, attrs.y, attrs.width, attrs.height,
             attrs.border_width, mapped,
             ex, ey, ew, eh, ebw, emapped);
    report(name, 0, detail);
    return 0;
  }

  {
    char detail[128];
    snprintf(detail, sizeof(detail),
             "x=%d y=%d w=%d h=%d bw=%d mapped=%d",
             attrs.x, attrs.y, attrs.width, attrs.height,
             attrs.border_width, mapped);
    report(name, 1, detail);
  }
  return 1;
}

static void
drain_events(Display *dpy)
{
  XEvent ev;
  int guard;

  guard = 0;
  while(guard < 1024 && XPending(dpy) > 0) {
    if(XNextEvent(dpy, &ev) != 0)
      break;
    guard++;
  }
}

static int
event_window_match(const XEvent *ev, Window w,
                   int shape_type, int randr_type, int damage_type)
{
  if(!ev)
    return 0;

  if(ev->type == shape_type)
    return ((const XShapeEvent *)ev)->window == w;
  if(ev->type == randr_type)
    return ((const XRRScreenChangeNotifyEvent *)ev)->window == w;
  if(ev->type == damage_type)
    return ((const XDamageNotifyEvent *)ev)->drawable == w;

  if(ev->type == ConfigureNotify)
    return ev->xconfigure.window == w;
  if(ev->type == Expose)
    return ev->xexpose.window == w;
  if(ev->type == FocusIn || ev->type == FocusOut)
    return ev->xfocus.window == w;
  if(ev->type == KeyPress || ev->type == KeyRelease)
    return ev->xkey.window == w;
  if(ev->type == ButtonPress || ev->type == ButtonRelease)
    return ev->xbutton.window == w;
  if(ev->type == MotionNotify)
    return ev->xmotion.window == w;
  if(ev->type == PropertyNotify)
    return ev->xproperty.window == w;
  if(ev->type == ColormapNotify)
    return ev->xcolormap.window == w;
  if(ev->type == MapNotify)
    return ev->xmap.window == w;
  if(ev->type == SelectionClear)
    return ev->xselectionclear.window == w;
  if(ev->type == SelectionRequest)
    return ev->xselectionrequest.owner == w;
  if(ev->type == SelectionNotify)
    return ev->xselection.requestor == w;

  return ev->xany.window == w;
}

static int
wait_event_type_for_window(Display *dpy, Window w, int wanted_type,
                           int shape_type, int randr_type, int damage_type,
                           XEvent *out)
{
  XEvent ev;
  int spin;
  int saw;

  for(spin = 0; spin < 256; spin++) {
    saw = 0;
    while(XPending(dpy) > 0) {
      saw = 1;
      if(XNextEvent(dpy, &ev) != 0)
        return 0;
      if(ev.type == wanted_type &&
         event_window_match(&ev, w, shape_type, randr_type, damage_type)) {
        if(out)
          *out = ev;
        return 1;
      }
    }
    if(!saw)
      continue;
  }
  return 0;
}

static int
count_event_type_for_window(Display *dpy, Window w, int wanted_type,
                            int shape_type, int randr_type, int damage_type)
{
  XEvent ev;
  int spin;
  int saw;
  int count;

  count = 0;
  for(spin = 0; spin < 256; spin++) {
    saw = 0;
    while(XPending(dpy) > 0) {
      saw = 1;
      if(XNextEvent(dpy, &ev) != 0)
        return count;
      if(ev.type == wanted_type &&
         event_window_match(&ev, w, shape_type, randr_type, damage_type))
        count++;
    }
    if(!saw)
      continue;
  }
  return count;
}

static int
check_query_pointer(Display *dpy, Window query_w,
                    Window exp_root, Window exp_child,
                    int exp_x, int exp_y,
                    int exp_wx, int exp_wy,
                    const char *name)
{
  Window qroot;
  Window qchild;
  int qx;
  int qy;
  int wx;
  int wy;
  unsigned int qmask;
  int spin;

  qroot = None;
  qchild = None;
  qx = qy = wx = wy = -1;
  qmask = 0;

  for(spin = 0; spin < 16; spin++) {
     if(XQueryPointer(dpy, query_w, &qroot, &qchild, &qx, &qy, &wx, &wy, &qmask) &&
       qroot == exp_root && qchild == exp_child &&
       qx == exp_x && qy == exp_y &&
       wx == exp_wx && wy == exp_wy) {
      char detail[128];
      snprintf(detail, sizeof(detail),
            "root=%lu child=%lu x=%d y=%d wx=%d wy=%d",
            (ulong)qroot, (ulong)qchild, qx, qy, wx, wy);
      report(name, 1, detail);
      return 1;
    }
    XSync(dpy, False);
  }

  {
    char detail[192];
    snprintf(detail, sizeof(detail),
             "got root=%lu child=%lu x=%d y=%d wx=%d wy=%d expected root=%lu child=%lu x=%d y=%d wx=%d wy=%d",
             (ulong)qroot, (ulong)qchild, qx, qy, wx, wy,
             (ulong)exp_root, (ulong)exp_child, exp_x, exp_y, exp_wx, exp_wy);
    report(name, 0, detail);
  }
  return 0;
}

static int
check_configure_before_expose(Display *dpy, Window w,
                              int shape_type, int randr_type, int damage_type,
                              const char *name)
{
  XEvent ev;
  int spin;
  int saw;
  int idx;
  int cfg_idx;
  int exp_idx;

  idx = 0;
  cfg_idx = -1;
  exp_idx = -1;
  for(spin = 0; spin < 96; spin++) {
    if((spin % 8) == 0)
      XSync(dpy, False);
    saw = 0;
    while(XPending(dpy) > 0) {
      saw = 1;
      if(XNextEvent(dpy, &ev) != 0)
        break;
      if(!event_window_match(&ev, w, shape_type, randr_type, damage_type))
        continue;
      if(ev.type == ConfigureNotify && cfg_idx < 0)
        cfg_idx = idx++;
      else if(ev.type == Expose && exp_idx < 0)
        exp_idx = idx++;
      if(cfg_idx >= 0 && exp_idx >= 0)
        break;
    }
    if(cfg_idx >= 0 && exp_idx >= 0)
      break;
    if(!saw)
      continue;
  }

  if(cfg_idx < 0) {
    report(name, 0, "missing ConfigureNotify");
    return 0;
  }

  if(exp_idx < 0) {
    report(name, 1, "ConfigureNotify observed (no Expose emitted)");
    return 1;
  }

  if(cfg_idx >= exp_idx) {
    report(name, 0, "Expose arrived before ConfigureNotify");
    return 0;
  }
  report(name, 1, "ConfigureNotify observed before Expose");
  return 1;
}

int
main(void)
{
  Display *dpy;
  Display *dpy2;
  Window root;
  Window root2;
  Window parent;
  Window parent2;
  Window child_a;
  Window child_b;
  Window stack_parent;
  Window stack_a;
  Window stack_b;
  Window stack_c;
  Window foreign_parent;
  Window foreign_child;
  Window prune_parent;
  Window prune_a;
  Window prune_b;
  Window temp_window;
  Window doomed_parent;
  Window doomed_child;
  Window clear_window;
  Window cmap_window;
  Window geom_window;
  Window sel_owner;
  Window sel_requestor;
  Window mc_owner;
  Window mc_requestor;
  Window mc_focus2;
  XEvent ev;
  XWindowChanges wc;
  XRectangle sr;
  Atom p_atom;
  Atom p_type;
  Atom sel_atom;
  Atom sel_target;
  Atom sel_prop;
  Atom mc_sel_atom;
  Atom mc_sel_target;
  Atom mc_sel_prop;
  Window focus_cur;
  int shape_base;
  int shape_err;
  int randr_base;
  int randr_err;
  int damage_base;
  int damage_err;
  int shape_type = -1;
  int randr_type = -1;
  int damage_type = -1;
  int damage_count;
  Damage dmg;
  GC geom_gc;
  unsigned char pval[2];
  int shape_major;
  int shape_minor;
  int randr_major;
  int randr_minor;
  int damage_major;
  int damage_minor;
  int revert_to;
  int grab_rc;
  int injected_keycode;
  int (*prev_error_handler)(Display *, XErrorEvent *);
  Window sel_owner_q;
  Window mc_owner_q;
  unsigned long black;
  unsigned long white;

  printf("xwmselftest: starting XTranslateCoordinates wm-path checks\n");

  dpy = XOpenDisplay(0);
  dpy2 = (Display *)0;
  if(!dpy) {
    printf("xwmselftest: FAIL open-display\n");
    return 1;
  }

  root = DefaultRootWindow(dpy);
  black = XBlackPixel(dpy, DefaultScreen(dpy));
  white = XWhitePixel(dpy, DefaultScreen(dpy));

  parent = XCreateSimpleWindow(dpy, root, 40, 40, 220, 140, 0, black, black);
  parent2 = XCreateSimpleWindow(dpy, root, 320, 40, 220, 140, 0, black, black);
  child_a = XCreateSimpleWindow(dpy, parent, 10, 10, 60, 40, 0, white, white);
  child_b = XCreateSimpleWindow(dpy, parent, 100, 10, 60, 40, 0, white, white);
  clear_window = (Window)0;
  cmap_window = (Window)0;

  XMapWindow(dpy, parent);
  XMapWindow(dpy, parent2);
  XMapWindow(dpy, child_a);
  XMapWindow(dpy, child_b);
  XSync(dpy, False);

  {
    Window kids[2];
    kids[0] = child_a;
    kids[1] = child_b;
    check_tree(dpy, parent, root, root, kids, 2, "tree parent initial");
  }

  if(XReparentWindow(dpy, child_b, parent2, 20, 15) != 0) {
    report("reparent child_b", 0, "XReparentWindow failed");
  } else {
    report("reparent child_b", 1, "moved under parent2 at 20,15");
  }
  XSync(dpy, False);

  {
    Window kids1[1];
    Window kids2[1];
    kids1[0] = child_a;
    kids2[0] = child_b;
    check_tree(dpy, parent, root, root, kids1, 1, "tree parent after");
    check_tree(dpy, parent2, root, root, kids2, 1, "tree parent2 after");
    check_tree(dpy, child_b, root, parent2, (Window *)0, 0, "tree child_b after");
  }

  check_translate(dpy, root, parent,
                  55, 55,
                  15, 15,
                  child_a,
                  "root->parent child_a");

  check_translate(dpy, root, parent,
                  145, 55,
                  105, 15,
                  None,
                  "root->parent old-child_b");

  check_translate(dpy, root, parent,
                  75, 105,
                  35, 65,
                  None,
                  "root->parent no-child");

  check_translate(dpy, parent, root,
                  12, 12,
                  52, 52,
                  child_a,
                  "parent->root child_a");

  check_translate(dpy, parent, root,
                  30, 80,
                  70, 120,
                  parent,
                  "parent->root parent");

  check_translate(dpy, root, parent2,
                  345, 70,
                  25, 30,
                  child_b,
                  "root->parent2 child_b");

  geom_window = XCreateSimpleWindow(dpy, root, 560, 260, 80, 50, 3, white, black);
  XMapWindow(dpy, geom_window);
  XSync(dpy, False);

  check_attrs(dpy, geom_window, 560, 260, 80, 50, 3, 1, "attrs geom initial");

  if(XMoveWindow(dpy, geom_window, 580, 280) == 0)
    report("move geom", 1, "accepted");
  else
    report("move geom", 0, "XMoveWindow failed");
  XSync(dpy, False);
  check_attrs(dpy, geom_window, 580, 280, 80, 50, 3, 1, "attrs geom moved");

  if(XResizeWindow(dpy, geom_window, 96, 64) == 0)
    report("resize geom", 1, "accepted");
  else
    report("resize geom", 0, "XResizeWindow failed");
  XSync(dpy, False);
  check_attrs(dpy, geom_window, 580, 280, 96, 64, 3, 1, "attrs geom resized");

  {
    XWindowChanges wc;
    wc.x = 600;
    wc.width = 120;
    if(XConfigureWindow(dpy, geom_window, CWX | CWWidth, &wc) == 0)
      report("configure geom xw", 1, "accepted");
    else
      report("configure geom xw", 0, "XConfigureWindow failed");
  }
  XSync(dpy, False);
  check_attrs(dpy, geom_window, 600, 280, 120, 64, 3, 1, "attrs geom xw");

  {
    XWindowChanges wc;
    wc.y = 300;
    wc.height = 70;
    if(XConfigureWindow(dpy, geom_window, CWY | CWHeight, &wc) == 0)
      report("configure geom yh", 1, "accepted");
    else
      report("configure geom yh", 0, "XConfigureWindow failed");
  }
  XSync(dpy, False);
  check_attrs(dpy, geom_window, 600, 300, 120, 70, 3, 1, "attrs geom yh");

  if(XResizeWindow(dpy, geom_window, 0, 0) == 0)
    report("resize geom min", 1, "accepted");
  else
    report("resize geom min", 0, "XResizeWindow failed");
  XSync(dpy, False);
  check_attrs(dpy, geom_window, 600, 300, 1, 1, 3, 1, "attrs geom min");

  if(XMoveWindow(dpy, (Window)999999, 1, 1) != 0)
    report("move bad wid", 1, "expected failure");
  else
    report("move bad wid", 0, "unexpected success");

  if(XResizeWindow(dpy, (Window)999999, 10, 10) != 0)
    report("resize bad wid", 1, "expected failure");
  else
    report("resize bad wid", 0, "unexpected success");

  {
    wc.x = 1;
    wc.y = 1;
    wc.width = 10;
    wc.height = 10;
    if(XConfigureWindow(dpy, (Window)999999, CWX | CWY | CWWidth | CWHeight, &wc) != 0)
      report("configure bad wid", 1, "expected failure");
    else
      report("configure bad wid", 0, "unexpected success");
  }
  XSync(dpy, False);

  if(XSelectInput(dpy, geom_window,
                  StructureNotifyMask | ExposureMask | PropertyChangeMask) == 0)
    report("select geom events", 1, "accepted");
  else
    report("select geom events", 0, "XSelectInput failed");
  XSync(dpy, False);
  drain_events(dpy);

  clear_window = XCreateSimpleWindow(dpy, root, 560, 360, 48, 32, 0, white, black);
  cmap_window = XCreateSimpleWindow(dpy, root, 620, 360, 48, 32, 0, white, black);
  XMapWindow(dpy, clear_window);
  XMapWindow(dpy, cmap_window);
  XSync(dpy, False);

  if(XSelectInput(dpy, clear_window, ExposureMask) == 0)
    report("clear select expose", 1, "accepted");
  else
    report("clear select expose", 0, "XSelectInput failed");

  if(XSetWindowBackground(dpy, clear_window, 0x00112233UL) == 0)
    report("clear set backpixel", 1, "accepted");
  else
    report("clear set backpixel", 0, "XSetWindowBackground failed");

  drain_events(dpy);
  if(XClearArea(dpy, clear_window, 3, 4, 17, 11, True) == 0)
    report("clear area pixel", 1, "accepted");
  else
    report("clear area pixel", 0, "XClearArea failed");
  XSync(dpy, False);

  if(wait_event_type_for_window(dpy, clear_window, Expose,
                                shape_type, randr_type, damage_type, &ev)) {
    if(ev.xexpose.x == 3 && ev.xexpose.y == 4 &&
       ev.xexpose.width == 17 && ev.xexpose.height == 11)
      report("clear expose pixel", 1, "Expose geometry matched");
    else
      report("clear expose pixel", 0, "Expose geometry mismatch");
  } else {
    report("clear expose pixel", 0, "Expose missing");
  }

  {
    Pixmap bgpm;
    GC bg_gc;

    bgpm = XCreatePixmap(dpy, clear_window, 8, 8,
                         (unsigned int)DefaultDepth(dpy, DefaultScreen(dpy)));
    bg_gc = (GC)0;
    if(bgpm != None)
      bg_gc = XCreateGC(dpy, bgpm, 0, 0);

    if(bgpm != None && bg_gc != 0) {
      XSetForeground(dpy, bg_gc, 0x00556677UL);
      XFillRectangle(dpy, bgpm, bg_gc, 0, 0, 8, 8);
      if(XSetWindowBackgroundPixmap(dpy, clear_window, bgpm) == 0)
        report("clear set backpixmap", 1, "accepted");
      else
        report("clear set backpixmap", 0, "XSetWindowBackgroundPixmap failed");

      drain_events(dpy);
      if(XClearArea(dpy, clear_window, 0, 0, 8, 8, True) == 0)
        report("clear area pixmap", 1, "accepted");
      else
        report("clear area pixmap", 0, "XClearArea failed");
      XSync(dpy, False);

      if(wait_event_type_for_window(dpy, clear_window, Expose,
                                    shape_type, randr_type, damage_type, &ev))
        report("clear expose pixmap", 1, "Expose observed");
      else
        report("clear expose pixmap", 0, "Expose missing");

      XFreeGC(dpy, bg_gc);
      XFreePixmap(dpy, bgpm);
    } else {
      report("clear set backpixmap", 0, "pixmap/gc setup failed");
      if(bg_gc)
        XFreeGC(dpy, bg_gc);
      if(bgpm != None)
        XFreePixmap(dpy, bgpm);
    }
  }

  if(XSetWindowBackgroundPixmap(dpy, clear_window, (Pixmap)999999) != 0)
    report("clear invalid pixmap", 1, "expected failure");
  else
    report("clear invalid pixmap", 0, "unexpected success");

  if(XSelectInput(dpy, cmap_window, ColormapChangeMask) == 0)
    report("cmap select notify", 1, "accepted");
  else
    report("cmap select notify", 0, "XSelectInput failed");

  {
    XSetWindowAttributes swa;
    memset(&swa, 0, sizeof(swa));
    swa.colormap = (Colormap)77;
    drain_events(dpy);
    if(XChangeWindowAttributes(dpy, cmap_window, CWColormap, &swa) == 0)
      report("cmap set 77", 1, "accepted");
    else
      report("cmap set 77", 0, "XChangeWindowAttributes failed");
    XSync(dpy, False);

    if(wait_event_type_for_window(dpy, cmap_window, ColormapNotify,
                                  shape_type, randr_type, damage_type, &ev)) {
      if(ev.xcolormap.colormap == (Colormap)77 &&
         ev.xcolormap.c_new == True &&
         ev.xcolormap.state == ColormapUninstalled)
        report("cmap notify set", 1, "new/uninstalled event matched");
      else
        report("cmap notify set", 0, "unexpected ColormapNotify payload");
    } else {
      report("cmap notify set", 0, "ColormapNotify missing");
    }
  }

  drain_events(dpy);
  if(XInstallColormap(dpy, (Colormap)77) == 0)
    report("cmap install 77", 1, "accepted");
  else
    report("cmap install 77", 0, "XInstallColormap failed");
  XSync(dpy, False);

  if(wait_event_type_for_window(dpy, cmap_window, ColormapNotify,
                                shape_type, randr_type, damage_type, &ev)) {
    if(ev.xcolormap.colormap == (Colormap)77 &&
       ev.xcolormap.c_new == False &&
       ev.xcolormap.state == ColormapInstalled)
      report("cmap notify install", 1, "installed event matched");
    else
      report("cmap notify install", 0, "unexpected install payload");
  } else {
    report("cmap notify install", 0, "ColormapNotify missing");
  }

  shape_base = 0;
  shape_err = 0;
  shape_major = 0;
  shape_minor = 0;
  if(XShapeQueryExtension(dpy, &shape_base, &shape_err))
    report("shape query ext", 1, "available");
  else
    report("shape query ext", 0, "missing");
  if(XShapeQueryVersion(dpy, &shape_major, &shape_minor))
    report("shape query ver", 1, "available");
  else
    report("shape query ver", 0, "missing");

  randr_base = 0;
  randr_err = 0;
  randr_major = 0;
  randr_minor = 0;
  if(XRRQueryExtension(dpy, &randr_base, &randr_err))
    report("randr query ext", 1, "available");
  else
    report("randr query ext", 0, "missing");
  if(XRRQueryVersion(dpy, &randr_major, &randr_minor))
    report("randr query ver", 1, "available");
  else
    report("randr query ver", 0, "missing");

  damage_base = 0;
  damage_err = 0;
  damage_major = 0;
  damage_minor = 0;
  if(XDamageQueryExtension(dpy, &damage_base, &damage_err))
    report("damage query ext", 1, "available");
  else
    report("damage query ext", 0, "missing");
  if(XDamageQueryVersion(dpy, &damage_major, &damage_minor))
    report("damage query ver", 1, "available");
  else
    report("damage query ver", 0, "missing");

  shape_type = shape_base + ShapeNotify;
  randr_type = randr_base + RRScreenChangeNotify;
  damage_type = damage_base + XDamageNotify;

  XShapeSelectInput(dpy, geom_window, ShapeNotifyMask);
  XSync(dpy, False);
  drain_events(dpy);

  sr.x = 2;
  sr.y = 3;
  sr.width = 7;
  sr.height = 5;
  XShapeCombineRectangles(dpy, geom_window, ShapeBounding,
                          0, 0, &sr, 1, ShapeSet, 0);
  XSync(dpy, False);
  if(wait_event_type_for_window(dpy, geom_window, shape_type,
                                shape_type, randr_type, damage_type, &ev)) {
    XShapeEvent *sev;
    sev = (XShapeEvent *)&ev;
    if(sev->kind == ShapeBounding && sev->shaped)
      report("shape notify set", 1, "bounding shaped event");
    else
      report("shape notify set", 0, "unexpected shape payload");
  } else {
    report("shape notify set", 0, "shape event missing");
  }

  XShapeCombineRectangles(dpy, geom_window, ShapeBounding,
                          0, 0, (XRectangle *)0, 0, ShapeSet, 0);
  XSync(dpy, False);
  if(wait_event_type_for_window(dpy, geom_window, shape_type,
                                shape_type, randr_type, damage_type, &ev)) {
    XShapeEvent *sev;
    sev = (XShapeEvent *)&ev;
    if(sev->kind == ShapeBounding && !sev->shaped)
      report("shape notify clear", 1, "bounding clear event");
    else
      report("shape notify clear", 0, "unexpected clear payload");
  } else {
    report("shape notify clear", 0, "shape clear event missing");
  }

  XRRSelectInput(dpy, geom_window, RRScreenChangeNotifyMask);
  XSync(dpy, False);
  drain_events(dpy);

  if(XResizeWindow(dpy, geom_window, 140, 90) == 0)
    report("randr trigger resize", 1, "accepted");
  else
    report("randr trigger resize", 0, "XResizeWindow failed");
  XSync(dpy, False);
  if(wait_event_type_for_window(dpy, geom_window, randr_type,
                                shape_type, randr_type, damage_type, &ev)) {
    XRRScreenChangeNotifyEvent *rev;
    rev = (XRRScreenChangeNotifyEvent *)&ev;
    if(rev->width == 140 && rev->height == 90)
      report("randr notify resize", 1, "screen-change payload matched");
    else
      report("randr notify resize", 0, "unexpected randr payload");

    if(XRRUpdateConfiguration(&ev) == 1)
      report("randr update conf", 1, "accepted");
    else
      report("randr update conf", 0, "XRRUpdateConfiguration failed");
  } else {
    report("randr notify resize", 0, "randr event missing");
    report("randr update conf", 0, "randr event unavailable");
  }

  drain_events(dpy);
  if(XMoveResizeWindow(dpy, geom_window, 610, 310, 150, 100) == 0)
    report("configure geom order", 1, "accepted");
  else
    report("configure geom order", 0, "XMoveResizeWindow failed");
  XSync(dpy, False);
  check_configure_before_expose(dpy, geom_window,
                                shape_type, randr_type, damage_type,
                                "event cfg before exp");

  p_atom = XInternAtom(dpy, "_AUXV6_XWMSELFTEST_PROP", False);
  p_type = XInternAtom(dpy, "STRING", False);
  pval[0] = '1';
  pval[1] = 0;
  drain_events(dpy);
  if(XChangeProperty(dpy, geom_window, p_atom, p_type,
                     8, PropModeReplace, pval, 1) == 0)
    report("property change set", 1, "accepted");
  else
    report("property change set", 0, "XChangeProperty failed");
  XSync(dpy, False);
  if(wait_event_type_for_window(dpy, geom_window, PropertyNotify,
                                shape_type, randr_type, damage_type, &ev))
    report("property notify set", 1, "event observed");
  else
    report("property notify set", 0, "PropertyNotify missing");

  geom_gc = XCreateGC(dpy, geom_window, 0, (void *)0);
  if(geom_gc != 0)
    report("damage gc create", 1, "accepted");
  else
    report("damage gc create", 0, "XCreateGC failed");

  dmg = XDamageCreate(dpy, geom_window, XDamageReportNonEmpty);
  if(dmg != 0)
    report("damage create", 1, "accepted");
  else
    report("damage create", 0, "XDamageCreate failed");
  XSync(dpy, False);
  drain_events(dpy);

  if(geom_gc != 0) {
    XFillRectangle(dpy, geom_window, geom_gc, 0, 0, 20, 20);
    XFillRectangle(dpy, geom_window, geom_gc, 5, 5, 20, 20);
    XSync(dpy, False);
    damage_count = count_event_type_for_window(dpy, geom_window, damage_type,
                                               shape_type, randr_type, damage_type);
    if(damage_count == 1)
      report("damage coalesce", 1, "single merged damage event");
    else {
      char detail[96];
      snprintf(detail, sizeof(detail), "damage events=%d expected=1", damage_count);
      report("damage coalesce", 0, detail);
    }
  } else {
    report("damage coalesce", 0, "missing GC");
  }

  if(dmg != 0)
    XDamageDestroy(dpy, dmg);
  if(geom_gc != 0)
    XFreeGC(dpy, geom_gc);

  sel_owner = XCreateSimpleWindow(dpy, root, 560, 420, 70, 40, 0, white, black);
  sel_requestor = XCreateSimpleWindow(dpy, root, 640, 420, 70, 40, 0, white, black);
  XMapWindow(dpy, sel_owner);
  XMapWindow(dpy, sel_requestor);
  XSync(dpy, False);
  drain_events(dpy);

  sel_atom = XInternAtom(dpy, "PRIMARY", False);
  sel_target = XInternAtom(dpy, "UTF8_STRING", False);
  if(sel_target == None)
    sel_target = XInternAtom(dpy, "STRING", False);
  sel_prop = XInternAtom(dpy, "_AUXV6_XWMSELFTEST_SEL", False);

  if(XSetSelectionOwner(dpy, sel_atom, sel_owner, CurrentTime) != 0)
    report("selection owner set", 1, "accepted");
  else
    report("selection owner set", 0, "XSetSelectionOwner failed");

  sel_owner_q = XGetSelectionOwner(dpy, sel_atom);
  if(sel_owner_q == sel_owner)
    report("selection owner get", 1, "owner matched");
  else
    report("selection owner get", 0, "owner mismatch");

  drain_events(dpy);
  if(XConvertSelection(dpy, sel_atom, sel_target, sel_prop,
                       sel_requestor, CurrentTime) != 0)
    report("selection convert", 1, "accepted");
  else
    report("selection convert", 0, "XConvertSelection failed");
  XSync(dpy, False);

  if(wait_event_type_for_window(dpy, sel_owner, SelectionRequest,
                                shape_type, randr_type, damage_type, &ev)) {
    if(ev.xselectionrequest.requestor == sel_requestor &&
       ev.xselectionrequest.selection == sel_atom)
      report("selection request evt", 1, "owner saw request");
    else
      report("selection request evt", 0, "request payload mismatch");
  } else {
    report("selection request evt", 0, "SelectionRequest missing");
  }

  memset(&ev, 0, sizeof(ev));
  ev.xselection.type = SelectionNotify;
  ev.xselection.display = dpy;
  ev.xselection.requestor = sel_requestor;
  ev.xselection.selection = sel_atom;
  ev.xselection.target = sel_target;
  ev.xselection.property = sel_prop;
  ev.xselection.time = CurrentTime;
  if(XSendEvent(dpy, sel_requestor, False, 0, &ev) != 0)
    report("selection notify send", 1, "accepted");
  else
    report("selection notify send", 0, "XSendEvent failed");
  XSync(dpy, False);

  if(wait_event_type_for_window(dpy, sel_requestor, SelectionNotify,
                                shape_type, randr_type, damage_type, &ev))
    report("selection notify evt", 1, "requestor saw notify");
  else
    report("selection notify evt", 0, "SelectionNotify missing");

  if(XSetSelectionOwner(dpy, sel_atom, sel_requestor, CurrentTime) != 0)
    report("selection owner swap", 1, "accepted");
  else
    report("selection owner swap", 0, "XSetSelectionOwner failed");
  XSync(dpy, False);

  if(wait_event_type_for_window(dpy, sel_owner, SelectionClear,
                                shape_type, randr_type, damage_type, &ev))
    report("selection clear evt", 1, "previous owner cleared");
  else
    report("selection clear evt", 0, "SelectionClear missing");

  if(XSetSelectionOwner(dpy, sel_atom, None, CurrentTime) != 0)
    report("selection owner none", 1, "accepted");
  else
    report("selection owner none", 0, "XSetSelectionOwner failed");

  sel_owner_q = XGetSelectionOwner(dpy, sel_atom);
  if(sel_owner_q == None)
    report("selection owner noneget", 1, "owner cleared");
  else
    report("selection owner noneget", 0, "owner not cleared");

  drain_events(dpy);
  if(XConvertSelection(dpy, sel_atom, sel_target, sel_prop,
                       sel_requestor, CurrentTime) != 0)
    report("selection convert none", 1, "accepted");
  else
    report("selection convert none", 0, "XConvertSelection failed");
  XSync(dpy, False);

  if(wait_event_type_for_window(dpy, sel_requestor, SelectionNotify,
                                shape_type, randr_type, damage_type, &ev)) {
    if(ev.xselection.selection == sel_atom && ev.xselection.property == None)
      report("selection notify none", 1, "property=None as expected");
    else
      report("selection notify none", 0, "SelectionNotify payload mismatch");
  } else {
    report("selection notify none", 0, "SelectionNotify missing");
  }

  if(XSetSelectionOwner(dpy, sel_atom, sel_owner, 50) != 0)
    report("selection owner timed", 1, "accepted");
  else
    report("selection owner timed", 0, "XSetSelectionOwner failed");
  XSync(dpy, False);
  drain_events(dpy);

  if(XConvertSelection(dpy, sel_atom, sel_target, sel_prop,
                       sel_requestor, 10) != 0)
    report("selection convert stale", 1, "accepted");
  else
    report("selection convert stale", 0, "XConvertSelection failed");
  XSync(dpy, False);

  if(wait_event_type_for_window(dpy, sel_requestor, SelectionNotify,
                                shape_type, randr_type, damage_type, &ev)) {
    if(ev.xselection.selection == sel_atom && ev.xselection.property == None)
      report("selection notify stale", 1, "property=None as expected");
    else
      report("selection notify stale", 0, "SelectionNotify payload mismatch");
  } else {
    report("selection notify stale", 0, "SelectionNotify missing");
  }

  if(XSetSelectionOwner(dpy, sel_atom, None, CurrentTime) != 0)
    report("selection owner reset", 1, "accepted");
  else
    report("selection owner reset", 0, "XSetSelectionOwner failed");

  if(XSelectInput(dpy, geom_window,
                  StructureNotifyMask | ExposureMask | PropertyChangeMask |
                      FocusChangeMask | KeyPressMask | KeyReleaseMask |
                      ButtonPressMask | ButtonReleaseMask |
                      PointerMotionMask | EnterWindowMask | LeaveWindowMask) == 0)
    report("select input events", 1, "accepted");
  else
    report("select input events", 0, "XSelectInput failed");
  if(XSelectInput(dpy, sel_owner, FocusChangeMask) == 0)
    report("select owner focus", 1, "accepted");
  else
    report("select owner focus", 0, "XSelectInput failed");
  XSync(dpy, False);
  drain_events(dpy);

  if(XSetInputFocus(dpy, geom_window, RevertToNone, CurrentTime) == 0)
    report("focus set geom", 1, "accepted");
  else
    report("focus set geom", 0, "XSetInputFocus failed");
  XSync(dpy, False);

  if(wait_event_type_for_window(dpy, geom_window, FocusIn,
                                shape_type, randr_type, damage_type, &ev))
    report("focusin geom", 1, "event observed");
  else
    report("focusin geom", 0, "FocusIn missing");

  focus_cur = None;
  revert_to = -1;
  if(XGetInputFocus(dpy, &focus_cur, &revert_to) == 0 && focus_cur == geom_window)
    report("focus get geom", 1, "focus matched");
  else
    report("focus get geom", 0, "focus mismatch");

  if(XSetInputFocus(dpy, sel_owner, RevertToNone, CurrentTime) == 0)
    report("focus set owner", 1, "accepted");
  else
    report("focus set owner", 0, "XSetInputFocus failed");
  XSync(dpy, False);

  if(wait_event_type_for_window(dpy, geom_window, FocusOut,
                                shape_type, randr_type, damage_type, &ev))
    report("focusout geom", 1, "event observed");
  else
    report("focusout geom", 0, "FocusOut missing");

  if(wait_event_type_for_window(dpy, sel_owner, FocusIn,
                                shape_type, randr_type, damage_type, &ev))
    report("focusin owner", 1, "event observed");
  else
    report("focusin owner", 0, "FocusIn missing");

  focus_cur = None;
  revert_to = -1;
  if(XGetInputFocus(dpy, &focus_cur, &revert_to) == 0 && focus_cur == sel_owner)
    report("focus get owner", 1, "focus matched");
  else
    report("focus get owner", 0, "focus mismatch");

  if(XSetInputFocus(dpy, root, RevertToNone, CurrentTime) == 0)
    report("focus clear root", 1, "accepted");
  else
    report("focus clear root", 0, "XSetInputFocus failed");
  XSync(dpy, False);

  if(wait_event_type_for_window(dpy, sel_owner, FocusOut,
                                shape_type, randr_type, damage_type, &ev))
    report("focusout owner", 1, "event observed");
  else
    report("focusout owner", 0, "FocusOut missing");

  focus_cur = (Window)1;
  revert_to = -1;
  if(XGetInputFocus(dpy, &focus_cur, &revert_to) == 0 && focus_cur == None)
    report("focus get none", 1, "focus cleared");
  else
    report("focus get none", 0, "focus not cleared");

  if(XSetInputFocus(dpy, (Window)0x00c0ffee, RevertToNone, CurrentTime) != 0)
    report("focus set bad", 1, "rejected missing window");
  else
    report("focus set bad", 0, "unexpected success");

  if(XWarpPointer(dpy, None, None, 0, 0, 0, 0, 615, 315) == 0)
    report("warp pointer prep", 1, "accepted");
  else
    report("warp pointer prep", 0, "XWarpPointer failed");
  XSync(dpy, False);
  drain_events(dpy);

  if(XWarpPointer(dpy, None, None, 0, 0, 0, 0, 2, 2) == 0)
    report("warp pointer root", 1, "accepted");
  else
    report("warp pointer root", 0, "XWarpPointer failed");
  XSync(dpy, False);

  if(wait_event_type_for_window(dpy, geom_window, LeaveNotify,
                                shape_type, randr_type, damage_type, &ev))
    report("leave geom", 1, "crossing observed");
  else
    report("leave geom", 0, "LeaveNotify missing");

  check_query_pointer(dpy, root, root, None, 2, 2, 2, 2, "query pointer root");
  check_query_pointer(dpy, geom_window, root, None,
                      2, 2,
                      2 - 610, 2 - 310,
                      "query pointer geom out");

  if(XWarpPointer(dpy, None, None, 0, 0, 0, 0, 615, 315) == 0)
    report("warp pointer geom", 1, "accepted");
  else
    report("warp pointer geom", 0, "XWarpPointer failed");
  XSync(dpy, False);

  if(wait_event_type_for_window(dpy, geom_window, EnterNotify,
                                shape_type, randr_type, damage_type, &ev))
    report("enter geom", 1, "crossing observed");
  else
    report("enter geom", 0, "EnterNotify missing");

  check_query_pointer(dpy, root, root, geom_window, 615, 315, 615, 315,
                      "query pointer geom");
  check_query_pointer(dpy, geom_window, root, None, 615, 315, 5, 5,
                      "query pointer geom in");

  grab_rc = XGrabKeyboard(dpy, geom_window, False,
                          GrabModeAsync, GrabModeAsync, CurrentTime);
  if(grab_rc != GrabSuccess)
    report("grab keyboard deny", 1, "non-WM denied");
  else
    report("grab keyboard deny", 0, "unexpected keyboard grab success");

  grab_rc = XGrabPointer(dpy, geom_window, False,
                         ButtonPressMask | PointerMotionMask,
                         GrabModeAsync, GrabModeAsync,
                         None, None, CurrentTime);
  if(grab_rc == GrabSuccess)
    report("grab pointer", 1, "accepted");
  else
    report("grab pointer", 0, "XGrabPointer failed");

  if(XAllowEvents(dpy, ReplayPointer, CurrentTime) == 0)
    report("allow events replay", 1, "accepted");
  else
    report("allow events replay", 0, "XAllowEvents failed");

  if(XUngrabPointer(dpy, CurrentTime) == 0)
    report("ungrab pointer", 1, "accepted");
  else
    report("ungrab pointer", 0, "XUngrabPointer failed");

  if(XGrabKey(dpy, 38, AnyModifier, geom_window, False,
              GrabModeAsync, GrabModeAsync) == 0)
    report("grab key", 1, "accepted");
  else
    report("grab key", 0, "XGrabKey failed");

  if(XUngrabKey(dpy, 38, AnyModifier, geom_window) == 0)
    report("ungrab key", 1, "accepted");
  else
    report("ungrab key", 0, "XUngrabKey failed");

  if(XGrabButton(dpy, 1, AnyModifier, geom_window, False,
                 ButtonPressMask, GrabModeAsync, GrabModeAsync,
                 None, None) == 0)
    report("grab button", 1, "accepted");
  else
    report("grab button", 0, "XGrabButton failed");

  if(XUngrabButton(dpy, 1, AnyModifier, geom_window) == 0)
    report("ungrab button", 1, "accepted");
  else
    report("ungrab button", 0, "XUngrabButton failed");

  XSync(dpy, False);
  drain_events(dpy);

  injected_keycode = 38;
  if(selftest_inject_key(dpy, geom_window, injected_keycode, ShiftMask))
    report("inject key cmd", 1, "accepted");
  else
    report("inject key cmd", 0, "INJECT_KEY failed");
  XSync(dpy, False);
  if(wait_event_type_for_window(dpy, geom_window, KeyPress,
                                shape_type, randr_type, damage_type, &ev)) {
    if((int)ev.xkey.keycode == injected_keycode && ev.xkey.state == ShiftMask)
      report("inject key evt", 1, "payload matched");
    else
      report("inject key evt", 0, "payload mismatch");
  } else {
    report("inject key evt", 0, "KeyPress missing");
  }

  if(selftest_inject_key_release(dpy, geom_window, injected_keycode, ShiftMask))
    report("inject keyrel cmd", 1, "accepted");
  else
    report("inject keyrel cmd", 0, "INJECT_KEY_RELEASE failed");
  XSync(dpy, False);
  if(wait_event_type_for_window(dpy, geom_window, KeyRelease,
                                shape_type, randr_type, damage_type, &ev)) {
    if((int)ev.xkey.keycode == injected_keycode && ev.xkey.state == ShiftMask)
      report("inject keyrel evt", 1, "payload matched");
    else
      report("inject keyrel evt", 0, "payload mismatch");
  } else {
    report("inject keyrel evt", 0, "KeyRelease missing");
  }

  if(selftest_inject_button(dpy, geom_window, 1, ControlMask, 17, 19))
    report("inject button cmd", 1, "accepted");
  else
    report("inject button cmd", 0, "INJECT_BUTTON failed");
  XSync(dpy, False);
  if(wait_event_type_for_window(dpy, geom_window, ButtonPress,
                                shape_type, randr_type, damage_type, &ev)) {
    if(ev.xbutton.button == 1 && ev.xbutton.state == ControlMask &&
       ev.xbutton.x == 17 && ev.xbutton.y == 19)
      report("inject button evt", 1, "payload matched");
    else
      report("inject button evt", 0, "payload mismatch");
  } else {
    report("inject button evt", 0, "ButtonPress missing");
  }

  if(selftest_inject_button_release(dpy, geom_window, 1, ControlMask, 17, 19))
    report("inject butrel cmd", 1, "accepted");
  else
    report("inject butrel cmd", 0, "INJECT_BUTTON_RELEASE failed");
  XSync(dpy, False);
  if(wait_event_type_for_window(dpy, geom_window, ButtonRelease,
                                shape_type, randr_type, damage_type, &ev)) {
    if(ev.xbutton.button == 1 && ev.xbutton.state == ControlMask &&
       ev.xbutton.x == 17 && ev.xbutton.y == 19)
      report("inject butrel evt", 1, "payload matched");
    else
      report("inject butrel evt", 0, "payload mismatch");
  } else {
    report("inject butrel evt", 0, "ButtonRelease missing");
  }

  if(selftest_inject_motion(dpy, geom_window, 22, 24, Button1Mask))
    report("inject motion cmd", 1, "accepted");
  else
    report("inject motion cmd", 0, "INJECT_MOTION failed");
  XSync(dpy, False);
  if(wait_event_type_for_window(dpy, geom_window, MotionNotify,
                                shape_type, randr_type, damage_type, &ev)) {
    if(ev.xmotion.x == 22 && ev.xmotion.y == 24 &&
       ev.xmotion.state == Button1Mask)
      report("inject motion evt", 1, "payload matched");
    else
      report("inject motion evt", 0, "payload mismatch");
  } else {
    report("inject motion evt", 0, "MotionNotify missing");
  }

  dpy2 = XOpenDisplay(0);
  if(!dpy2) {
    report("mc open display2", 0, "XOpenDisplay failed");
  } else {
    root2 = DefaultRootWindow(dpy2);
    if(root2 == root)
      report("mc root match", 1, "shared root id");
    else
      report("mc root match", 0, "root mismatch");

    mc_owner = XCreateSimpleWindow(dpy2, root2, 700, 420, 70, 40, 0, white, black);
    mc_requestor = XCreateSimpleWindow(dpy, root, 780, 420, 70, 40, 0, white, black);
    mc_focus2 = XCreateSimpleWindow(dpy2, root2, 700, 500, 90, 45, 0, white, black);

    XMapWindow(dpy2, mc_owner);
    XMapWindow(dpy, mc_requestor);
    XMapWindow(dpy2, mc_focus2);
    XSync(dpy2, False);
    XSync(dpy, False);
    drain_events(dpy);
    drain_events(dpy2);

    mc_sel_atom = XInternAtom(dpy, "CLIPBOARD", False);
    mc_sel_target = XInternAtom(dpy, "UTF8_STRING", False);
    if(mc_sel_target == None)
      mc_sel_target = XInternAtom(dpy, "STRING", False);
    mc_sel_prop = XInternAtom(dpy, "_AUXV6_XWMSELFTEST_MCSEL", False);

    if(XSetSelectionOwner(dpy2, mc_sel_atom, mc_owner, CurrentTime) != 0)
      report("mc sel owner set", 1, "accepted");
    else
      report("mc sel owner set", 0, "XSetSelectionOwner failed");

    mc_owner_q = XGetSelectionOwner(dpy, mc_sel_atom);
    if(mc_owner_q == mc_owner)
      report("mc sel owner get", 1, "cross-client owner visible");
    else
      report("mc sel owner get", 0, "owner mismatch");

    drain_events(dpy);
    drain_events(dpy2);
    if(XConvertSelection(dpy, mc_sel_atom, mc_sel_target, mc_sel_prop,
                         mc_requestor, CurrentTime) != 0)
      report("mc sel convert", 1, "accepted");
    else
      report("mc sel convert", 0, "XConvertSelection failed");
    XSync(dpy, False);
    XSync(dpy2, False);

    if(wait_event_type_for_window(dpy2, mc_owner, SelectionRequest,
                                  shape_type, randr_type, damage_type, &ev)) {
      if(ev.xselectionrequest.requestor == mc_requestor &&
         ev.xselectionrequest.selection == mc_sel_atom)
        report("mc sel request evt", 1, "routed to owner display");
      else
        report("mc sel request evt", 0, "payload mismatch");
    } else {
      report("mc sel request evt", 0, "SelectionRequest missing");
    }

    memset(&ev, 0, sizeof(ev));
    ev.xselection.type = SelectionNotify;
    ev.xselection.display = dpy2;
    ev.xselection.requestor = mc_requestor;
    ev.xselection.selection = mc_sel_atom;
    ev.xselection.target = mc_sel_target;
    ev.xselection.property = mc_sel_prop;
    ev.xselection.time = CurrentTime;
    if(XSendEvent(dpy2, mc_requestor, False, 0, &ev) != 0)
      report("mc sel notify send", 1, "accepted");
    else
      report("mc sel notify send", 0, "XSendEvent failed");
    XSync(dpy2, False);

    if(wait_event_type_for_window(dpy, mc_requestor, SelectionNotify,
                                  shape_type, randr_type, damage_type, &ev))
      report("mc sel notify evt", 1, "routed to requestor display");
    else
      report("mc sel notify evt", 0, "SelectionNotify missing");

    if(XSetSelectionOwner(dpy, mc_sel_atom, None, CurrentTime) != 0)
      report("mc sel owner none", 1, "accepted");
    else
      report("mc sel owner none", 0, "XSetSelectionOwner failed");
    XSync(dpy, False);

    if(wait_event_type_for_window(dpy2, mc_owner, SelectionClear,
                                  shape_type, randr_type, damage_type, &ev))
      report("mc sel clear evt", 1, "clear routed to old owner");
    else
      report("mc sel clear evt", 0, "SelectionClear missing");

    if(XSelectInput(dpy2, mc_focus2, FocusChangeMask) == 0)
      report("mc focus select", 1, "accepted");
    else
      report("mc focus select", 0, "XSelectInput failed");
    XSync(dpy2, False);
    drain_events(dpy);
    drain_events(dpy2);

    if(XSetInputFocus(dpy, mc_focus2, RevertToNone, CurrentTime) == 0)
      report("mc focus set dpy2", 1, "accepted");
    else
      report("mc focus set dpy2", 0, "XSetInputFocus failed");
    XSync(dpy, False);

    if(wait_event_type_for_window(dpy2, mc_focus2, FocusIn,
                                  shape_type, randr_type, damage_type, &ev))
      report("mc focusin dpy2", 1, "routed to owner display");
    else
      report("mc focusin dpy2", 0, "FocusIn missing");

    focus_cur = None;
    revert_to = -1;
    if(XGetInputFocus(dpy, &focus_cur, &revert_to) == 0 && focus_cur == mc_focus2)
      report("mc focus get global", 1, "focus visible cross-client");
    else
      report("mc focus get global", 0, "focus mismatch");

    if(XSetInputFocus(dpy, geom_window, RevertToNone, CurrentTime) == 0)
      report("mc focus set geom", 1, "accepted");
    else
      report("mc focus set geom", 0, "XSetInputFocus failed");
    XSync(dpy, False);

    if(wait_event_type_for_window(dpy2, mc_focus2, FocusOut,
                                  shape_type, randr_type, damage_type, &ev))
      report("mc focusout dpy2", 1, "routed to owner display");
    else
      report("mc focusout dpy2", 0, "FocusOut missing");

    if(wait_event_type_for_window(dpy, geom_window, FocusIn,
                                  shape_type, randr_type, damage_type, &ev))
      report("mc focusin geom", 1, "focus returned to primary display");
    else
      report("mc focusin geom", 0, "FocusIn missing");

    XDestroyWindow(dpy2, mc_focus2);
    XDestroyWindow(dpy2, mc_owner);
    XDestroyWindow(dpy, mc_requestor);
    XSync(dpy2, False);
    XSync(dpy, False);
    if(dpy2 != dpy)
      XCloseDisplay(dpy2);
    dpy2 = (Display *)0;
  }

  stack_parent = XCreateSimpleWindow(dpy, root, 40, 240, 260, 120, 0, black, black);
  stack_a = XCreateSimpleWindow(dpy, stack_parent, 5, 5, 40, 30, 0, white, white);
  stack_b = XCreateSimpleWindow(dpy, stack_parent, 15, 15, 40, 30, 0, white, white);
  stack_c = XCreateSimpleWindow(dpy, stack_parent, 25, 25, 40, 30, 0, white, white);
  foreign_parent = XCreateSimpleWindow(dpy, root, 340, 240, 120, 90, 0, black, black);
  foreign_child = XCreateSimpleWindow(dpy, foreign_parent, 10, 10, 30, 20, 0, white, white);
  XMapWindow(dpy, stack_parent);
  XMapWindow(dpy, stack_a);
  XMapWindow(dpy, stack_b);
  XMapWindow(dpy, stack_c);
  XMapWindow(dpy, foreign_parent);
  XMapWindow(dpy, foreign_child);
  XSync(dpy, False);

  {
    Window kids[3];
    kids[0] = stack_a;
    kids[1] = stack_b;
    kids[2] = stack_c;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack initial");
  }

  {
    XWindowChanges wc;
    wc.sibling = stack_a;
    wc.stack_mode = Below;
    if(XConfigureWindow(dpy, stack_c, CWSibling | CWStackMode, &wc) != 0)
      report("restack c below a", 0, "XConfigureWindow failed");
    else
      report("restack c below a", 1, "accepted");
  }
  XSync(dpy, False);

  {
    Window kids[3];
    kids[0] = stack_c;
    kids[1] = stack_a;
    kids[2] = stack_b;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack after below");
  }

  {
    XWindowChanges wc;
    wc.sibling = stack_b;
    wc.stack_mode = Above;
    if(XConfigureWindow(dpy, stack_a, CWSibling | CWStackMode, &wc) != 0)
      report("restack a above b", 0, "XConfigureWindow failed");
    else
      report("restack a above b", 1, "accepted");
  }
  XSync(dpy, False);

  {
    Window kids[3];
    kids[0] = stack_c;
    kids[1] = stack_b;
    kids[2] = stack_a;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack after above");
  }

  {
    XWindowChanges wc;
    wc.sibling = (Window)0;
    wc.stack_mode = Above;
    if(XConfigureWindow(dpy, stack_c, CWStackMode, &wc) != 0)
      report("restack c to top", 0, "XConfigureWindow failed");
    else
      report("restack c to top", 1, "accepted");
  }
  XSync(dpy, False);

  {
    Window kids[3];
    kids[0] = stack_b;
    kids[1] = stack_a;
    kids[2] = stack_c;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack top");
  }

  {
    Window order[3];
    order[0] = stack_a;
    order[1] = stack_c;
    order[2] = stack_b;
    if(XRestackWindows(dpy, order, 3) != 0)
      report("restack list top-order", 1, "accepted");
    else
      report("restack list top-order", 0, "XRestackWindows failed");
  }
  XSync(dpy, False);

  {
    Window kids[3];
    kids[0] = stack_b;
    kids[1] = stack_c;
    kids[2] = stack_a;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack list");
  }

  if(XMapRaised(dpy, stack_b) == 0)
    report("mapraised stack_b", 1, "accepted");
  else
    report("mapraised stack_b", 0, "XMapRaised failed");
  XSync(dpy, False);

  {
    Window kids[3];
    kids[0] = stack_c;
    kids[1] = stack_a;
    kids[2] = stack_b;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack mapraised");
  }

  if(XMapWindow(dpy, stack_a) == 0)
    report("map mapped stack_a", 1, "accepted");
  else
    report("map mapped stack_a", 0, "XMapWindow failed");
  XSync(dpy, False);

  {
    Window kids[3];
    kids[0] = stack_c;
    kids[1] = stack_a;
    kids[2] = stack_b;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack map-noop");
  }

  if(XUnmapWindow(dpy, stack_a) == 0)
    report("unmap stack_a", 1, "accepted");
  else
    report("unmap stack_a", 0, "XUnmapWindow failed");
  XSync(dpy, False);

  if(XUnmapWindow(dpy, stack_a) == 0)
    report("unmap unmapped a", 1, "accepted");
  else
    report("unmap unmapped a", 0, "XUnmapWindow failed");
  XSync(dpy, False);

  if(XMapWindow(dpy, stack_a) == 0)
    report("remap stack_a", 1, "accepted");
  else
    report("remap stack_a", 0, "XMapWindow failed");
  XSync(dpy, False);

  {
    Window kids[3];
    kids[0] = stack_c;
    kids[1] = stack_a;
    kids[2] = stack_b;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack remap");
  }

  if(XLowerWindow(dpy, stack_b) == 0)
    report("lower stack_b", 1, "accepted");
  else
    report("lower stack_b", 0, "XLowerWindow failed");
  XSync(dpy, False);

  {
    Window kids[3];
    kids[0] = stack_b;
    kids[1] = stack_c;
    kids[2] = stack_a;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack lower");
  }

  if(XRaiseWindow(dpy, stack_b) == 0)
    report("raise stack_b", 1, "accepted");
  else
    report("raise stack_b", 0, "XRaiseWindow failed");
  XSync(dpy, False);

  {
    Window kids[3];
    kids[0] = stack_c;
    kids[1] = stack_a;
    kids[2] = stack_b;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack raise");
  }

  {
    XWindowChanges wc;
    wc.sibling = stack_a;
    wc.stack_mode = TopIf;
    if(XConfigureWindow(dpy, stack_b, CWSibling | CWStackMode, &wc) == 0)
      report("topif b over a", 1, "accepted");
    else
      report("topif b over a", 0, "XConfigureWindow failed");
  }
  XSync(dpy, False);

  {
    Window kids[3];
    kids[0] = stack_c;
    kids[1] = stack_a;
    kids[2] = stack_b;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack topif");
  }

  {
    XWindowChanges wc;
    wc.sibling = stack_a;
    wc.stack_mode = BottomIf;
    if(XConfigureWindow(dpy, stack_b, CWSibling | CWStackMode, &wc) == 0)
      report("bottomif b vs a", 1, "accepted");
    else
      report("bottomif b vs a", 0, "XConfigureWindow failed");
  }
  XSync(dpy, False);

  {
    Window kids[3];
    kids[0] = stack_b;
    kids[1] = stack_c;
    kids[2] = stack_a;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack bottomif");
  }

  {
    XWindowChanges wc;
    wc.sibling = stack_a;
    wc.stack_mode = Opposite;
    if(XConfigureWindow(dpy, stack_b, CWSibling | CWStackMode, &wc) == 0)
      report("opposite b vs a", 1, "accepted");
    else
      report("opposite b vs a", 0, "XConfigureWindow failed");
  }
  XSync(dpy, False);

  {
    Window kids[3];
    kids[0] = stack_c;
    kids[1] = stack_a;
    kids[2] = stack_b;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack opposite");
  }

  {
    XWindowChanges wc;
    wc.sibling = (Window)0;
    wc.stack_mode = TopIf;
    if(XConfigureWindow(dpy, stack_c, CWStackMode, &wc) == 0)
      report("topif c no sibling", 1, "accepted");
    else
      report("topif c no sibling", 0, "XConfigureWindow failed");
  }
  XSync(dpy, False);

  {
    Window kids[3];
    kids[0] = stack_a;
    kids[1] = stack_b;
    kids[2] = stack_c;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack topif nosib");
  }

  {
    XWindowChanges wc;
    wc.sibling = (Window)0;
    wc.stack_mode = BottomIf;
    if(XConfigureWindow(dpy, stack_c, CWStackMode, &wc) == 0)
      report("bottomif c no sib", 1, "accepted");
    else
      report("bottomif c no sib", 0, "XConfigureWindow failed");
  }
  XSync(dpy, False);

  {
    Window kids[3];
    kids[0] = stack_c;
    kids[1] = stack_a;
    kids[2] = stack_b;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack bottom nosib");
  }

  {
    XWindowChanges wc;
    wc.sibling = (Window)0;
    wc.stack_mode = Opposite;
    if(XConfigureWindow(dpy, stack_c, CWStackMode, &wc) == 0)
      report("opposite c no sib", 1, "accepted");
    else
      report("opposite c no sib", 0, "XConfigureWindow failed");
  }
  XSync(dpy, False);

  {
    Window kids[3];
    kids[0] = stack_a;
    kids[1] = stack_b;
    kids[2] = stack_c;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack opp nosib");
  }

  {
    XWindowChanges wc;
    wc.sibling = stack_b;
    wc.stack_mode = TopIf;
    if(XConfigureWindow(dpy, stack_c, CWSibling | CWStackMode, &wc) == 0)
      report("topif c under b", 1, "accepted");
    else
      report("topif c under b", 0, "XConfigureWindow failed");
  }
  XSync(dpy, False);

  {
    Window kids[3];
    kids[0] = stack_a;
    kids[1] = stack_b;
    kids[2] = stack_c;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack topif noop");
  }

  {
    XWindowChanges wc;
    wc.sibling = stack_b;
    wc.stack_mode = BottomIf;
    if(XConfigureWindow(dpy, stack_a, CWSibling | CWStackMode, &wc) == 0)
      report("bottomif a over b", 1, "accepted");
    else
      report("bottomif a over b", 0, "XConfigureWindow failed");
  }
  XSync(dpy, False);

  {
    Window kids[3];
    kids[0] = stack_a;
    kids[1] = stack_b;
    kids[2] = stack_c;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack bottom noop");
  }

  {
    XWindowChanges wc;
    wc.sibling = stack_b;
    wc.stack_mode = Opposite;
    if(XConfigureWindow(dpy, stack_c, CWSibling | CWStackMode, &wc) == 0)
      report("opposite c vs b", 1, "accepted");
    else
      report("opposite c vs b", 0, "XConfigureWindow failed");
  }
  XSync(dpy, False);

  {
    Window kids[3];
    kids[0] = stack_a;
    kids[1] = stack_c;
    kids[2] = stack_b;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack opp flip");
  }

  {
    XWindowChanges wc;
    wc.sibling = foreign_child;
    wc.stack_mode = Above;
    if(XConfigureWindow(dpy, stack_a, CWSibling | CWStackMode, &wc) != 0)
      report("restack invalid sib", 1, "expected failure");
    else
      report("restack invalid sib", 0, "unexpected success");
  }

  if(XReparentWindow(dpy, stack_a, stack_a, 0, 0) != 0)
    report("reparent self reject", 1, "expected failure");
  else
    report("reparent self reject", 0, "unexpected success");

  if(XReparentWindow(dpy, stack_parent, stack_a, 0, 0) != 0)
    report("reparent cycle reject", 1, "expected failure");
  else
    report("reparent cycle reject", 0, "unexpected success");

  if(XReparentWindow(dpy, stack_a, (Window)999999, 0, 0) != 0)
    report("reparent bad parent", 1, "expected failure");
  else
    report("reparent bad parent", 0, "unexpected success");

  {
    XWindowChanges wc;
    wc.sibling = stack_b;
    wc.stack_mode = 99;
    if(XConfigureWindow(dpy, stack_a, CWSibling | CWStackMode, &wc) != 0)
      report("restack bad mode", 1, "expected failure");
    else
      report("restack bad mode", 0, "unexpected success");
  }

  {
    XWindowChanges wc;
    wc.sibling = (Window)0;
    wc.stack_mode = 99;
    if(XConfigureWindow(dpy, stack_a, CWStackMode, &wc) != 0)
      report("restack bad mode ns", 1, "expected failure");
    else
      report("restack bad mode ns", 0, "unexpected success");
  }

  {
    XWindowChanges wc;
    wc.sibling = stack_a;
    wc.stack_mode = Above;
    if(XConfigureWindow(dpy, stack_a, CWSibling | CWStackMode, &wc) != 0)
      report("restack self sib", 1, "expected failure");
    else
      report("restack self sib", 0, "unexpected success");
  }
  XSync(dpy, False);

  {
    Window kids[3];
    kids[0] = stack_a;
    kids[1] = stack_c;
    kids[2] = stack_b;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack rejects");
  }

  {
    Window kids[3];
    kids[0] = stack_a;
    kids[1] = stack_c;
    kids[2] = stack_b;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack badmode");
  }

  {
    Window kids[3];
    kids[0] = stack_a;
    kids[1] = stack_c;
    kids[2] = stack_b;
    check_tree(dpy, stack_parent, root, root, kids, 3, "tree stack selfsib");
  }

  {
    Window kids[1];
    kids[0] = foreign_child;
    check_tree(dpy, foreign_parent, root, root, kids, 1, "tree foreign stable");
  }

  if(XRaiseWindow(dpy, (Window)999999) != 0)
    report("raise bad wid", 1, "expected failure");
  else
    report("raise bad wid", 0, "unexpected success");

  if(XLowerWindow(dpy, (Window)999999) != 0)
    report("lower bad wid", 1, "expected failure");
  else
    report("lower bad wid", 0, "unexpected success");

  if(XMapWindow(dpy, (Window)999999) != 0)
    report("map bad wid", 1, "expected failure");
  else
    report("map bad wid", 0, "unexpected success");

  if(XMapRaised(dpy, (Window)999999) != 0)
    report("mapraised bad wid", 1, "expected failure");
  else
    report("mapraised bad wid", 0, "unexpected success");

  if(XUnmapWindow(dpy, (Window)999999) != 0)
    report("unmap bad wid", 1, "expected failure");
  else
    report("unmap bad wid", 0, "unexpected success");

  if(XDestroyWindow(dpy, (Window)999999) != 0)
    report("destroy bad wid", 1, "expected failure");
  else
    report("destroy bad wid", 0, "unexpected success");

  if(XDestroyWindow(dpy, root) != 0)
    report("destroy root reject", 1, "expected failure");
  else
    report("destroy root reject", 0, "unexpected success");

  check_tree_missing(dpy, (Window)999999, "tree missing bad wid");
  XSync(dpy, False);

  {
    Window kids[1];
    kids[0] = foreign_child;
    check_tree(dpy, foreign_parent, root, root, kids, 1, "tree foreign after bad");
  }

  temp_window = XCreateSimpleWindow(dpy, root, 500, 260, 50, 30, 0, black, black);
  XMapWindow(dpy, temp_window);
  XSync(dpy, False);

  if(XDestroyWindow(dpy, temp_window) == 0)
    report("destroy temp once", 1, "accepted");
  else
    report("destroy temp once", 0, "XDestroyWindow failed");

  if(XDestroyWindow(dpy, temp_window) != 0)
    report("destroy temp twice", 1, "expected failure");
  else
    report("destroy temp twice", 0, "unexpected success");

  if(XMapWindow(dpy, temp_window) != 0)
    report("map destroyed temp", 1, "expected failure");
  else
    report("map destroyed temp", 0, "unexpected success");

  if(XUnmapWindow(dpy, temp_window) != 0)
    report("unmap destroyed temp", 1, "expected failure");
  else
    report("unmap destroyed temp", 0, "unexpected success");

  check_tree_missing(dpy, temp_window, "tree missing temp");
  check_translate_missing(dpy, (Window)999999, root, "translate missing src");
  check_translate_missing(dpy, root, (Window)999999, "translate missing dst");
  check_translate_missing(dpy, temp_window, root, "translate destroyed src");
  check_translate_missing(dpy, root, temp_window, "translate destroyed dst");
  XSync(dpy, False);

  prune_parent = XCreateSimpleWindow(dpy, root, 760, 60, 160, 100, 0, black, black);
  prune_a = XCreateSimpleWindow(dpy, prune_parent, 10, 10, 40, 30, 0, white, white);
  prune_b = XCreateSimpleWindow(dpy, prune_parent, 70, 10, 40, 30, 0, white, white);
  XMapWindow(dpy, prune_parent);
  XMapWindow(dpy, prune_a);
  XMapWindow(dpy, prune_b);
  XSync(dpy, False);

  {
    Window kids[2];
    kids[0] = prune_a;
    kids[1] = prune_b;
    check_tree(dpy, prune_parent, root, root, kids, 2, "tree prune initial");
  }

  if(XDestroyWindow(dpy, prune_a) == 0)
    report("destroy prune_a", 1, "accepted");
  else
    report("destroy prune_a", 0, "XDestroyWindow failed");
  XSync(dpy, False);

  {
    Window kids[1];
    kids[0] = prune_b;
    check_tree(dpy, prune_parent, root, root, kids, 1, "tree prune after");
  }

  check_tree_missing(dpy, prune_a, "tree missing prune_a");

  check_translate(dpy, root, prune_parent,
                  775, 75,
                  15, 15,
                  None,
                  "root->prune no-prune_a");

  if(XDestroyWindow(dpy, prune_parent) == 0)
    report("destroy prune parent", 1, "accepted");
  else
    report("destroy prune parent", 0, "XDestroyWindow failed");
  XSync(dpy, False);

  check_tree_missing(dpy, prune_parent, "tree missing prune_parent");
  check_tree_missing(dpy, prune_b, "tree missing prune_b");

  doomed_parent = XCreateSimpleWindow(dpy, root, 620, 50, 120, 90, 0, black, black);
  doomed_child = XCreateSimpleWindow(dpy, doomed_parent, 10, 10, 40, 30, 0, white, white);
  XMapWindow(dpy, doomed_parent);
  XMapWindow(dpy, doomed_child);
  XSync(dpy, False);

  if(XDestroyWindow(dpy, doomed_parent) == 0)
    report("destroy parent subtree", 1, "destroy accepted");
  else
    report("destroy parent subtree", 0, "XDestroyWindow failed");
  XSync(dpy, False);

  check_tree_missing(dpy, doomed_parent, "tree missing doomed_parent");
  check_tree_missing(dpy, doomed_child, "tree missing doomed_child");

  dpy2 = XOpenDisplay(0);
  if(!dpy2) {
    report("redirect dpy2 open", 0, "XOpenDisplay failed");
  } else {
    Window root_wm;
    long wm_mask;
    XSetWindowAttributes swa;

    root_wm = DefaultRootWindow(dpy2);
    wm_mask = SubstructureRedirectMask | SubstructureNotifyMask;
    prev_error_handler = XSetErrorHandler(selftest_error_handler);

    reset_error_capture();
    if(XSelectInput(dpy, root, wm_mask) == 0) {
      XSync(dpy, False);
      if(g_xerr_count == 0)
        report("redirect claim primary", 1, "granted");
      else
        report("redirect claim primary", 0, "unexpected XError");
    } else {
      report("redirect claim primary", 0, "XSelectInput failed");
    }

    reset_error_capture();
    if(XSelectInput(dpy2, root_wm, wm_mask) == 0) {
      XSync(dpy2, False);
      if(g_xerr_count > 0 && g_xerr_code == BadAccess)
        report("redirect conflict select", 1, "BadAccess observed");
      else
        report("redirect conflict select", 0, "missing BadAccess");
    } else {
      report("redirect conflict select", 0, "XSelectInput call failed");
    }

    reset_error_capture();
    memset(&swa, 0, sizeof(swa));
    swa.event_mask = wm_mask;
    if(XChangeWindowAttributes(dpy2, root_wm, CWEventMask, &swa) == 0) {
      XSync(dpy2, False);
      if(g_xerr_count > 0 && g_xerr_code == BadAccess &&
         g_xerr_request == X_ChangeWindowAttributes &&
         g_xerr_resource == (XID)root_wm)
        report("redirect conflict cwa", 1, "BadAccess + request matched");
      else
        report("redirect conflict cwa", 0, "missing expected XError payload");
    } else {
      report("redirect conflict cwa", 0, "XChangeWindowAttributes failed");
    }

    XSetErrorHandler(prev_error_handler);
    XCloseDisplay(dpy2);
    dpy2 = (Display *)0;
  }

  XDestroyWindow(dpy, child_b);
  XDestroyWindow(dpy, child_a);
  XDestroyWindow(dpy, stack_c);
  XDestroyWindow(dpy, stack_b);
  XDestroyWindow(dpy, stack_a);
  XDestroyWindow(dpy, stack_parent);
  XDestroyWindow(dpy, sel_requestor);
  XDestroyWindow(dpy, sel_owner);
  XDestroyWindow(dpy, cmap_window);
  XDestroyWindow(dpy, clear_window);
  XDestroyWindow(dpy, geom_window);
  XDestroyWindow(dpy, foreign_child);
  XDestroyWindow(dpy, foreign_parent);
  XDestroyWindow(dpy, parent2);
  XDestroyWindow(dpy, parent);
  if(dpy2 && dpy2 != dpy)
    XCloseDisplay(dpy2);
  XCloseDisplay(dpy);

  printf("xwmselftest: summary pass=%d fail=%d\n", g_pass, g_fail);
  if(g_fail != 0)
    return 1;
  return 0;
}
