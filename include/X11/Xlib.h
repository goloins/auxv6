#ifndef _X11_XLIB_H_
#define _X11_XLIB_H_

#include <stddef.h>

typedef unsigned long XID;
typedef XID Window;
typedef XID Drawable;
typedef XID Pixmap;
typedef XID Cursor;
typedef XID Font;
typedef XID Colormap;
typedef XID Atom;
typedef XID VisualID;
typedef unsigned long Time;
typedef unsigned long KeySym;
typedef unsigned char KeyCode;
typedef unsigned long GC;

typedef int Bool;
typedef int Status;

#ifndef True
#define True 1
#endif
#ifndef False
#define False 0
#endif

typedef struct _XDisplay Display;

typedef struct _XDisplay {
  int fd;
  int screen;
  Window root;
  int width;
  int height;
  int depth;
} _XDisplay;

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window window;
} XAnyEvent;

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window window;
  Window root;
  Window subwindow;
  Time time;
  int x, y;
  int x_root, y_root;
  unsigned int state;
  unsigned int keycode;
  Bool same_screen;
} XKeyEvent;

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window window;
  Window root;
  Window subwindow;
  Time time;
  int x, y;
  int x_root, y_root;
  unsigned int state;
  unsigned int button;
  Bool same_screen;
} XButtonEvent;

typedef XButtonEvent XButtonPressedEvent;

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window window;
  Window root;
  Window subwindow;
  Time time;
  int x, y;
  int x_root, y_root;
  unsigned int state;
  char is_hint;
  Bool same_screen;
} XMotionEvent;

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window window;
  Window root;
  Window subwindow;
  Time time;
  int x, y;
  int x_root, y_root;
  int mode;
  int detail;
  Bool same_screen;
  Bool focus;
  unsigned int state;
} XCrossingEvent;

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window window;
  int mode;
  int detail;
} XFocusChangeEvent;

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window window;
  int x, y;
  int width, height;
  int count;
} XExposeEvent;

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window parent;
  Window window;
  int x, y;
  int width, height;
  int border_width;
  Bool override_redirect;
} XCreateWindowEvent;

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window event;
  Window window;
  Bool from_configure;
} XUnmapEvent;

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window event;
  Window window;
  Bool override_redirect;
} XMapEvent;

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window event;
  Window window;
  int x, y;
  int width, height;
  int border_width;
  Window above;
  Bool override_redirect;
} XConfigureEvent;

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window event;
  Window window;
} XDestroyWindowEvent;

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window parent;
  Window window;
} XMapRequestEvent;

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window parent;
  Window window;
  int x, y;
  int width, height;
  int border_width;
  Window above;
  int detail;
  unsigned long value_mask;
} XConfigureRequestEvent;

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window window;
  Atom atom;
  Time time;
  int state;
} XPropertyEvent;

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window window;
  int request;
  int first_keycode;
  int count;
} XMappingEvent;

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window window;
  Atom message_type;
  int format;
  union {
    char b[20];
    short s[10];
    long l[5];
  } data;
} XClientMessageEvent;

typedef struct {
  int type;
  Display *display;
  XID resourceid;
  unsigned long serial;
  unsigned char error_code;
  unsigned char request_code;
  unsigned char minor_code;
} XErrorEvent;

typedef union {
  int type;
  XAnyEvent xany;
  XKeyEvent xkey;
  XButtonEvent xbutton;
  XMotionEvent xmotion;
  XCrossingEvent xcrossing;
  XFocusChangeEvent xfocus;
  XExposeEvent xexpose;
  XCreateWindowEvent xcreatewindow;
  XMapEvent xmap;
  XMapRequestEvent xmaprequest;
  XUnmapEvent xunmap;
  XConfigureEvent xconfigure;
  XConfigureRequestEvent xconfigurerequest;
  XDestroyWindowEvent xdestroywindow;
  XPropertyEvent xproperty;
  XClientMessageEvent xclient;
  XMappingEvent xmapping;
  long pad[24];
} XEvent;

