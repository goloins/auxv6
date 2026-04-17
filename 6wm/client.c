/*
 * 6wm/client.c — client window lifecycle and classification
 */

#include "6wm.h"
#include "client.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

/* ------------------------------------------------------------------ *
 * Role classification (§9)                                            *
 * ------------------------------------------------------------------ */

WmRole
client_classify_role(Window win)
{
    XWindowAttributes  attrs;
    Atom               actual_type;
    int                actual_format;
    unsigned long      nitems, bytes_after;
    unsigned char     *prop = NULL;
    Window             transient = None;

    /* 1. Override-redirect → unmanaged (§2, §3.5) */
    if (XGetWindowAttributes(g_wm.dpy, win, &attrs) &&
        attrs.override_redirect)
        return ROLE_UNMANAGED;

    /* 2. _NET_WM_WINDOW_TYPE hint */
    if (g_wm.a_net_wm_window_type &&
        XGetWindowProperty(g_wm.dpy, win,
                           g_wm.a_net_wm_window_type,
                           0, 32, False, XA_ATOM,
                           &actual_type, &actual_format,
                           &nitems, &bytes_after, &prop) == Success
        && prop) {
        Atom         *types = (Atom *)prop;
        unsigned long i;
        WmRole        r = ROLE_DOCUMENT;

        for (i = 0; i < nitems; i++) {
            if (types[i] == g_wm.a_net_wm_window_type_dialog)
                { r = ROLE_DIALOG;    break; }
            if (types[i] == g_wm.a_net_wm_window_type_utility)
                { r = ROLE_UTILITY;   break; }
            if (types[i] == g_wm.a_net_wm_window_type_menu  ||
                types[i] == g_wm.a_net_wm_window_type_tooltip)
                { r = ROLE_UNMANAGED; break; }
        }
        XFree(prop);
        if (r != ROLE_DOCUMENT)
            return r;
    }

    /* 3. WM_TRANSIENT_FOR → dialog */
    if (XGetTransientForHint(g_wm.dpy, win, &transient)
        && transient != None && transient != win)
        return ROLE_DIALOG;

    return ROLE_DOCUMENT;
}

WmLayer
client_role_to_layer(WmRole role)
{
    switch (role) {
    case ROLE_UTILITY: return LAYER_UTILITY;
    case ROLE_MODAL:   return LAYER_MODAL;
    default:           return LAYER_DOCUMENT;
    }
}

/* ------------------------------------------------------------------ *
 * Title                                                               *
 * ------------------------------------------------------------------ */

void
client_update_title(Client *c)
{
    XTextProperty  tp;
    char         **list = NULL;
    int            n    = 0;

    if (!c) return;
    c->title[0] = '\0';

    /* Try WM_NAME */
    if (!XGetTextProperty(g_wm.dpy, c->win, &tp, XA_WM_NAME)
        || !tp.value)
        return;

    if (XmbTextPropertyToTextList(g_wm.dpy, &tp, &list, &n) == Success
        && n > 0 && list[0]) {
        snprintf(c->title, sizeof(c->title), "%s", list[0]);
        XFreeStringList(list);
    } else {
        /* Fallback: treat value as a Latin-1 string */
        snprintf(c->title, sizeof(c->title), "%s", (char *)tp.value);
    }
    XFree(tp.value);
}

/* ------------------------------------------------------------------ *
 * Protocol check                                                      *
 * ------------------------------------------------------------------ */

void
client_check_protocols(Client *c)
{
    Atom *protocols = NULL;
    int   n, i;

    if (!c) return;
    c->has_wm_delete = 0;

    if (!XGetWMProtocols(g_wm.dpy, c->win, &protocols, &n))
        return;

    for (i = 0; i < n; i++) {
        if (protocols[i] == g_wm.a_wm_delete_window) {
            c->has_wm_delete = 1;
            break;
        }
    }
    XFree(protocols);
}

/* ------------------------------------------------------------------ *
 * Geometry helpers                                                    *
 * ------------------------------------------------------------------ */

void
client_content_rect(const Client *c, int *cx, int *cy, int *cw, int *ch)
{
    int title_h, border;

    if (!c) return;

    border  = FRAME_BORDER;
    title_h = (c->role == ROLE_UTILITY) ? TITLE_H_UTIL : TITLE_H;

    if (cx) *cx = border;
    if (cy) *cy = border + title_h;
    if (cw) *cw = c->w - 2 * border;
    if (ch) *ch = c->h - border - title_h - border;
}

/* ------------------------------------------------------------------ *
 * Manage / unmanage                                                   *
 * ------------------------------------------------------------------ */

Client *
client_manage(Window win)
{
    Client            *c;
    XWindowAttributes  attrs;
    WmRole             role;
    int                title_h;

    role = client_classify_role(win);
    if (role == ROLE_UNMANAGED)
        return NULL;

    if (!XGetWindowAttributes(g_wm.dpy, win, &attrs))
        return NULL;

    c = (Client *)calloc(1, sizeof(Client));
    if (!c) return NULL;

    c->win   = win;
    c->frame = None;
    c->role  = role;
    c->layer = client_role_to_layer(role);
    c->state = STATE_INACTIVE;

    c->cw = attrs.width;
    c->ch = attrs.height;
    c->zoomed = 0;
    c->restore_x = c->restore_y = 0;
    c->restore_cw = c->restore_ch = 0;

    /* Frame size = client size + chrome insets */
    title_h = (role == ROLE_UTILITY) ? TITLE_H_UTIL : TITLE_H;
    c->w = c->cw + 2 * FRAME_BORDER;
    c->h = c->ch + FRAME_BORDER + title_h + FRAME_BORDER;

    /* Initial frame position: shift client position to account for chrome */
    c->x = attrs.x - FRAME_BORDER;
    c->y = attrs.y - FRAME_BORDER - title_h;

    /* Clamp: do not obscure menubar */
    if (c->x < 0)             c->x = 0;
    if (c->y < g_wm.menubar_h) c->y = g_wm.menubar_h;

    client_update_title(c);
    client_check_protocols(c);
    XGetTransientForHint(g_wm.dpy, win, &c->transient_for);

    /* Prepend to list */
    c->next      = g_wm.clients;
    g_wm.clients = c;

    return c;
}

void
client_unmanage(Client *c)
{
    Client **pp;

    if (!c) return;

    for (pp = &g_wm.clients; *pp; pp = &(*pp)->next) {
        if (*pp == c) {
            *pp = c->next;
            break;
        }
    }

    if (g_wm.focused == c)
        g_wm.focused = NULL;

    free(c);
}

Client *
client_find(Window win)
{
    Client *c;

    for (c = g_wm.clients; c; c = c->next) {
        if (c->win == win || c->frame == win)
            return c;
    }
    return NULL;
}

/* ------------------------------------------------------------------ *
 * Close (§8)                                                          *
 * ------------------------------------------------------------------ */

void
client_close(Client *c)
{
    XEvent ev;

    if (!c) return;

    if (c->has_wm_delete) {
        /* Graceful: WM_DELETE_WINDOW ClientMessage */
        memset(&ev, 0, sizeof(ev));
        ev.type                  = ClientMessage;
        ev.xclient.window        = c->win;
        ev.xclient.message_type  = g_wm.a_wm_protocols;
        ev.xclient.format        = 32;
        ev.xclient.data.l[0]     = (long)g_wm.a_wm_delete_window;
        ev.xclient.data.l[1]     = CurrentTime;
        XSendEvent(g_wm.dpy, c->win, False, NoEventMask, &ev);
    } else {
        /* Forced fallback (§8) */
        XKillClient(g_wm.dpy, c->win);
    }
}
