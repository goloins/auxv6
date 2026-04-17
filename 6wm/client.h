/*
 * 6wm/client.h — client window tracking and lifecycle
 */

#ifndef WM6_CLIENT_H
#define WM6_CLIENT_H

#include "6wm.h"

/*
 * Classify the role of a window from available hints (§9).
 *
 * Policy order (highest-priority first):
 *   1. override_redirect => ROLE_UNMANAGED
 *   2. _NET_WM_WINDOW_TYPE atom list
 *   3. WM_TRANSIENT_FOR present => ROLE_DIALOG
 *   4. Default => ROLE_DOCUMENT
 */
WmRole  client_classify_role(Window win);

/* Map role to stack layer. */
WmLayer client_role_to_layer(WmRole role);

/*
 * Add win to the managed client list.
 * Classifies role, queries geometry and WM hints, allocates Client.
 * Does NOT create the frame window — call frame_create() next.
 * Returns NULL if the window should not be managed (unmanaged role,
 * destroyed before we could query it, or OOM).
 */
Client *client_manage(Window win);

/*
 * Remove c from the managed list and free it.
 * The caller is responsible for calling frame_destroy() first if
 * c->frame is still live.
 */
void    client_unmanage(Client *c);

/*
 * Find the Client that owns 'win' — either the client XID or the
 * frame XID.  Returns NULL if not found.
 */
Client *client_find(Window win);

/* Refresh c->title from WM_NAME (then _NET_WM_NAME) on the server. */
void    client_update_title(Client *c);

/* Query WM_PROTOCOLS; set c->has_wm_delete. */
void    client_check_protocols(Client *c);

/* Resolve and cache app leader identity (WM_HINTS / WM_CLIENT_LEADER). */
void    client_update_app_identity(Client *c);

/* Non-zero if both clients belong to the same app identity. */
int     client_same_app(const Client *a, const Client *b);

/*
 * Compute the client-area rectangle inside the frame (§4.1).
 * Frame coords; (cx,cy) is the top-left of the client origin.
 */
void    client_content_rect(const Client *c,
                             int *cx, int *cy, int *cw, int *ch);

/*
 * Close the client gracefully (WM_DELETE_WINDOW if supported) or by
 * force (XKillClient fallback) — §8.
 */
void    client_close(Client *c);

#endif /* WM6_CLIENT_H */
