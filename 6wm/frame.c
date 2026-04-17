/*
 * 6wm/frame.c — WM-owned frame window lifecycle and chrome rendering
 *
 * Implements §7 (Lifecycle Contract) and §14 (Visual Appendix / hit regions).
 */

#include "6wm.h"
#include "frame.h"
#include "client.h"
#include "draw.h"
#include "stdio.h"
#include "string.h"

/* ------------------------------------------------------------------ *
 * Internal helpers                                                    *
 * ------------------------------------------------------------------ */

static long
frame_event_mask(void)
{
    return SubstructureNotifyMask
         | ExposureMask
         | ButtonPressMask
         | ButtonReleaseMask
         | PointerMotionMask
         | EnterWindowMask;
}

/* ------------------------------------------------------------------ *
 * frame_create (§7.1)                                                 *
 * ------------------------------------------------------------------ */

void
frame_create(Client *c)
{
    Window frame;
    int    cx, cy, cw, ch;

    if (!c || c->frame)
        return;

    /*
     * Create the frame window with a plain platinum background.
     * XCreateSimpleWindow takes (border_width, border_pixel, bg_pixel);
     * we manage all drawing ourselves, so border_width = 0.
     */
    frame = XCreateSimpleWindow(
        g_wm.dpy, g_wm.root,
        c->x, c->y,
        (unsigned int)c->w, (unsigned int)c->h,
        0,                   /* border_width — we draw our own bevel */
        PLT_BLACK,           /* border pixel (unused at width 0)     */
        PLT_FRAME_BG);       /* background pixel                     */

    if (!frame) {
        dprintf(2, "6wm: frame_create: XCreateSimpleWindow failed "
                   "for win %lu\n", (unsigned long)c->win);
        return;
    }

    /* Select events on the frame */
    XSelectInput(g_wm.dpy, frame, frame_event_mask());

    c->frame = frame;

    /* Reparent client into frame at the content inset position (§7.1.5) */
    client_content_rect(c, &cx, &cy, &cw, &ch);
    XReparentWindow(g_wm.dpy, c->win, frame, cx, cy);

    /* Select property and structure events on the client */
    XSelectInput(g_wm.dpy, c->win,
                 PropertyChangeMask
                 | StructureNotifyMask
                 | FocusChangeMask);

    /* §7.1.7 — synthetic ConfigureNotify */
    frame_send_configure(c);
}

/* ------------------------------------------------------------------ *
 * frame_destroy (§7.3)                                               *
 * ------------------------------------------------------------------ */

void
frame_destroy(Client *c)
{
    if (!c || !c->frame)
        return;

    /* Reparent client back to root before destroying the frame */
    XReparentWindow(g_wm.dpy, c->win, g_wm.root, c->x, c->y);
    XDestroyWindow(g_wm.dpy, c->frame);
    c->frame = None;
}

/* ------------------------------------------------------------------ *
 * frame_draw (§14)                                                    *
 *                                                                     *
 * Layout (document example, §14.1):                                  *
 *                                                                     *
 *  ## ============================================== ##   <- FRAME_BORDER px outer bevel
 *  ## [X] [+]        Title Text...                  ##   <- title bar height px
 *  ## ============================================== ##
 *  ## ................. client area ................. ##
 *  ####################################################
 *                                                                     *
 * Utility layout (§14.2): no title text, compact metrics.            *
 * ------------------------------------------------------------------ */

void
frame_draw(Client *c)
{
    int     title_h, ctrl_sz, ctrl_margin;
    int     title_x, title_y, title_w;
    int     ctrl_y;
    int     zoom_x;

    if (!c || !c->frame)
        return;

    title_h     = (c->role == ROLE_UTILITY) ? TITLE_H_UTIL : TITLE_H;
    ctrl_sz     = (c->role == ROLE_UTILITY) ? CTRL_SIZE_UTIL : CTRL_SIZE;
    ctrl_margin = (c->role == ROLE_UTILITY) ? CTRL_MARGIN_UTIL : CTRL_MARGIN;

    /* Frame background fill */
    draw_rect(c->frame, PLT_FRAME_BG, 0, 0, c->w, c->h);

    /* Outer/inner bevel around the entire frame (§4.1) */
    draw_bevel(c->frame, 0, 0, c->w, c->h);

    /* Title bar strip (inside bevel) */
    title_x = FRAME_BORDER;
    title_y = FRAME_BORDER;
    title_w = c->w - 2 * FRAME_BORDER;

    /* Vertically center controls within title bar */
    ctrl_y = FRAME_BORDER + (title_h - ctrl_sz) / 2;

    /* Title bar background + text.
     * ctrl_right is the x-coordinate of the right edge of the last control;
     * draw_title_bar uses this to compute the text centering region. */
    c->close_x = title_x + ctrl_margin;
    c->close_y = ctrl_y;
    zoom_x     = c->close_x + ctrl_sz + CTRL_GAP;
    c->zoom_x  = zoom_x;
    c->zoom_y  = ctrl_y;

    draw_title_bar(c->frame, c->role, c->state,
                   title_x, title_y, title_w, title_h,
                   c->title,
                   c->zoom_x + ctrl_sz /* ctrl_right */);

    /* Controls are drawn after title fill/text so they stay visible. */
    draw_ctrl(c->frame, c->state, c->close_x, c->close_y, ctrl_sz, 'X');
    draw_ctrl(c->frame, c->state, c->zoom_x, c->zoom_y, ctrl_sz, '+');

    XFlush(g_wm.dpy);
}

