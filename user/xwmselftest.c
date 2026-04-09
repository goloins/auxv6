#include "types.h"
#include "auxv6/user.h"
#include "X11/Xlib.h"
#include "stdio.h"

static int g_pass;
static int g_fail;

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

int
main(void)
{
  Display *dpy;
  Window root;
  Window parent;
  Window child_a;
  Window child_b;
  unsigned long black;
  unsigned long white;

  printf("xwmselftest: starting XTranslateCoordinates wm-path checks\n");

  dpy = XOpenDisplay(0);
  if(!dpy) {
    printf("xwmselftest: FAIL open-display\n");
    return 1;
  }

  root = DefaultRootWindow(dpy);
  black = XBlackPixel(dpy, DefaultScreen(dpy));
  white = XWhitePixel(dpy, DefaultScreen(dpy));

  parent = XCreateSimpleWindow(dpy, root, 40, 40, 220, 140, 0, black, black);
  child_a = XCreateSimpleWindow(dpy, parent, 10, 10, 60, 40, 0, white, white);
  child_b = XCreateSimpleWindow(dpy, parent, 100, 10, 60, 40, 0, white, white);

  XMapWindow(dpy, parent);
  XMapWindow(dpy, child_a);
  XMapWindow(dpy, child_b);
  XSync(dpy, False);

  check_translate(dpy, root, parent,
                  55, 55,
                  15, 15,
                  child_a,
                  "root->parent child_a");

  check_translate(dpy, root, parent,
                  145, 55,
                  105, 15,
                  child_b,
                  "root->parent child_b");

  check_translate(dpy, root, parent,
                  75, 105,
                  35, 65,
                  None,
                  "root->parent no-child");

  check_translate(dpy, parent, root,
                  12, 12,
                  52, 52,
                  parent,
                  "parent->root child");

  XDestroyWindow(dpy, child_b);
  XDestroyWindow(dpy, child_a);
  XDestroyWindow(dpy, parent);
  XCloseDisplay(dpy);

  printf("xwmselftest: summary pass=%d fail=%d\n", g_pass, g_fail);
  if(g_fail != 0)
    return 1;
  return 0;
}
