#ifndef _X11_XLIB_H_
#define _X11_XLIB_H_

#include <stddef.h>
#include <stdio.h>
#include <limits.h>

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

/* Input method enumeration */
typedef int XICCEncodingStyle;

typedef int Bool;
typedef int Status;

/* Visual and XColor types */
typedef struct {
  XID visualid;
  int class;
  unsigned long red_mask;
  unsigned long green_mask;
  unsigned long blue_mask;
  int bits_per_rgb;
  int map_entries;
} Visual;

typedef struct {
  unsigned long pixel;
  unsigned short red;
  unsigned short green;
  unsigned short blue;
  char flags;
} XColor;

/* Additional types for st/dwm compatibility */
typedef struct _XIM *XIM;
typedef struct _XIC *XIC;
typedef void *XVaNestedList;
typedef char *XPointer;

/* Input Method callback structures */
typedef struct {
  void *client_data;
  void (*callback)(XIM, XPointer, XPointer);
} XIMCallback;

typedef struct {
  void *client_data;
  int (*callback)(XIC, XPointer, XPointer);
} XICCallback;

typedef struct {
  short x;
  short y;
} XPoint;

/* st application types - defined in x.c, not here */
typedef unsigned int uint;

typedef struct {
  short x, y;
  unsigned short width, height;
} XRectangle;

/* XKeysym values */
#define XK_BackSpace 0xff08
#define XK_Tab 0xff09
#define XK_Return 0xff0d
#define XK_Escape 0xff1b
#define XK_Home 0xff50
#define XK_Left 0xff51
#define XK_Up 0xff52
#define XK_Right 0xff53
#define XK_Down 0xff54
#define XK_Prior 0xff55
#define XK_Next 0xff56
#define XK_End 0xff57
#define XK_Insert 0xff63
#define XK_Delete 0xffff
#define XK_Break 0xff6b
#define XK_KP_Home 0xff95
#define XK_KP_Up 0xff97
#define XK_KP_Prior 0xff99
#define XK_KP_Down 0xff99
#define XK_KP_Begin 0xff9d
#define XK_KP_End 0xff9c
#define XK_KP_Next 0xff9a
#define XK_KP_Insert 0xff9e
#define XK_KP_Delete 0xff9f
#define XK_KP_Left 0xff96
#define XK_KP_Right 0xff98
#define XK_F1 0xffbe
#define XK_F2 0xffbf
#define XK_F3 0xffc0
#define XK_F4 0xffc1
#define XK_F5 0xffc2
#define XK_F6 0xffc3
#define XK_F7 0xffc4
#define XK_F8 0xffc5
#define XK_F9 0xffc6
#define XK_F10 0xffc7
#define XK_F11 0xffc8
#define XK_F12 0xffc9
#define XK_F13 0xffca
#define XK_F14 0xffcb
#define XK_F15 0xffcc
#define XK_F16 0xffcd
#define XK_F17 0xffce
#define XK_F18 0xffcf
#define XK_F19 0xffd0
#define XK_F20 0xffd1
#define XK_F21 0xffd2
#define XK_F22 0xffd3
#define XK_F23 0xffd4
#define XK_F24 0xffd5
#define XK_F25 0xffd6
#define XK_F26 0xffd7
#define XK_F27 0xffd8
#define XK_F28 0xffd9
#define XK_F29 0xffda
#define XK_F30 0xffdb
#define XK_F31 0xffdc
#define XK_F32 0xffdd
#define XK_F33 0xffde
#define XK_F34 0xffdf
#define XK_F35 0xffe0
#define XK_Num_Lock 0xff7f
#define XK_Y 0x0079
#define XK_NO_MOD 0
#define XK_SWITCH_MOD Mod5Mask
/* Additional keysyms for config shortcuts */
#define XK_Print 0xff61
#define XK_C 0x0063
#define XK_V 0x0076
#define XK_ISO_Left_Tab 0xfe20
#define XK_KP_Multiply 0xffaa
#define XK_KP_Add 0xffab
#define XK_KP_Enter 0xff8d
#define XK_KP_Subtract 0xffad
#define XK_KP_Decimal 0xffae
#define XK_KP_Divide 0xffaf
#define XK_KP_0 0xfff0
#define XK_KP_1 0xfff1
#define XK_KP_2 0xfff2
#define XK_KP_3 0xfff3
#define XK_KP_4 0xfff4
#define XK_KP_5 0xfff5
#define XK_KP_6 0xfff6
#define XK_KP_7 0xfff7
#define XK_KP_8 0xfff8
#define XK_KP_9 0xfff9

