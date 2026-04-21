/* fvwm2-stubs.c - portability stubs for auxv6 first-pass lane.
 * Compiled and linked by ports/makefiles/fvwm2.Makefile.
 * Provides link-time stubs for symbols that come from:
 *   - Excluded optional feature files (FRender, FRenderInit, Fft, Ficonv, session)
 *   - Missing system functions (getdtablesize, asprintf)
 *   - Missing Xlib symbols (XGetWMColormapWindows, XGetVisualInfo)
 */

#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>

/* ---- FGettext (no gettext on auxv6; FGettext.h declares this as a function) ---- */
const char *FGettext(char *s) { return s; }

/* ---- Session management (session.c excluded) ---- */
int sm_fd = -1;
void ProcessICEMsgs(void) {}

/* ---- AllocColorset (Colorset.c may not provide it if excluded path) ---- */
void AllocColorset(int n) { (void)n; }

/* ---- FRenderInit stubs (FRenderInit.c excluded) ---- */
void FRenderInit(Display *dpy) { (void)dpy; }
int  FRenderGetAlphaDepth(void) { return 0; }
int  FRenderGetMajorOpCode(void) { return 0; }
Bool FRenderGetErrorText(int code, char *msg) {
    (void)code;
    if (msg) msg[0] = '\0';
    return False;
}

/* ---- FRenderInterface stub (FRender.c excluded) ---- */
int FRenderRender(
        Display *dpy, Window win, Pixmap pixmap, Pixmap mask, Pixmap alpha,
        int depth, int shade_percent, unsigned long tint, int tint_percent,
        Drawable d, unsigned long gc, unsigned long alpha_gc,
        int src_x, int src_y, int src_w, int src_h,
        int dest_x, int dest_y, int dest_w, int dest_h,
        int do_repeat)
{
    (void)dpy; (void)win; (void)pixmap; (void)mask; (void)alpha;
    (void)depth; (void)shade_percent; (void)tint; (void)tint_percent;
    (void)d; (void)gc; (void)alpha_gc;
    (void)src_x; (void)src_y; (void)src_w; (void)src_h;
    (void)dest_x; (void)dest_y; (void)dest_w; (void)dest_h;
    (void)do_repeat;
    return 0;
}

/* ---- Ficonv stubs (Ficonv.c excluded) ---- */
char *FiconvUtf8ToCharset(Display *dpy, void *fc,
                          const char *in, unsigned int in_size)
{
    (void)dpy; (void)fc; (void)in_size;
    return in ? strdup(in) : NULL;
}

char *FiconvCharsetToCharset(Display *dpy, void *in_fc, void *out_fc,
                             const char *in, unsigned int in_size)
{
    (void)dpy; (void)in_fc; (void)out_fc; (void)in_size;
    return in ? strdup(in) : NULL;
}

/* ---- Fft stub (Fft.c excluded) ---- */
void FftPrintPatternInfo(void *f, int vertical) { (void)f; (void)vertical; }

/* ---- PImage stubs (PictureImageLoader.c excluded) ---- */
Bool PImageLoadPixmapFromFile(
        Display *dpy, Window win, char *file, Pixmap *pixmap, Pixmap *mask,
        Pixmap *alpha, int *width, int *height, int *depth,
        int *nalloc_pixels, unsigned long **alloc_pixels, int *no_limit,
        unsigned long fpa)
{
    (void)dpy; (void)win; (void)file; (void)fpa;
    if (pixmap) *pixmap = 0;
    if (mask)   *mask   = 0;
    if (alpha)  *alpha  = 0;
    if (width)  *width  = 0;
    if (height) *height = 0;
    if (depth)  *depth  = 0;
    if (nalloc_pixels) *nalloc_pixels = 0;
    if (alloc_pixels)  *alloc_pixels  = NULL;
    if (no_limit)      *no_limit      = 0;
    return False;
}

void *PImageLoadFvwmPictureFromFile(Display *dpy, Window win, char *path,
                                    unsigned long fpa)
{
    (void)dpy; (void)win; (void)path; (void)fpa;
    return NULL;
}

