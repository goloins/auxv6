/*
 * 6wm/menu.h — AUX menubar protocol service (ui-menu-protocol.md)
 *
 * The WM acts as the integration point between focused app windows and
 * the menubar service process:
 *   - On focus change, the WM updates _NET_ACTIVE_WINDOW on root so
 *     the menubar can read the new app's _AUX_MENU_MODEL/_AUX_MENU_STATE.
 *   - On PropertyNotify for menu properties, the WM propagates an
 *     update hint to the menubar via a ClientMessage.
 *   - On _AUX_MENU_COMMAND ClientMessage (menubar → active app window),
 *     the WM routes it through to the correct client.
 */

#ifndef WM6_MENU_H
#define WM6_MENU_H

#include "6wm.h"

/*
 * Register the menubar service window.
 * bar_h is the pixel height reserved at the top of the screen.
 * Called once when the menubar process announces itself.
 */
void menu_register_bar(Window bar_win, int bar_h);

/*
 * Notify the menubar that the active window has changed (§5.2).
 * c may be NULL (no managed window is focused).
 * Updates _NET_ACTIVE_WINDOW on root.
 */
void menu_on_focus(Client *c);

/*
 * Handle a PropertyNotify event on a managed client window.
 * If the changed property is a menu-protocol atom, the menubar is
 * prompted to re-read the client's model/state (§5.3).
 */
void menu_on_property(Client *c, XPropertyEvent *ev);

/*
 * Route a _AUX_MENU_COMMAND ClientMessage from the menubar service
 * to the currently focused app window (§5.4, §6).
 */
void menu_dispatch_command(XClientMessageEvent *ev);

#endif /* WM6_MENU_H */
