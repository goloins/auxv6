/*
 * 6wm/draw.h — platinum chrome drawing primitives
 *
 * Visual contract: ui-window-contract.md §4.4
 *
 * This module owns only low-level drawing calls; it takes explicit
 * geometry and role/state arguments.  Higher-level frame composition
 * (geometry math from Client state) lives in frame.c.
 */

#ifndef WM6_DRAW_H
#define WM6_DRAW_H

#include "6wm.h"

/* ------------------------------------------------------------------ *
 * Platinum palette (§4.4)                                             *
 * Values are 0xRRGGBB; XSetForeground takes unsigned long.           *
 * ------------------------------------------------------------------ */

#define PLT_WHITE           0xFFFFFFUL  /* outer bevel highlight       */
#define PLT_LIGHT_GRAY      0xDDDDDDUL  /* inner bevel light edge      */
#define PLT_FRAME_BG        0xC8C8C8UL  /* standard platinum chrome    */
#define PLT_TITLE_ACTIVE    0xAAAAA0UL  /* active title base fill       */
#define PLT_TITLE_STRIPE_A  0xB5B5AAUL  /* active stripe light band     */
#define PLT_TITLE_STRIPE_B  0x9F9F95UL  /* active stripe dark band      */
#define PLT_TITLE_INACTIVE  0xC8C8C8UL  /* inactive title (= frame bg) */
#define PLT_DARK_GRAY       0x888888UL  /* inner bevel shadow edge     */
#define PLT_BLACK           0x000000UL  /* outer bevel shadow edge     */

/* Title text (§4.3) */
#define PLT_TEXT_ACTIVE     0x000000UL
#define PLT_TEXT_INACTIVE   0x888888UL

/* Control box face */
#define PLT_CTRL_FACE       0xC8C8C8UL
#define PLT_CTRL_SYMBOL     0x000000UL  /* glyph color when active     */

/*
 * Font loaded at draw_init().  The runtime tries a small fallback chain,
 * starting with auxv6's bitmap Montecarlo face, then fixed and server default.
 *
 * TODO(draw): replace with a Chicago-style bitmap face once available
 * in the auxv6 font directory (§4.3).
 */
#define FONT_PRIMARY_MONTECARLO_XLFD \
    "-montecarlo-montecarlo-medium-r-normal--16-120-100-100-m-80-iso8859-1"
#define FONT_PRIMARY_MONTECARLO_SHORT "montecarlo-8x16"
#define FONT_FALLBACK_FIXED           "fixed"

/* ------------------------------------------------------------------ *
 * Lifecycle                                                           *
 * ------------------------------------------------------------------ */

/* Allocate GC and load font into g_wm.gc / g_wm.font. */
void draw_init(void);

/* Release GC and font. */
void draw_fini(void);

/* ------------------------------------------------------------------ *
 * Primitive helpers                                                   *
 * ------------------------------------------------------------------ */

/* Set foreground color (0xRRGGBB) on g_wm.gc. */
void draw_color(unsigned long rgb);

/* Fill rectangle (win-relative) with color. */
void draw_rect(Window win, unsigned long rgb, int x, int y, int w, int h);

/* ------------------------------------------------------------------ *
 * Chrome components                                                   *
 * ------------------------------------------------------------------ */

/*
 * Dual-edge bevel (§4.4) around (x,y,w,h) — the full outer bound.
 *   outer edges: PLT_WHITE (top/left) + PLT_BLACK (btm/right)
 *   inner edges: PLT_LIGHT_GRAY (top/left) + PLT_DARK_GRAY (btm/right)
 */
void draw_bevel(Window win, int x, int y, int w, int h);

/*
 * Title bar strip: background fill + title text centered in remaining
 * region after controls.
 *
 * (tx,ty,tw,th) — title bar rect in frame coords.
 * role          — controls which metrics apply and whether text renders.
 * state         — active vs inactive fill + text contrast.
 * title         — WM_NAME string (may be NULL or empty).
 * ctrl_right    — x-coord of right edge of rightmost control; text
 *                 is centered in the space to the right of this.
 *
 * Utility windows: title text suppressed per §4.3/§4.5.
 */
void draw_title_bar(Window win, WmRole role, WmState state,
                    int tx, int ty, int tw, int th,
                    const char *title, int ctrl_right);

/*
 * Window control box at (bx,by) with side dimension sz.
 * sym: 'X' for close, '+' for zoom (placeholder; §8).
 * Symbol is only drawn when state == STATE_ACTIVE; dimmed otherwise.
 */
void draw_ctrl(Window win, WmState state,
               int bx, int by, int sz, char sym);

#endif /* WM6_DRAW_H */