Bool PImageCreatePixmapFromArgbData(
        Display *dpy, Window win, unsigned int *data,
        int start, int width, int height,
        Pixmap *pixmap, Pixmap *mask, Pixmap *alpha,
        int *nalloc_pixels, unsigned long **alloc_pixels, int *no_limit,
        unsigned long fpa)
{
    (void)dpy; (void)win; (void)data; (void)start;
    (void)width; (void)height; (void)fpa;
    if (pixmap) *pixmap = 0;
    if (mask)   *mask   = 0;
    if (alpha)  *alpha  = 0;
    if (nalloc_pixels) *nalloc_pixels = 0;
    if (alloc_pixels)  *alloc_pixels  = NULL;
    if (no_limit)      *no_limit      = 0;
    return False;
}

Cursor PImageLoadCursorFromFile(Display *dpy, Window win, char *path,
                                int x_hot, int y_hot)
{
    (void)dpy; (void)win; (void)path; (void)x_hot; (void)y_hot;
    return 0; /* None */
}

/* ---- Xlib stubs ---- */
int XGetWMColormapWindows(Display *dpy, Window w,
                          Window **cmap_windows, int *count)
{
    (void)dpy; (void)w;
    if (cmap_windows) *cmap_windows = NULL;
    if (count)        *count        = 0;
    return 0;
}

XVisualInfo *XGetVisualInfo(Display *dpy, long vinfo_mask,
                            XVisualInfo *vinfo_template, int *nitems_return)
{
    (void)dpy; (void)vinfo_mask; (void)vinfo_template;
    if (nitems_return) *nitems_return = 0;
    return NULL;
}

/* ---- Session management stubs (session.c excluded) ---- */
void RestartInSession(char *filename, int isNative, int doPreserveState) {
    (void)filename; (void)isNative; (void)doPreserveState;
}
void LoadGlobalState(char *filename)      { (void)filename; }
void DisableRestoringState(void)          {}
void LoadWindowStates(char *filename)     { (void)filename; }
void SessionInit(void)                    {}
void SetClientID(char *client_id)         { (void)client_id; }
/* MatchWinToSM: Bool MatchWinToSM(FvwmWindow*, mwtsm_state_args*, initial_window_options_t*) */
int  MatchWinToSM(void *ewin, void *ret_state, void *win_opts) {
    (void)ewin; (void)ret_state; (void)win_opts; return 0; /* False */
}
/* CMD_*Session use F_CMD_ARGS = (cond_rc_t*, const exec_context_t*, char*) */
void CMD_QuitSession(void *cond_rc, const void *exc, char *action) {
    (void)cond_rc; (void)exc; (void)action;
}
void CMD_SaveQuitSession(void *cond_rc, const void *exc, char *action) {
    (void)cond_rc; (void)exc; (void)action;
}
void CMD_SaveSession(void *cond_rc, const void *exc, char *action) {
    (void)cond_rc; (void)exc; (void)action;
}

/* ---- FGettext/Ficonv init stubs (NLS/iconv excluded) ---- */
void FGettextInit(const char *domain, const char *dir, const char *module) {
    (void)domain; (void)dir; (void)module;
}
void FGettextSetLocalePath(const char *path)     { (void)path; }
void FGettextPrintLocalePath(int verbose)        { (void)verbose; }
void FiconvSetTransliterateUtf8(int toggle)      { (void)toggle; }

/* ---- Xlib stubs not yet in libX11.a ---- */
int XGetWMName(Display *dpy, Window w, XTextProperty *text_prop_return) {
    (void)dpy; (void)w;
    if (text_prop_return) {
        text_prop_return->value   = NULL;
        text_prop_return->encoding = 0;
        text_prop_return->format  = 0;
        text_prop_return->nitems  = 0;
    }
    return 0;
}
int XGetWMIconName(Display *dpy, Window w, XTextProperty *text_prop_return) {
    return XGetWMName(dpy, w, text_prop_return);
}

/* ---- freopen (not in auxv6 libc) ---- */
void *freopen(const char *path, const char *mode, void *stream) {
    (void)path; (void)mode; return stream;
}

/* ---- System functions missing from auxv6 libc ---- */
int getdtablesize(void) { return 256; }

int asprintf(char **strp, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { *strp = NULL; return -1; }
    *strp = (char *)malloc((size_t)n + 1);
    if (!*strp) return -1;
    va_start(ap, fmt);
    n = vsnprintf(*strp, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return n;
}
