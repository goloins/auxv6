/*
 * 6wm/draw.c — platinum chrome drawing primitives
 *
 * Implements the visual contract defined in docs/ui-window-contract.md §4.
 */

#include "6wm.h"
#include "draw.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"

/* ------------------------------------------------------------------ *
 * Lifecycle                                                           *
 * ------------------------------------------------------------------ */

void
draw_init(void)
{
    static const char *font_candidates[] = {
        FONT_PRIMARY_MONTECARLO_XLFD,
        FONT_PRIMARY_MONTECARLO_SHORT,
        FONT_FALLBACK_FIXED,
        0
    };
    int i;

    g_wm.gc = XCreateGC(g_wm.dpy, g_wm.root, 0, 0);
    if (!g_wm.gc) {
        dprintf(2, "6wm: draw_init: XCreateGC failed\n");
        return;
    }

    g_wm.font = 0;
    for (i = 0; font_candidates[i]; i++) {
        g_wm.font = XLoadQueryFont(g_wm.dpy, font_candidates[i]);
        if (g_wm.font) {
            dprintf(2, "6wm: draw_init: using font '%s'\n", font_candidates[i]);
            break;
        }
    }

    if (!g_wm.font) {
        dprintf(2, "6wm: draw_init: montecarlo/fixed unavailable, "
                   "using server default\n");
        g_wm.font = XQueryFont(g_wm.dpy, 0);
        if (g_wm.font)
            dprintf(2, "6wm: draw_init: using server default font fid=%lu\n",
                    (unsigned long)g_wm.font->fid);
    }

    if (g_wm.font)
        XSetFont(g_wm.dpy, g_wm.gc, g_wm.font->fid);
}

static void
draw_title_stripes(Window win, int x, int y, int w, int h)
{
    int row;

    if (w <= 0 || h <= 0)
        return;

    for (row = 0; row < h; row++) {
        unsigned long c = ((row / 2) & 1) ? PLT_TITLE_STRIPE_A : PLT_TITLE_STRIPE_B;
        draw_rect(win, c, x, y + row, w, 1);
    }
}

void
draw_fini(void)
{
    if (g_wm.font) {
        XFreeFont(g_wm.dpy, g_wm.font);
        g_wm.font = 0;
    }
    if (g_wm.gc) {
        XFreeGC(g_wm.dpy, g_wm.gc);
        g_wm.gc = 0;
    }
}

/* ------------------------------------------------------------------ *
 * Primitives                                                          *
 * ------------------------------------------------------------------ */

void
draw_color(unsigned long rgb)
{
    XSetForeground(g_wm.dpy, g_wm.gc, rgb);
}

void
draw_rect(Window win, unsigned long rgb, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    draw_color(rgb);
    XFillRectangle(g_wm.dpy, win, g_wm.gc,
                   x, y, (unsigned int)w, (unsigned int)h);
}

/* ------------------------------------------------------------------ *
 * Bevel (§4.4)                                                        *
 *                                                                     *
 * Two-pixel border:                                                   *
 *   pixel 0: outer — PLT_WHITE / PLT_BLACK                           *
 *   pixel 1: inner — PLT_LIGHT_GRAY / PLT_DARK_GRAY                  *
 *                                                                     *
 * (x,y,w,h) is the full outer bounding rect.                         *
 * ------------------------------------------------------------------ */

void
draw_bevel(Window win, int x, int y, int w, int h)
{
    Display *dpy = g_wm.dpy;
    GC       gc  = g_wm.gc;

    /* Outer: white top+left, black bottom+right */
    XSetForeground(dpy, gc, PLT_WHITE);
    XDrawLine(dpy, win, gc, x,         y,         x + w - 1, y);
    XDrawLine(dpy, win, gc, x,         y,         x,         y + h - 1);

    XSetForeground(dpy, gc, PLT_BLACK);
    XDrawLine(dpy, win, gc, x,         y + h - 1, x + w - 1, y + h - 1);
    XDrawLine(dpy, win, gc, x + w - 1, y,         x + w - 1, y + h - 1);

    /* Inner: light-gray top+left, dark-gray bottom+right */
    XSetForeground(dpy, gc, PLT_LIGHT_GRAY);
    XDrawLine(dpy, win, gc, x + 1,     y + 1,     x + w - 2, y + 1);
    XDrawLine(dpy, win, gc, x + 1,     y + 1,     x + 1,     y + h - 2);

    XSetForeground(dpy, gc, PLT_DARK_GRAY);
    XDrawLine(dpy, win, gc, x + 1,     y + h - 2, x + w - 2, y + h - 2);
    XDrawLine(dpy, win, gc, x + w - 2, y + 1,     x + w - 2, y + h - 2);
}

/* ------------------------------------------------------------------ *
 * Title bar                                                           *
 * ------------------------------------------------------------------ */

/*
 * Truncate 'src' into 'dst[dstsz]' with ellipsis if it overflows
 * max_px pixels wide using the loaded font metrics.
 */