/* Event type codes */
#define KeyPress 2
#define KeyRelease 3
#define ButtonPress 4
#define ButtonRelease 5
#define MotionNotify 6
#define EnterNotify 7
#define LeaveNotify 8
#define FocusIn 9
#define FocusOut 10
#define KeymapNotify 11
#define Expose 12
#define GraphicsExpose 13
#define NoExpose 14
#define VisibilityNotify 15
#define CreateNotify 16
#define DestroyNotify 17
#define UnmapNotify 18
#define MapNotify 19
#define MapRequest 20
#define ReparentNotify 21
#define ConfigureNotify 22
#define ConfigureRequest 23
#define GravityNotify 24
#define ResizeRequest 25
#define CirculateNotify 26
#define CirculateRequest 27
#define PropertyNotify 28
#define SelectionClear 29
#define SelectionRequest 30
#define SelectionNotify 31
#define ColormapNotify 32
#define ClientMessage 33
#define MappingNotify 34

/* Button masks */
#define Button1Mask (1 << 8)
#define Button2Mask (1 << 9)
#define Button3Mask (1 << 10)
#define Button4Mask (1 << 11)
#define Button5Mask (1 << 12)

/* Modifier masks */
#define ShiftMask (1 << 0)
#define LockMask (1 << 1)
#define ControlMask (1 << 2)
#define Mod1Mask (1 << 3)
#define Mod2Mask (1 << 4)
#define Mod3Mask (1 << 5)
#define Mod4Mask (1 << 6)
#define Mod5Mask (1 << 7)
#define XK_SWITCH_MOD Mod5Mask
#define XK_ANY_MOD ~0

/* Property notify states */
#define PropertyNewValue 0
#define PropertyDelete 1

/* Cursor font shapes */
#define XC_arrow 0
#define XC_xterm 152
#define XC_hand2 60
#define XC_watch 150

/* Input Method constants */
#define XNDestroyCallback "destroyCallback"
#define XNSpotLocation "spotLocation"
#define XNInputStyle "inputStyle"
#define XIMPreeditNothing 0
#define XIMStatusNothing 0

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
  Atom selection;
  Time time;
} XSelectionClearEvent;

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window owner;
  Window requestor;
  Atom selection;
  Atom target;
  Atom property;
  Time time;
} XSelectionRequestEvent;

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window requestor;
  Atom selection;
  Atom target;
  Atom property;
  Time time;
} XSelectionEvent;

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
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window window;
  int state;
} XVisibilityEvent;

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
  XVisibilityEvent xvisibility;
  XCreateWindowEvent xcreatewindow;
  XMapEvent xmap;
  XMapRequestEvent xmaprequest;
  XUnmapEvent xunmap;
  XConfigureEvent xconfigure;
  XConfigureRequestEvent xconfigurerequest;
  XDestroyWindowEvent xdestroywindow;
  XPropertyEvent xproperty;
  XSelectionClearEvent xselectionclear;
  XSelectionRequestEvent xselectionrequest;
  XSelectionEvent xselection;
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
#define VisibilityChangeMask (1L << 15)

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
#define AnyPropertyType 0L

#define NotifyNormal 0
#define NotifyInferior 2
#define NotifyGrab 3

#define VisibilityUnobscured 0
#define VisibilityPartiallyObscured 1
#define VisibilityFullyObscured 2

#define NoSymbol 0L
#define XBufferOverflow 0

#define GCFunction (1 << 0)
#define GCPlaneMask (1 << 1)
#define GCForeground (1 << 2)
#define GCBackground (1 << 3)
#define GCLineWidth (1 << 4)
#define GCLineStyle (1 << 5)
#define GCCapStyle (1 << 6)
#define GCJoinStyle (1 << 7)
#define GCFillStyle (1 << 8)
#define GCFillRule (1 << 9)
#define GCTile (1 << 10)
#define GCStipple (1 << 11)
#define GCTileStipXOrigin (1 << 12)
#define GCTileStipYOrigin (1 << 13)
#define GCFont (1 << 14)
#define GCSubwindowMode (1 << 15)
#define GCGraphicsExposures (1 << 16)
#define GCClipXOrigin (1 << 17)
#define GCClipYOrigin (1 << 18)
#define GCClipMask (1 << 19)
#define GCDashOffset (1 << 20)
#define GCDashList (1 << 21)
#define GCArcMode (1 << 22)

/* Graphics context values structure */
typedef struct {
  int function;       /* logical operation */
  unsigned long plane_mask;  /* plane mask */
  unsigned long foreground;  /* foreground pixel */
  unsigned long background;  /* background pixel */
  int line_width;     /* line width */
  int line_style;     /* LineSolid, LineOnOffDash, LineDoubleDash */
  int cap_style;      /* CapNotLast, CapButt, CapRound, CapProjecting */
  int join_style;     /* JoinMiter, JoinBevel, JoinRound */
  int fill_style;     /* FillSolid, FillTiled, FillStippled, FillOpaqueStippled */
  int fill_rule;      /* EvenOddRule, WindingRule */
  Pixmap tile;        /* tile pixmap for tiling operations */
  Pixmap stipple;     /* stipple 1 plane pixmap for stippling */
  int ts_x_origin;    /* offset for tile or stipple operations */
  int ts_y_origin;
  Font font;          /* default text font for text operations */
  int subwindow_mode; /* ClipByChildren, IncludeInferiors */
  Bool graphics_exposures;  /* boolean, should exposures be generated */
  int clip_x_origin;  /* origin for clipping */
  int clip_y_origin;
  Pixmap clip_mask;   /* bitmap clipping; other calls for rects */
  int dash_offset;    /* patterned/dashed line information */
  char dashes;
  int arc_mode;       /* ArcChord or ArcPieSlice */
} XGCValues;