typedef struct {
  int x, y;
  int width, height;
  int border_width;
  int depth;
  Bool override_redirect;
  int map_state;
  long your_event_mask;
} XWindowAttributes;

typedef struct {
  int x, y;
  int width, height;
  int border_width;
  Window sibling;
  int stack_mode;
} XWindowChanges;

typedef struct {
  long background_pixmap;
  unsigned long background_pixel;
  long border_pixmap;
  unsigned long border_pixel;
  int bit_gravity;
  int win_gravity;
  int backing_store;
  unsigned long backing_planes;
  unsigned long backing_pixel;
  Bool save_under;
  long event_mask;
  long do_not_propagate_mask;
  Bool override_redirect;
  Colormap colormap;
  Cursor cursor;
} XSetWindowAttributes;

typedef struct {
  int max_keypermod;
  KeyCode *modifiermap;
} XModifierKeymap;

#define None 0L
#define ParentRelative 1L
#define CopyFromParent 0
#define InputOutput 1

#define KeyPress 2
#define KeyRelease 3
#define ButtonPress 4
#define ButtonRelease 5
#define MotionNotify 6
#define EnterNotify 7
#define LeaveNotify 8
#define FocusIn 9
#define FocusOut 10
#define Expose 12
#define CreateNotify 16
#define DestroyNotify 17
#define UnmapNotify 18
#define MapNotify 19
#define MapRequest 20
#define ConfigureNotify 22
#define ConfigureRequest 23
#define PropertyNotify 28
#define ClientMessage 33
#define MappingNotify 34
#define LASTEvent 35

#define NotifyNormal 0
#define NotifyInferior 2
#define MappingKeyboard 1
#define PropertyDelete 1

#define NoEventMask 0L
#define KeyPressMask (1L << 0)
#define KeyReleaseMask (1L << 1)
#define ButtonPressMask (1L << 2)
#define ButtonReleaseMask (1L << 3)
#define EnterWindowMask (1L << 4)
#define LeaveWindowMask (1L << 5)
#define PointerMotionMask (1L << 6)
#define ButtonMotionMask (1L << 13)
#define ExposureMask (1L << 15)
#define StructureNotifyMask (1L << 17)
#define SubstructureNotifyMask (1L << 19)
#define SubstructureRedirectMask (1L << 20)
#define FocusChangeMask (1L << 21)
#define PropertyChangeMask (1L << 22)

#define ShiftMask (1 << 0)
#define LockMask (1 << 1)
#define ControlMask (1 << 2)
#define Mod1Mask (1 << 3)
#define Mod2Mask (1 << 4)
#define Mod3Mask (1 << 5)
#define Mod4Mask (1 << 6)
#define Mod5Mask (1 << 7)

#define Button1 1
#define Button2 2
#define Button3 3
#define Button4 4
#define Button5 5
#define AnyButton 0
#define AnyKey 0
#define AnyModifier (1 << 15)

#define ReplayPointer 2
#define GrabModeSync 0
#define GrabModeAsync 1
#define GrabSuccess 0

#define CurrentTime 0L
#define RevertToNone 0
#define RevertToPointerRoot 1
#define PointerRoot 1

#define Above 0
#define Below 1

#define IsUnmapped 0
#define IsUnviewable 1
#define IsViewable 2

#define CWX (1 << 0)
#define CWY (1 << 1)
#define CWWidth (1 << 2)
#define CWHeight (1 << 3)
#define CWBorderWidth (1 << 4)
#define CWSibling (1 << 5)
#define CWStackMode (1 << 6)

#define CWBackPixmap (1L << 0)
#define CWBackPixel (1L << 1)
#define CWBorderPixmap (1L << 2)
#define CWBorderPixel (1L << 3)
#define CWBitGravity (1L << 4)
#define CWWinGravity (1L << 5)
#define CWBackingStore (1L << 6)
#define CWBackingPlanes (1L << 7)
#define CWBackingPixel (1L << 8)
#define CWSaveUnder (1L << 9)
#define CWEventMask (1L << 10)
#define CWDontPropagate (1L << 11)
#define CWOverrideRedirect (1L << 12)
#define CWColormap (1L << 13)
#define CWCursor (1L << 14)