static void
title_truncate(const char *src, char *dst, int dstsz, int max_px)
{
    static const char ell[] = "...";
    int ell_w, src_len, full_w, i;

    if (!src || !dst || dstsz <= 0)
        return;

    dst[0] = '\0';

    if (!g_wm.font) {
        snprintf(dst, (size_t)dstsz, "%s", src);
        return;
    }

    src_len = (int)strlen(src);
    full_w  = XTextWidth(g_wm.font, src, src_len);

    if (full_w <= max_px) {
        snprintf(dst, (size_t)dstsz, "%s", src);
        return;
    }

    ell_w = XTextWidth(g_wm.font, ell, (int)(sizeof(ell) - 1));

    /* Scan from tail until truncated text + ellipsis fits. */
    for (i = src_len - 1; i > 0; i--) {
        if (XTextWidth(g_wm.font, src, i) + ell_w <= max_px)
            break;
    }
    snprintf(dst, (size_t)dstsz, "%.*s%s", i, src, ell);
}

void
draw_title_bar(Window win, WmRole role, WmState state,
               int tx, int ty, int tw, int th,
               const char *title, int ctrl_right)
{
    Display      *dpy  = g_wm.dpy;
    GC            gc   = g_wm.gc;
    unsigned long bg   = (state == STATE_ACTIVE)
                             ? PLT_TITLE_ACTIVE
                             : PLT_TITLE_INACTIVE;

    /* Fill title bar background */
    draw_rect(win, bg, tx, ty, tw, th);
    if (state == STATE_ACTIVE)
        draw_title_stripes(win, tx, ty, tw, th);

    /*
     * Title text: document windows only (§4.3).
     * Utility windows render no text; the strip is purely structural.
     */
    if (role == ROLE_UTILITY || !title || !title[0] || !g_wm.font)
        return;

    {
        char trunc[256];
        int  text_left = ctrl_right + CTRL_GAP;
        int  text_avail = (tx + tw) - text_left - CTRL_MARGIN;
        int  tw_px, th_px, ty_base;

        if (text_avail <= 0)
            return;

        title_truncate(title, trunc, (int)sizeof(trunc), text_avail);
        if (!trunc[0])
            return;

        tw_px   = XTextWidth(g_wm.font, trunc, (int)strlen(trunc));
        th_px   = g_wm.font->ascent + g_wm.font->descent;
        /* Baseline: vertically center within title height */
        ty_base = ty + th / 2 + g_wm.font->ascent - th_px / 2;

        XSetForeground(dpy, gc, (state == STATE_ACTIVE)
                                    ? PLT_TEXT_ACTIVE
                                    : PLT_TEXT_INACTIVE);
        /* Horizontal center in remaining region (§4.3) */
        XDrawString(dpy, win, gc,
                    text_left + (text_avail - tw_px) / 2, ty_base,
                    trunc, (int)strlen(trunc));
    }
}

/* ------------------------------------------------------------------ *
 * Control box (§4.2)                                                  *
 *                                                                     *
 * Simple bevel + face fill.  The inner 'X' or '+' symbol is a        *
 * placeholder; precise Mac-style glyphs are a TODO (§8).             *
 * ------------------------------------------------------------------ */

void
draw_ctrl(Window win, WmState state, int bx, int by, int sz, char sym)
{
    Display      *dpy  = g_wm.dpy;
    GC            gc   = g_wm.gc;
    unsigned long face = (state == STATE_ACTIVE)
                             ? PLT_CTRL_FACE
                             : PLT_LIGHT_GRAY;

    /* Face fill */
    draw_rect(win, face, bx, by, sz, sz);

    /* 1-px bevel: white top+left, dark bottom+right */
    XSetForeground(dpy, gc, PLT_WHITE);
    XDrawLine(dpy, win, gc, bx,          by,          bx + sz - 1, by);
    XDrawLine(dpy, win, gc, bx,          by,          bx,          by + sz - 1);

    XSetForeground(dpy, gc, PLT_DARK_GRAY);
    XDrawLine(dpy, win, gc, bx,          by + sz - 1, bx + sz - 1, by + sz - 1);
    XDrawLine(dpy, win, gc, bx + sz - 1, by,          bx + sz - 1, by + sz - 1);

    /* Symbol — drawn only in active state (§5) */
    if (state == STATE_ACTIVE && sym != '\0' && g_wm.font) {
        char s[2];
        int  sw, sbase;

        s[0] = sym;
        s[1] = '\0';
        sw    = XTextWidth(g_wm.font, s, 1);
        sbase = by + sz / 2 + g_wm.font->ascent
                  - (g_wm.font->ascent + g_wm.font->descent) / 2;

        XSetForeground(dpy, gc, PLT_CTRL_SYMBOL);
        XDrawString(dpy, win, gc, bx + (sz - sw) / 2, sbase, s, 1);
    }
}
