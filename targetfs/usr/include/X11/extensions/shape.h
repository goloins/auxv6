#ifndef _X11_EXTENSIONS_SHAPE_H_
#define _X11_EXTENSIONS_SHAPE_H_

#include <X11/Xlib.h>

#define ShapeBounding 0
#define ShapeClip 1
#define ShapeInput 2

#define ShapeSet 0
#define ShapeUnion 1
#define ShapeIntersect 2
#define ShapeSubtract 3
#define ShapeInvert 4

#define ShapeNotifyMask (1L << 0)
#define ShapeNotify 0

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window window;
  int kind;
  int x;
  int y;
  unsigned int width;
  unsigned int height;
  Time time;
  Bool shaped;
} XShapeEvent;

Status XShapeQueryExtension(Display *display,
                            int *event_base_return,
                            int *error_base_return);
Status XShapeQueryVersion(Display *display,
                          int *major_version_return,
                          int *minor_version_return);

void XShapeCombineMask(Display *display, Window dest, int dest_kind,
                       int x_off, int y_off, Pixmap src, int op);
void XShapeCombineShape(Display *display, Window dest, int dest_kind,
                        int x_off, int y_off,
                        Window src, int src_kind, int op);
void XShapeCombineRectangles(Display *display, Window dest, int dest_kind,
                             int x_off, int y_off,
                             XRectangle *rectangles, int n_rectangles,
                             int op, int ordering);
Status XShapeQueryExtents(Display *display, Window window,
                          Bool *bounding_shaped,
                          int *x_bounding, int *y_bounding,
                          unsigned int *w_bounding,
                          unsigned int *h_bounding,
                          Bool *clip_shaped,
                          int *x_clip, int *y_clip,
                          unsigned int *w_clip,
                          unsigned int *h_clip);
void XShapeSelectInput(Display *display, Window window,
                       unsigned long mask);

#endif
