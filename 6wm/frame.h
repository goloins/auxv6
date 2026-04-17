/*
 * 6wm/frame.h — WM-owned frame window lifecycle
 *
 * The frame is the reparent target for managed clients.  It owns the
 * non-client chrome: title bar, border bevel, and controls.
 */

#ifndef WM6_FRAME_H
#define WM6_FRAME_H

#include "6wm.h"

/* Hit-test result codes */
#define HITTEST_NONE    0
#define HITTEST_DRAG    1   /* title bar drag region                  */
#define HITTEST_CLOSE   2   /* close box                              */
#define HITTEST_ZOOM    3   /* zoom box                               */
#define HITTEST_RESIZE  4   /* resize affordance (edge/corner)        */

/*
 * Create the frame window, reparent the client into it, and send
 * the initial synthetic ConfigureNotify (§7.1 steps 3-7).
 * Sets c->frame; does NOT map — caller maps frame and client.
 */
void frame_create(Client *c);

/*
 * Reparent the client back to root and destroy the frame window.
 * Called during unmap/destroy cleanup (§7.3).
 */
void frame_destroy(Client *c);

/*
 * Redraw the full chrome for this client.
 * Called on Expose and whenever role, state, or title changes.
 * Updates c->close_x/y and c->zoom_x/y hit-region state (§14).
 */
void frame_draw(Client *c);

/*
 * Apply a geometry change through WM policy (§7.2).
 * (x,y) is the new frame origin; (w,h) is the requested client size.
 * Clamps to screen bounds (respects g_wm.menubar_h).
 * Calls frame_send_configure on completion.
 */
void frame_configure(Client *c, int x, int y, int w, int h);

/*
 * Send a synthetic ConfigureNotify to the client with the resolved
 * geometry (§7.2 — client receives resolved notifications).
 */
void frame_send_configure(const Client *c);

/*
 * Hit-test frame-relative point (fx,fy).
 * Returns one of the HITTEST_* codes.
 */
int frame_hittest(const Client *c, int fx, int fy);

#endif /* WM6_FRAME_H */
