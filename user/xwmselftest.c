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

int
main(void)
{
  Display *dpy;
  Window root;
  Window parent;
  Window parent2;
  Window child_a;
  Window child_b;
  Window doomed_parent;
  Window doomed_child;
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
  parent2 = XCreateSimpleWindow(dpy, root, 320, 40, 220, 140, 0, black, black);
  child_a = XCreateSimpleWindow(dpy, parent, 10, 10, 60, 40, 0, white, white);
  child_b = XCreateSimpleWindow(dpy, parent, 100, 10, 60, 40, 0, white, white);

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

  XDestroyWindow(dpy, child_b);
  XDestroyWindow(dpy, child_a);
  XDestroyWindow(dpy, parent2);
  XDestroyWindow(dpy, parent);
  XCloseDisplay(dpy);

  printf("xwmselftest: summary pass=%d fail=%d\n", g_pass, g_fail);
  if(g_fail != 0)
    return 1;
  return 0;
}