#define LineSolid 0
#define CapButt 1
#define JoinMiter 0

#define PropModeReplace 0
#define PropModePrepend 1
#define PropModeAppend 2

#define DestroyAll 0

#define Success 0
#define BadWindow 3
#define BadMatch 8
#define BadDrawable 9
#define BadAccess 10

#define XA_PRIMARY 1L
#define XA_SECONDARY 2L
#define XA_ATOM 4L
#define XA_STRING 31L
#define XA_VISUALID 32L
#define XA_WINDOW 33L
#define XA_WM_HINTS 35L
#define XA_WM_NAME 39L
#define XA_WM_NORMAL_HINTS 40L
#define XA_WM_TRANSIENT_FOR 68L

#define DefaultScreen(dpy) ((dpy)->screen)
#define DefaultRootWindow(dpy) ((dpy)->root)
#define RootWindow(dpy, scr) ((dpy)->root)
#define DisplayWidth(dpy, scr) ((dpy)->width)
#define DisplayHeight(dpy, scr) ((dpy)->height)
#define DefaultDepth(dpy, scr) ((dpy)->depth)
#define DefaultVisual(dpy, scr) ((void *)0)
#define ConnectionNumber(dpy) ((dpy)->fd)

Display *XOpenDisplay(char *display_name);
int XCloseDisplay(Display *display);
int XSync(Display *display, Bool discard);
int XFlush(Display *display);

Window XCreateWindow(Display *display, Window parent, int x, int y,
                     unsigned int width, unsigned int height,
                     unsigned int border_width, int depth, unsigned int class,
                     void *visual, unsigned long valuemask,
                     XSetWindowAttributes *attributes);
Window XCreateSimpleWindow(Display *display, Window parent, int x, int y,
                           unsigned int width, unsigned int height,
                           unsigned int border_width, unsigned long border,
                           unsigned long background);
int XDestroyWindow(Display *display, Window w);
int XMapWindow(Display *display, Window w);
int XMapRaised(Display *display, Window w);
int XUnmapWindow(Display *display, Window w);
int XMoveResizeWindow(Display *display, Window w, int x, int y, unsigned int width, unsigned int height);
int XMoveWindow(Display *display, Window w, int x, int y);
int XRaiseWindow(Display *display, Window w);
int XLowerWindow(Display *display, Window w);
int XConfigureWindow(Display *display, Window w, unsigned int value_mask, XWindowChanges *values);

int XGetWindowAttributes(Display *display, Window w, XWindowAttributes *attrs);
int XSelectInput(Display *display, Window w, long event_mask);
int XNextEvent(Display *display, XEvent *event);
int XMaskEvent(Display *display, long event_mask, XEvent *event);
Bool XCheckMaskEvent(Display *display, long event_mask, XEvent *event);
int XPending(Display *display);

Atom XInternAtom(Display *display, char *atom_name, Bool only_if_exists);
char *XGetAtomName(Display *display, Atom atom);
int XChangeProperty(Display *display, Window w, Atom property, Atom type,
                    int format, int mode, unsigned char *data, int nelements);
int XGetWindowProperty(Display *display, Window w, Atom property,
                       long long_offset, long long_length, Bool del,
                       Atom req_type, Atom *actual_type_return,
                       int *actual_format_return, unsigned long *nitems_return,
                       unsigned long *bytes_after_return,
                       unsigned char **prop_return);
int XDeleteProperty(Display *display, Window w, Atom property);
Status XSendEvent(Display *display, Window w, Bool propagate, long event_mask, XEvent *event_send);

int XSetInputFocus(Display *display, Window focus, int revert_to, Time time);
int XGetInputFocus(Display *display, Window *focus_return, int *revert_to_return);
int XGrabKeyboard(Display *display, Window grab_window, Bool owner_events,
                  int pointer_mode, int keyboard_mode, Time time);
