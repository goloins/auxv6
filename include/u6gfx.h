/*
 * User-Level Graphics Library for auxv6 (u6gfx)
 *
 * High-level graphics API with X11-compatible function signatures
 * but built on top of /dev/dri kernel interfaces.
 *
 * This is for simple graphics applications - not a full X11 server.
 */

#ifndef _U6GFX_H_
#define _U6GFX_H_

#include "types.h"
#include "stdint.h"

/* Opaque/forward declarations */
typedef struct Display Display;
typedef struct Screen Screen;
typedef struct Window Window;
typedef struct Drawable Drawable;
typedef struct Pixmap Pixmap;
typedef struct GC GC;
typedef struct XImage XImage;
typedef struct Visual Visual;

/* Standard X11 types */
typedef uint32_t XID;
typedef uint32_t Atom;
typedef uint32_t Time;
typedef uint32_t KeySym;
typedef int Status;
typedef int Bool;

#define True  1
#define False 0
#define None  0

/* Color */
struct XColor {
    uint32_t pixel;
    uint16_t red, green, blue;
    uint8_t flags;
    uint8_t pad;
};

/* Rectangle */
struct XRectangle {
    int16_t x, y;
    uint16_t width, height;
};

/* Point */
struct XPoint {
    int16_t x, y;
};

/* Basic graphics context attributes */
#define GCForeground    (1L << 0)
#define GCBackground    (1L << 1)
#define GCFunction      (1L << 2)
#define GCFill          (1L << 3)
#define GCLineWidth     (1L << 4)
#define GCFont          (1L << 5)

/* Graphics functions (ROP) */
#define GXcopy          3
#define GXclear         0
#define GXset           1
#define GXinvert        6
#define GXxor           7

/* Fill styles */
#define FillSolid       0
#define FillTiled       1
#define FillStippled    2
#define FillOpaqueStippled 3

/* Arc modes */
#define ArcChord        0
#define ArcPieSlice     1

/* Window event masks */
#define ExposureMask        (1L << 15)
#define KeyPressMask        (1L << 0)
#define KeyReleaseMask      (1L << 1)
#define ButtonPressMask     (1L << 2)
#define ButtonReleaseMask   (1L << 3)
#define PointerMotionMask   (1L << 6)
#define StructureNotifyMask (1L << 17)

/* Event types */
#define Expose              12
#define KeyPress            2
#define KeyRelease          3
#define ButtonPress         4
#define ButtonRelease       5
#define MotionNotify        6
#define EnterNotify         7
#define LeaveNotify         8
#define StructureNotify     17

/* Display management */
Display *XOpenDisplay(const char *display_name);
int XCloseDisplay(Display *display);
int XSync(Display *display, Bool discard);
const char *XDisplayName(const char *string);

/* Screen and visual */
Screen *XDefaultScreenOfDisplay(Display *display);
Visual *XDefaultVisualOfScreen(Screen *screen);
int XDisplayWidth(Display *display, int screen_number);
int XDisplayHeight(Display *display, int screen_number);
int XDisplayWidthMM(Display *display, int screen_number);
int XDisplayHeightMM(Display *display, int screen_number);

/* Geometry and windows */
Window XRootWindowOfScreen(Screen *screen);
Window XDefaultRootWindow(Display *display);
Window XCreateWindow(Display *display, Window parent,
                     int x, int y, uint width, uint height,
                     uint border_width, int depth, uint window_class,
                     Visual *visual, uint valuemask,
                     void *attributes); /* XSetWindowAttributes* */
int XDestroyWindow(Display *display, Window w);
int XMapWindow(Display *display, Window w);
int XUnmapWindow(Display *display, Window w);
int XGetWindowAttributes(Display *display, Window w, void *window_attributes);
int XResizeWindow(Display *display, Window w, uint width, uint height);
int XMoveWindow(Display *display, Window w, int x, int y);
int XClearWindow(Display *display, Window w);

/* Graphics context */
GC XCreateGC(Display *display, Drawable d, uint valuemask, void *values);
int XFreeGC(Display *display, GC gc);
int XChangeGC(Display *display, GC gc, uint valuemask, void *values);
int XSetForeground(Display *display, GC gc, uint pixel);
int XSetBackground(Display *display, GC gc, uint pixel);
int XSetFont(Display *display, GC gc, uint font);
int XSetLineAttributes(Display *display, GC gc,
                       uint line_width, int line_style,
                       int cap_style, int join_style);
int XSetFillStyle(Display *display, GC gc, int fill_style);

