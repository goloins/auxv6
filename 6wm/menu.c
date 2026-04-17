/*
 * 6wm/menu.c — AUX menubar protocol integration
 *
 * Implements the WM side of ui-menu-protocol.md.
 *
 * The menubar service is a separate process that renders the global
 * top menu bar.  The WM bridges focused-window changes and property
 * updates between the app and the menubar service.
 */

#include "6wm.h"
#include "menu.h"
#include "client.h"
#include "frame.h"
#include "stdio.h"
#include "string.h"

#define MENU_DEFAULT_BAR_H (TITLE_H + 2)

static int
menu_has_command_text(Window w)
{
    Atom actual_type;
    int actual_format;
    unsigned long nitems;
    unsigned long bytes_after;
    unsigned char *data;
    int ok = 0;

    data = NULL;
    if (XGetWindowProperty(g_wm.dpy, w,
                           g_wm.a_aux_menu_command_text,
                           0, 1, False,
                           AnyPropertyType,
                           &actual_type, &actual_format,
                           &nitems, &bytes_after,
                           &data) == Success) {
        if (actual_type != None && nitems > 0)
            ok = 1;
    }
    if (data)
        XFree(data);

    return ok;
}

static void
menu_reflow_clients_for_bar(int bar_h)
{
    Client *c;

    for (c = g_wm.clients; c; c = c->next) {
        int nx, ny;

        if (!c->frame)
            continue;

        nx = c->x;
        ny = c->y;

        if (ny < bar_h)
            ny = bar_h;

        if (ny + c->h > g_wm.sh)
            ny = g_wm.sh - c->h;
        if (ny < bar_h)
            ny = bar_h;

        if (nx < 0)
            nx = 0;
        if (nx + c->w > g_wm.sw)
            nx = g_wm.sw - c->w;
        if (nx < 0)
            nx = 0;

        if (nx != c->x || ny != c->y)
            frame_configure(c, nx, ny, c->cw, c->ch);
    }
}

void
menu_publish_registration_to_root(void)
{
    unsigned long h;

    h = (unsigned long)((g_wm.menubar_h > 0) ? g_wm.menubar_h : MENU_DEFAULT_BAR_H);
    XChangeProperty(g_wm.dpy, g_wm.root,
                    g_wm.a_aux_menubar_height,
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&h, 1);

    if (g_wm.menubar_win != None) {
        Window w = g_wm.menubar_win;
        XChangeProperty(g_wm.dpy, g_wm.root,
                        g_wm.a_aux_menubar_window,
                        XA_WINDOW, 32, PropModeReplace,
                        (unsigned char *)&w, 1);
    } else {
        XDeleteProperty(g_wm.dpy, g_wm.root, g_wm.a_aux_menubar_window);
    }
}

/* ------------------------------------------------------------------ *
 * menu_register_bar                                                   *
 * ------------------------------------------------------------------ */

void
menu_register_bar(Window bar_win, int bar_h)
{
    int old_h = g_wm.menubar_h;

    if (bar_h < 1)
        bar_h = 1;
    if (bar_h > g_wm.sh)
        bar_h = g_wm.sh;

    g_wm.menubar_win = bar_win;
    g_wm.menubar_h   = bar_h;
    dprintf(2, "6wm: menubar registered win=%lu h=%d\n",
            (unsigned long)bar_win, bar_h);

    if (old_h != g_wm.menubar_h)
        menu_reflow_clients_for_bar(g_wm.menubar_h);

    menu_publish_registration_to_root();
}

int
menu_sync_registration_from_root(void)
{
    Atom actual_type;
    int actual_format;
    unsigned long nitems;
    unsigned long bytes_after;
    unsigned char *data;
    Window bar_win = None;
    int bar_h = 0;
    int changed = 0;
    XWindowAttributes attrs;

    data = NULL;
    if (XGetWindowProperty(g_wm.dpy, g_wm.root,
                           g_wm.a_aux_menubar_window,
                           0, 1, False,
                           XA_WINDOW,
                           &actual_type, &actual_format,
                           &nitems, &bytes_after,
                           &data) == Success) {
        if (actual_type == XA_WINDOW && actual_format == 32 && nitems >= 1 && data)
            bar_win = *((Window *)data);
    }
    if (data)
        XFree(data);

    data = NULL;
    if (XGetWindowProperty(g_wm.dpy, g_wm.root,
                           g_wm.a_aux_menubar_height,
                           0, 1, False,
                           XA_CARDINAL,
                           &actual_type, &actual_format,
                           &nitems, &bytes_after,
                           &data) == Success) {
        if (actual_type == XA_CARDINAL && actual_format == 32 && nitems >= 1 && data)
            bar_h = (int)(*((unsigned long *)data));
    }
    if (data)
        XFree(data);

    if (bar_win != None && bar_h > 0) {
        if (bar_win == g_wm.root ||
            !XGetWindowAttributes(g_wm.dpy, bar_win, &attrs) ||
            attrs.map_state != IsViewable) {
            bar_win = None;
        }
    }

    if (bar_win != None && bar_h > 0) {
        if (g_wm.menubar_win != bar_win || g_wm.menubar_h != bar_h) {
            menu_register_bar(bar_win, bar_h);
            changed = 1;
        }
    } else if (g_wm.menubar_win != None) {
        dprintf(2, "6wm: menubar unregistered\n");
        g_wm.menubar_win = None;
        if (g_wm.menubar_h <= 0)
            g_wm.menubar_h = MENU_DEFAULT_BAR_H;
        menu_reflow_clients_for_bar(g_wm.menubar_h);
        menu_publish_registration_to_root();
        changed = 1;
    }

    return changed;
}