int XUngrabKeyboard(Display *display, Time time);
int XGrabPointer(Display *display, Window grab_window, Bool owner_events,
                 unsigned int event_mask, int pointer_mode, int keyboard_mode,
                 Window confine_to, Cursor cursor, Time time);
int XUngrabPointer(Display *display, Time time);
int XWarpPointer(Display *display, Window src_w, Window dest_w,
                 int src_x, int src_y, unsigned int src_width, unsigned int src_height,
                 int dest_x, int dest_y);

int XGrabKey(Display *display, int keycode, unsigned int modifiers,
             Window grab_window, Bool owner_events, int pointer_mode,
             int keyboard_mode);
int XUngrabKey(Display *display, int keycode, unsigned int modifiers, Window grab_window);
int XGrabButton(Display *display, unsigned int button, unsigned int modifiers,
                Window grab_window, Bool owner_events, unsigned int event_mask,
                int pointer_mode, int keyboard_mode, Window confine_to,
                Cursor cursor);
int XUngrabButton(Display *display, unsigned int button, unsigned int modifiers, Window grab_window);
int XAllowEvents(Display *display, int event_mode, Time time);

int XSetWindowBorder(Display *display, Window w, unsigned long border_pixel);
int XChangeWindowAttributes(Display *display, Window w, unsigned long valuemask, XSetWindowAttributes *attributes);
int XDefineCursor(Display *display, Window w, Cursor cursor);
Cursor XCreateFontCursor(Display *display, unsigned int shape);
int XFreeCursor(Display *display, Cursor cursor);

Pixmap XCreatePixmap(Display *display, Drawable d, unsigned int width, unsigned int height, unsigned int depth);
int XFreePixmap(Display *display, Pixmap pixmap);
GC XCreateGC(Display *display, Drawable d, unsigned long valuemask, void *values);
int XFreeGC(Display *display, GC gc);
int XSetForeground(Display *display, GC gc, unsigned long foreground);
int XSetLineAttributes(Display *display, GC gc, unsigned int line_width, int line_style, int cap_style, int join_style);
int XFillRectangle(Display *display, Drawable d, GC gc, int x, int y, unsigned int width, unsigned int height);
int XDrawRectangle(Display *display, Drawable d, GC gc, int x, int y, unsigned int width, unsigned int height);
int XCopyArea(Display *display, Drawable src, Drawable dest, GC gc,
              int src_x, int src_y, unsigned int width, unsigned int height,
              int dest_x, int dest_y);

int XGetTransientForHint(Display *display, Window w, Window *prop_window_return);
int XQueryTree(Display *display, Window w, Window *root_return, Window *parent_return,
               Window **children_return, unsigned int *nchildren_return);
Bool XQueryPointer(Display *display, Window w, Window *root_return, Window *child_return,
                   int *root_x_return, int *root_y_return,
                   int *win_x_return, int *win_y_return,
                   unsigned int *mask_return);

int (*XSetErrorHandler(int (*handler)(Display *, XErrorEvent *)))(Display *, XErrorEvent *);
void XSetCloseDownMode(Display *display, int close_mode);
void XGrabServer(Display *display);
void XUngrabServer(Display *display);
int XKillClient(Display *display, XID resource);

void XFree(void *data);

KeyCode XKeysymToKeycode(Display *display, KeySym keysym);
KeySym XKeycodeToKeysym(Display *display, KeyCode keycode, int index);
void XDisplayKeycodes(Display *display, int *min_keycodes_return, int *max_keycodes_return);
KeySym *XGetKeyboardMapping(Display *display, KeyCode first_keycode, int keycode_count, int *keysyms_per_keycode_return);
XModifierKeymap *XGetModifierMapping(Display *display);
int XFreeModifiermap(XModifierKeymap *modmap);
int XRefreshKeyboardMapping(XMappingEvent *event_map);

int XSupportsLocale(void);

#endif
