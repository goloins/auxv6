/* X11/Xft/Xft.h - Minimal Xft stub for auxv6
 *
 * This provides minimal Xft declarations to allow compiling st upstream code.
 * Actual implementations are in auxv6 user/x11.c
 */

#ifndef _XFT_H_
#define _XFT_H_

#include <stdarg.h>
#include "X11/Xlib.h"
#include "X11/Xutil.h"

#ifndef _X11_XRENDER_COLOR_DEFINED_
#define _X11_XRENDER_COLOR_DEFINED_
typedef struct {
  unsigned short red;
  unsigned short green;
  unsigned short blue;
  unsigned short alpha;
} XRenderColor;
#endif

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
typedef int XftMatchKind;

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

/* TX — Xft tier: types */

typedef struct {
  double xx, xy, yx, yy;
} XftMatrix;

typedef enum {
  XftTypeVoid,
  XftTypeInteger,
  XftTypeDouble,
  XftTypeString,
  XftTypeBool,
  XftTypeMatrix,
  XftTypeCharSet,
  XftTypeFontSet
} XftType;

typedef struct {
  XftType type;
  union {
    int       integer;
    double    d;
    char     *string;
    int       b;
    XftMatrix matrix;
  } u;
} XftValue;

typedef struct _XftValueList XftValueList;
struct _XftValueList {
  XftValue     value;
  XftValueList *next;
};

typedef struct {
  char **objects;
  int    count;
  int    alloc;
} XftObjectSet;

/* TX — Xft tier: font set / list management */
extern Bool   XftInit(const char *config);
extern XftDraw *XftDrawCreateBitmap(Display *display, Pixmap bitmap);
extern Bool   XftGlyphExists(Display *display, XftFont *font, FcChar32 ucs4);
extern XftFont *XftFontOpenXlfd(Display *display, int screen, const char *xlfd);
typedef FcFontSet XftFontSet;
extern XftFontSet *XftFontSetCreate(void);
extern Bool   XftFontSetAdd(XftFontSet *s, XftPattern *font);
extern XftPattern *XftFontSetMatch(Display *display, int screen,
                                   XftFontSet **sets, int nsets,
                                   XftPattern *pattern, XftResult *result);
extern void   XftFontSetPrint(XftFontSet *s);
extern XftFontSet *XftListFontSets(XftFontSet **sets, int nsets,
                                   XftPattern *pattern,
                                   XftObjectSet *os);
extern XftFontSet *XftListFontsPatternObjects(Display *display, int screen,
                                              XftPattern *pattern,
                                              XftObjectSet *os);

/* TX — Xft tier: pattern manipulation */
extern XftPattern *XftNameParse(const char *name);
extern Bool   XftPatternAdd(XftPattern *p, const char *object,
                            XftValue value, Bool append);
extern Bool   XftPatternAddBool(XftPattern *p, const char *object, Bool b);
extern Bool   XftPatternAddDouble(XftPattern *p, const char *object, double d);
extern Bool   XftPatternAddInteger(XftPattern *p, const char *object, int i);
extern Bool   XftPatternAddMatrix(XftPattern *p, const char *object,
                                  const XftMatrix *matrix);
extern Bool   XftPatternAddString(XftPattern *p, const char *object,
                                  const char *s);
extern XftPattern *XftPatternDuplicate(XftPattern *p);
extern XftValue *XftPatternFind(XftPattern *p, const char *object, int id);
extern XftResult XftPatternGet(XftPattern *p, const char *object, int id,
                               XftValue *v);
extern XftResult XftPatternGetBool(XftPattern *p, const char *object,
                                   int id, Bool *b);
extern XftResult XftPatternGetDouble(XftPattern *p, const char *object,
                                     int id, double *d);
extern XftResult XftPatternGetMatrix(XftPattern *p, const char *object,
                                     int id, XftMatrix **matrix);
extern XftResult XftPatternGetString(XftPattern *p, const char *object,
                                     int id, char **s);
extern void   XftPatternPrint(XftPattern *p);
extern XftPattern *XftPatternVaBuild(XftPattern *p, va_list va);

/* TX — Xft tier: object set */
extern XftObjectSet *XftObjectSetCreate(void);
extern Bool   XftObjectSetAdd(XftObjectSet *os, const char *object);
extern void   XftObjectSetDestroy(XftObjectSet *os);
extern XftObjectSet *XftObjectSetVaBuild(const char *first, va_list va);

/* TX — Xft tier: config / defaults */
extern Bool   XftConfigSubstitute(Display *display, XftPattern *p,
                                  XftMatchKind kind);
extern Bool   XftDefaultHasRender(Display *display);
extern Bool   XftDefaultSet(Display *display, XftPattern *defaults);

/* TX — Xft tier: value */
extern void   XftValueDestroy(XftValue v);
extern void   XftValueListDestroy(XftValueList *l);
extern void   XftValuePrint(XftValue v);

#endif /* _XFT_H_ */