/* ------------------------------------------------------------------ *
 * frame_configure (§7.2)                                             *
 * ------------------------------------------------------------------ */

void
frame_configure(Client *c, int x, int y, int w, int h)
{
    int title_h, cx, cy, min_w, min_h;

    if (!c) return;

    title_h = (c->role == ROLE_UTILITY) ? TITLE_H_UTIL : TITLE_H;
    min_w = ((c->role == ROLE_UTILITY)
                ? (CTRL_MARGIN_UTIL + 2 * CTRL_SIZE_UTIL + CTRL_GAP + CTRL_MARGIN_UTIL + 16)
                : (CTRL_MARGIN + 2 * CTRL_SIZE + CTRL_GAP + CTRL_MARGIN + 16));
    min_h = 16;

    if (w < min_w) w = min_w;
    if (h < min_h) h = min_h;

    if (x < 0) x = 0;

    /* Clamp: do not obscure menubar */
    if (y < g_wm.menubar_h)
        y = g_wm.menubar_h;

    c->x  = x;
    c->y  = y;
    c->cw = w;
    c->ch = h;
    c->w  = w + 2 * FRAME_BORDER;
    c->h  = h + FRAME_BORDER + title_h + FRAME_BORDER;

    if (c->w > g_wm.sw)
        x = 0;
    else if (x + c->w > g_wm.sw)
        x = g_wm.sw - c->w;

    if (c->h > g_wm.sh - g_wm.menubar_h)
        y = g_wm.menubar_h;
    else if (y + c->h > g_wm.sh)
        y = g_wm.sh - c->h;

    c->x = x;
    c->y = y;

    cx = FRAME_BORDER;
    cy = FRAME_BORDER + title_h;

    if (c->frame) {
        XMoveResizeWindow(g_wm.dpy, c->frame,
                          c->x, c->y,
                          (unsigned int)c->w, (unsigned int)c->h);
        XMoveResizeWindow(g_wm.dpy, c->win,
                          cx, cy,
                          (unsigned int)c->cw, (unsigned int)c->ch);
    }

    frame_send_configure(c);
}

/* ------------------------------------------------------------------ *
 * frame_send_configure (§7.2)                                        *
 * ------------------------------------------------------------------ */

void
frame_send_configure(const Client *c)
{
    XEvent ev;
    int    cx, cy, cw, ch;

    if (!c) return;

    client_content_rect(c, &cx, &cy, &cw, &ch);

    memset(&ev, 0, sizeof(ev));
    ev.type                       = ConfigureNotify;
    ev.xconfigure.event           = c->win;
    ev.xconfigure.window          = c->win;
    ev.xconfigure.x               = c->x + cx;
    ev.xconfigure.y               = c->y + cy;
    ev.xconfigure.width           = c->cw;
    ev.xconfigure.height          = c->ch;
    ev.xconfigure.border_width    = 0;
    ev.xconfigure.above           = None;
    ev.xconfigure.override_redirect = False;

    XSendEvent(g_wm.dpy, c->win, False, StructureNotifyMask, &ev);
}

/* ------------------------------------------------------------------ *
 * frame_hittest (§14 — hit regions)                                  *
 * ------------------------------------------------------------------ */

int
frame_hittest(const Client *c, int fx, int fy)
{
    int title_h, ctrl_sz;

    if (!c) return HITTEST_NONE;

    title_h = (c->role == ROLE_UTILITY) ? TITLE_H_UTIL : TITLE_H;
    ctrl_sz = (c->role == ROLE_UTILITY) ? CTRL_SIZE_UTIL : CTRL_SIZE;

    /* Title bar band */
    if (fx >= FRAME_BORDER && fx < c->w - FRAME_BORDER &&
        fy >= FRAME_BORDER && fy < FRAME_BORDER + title_h) {

        /* Close box */
        if (fx >= c->close_x && fx < c->close_x + ctrl_sz &&
            fy >= c->close_y && fy < c->close_y + ctrl_sz)
            return HITTEST_CLOSE;

        /* Zoom box */
        if (fx >= c->zoom_x && fx < c->zoom_x + ctrl_sz &&
            fy >= c->zoom_y && fy < c->zoom_y + ctrl_sz)
            return HITTEST_ZOOM;

        return HITTEST_DRAG;
    }

    /*
     * Resize affordance: bottom edge (§6.3).
     * Dialogs may restrict resize per role policy — TODO when implementing
     * per-role constraints.
     */
    if (fy >= c->h - FRAME_BORDER && fy < c->h)
        return HITTEST_RESIZE;

    return HITTEST_NONE;
}
