/* X11/Xft/Xft.h - Minimal Xft stub for auxv6
 *
 * This provides minimal Xft declarations to allow compiling st upstream code.
 * Actual implementations are in auxv6 user/x11.c
 */

#ifndef _XFT_H_
#define _XFT_H_

#include "X11/Xlib.h"
#include "X11/Xutil.h"

typedef struct {
  unsigned short red;
  unsigned short green;
  unsigned short blue;
  unsigned short alpha;
} XRenderColor;

typedef int FcChar32;
typedef unsigned char FcChar8;
typedef int FcCharSet;
typedef int FcBool;
typedef void XftPattern;
typedef void FcPattern;
typedef void FcFontSet;
typedef int FcMatchKind;
typedef int FcResult;
typedef unsigned int FT_UInt;

/* FontConfig result/match constants */
#define FcResultMatch 0
#define FcResultNoMatch 1
#define FcMatchPattern 0
#define FcMatchFont 1

/* FontConfig boolean constants */
#define FcTrue 1
#define FcFalse 0

/* FontConfig property names (simplified as string constants) */
#define FC_FAMILY "family"
#define FC_STYLE "style"
#define FC_SLANT "slant"
#define FC_WEIGHT "weight"
#define FC_SIZE "size"
#define FC_PIXEL_SIZE "pixelsize"
#define FC_WIDTH "width"
#define FC_SPACING "spacing"
#define FC_FOUNDRY "foundry"
#define FC_LANG "lang"
#define FC_CHARSET "charset"
#define FC_SCALABLE "scalable"

/* FontConfig font values */
#define FC_SLANT_ROMAN 0
#define FC_SLANT_ITALIC 110
#define FC_SLANT_OBLIQUE 120
#define FC_WEIGHT_THIN 0
#define FC_WEIGHT_EXTRALIGHT 40
#define FC_WEIGHT_LIGHT 50
#define FC_WEIGHT_BOOK 75
#define FC_WEIGHT_REGULAR 80
#define FC_WEIGHT_MEDIUM 100
#define FC_WEIGHT_SEMIBOLD 180
#define FC_WEIGHT_BOLD 200
#define FC_WEIGHT_EXTRABOLD 205
#define FC_WEIGHT_BLACK 210

/* Glyph type for st */
typedef unsigned int Glyph;

typedef enum {
  XftResultMatch,
  XftResultNoMatch,
  XftResultTypeMismatch,
  XftResultNoId
} XftResult;

typedef struct {
  Drawable drawable;
  Display *display;
  int has_clip;
  XRectangle clip;
} XftDraw;

typedef struct {
  unsigned long pixel;
  struct {
    unsigned short red, green, blue, alpha;
  } color;
} XftColor;

typedef struct {
  int width, height;
  int x, y;
  int xOff, yOff;
} XGlyphInfo;

typedef struct {
  void *pattern;
  void *charset;
  int ascent;
  int descent;
  int height;
  int max_advance_width;
} XftFont;

typedef struct {
  XftFont *font;
  Glyph glyph;
  short x;
  short y;
} XftGlyphFontSpec;

typedef FcChar8 XftChar8;

/* Function declarations (implemented in auxv6 user/x11.c) */
extern XftDraw *XftDrawCreate(Display *display, Drawable drawable, Visual *visual, Colormap colormap);
extern void XftDrawChange(XftDraw *draw, Drawable drawable);
extern void XftDrawDestroy(XftDraw *draw);
extern void XftDrawRect(XftDraw *draw, XftColor *color, int x, int y, unsigned int width, unsigned int height);
extern void XftDrawSetClipRectangles(XftDraw *draw, int xOrigin, int yOrigin, XRectangle *rects, int nrects);
extern void XftDrawSetClip(XftDraw *draw, void *clip);
extern void XftDrawStringUtf8(XftDraw *draw, XftColor *color, XftFont *font, int x, int y, const XftChar8 *string, int len);
extern void XftDrawGlyphFontSpec(XftDraw *draw, XftColor *color, XftGlyphFontSpec *glyphs, int nglyphs);

extern int XftColorAllocValue(Display *display, Visual *visual, Colormap colormap, XRenderColor *color, XftColor *result);
extern int XftColorAllocName(Display *display, Visual *visual, Colormap colormap, const char *name, XftColor *result);
extern void XftColorFree(Display *display, Visual *visual, Colormap colormap, XftColor *color);

extern XftFont *XftFontOpenName(Display *display, int screen, const char *xlfd);
extern XftFont *XftFontOpenPattern(Display *display, XftPattern *pattern);
extern void XftFontClose(Display *display, XftFont *font);

extern void XftTextExtentsUtf8(Display *display, XftFont *font, const FcChar8 *string, int len, XGlyphInfo *extents);
extern int XftCharExists(Display *display, XftFont *font, FcChar32 ucs4);
extern unsigned int XftCharIndex(Display *display, XftFont *font, FcChar32 ucs4);

extern XftPattern *XftPatternCreate(void);
extern void XftPatternDestroy(XftPattern *p);
extern XftPattern *XftFontMatch(Display *display, int screen, XftPattern *pattern, XftResult *result);
extern void XftDefaultSubstitute(Display *display, int screen, XftPattern *pattern);
extern XftResult XftPatternGetInteger(XftPattern *p, const char *object, int id, int *i);

/* FontConfig stubs */
extern FcPattern *FcNameParse(const FcChar8 *name);
extern void FcPatternDestroy(FcPattern *p);
extern FcCharSet *FcCharSetCreate(void);
extern void FcCharSetDestroy(FcCharSet *fcs);
extern FcBool FcCharSetAddChar(FcCharSet *fcs, FcChar32 ucs4);
extern void FcFontSetDestroy(FcFontSet *ffs);
extern FcPattern *FcPatternDuplicate(FcPattern *p);
extern FcBool FcPatternAddCharSet(FcPattern *p, const char *object, FcCharSet *charset);
extern FcBool FcPatternAddBool(FcPattern *p, const char *object, FcBool b);
extern FcBool FcConfigSubstitute(void *config, FcPattern *p, FcMatchKind kind);
extern void FcDefaultSubstitute(FcPattern *pattern);
extern FcPattern *FcFontMatch(void *config, FcPattern *p, FcResult *result);
extern FcPattern *FcFontSetMatch(void *config, FcFontSet **sets, int nsets, FcPattern *p, FcResult *result);
extern FcFontSet *FcFontSort(void *config, FcPattern *pattern, FcBool trim, FcCharSet **csp, FcResult *result);
extern FcBool FcPatternDel(FcPattern *p, const char *object);
extern FcBool FcPatternAddDouble(FcPattern *p, const char *object, double d);
extern FcBool FcPatternAddInteger(FcPattern *p, const char *object, int i);
extern FcResult FcPatternGetDouble(FcPattern *p, const char *object, int id, double *d);
extern XftPattern *XftXlfdParse(const char *xlfd, int expand, FcBool ignore_scalable);
extern FcBool FcInit(void);

#endif /* _XFT_H_ */