/* Drawing primitives */
int XDrawPoint(Display *display, Drawable d, GC gc, int x, int y);
int XDrawPoints(Display *display, Drawable d, GC gc,
                struct XPoint *points, int npoints, int mode);
int XDrawLine(Display *display, Drawable d, GC gc,
              int x1, int y1, int x2, int y2);
int XDrawLines(Display *display, Drawable d, GC gc,
               struct XPoint *points, int npoints, int mode);
int XDrawRectangle(Display *display, Drawable d, GC gc,
                   int x, int y, uint width, uint height);
int XDrawRectangles(Display *display, Drawable d, GC gc,
                    struct XRectangle *rectangles, int nrectangles);
int XDrawArc(Display *display, Drawable d, GC gc,
             int x, int y, uint width, uint height,
             int angle1, int angle2);
int XDrawArcs(Display *display, Drawable d, GC gc,
              void *arcs, int narcs); /* XArc* */

/* Filled primitives */
int XFillRectangle(Display *display, Drawable d, GC gc,
                   int x, int y, uint width, uint height);
int XFillRectangles(Display *display, Drawable d, GC gc,
                    struct XRectangle *rectangles, int nrectangles);
int XFillArc(Display *display, Drawable d, GC gc,
             int x, int y, uint width, uint height,
             int angle1, int angle2);
int XFillArcs(Display *display, Drawable d, GC gc,
              void *arcs, int narcs); /* XArc* */
int XFillPolygon(Display *display, Drawable d, GC gc,
                 struct XPoint *points, int npoints,
                 int shape, int mode);

/* Text drawing */
int XDrawString(Display *display, Drawable d, GC gc,
                int x, int y, const char *string, int length);
int XDrawImageString(Display *display, Drawable d, GC gc,
                     int x, int y, const char *string, int length);
int XTextWidth(void *font_struct, const char *string, int count);
void *XLoadQueryFont(Display *display, const char *name);
int XFreeFont(Display *display, void *font_struct);

/* Image operations */
XImage *XCreateImage(Display *display, Visual *visual, uint depth,
                     int format, int offset, char *data,
                     uint width, uint height, int bitmap_pad,
                     int bytes_per_line);
XImage *XGetImage(Display *display, Drawable d, int x, int y,
                  uint width, uint height, uint plane_mask, int format);
int XPutImage(Display *display, Drawable d, GC gc, XImage *image,
              int src_x, int src_y, int dest_x, int dest_y,
              uint width, uint height);
int XDestroyImage(XImage *ximage);

/* Color management */
int XAllocColor(Display *display, void *colormap, struct XColor *screen_in_out);
int XFreeColors(Display *display, void *colormap,
                uint32_t *pixels, int npixels, uint planes);
uint XGetPixel(XImage *image, int x, int y);
int XPutPixel(XImage *image, int x, int y, uint pixel);

/* Pixmaps */
Pixmap XCreatePixmap(Display *display, Drawable d,
                     uint width, uint height, uint depth);
int XFreePixmap(Display *display, Pixmap pixmap);
int XCopyArea(Display *display, Drawable src, Drawable dst, GC gc,
              int src_x, int src_y, uint width, uint height,
              int dest_x, int dest_y);
int XCopyPlane(Display *display, Drawable src, Drawable dst, GC gc,
               int src_x, int src_y, uint width, uint height,
               int dest_x, int dest_y, uint plane);

/* Events */
typedef struct {
    int type;
    uint serial;
    Bool send_event;
    Display *display;
    Window window;
    Window root;
    Window subwindow;
    Time time;
    int x, y;           /* pointer position */
    int x_root, y_root; /* root coords */
    uint state;
    uint button;        /* button number */
    Bool same_screen;
} XButtonEvent;

typedef struct {
    int type;
    uint serial;
    Bool send_event;
    Display *display;
    Window window;
    uint width, height;
    int x, y;
} XExposeEvent;

typedef struct {
    int type, serial;
    Display *display;
    Window window;
    uint count;
} XConfigureRequestEvent;

typedef union {
    int type;
    XButtonEvent xbutton;
    XExposeEvent xexpose;
    XConfigureRequestEvent xconfigurerequest;
    long pad[24];
} XEvent;

int XNextEvent(Display *display, XEvent *event_return);
int XPending(Display *display);
int XSelectInput(Display *display, Window w, long event_mask);

/* Atoms and properties */
Atom XInternAtom(Display *display, const char *atom_name, Bool only_if_exists);
char *XGetAtomName(Display *display, Atom atom);

/* Misc utilities */
int XBell(Display *display, int percent);
int XFlush(Display *display);

#endif /* _U6GFX_H_ */
