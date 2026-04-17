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
#include "stdio.h"
#include "string.h"

/* ------------------------------------------------------------------ *
 * menu_register_bar                                                   *
 * ------------------------------------------------------------------ */

void
menu_register_bar(Window bar_win, int bar_h)
{
    g_wm.menubar_win = bar_win;
    g_wm.menubar_h   = bar_h;
    dprintf(2, "6wm: menubar registered win=%lu h=%d\n",
            (unsigned long)bar_win, bar_h);
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
    /*
     * v1: the menubar sends _AUX_MENU_COMMAND directly to the client
     * window (not through the WM), so this path is only reached if the
     * WM itself is the target — which is not expected in normal operation.
     *
     * TODO(menu): add ownership validation per ui-menu-protocol.md §9
     * when security hardening pass is done.
     */
    (void)ev;
}