/* Input method attribute names and constants */
#define XUTF8StringStyle 0
#define XNClientWindow "clientWindow"
#define XNPreeditAttributes "preeditAttributes"

#define DefaultScreen(dpy) ((dpy)->screen)
#define DefaultRootWindow(dpy) ((dpy)->root)
#define RootWindow(dpy, scr) ((dpy)->root)
#define DisplayWidth(dpy, scr) ((dpy)->width)
#define DisplayHeight(dpy, scr) ((dpy)->height)
#define DefaultDepth(dpy, scr) ((dpy)->depth)
#define DefaultVisual(dpy, scr) ((void *)0)
#define DefaultColormap(dpy, scr) (XDefaultColormap((dpy), (scr)))
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
int XPeekEvent(Display *display, XEvent *event);
int XPutBackEvent(Display *display, XEvent *event);
int XMaskEvent(Display *display, long event_mask, XEvent *event);
Bool XCheckMaskEvent(Display *display, long event_mask, XEvent *event);
Bool XCheckTypedEvent(Display *display, int event_type, XEvent *event_return);
Bool XCheckTypedWindowEvent(Display *display, Window w, int event_type, XEvent *event_return);
Bool XCheckWindowEvent(Display *display, Window w, long event_mask, XEvent *event_return);
int XWindowEvent(Display *display, Window w, long event_mask, XEvent *event_return);
int XLookupString(XKeyEvent *event_struct, char *buffer_return, int bytes_buffer,
                  KeySym *keysym_return, void *status_in_out);
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
int XSetSelectionOwner(Display *display, Atom selection, Window owner, Time time);
int XGetSelectionOwner(Display *display, Atom selection);
int XConvertSelection(Display *display, Atom selection, Atom target, Atom property, Window requestor, Time time);
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
int XDrawString(Display *display, Drawable d, GC gc, int x, int y, const char *string, int length);
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

int XDefaultDepth(Display *display, int screen);
int XReparentWindow(Display *display, Window w, Window parent, int x, int y);
int XRegisterIMInstantiateCallback(Display *display, void *rdb, char *res_name, char *res_class, void (*callback)(Display *, XPointer, XPointer), void *client_data);
int XSetWMProtocols(Display *display, Window w, Atom *protocols, int count);
int XConnectionNumber(Display *display);
Bool XFilterEvent(XEvent *event, Window w);
int XParseGeometry(const char *parsestring, int *x_return, int *y_return, unsigned int *width_return, unsigned int *height_return);
int XSetWMName(Display *display, Window w, void *text_prop);
int XSetTextProperty(Display *display, Window w, void *text_prop, Atom property);
int Xutf8TextListToTextProperty(Display *display, char **list, int count, XICCEncodingStyle style, void *text_prop_return);
int XSetWMIconName(Display *display, Window w, void *text_prop);
int XSetICValues(XIC ic, ...);
char *XSetLocaleModifiers(const char *modifier_list);
XIM XOpenIM(Display *display, void *rdb, char *res_name, char *res_class);
int XSetIMValues(XIM im, ...);
void *XVaCreateNestedList(int dummy, ...);
void *XAllocSizeHints(void);
int XSetWMProperties(Display *display, Window w, void *window_name, void *icon_name, char **argv, int argc, void *normal_hints, void *wm_hints, void *class_hints);
int XmbLookupString(XIC ic, XKeyEvent *event, char *buffer, int nbytes, KeySym *keysym, void *status);
int XSetICFocus(XIC ic);
int XUnsetICFocus(XIC ic);
int XDefaultScreen(Display *display);
Visual *XDefaultVisual(Display *display, int screen);
Colormap XDefaultColormap(Display *display, int screen);
Window XRootWindow(Display *display, int screen);
int XRecolorCursor(Display *display, Cursor cursor, XColor *foreground, XColor *background);
XIC XCreateIC(XIM im, ...);
int XUnregisterIMInstantiateCallback(Display *display, void *rdb, char *res_name, char *res_class, void (*callback)(Display *, XPointer, XPointer), void *client_data);
int XParseColor(Display *display, Colormap colormap, const char *spec, XColor *exact_def_return);

/*
 * auxv6 compatibility: several upstream ports (e.g. dwm/st) expect Xft types
 * like XftColor/XftFont to be visible when including core X11 headers.
 */
#include <X11/Xft/Xft.h>

#endif
