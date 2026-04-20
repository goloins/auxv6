#ifndef _X11_XUTIL_H_
#define _X11_XUTIL_H_

#include <X11/Xlib.h>

typedef struct {
  long flags;
  int x, y;
  int width, height;
  int min_width, min_height;
  int max_width, max_height;
  int width_inc, height_inc;
  struct { int x, y; } min_aspect, max_aspect;
  int base_width, base_height;
  int win_gravity;
} XSizeHints;

typedef struct {
  char *value;
  Atom encoding;
  int format;
  unsigned long nitems;
} XTextProperty;

typedef struct {
  char *res_name;
  char *res_class;
} XClassHint;

typedef struct {
  long flags;
  int input;
  int initial_state;
  Pixmap icon_pixmap;
  Window icon_window;
  int icon_x, icon_y;
  Pixmap icon_mask;
  XID window_group;
} XWMHints;

#define PMinSize (1L << 4)
#define PSize (1L << 3)
#define PMaxSize (1L << 5)
#define PResizeInc (1L << 6)
#define PBaseSize (1L << 8)
#define PAspect (1L << 7)

#define InputHint (1L << 0)
#define WindowGroupHint (1L << 6)
#define XUrgencyHint (1L << 8)

#define WithdrawnState 0
#define NormalState 1
#define IconicState 3

#ifndef BitmapSuccess
#define BitmapSuccess 0
#endif
#ifndef BitmapOpenFailed
#define BitmapOpenFailed 1
#endif
#ifndef BitmapFileInvalid
#define BitmapFileInvalid 2
#endif
#ifndef BitmapNoMemory
#define BitmapNoMemory 3
#endif

int XSetWMNormalHints(Display *display, Window w, XSizeHints *hints);
int XSetTransientForHint(Display *display, Window w, Window prop_window);
int XStoreName(Display *display, Window w, const char *name);
int XGetClassHint(Display *display, Window w, XClassHint *class_hint_return);
int XGetTextProperty(Display *display, Window w, XTextProperty *text_prop_return, Atom property);
int XmbTextPropertyToTextList(Display *display, XTextProperty *text_prop, char ***list_return, int *count_return);
void XFreeStringList(char **list);
int XGetWMProtocols(Display *display, Window w, Atom **protocols_return, int *count_return);
XWMHints *XGetWMHints(Display *display, Window w);
int XSetWMHints(Display *display, Window w, XWMHints *wmhints);
int XGetWMNormalHints(Display *display, Window w, XSizeHints *hints_return, long *supplied_return);
int XGetNormalHints(Display *display, Window w, XSizeHints *hints_return);
int XGetSizeHints(Display *display, Window w, XSizeHints *hints_return,
                  Atom property);
int XSetClassHint(Display *display, Window w, XClassHint *class_hints);
int XGetCommand(Display *display, Window w,
                char ***argv_return, int *argc_return);
int XStringListToTextProperty(char **list, int count,
                              XTextProperty *text_prop_return);

#endif
