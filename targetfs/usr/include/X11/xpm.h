#ifndef _X11_XPM_H_
#define _X11_XPM_H_

#include <X11/Xlib.h>

typedef unsigned long Pixel;

#define XpmSuccess 0
#define XpmNoMemory 1
#define XpmColorError 2
#define XpmOpenFailed 3
#define XpmFileInvalid 4

#define XpmSize (1L << 0)
#define XpmReturnPixels (1L << 1)
#define XpmReturnExtensions (1L << 2)
#define XpmColorSymbols (1L << 3)
#define XpmRgbFilename (1L << 4)
#define XpmCloseness (1L << 5)

typedef struct {
  char *name;
  char *value;
  Pixel pixel;
} XpmColorSymbol;

typedef struct {
  unsigned long valuemask;
  Visual *visual;
  Colormap colormap;
  unsigned int depth;
  unsigned int width;
  unsigned int height;
  unsigned int x_hotspot;
  unsigned int y_hotspot;
  unsigned int cpp;
  Pixel *pixels;
  unsigned int npixels;
  XpmColorSymbol *colorsymbols;
  unsigned int numsymbols;
  char *rgb_fname;
} XpmAttributes;

int XpmReadFileToPixmap(Display *display, Drawable d, const char *filename,
                        Pixmap *pixmap_return, Pixmap *shapemask_return,
                        XpmAttributes *attributes);
int XpmCreatePixmapFromData(Display *display, Drawable d, char **data,
                            Pixmap *pixmap_return, Pixmap *shapemask_return,
                            XpmAttributes *attributes);
void XpmFreeAttributes(XpmAttributes *attributes);

#endif
