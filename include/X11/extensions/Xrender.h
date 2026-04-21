#ifndef _X11_EXTENSIONS_XRENDER_H_
#define _X11_EXTENSIONS_XRENDER_H_

#include <X11/Xlib.h>

typedef XID Picture;
typedef int XFixed;

typedef struct {
  XFixed matrix[3][3];
} XTransform;

#ifndef _X11_XRENDER_COLOR_DEFINED_
#define _X11_XRENDER_COLOR_DEFINED_
typedef struct {
  unsigned short red;
  unsigned short green;
  unsigned short blue;
  unsigned short alpha;
} XRenderColor;
#endif

typedef struct {
  int type;
  int depth;
  void *direct;
  Colormap colormap;
} XRenderPictFormat;

typedef struct {
  int repeat;
  Picture alpha_map;
  int alpha_x_origin;
  int alpha_y_origin;
  int clip_x_origin;
  int clip_y_origin;
  Pixmap clip_mask;
  Bool graphics_exposures;
  int subwindow_mode;
  int poly_edge;
  int poly_mode;
  Atom dither;
  Bool component_alpha;
} XRenderPictureAttributes;

#define PictStandardARGB32 0
#define PictStandardRGB24 1

#define PictOpClear 0
#define PictOpSrc 1
#define PictOpOver 3

#define CPRepeat (1L << 0)
#define CPAlphaMap (1L << 1)
#define CPAlphaXOrigin (1L << 2)
#define CPAlphaYOrigin (1L << 3)
#define CPClipXOrigin (1L << 4)
#define CPClipYOrigin (1L << 5)
#define CPClipMask (1L << 6)
#define CPGraphicsExposure (1L << 7)
#define CPSubwindowMode (1L << 8)
#define CPPolyEdge (1L << 9)
#define CPPolyMode (1L << 10)
#define CPDither (1L << 11)
#define CPComponentAlpha (1L << 12)

#define XDoubleToFixed(d) ((XFixed)((d) * 65536.0))

XRenderPictFormat *XRenderFindVisualFormat(Display *display, Visual *visual);
XRenderPictFormat *XRenderFindStandardFormat(Display *display, int format);
Picture XRenderCreatePicture(Display *display, Drawable drawable,
                             XRenderPictFormat *format,
                             unsigned long valuemask,
                             XRenderPictureAttributes *attributes);
void XRenderFreePicture(Display *display, Picture picture);
void XRenderComposite(Display *display, int op,
                      Picture src, Picture mask, Picture dst,
                      int src_x, int src_y,
                      int mask_x, int mask_y,
                      int dst_x, int dst_y,
                      unsigned int width, unsigned int height);
void XRenderFillRectangle(Display *display, int op, Picture dst,
                          const XRenderColor *color,
                          int x, int y,
                          unsigned int width, unsigned int height);
void XRenderFillRectangles(Display *display, int op, Picture dst,
                           const XRenderColor *color,
                           const XRectangle *rects, int nrects);
void XRenderSetPictureFilter(Display *display, Picture picture,
                             const char *filter,
                             XFixed *params, int nparams);
void XRenderSetPictureTransform(Display *display, Picture picture,
                                const XTransform *transform);
Bool XRenderQueryExtension(Display *display, int *event_basep, int *error_basep);
Status XRenderQueryVersion(Display *display, int *major, int *minor);
Status XRenderQueryFormats(Display *display);
XRenderPictFormat *XRenderFindFormat(Display *display, unsigned long mask,
                                     XRenderPictFormat *templ, int count);
void XRenderChangePicture(Display *display, Picture picture,
                          unsigned long valuemask,
                          XRenderPictureAttributes *attributes);
void XRenderSetPictureClipRectangles(Display *display, Picture picture,
                                     int xOrigin, int yOrigin,
                                     const XRectangle *rects, int nrects);
void XRenderSetPictureClipRegion(Display *display, Picture picture,
                                 Region r);

#endif