/* ------------------------------------------------------------------ *
 * Internal: notify menubar of an event via ClientMessage             *
 * ------------------------------------------------------------------ */

/*
 * Send a bare _AUX_MENU_COMMAND ClientMessage to the menubar with
 * no payload — used as a lightweight "please re-read" nudge.
 * The menubar is expected to re-query _NET_ACTIVE_WINDOW and then
 * the relevant client properties.
 *
 * NOTE: This is the v1 minimal nudge.  A richer update protocol
 * (e.g. carrying the serial number) can be layered on here.
 */
static void
notify_menubar_update(void)
{
    XEvent ev;

    if (!g_wm.menubar_win)
        return;

    memset(&ev, 0, sizeof(ev));
    ev.type                  = ClientMessage;
    ev.xclient.window        = g_wm.menubar_win;
    ev.xclient.message_type  = g_wm.a_aux_menu_command;
    ev.xclient.format        = 32;
    /* data.l[0] = 0 means "active window changed, re-read" in v1 */
    ev.xclient.data.l[0]     = 0;

    XSendEvent(g_wm.dpy, g_wm.menubar_win, False, NoEventMask, &ev);
}

/* ------------------------------------------------------------------ *
 * menu_on_focus (§5.2)                                               *
 * ------------------------------------------------------------------ */

void
menu_on_focus(Client *c)
{
    Window w = c ? c->win : None;

    /*
     * Publish _NET_ACTIVE_WINDOW on root.  The menubar service watches
     * this property to know which window's menu model to display.
     */
    XChangeProperty(g_wm.dpy, g_wm.root,
                    g_wm.a_net_active_window,
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&w, 1);

    notify_menubar_update();
}

/* ------------------------------------------------------------------ *
 * menu_on_property (§5.3)                                            *
 * ------------------------------------------------------------------ */

void
menu_on_property(Client *c, XPropertyEvent *ev)
{
    Atom p;

    if (!c || !ev) return;

    /* Only propagate changes on the currently focused app */
    if (c != g_wm.focused) return;

    p = ev->atom;

    if (p == g_wm.a_aux_menu_model  ||
        p == g_wm.a_aux_menu_state  ||
        p == g_wm.a_aux_menu_serial ||
        p == g_wm.a_aux_menu_caps) {
        notify_menubar_update();
    }
}

/* ------------------------------------------------------------------ *
 * menu_dispatch_command (§5.4, §6)                                   *
 *                                                                     *
 * The menubar sends _AUX_MENU_COMMAND to the active client window    *
 * after writing the command ID string into _AUX_MENU_COMMAND_TEXT.   *
 * The WM's role here is minimal in v1: the menubar addresses the     *
 * client directly.  This function is a hook for future WM-side       *
 * policy (e.g. ownership validation — ui-menu-protocol.md §9).       *
 * ------------------------------------------------------------------ */

void
menu_dispatch_command(XClientMessageEvent *ev)
{
    XEvent fwd;
    Window target;

    if (!ev)
        return;

    if (g_wm.menubar_win == None)
        return;
    if (ev->window != g_wm.root)
        return;

    if (!g_wm.focused)
        return;
    target = g_wm.focused->win;

    if (!g_wm.focused || g_wm.focused->win != target)
        return;
    if (!client_find(target))
        return;
    if (!menu_has_command_text(target))
        return;

    memset(&fwd, 0, sizeof(fwd));
    fwd.type = ClientMessage;
    fwd.xclient.window = target;
    fwd.xclient.message_type = g_wm.a_aux_menu_command;
    fwd.xclient.format = 32;
    fwd.xclient.data.l[0] = 1;
    fwd.xclient.data.l[1] = 0;
    XSendEvent(g_wm.dpy, target, False, NoEventMask, &fwd);
}
