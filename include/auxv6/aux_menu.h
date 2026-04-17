/*
 * auxv6/aux_menu.h — AUX Menu Protocol v1 app-side adapter helpers
 *
 * All functions use only standard X11 primitives (atoms, properties,
 * ClientMessage).  No non-standard Xlib extensions.
 *
 * Protocol atoms on each toplevel window:
 *   _AUX_MENU_VERSION   CARDINAL  — always AUX_MENU_VERSION (1)
 *   _AUX_MENU_SERIAL    CARDINAL  — monotonic, incremented on every change
 *   _AUX_MENU_MODEL     UTF8_STRING — menu tree; see ui-menu-protocol.md §4.1
 *   _AUX_MENU_STATE     UTF8_STRING — enable/check state; see §4.2
 *   _AUX_MENU_CAPS      UTF8_STRING — optional capability flags; see §8
 *   _AUX_MENU_COMMAND_TEXT UTF8_STRING — written by menubar before dispatch
 *   _AUX_MENU_COMMAND   (ClientMessage type atom)
 *
 * Dispatch flow:
 *   1. Menubar writes _AUX_MENU_COMMAND_TEXT on the active window.
 *   2. Menubar sends _AUX_MENU_COMMAND ClientMessage with data.l[0] = 1.
 *   3. App calls aux_menu_is_command_event() to identify the message.
 *   4. App calls aux_menu_get_command() to read and clear the command token.
 *   5. App dispatches by exact string match on the returned token.
 */

#ifndef AUXV6_AUX_MENU_H
#define AUXV6_AUX_MENU_H

#include "X11/Xlib.h"

/* Protocol version published in _AUX_MENU_VERSION. */
#define AUX_MENU_VERSION            1

/*
 * Maximum command-id string length including NUL.
 * Enforced on read by aux_menu_get_command().
 * Addresses ui-menu-protocol.md §12 open item 1.
 */
#define AUX_MENU_COMMAND_TEXT_MAX   256

/*
 * aux_menu_publish — publish a full menu model to window w.
 *
 *   model  UTF-8 text payload for _AUX_MENU_MODEL (NULL to clear).
 *   state  UTF-8 text payload for _AUX_MENU_STATE (NULL to clear).
 *   caps   UTF-8 text payload for _AUX_MENU_CAPS  (NULL to omit/leave).
 *
 * Sets _AUX_MENU_VERSION, writes model/state/caps properties, then
 * atomically increments _AUX_MENU_SERIAL to trigger menubar refresh.
 */
void aux_menu_publish(Display *dpy, Window w,
                      const char *model, const char *state,
                      const char *caps);

/*
 * aux_menu_set_state — update _AUX_MENU_STATE and increment serial.
 *
 * Use when only enablement/checked state changes (not the full model tree).
 */
void aux_menu_set_state(Display *dpy, Window w, const char *state);

/*
 * aux_menu_is_command_event — return 1 if ev is an _AUX_MENU_COMMAND
 * dispatch trigger (message_type == _AUX_MENU_COMMAND, data.l[0] == 1).
 */
int aux_menu_is_command_event(Display *dpy, XClientMessageEvent *ev);

/*
 * aux_menu_get_command — read _AUX_MENU_COMMAND_TEXT from window w.
 *
 * Copies the NUL-terminated command-id string into buf (up to bufsiz bytes
 * including NUL).  Deletes the property after reading so it is consumed.
 * Returns the number of bytes written (0 on miss or error).
 * Strings longer than AUX_MENU_COMMAND_TEXT_MAX-1 bytes are truncated.
 */
int aux_menu_get_command(Display *dpy, Window w, char *buf, int bufsiz);

#endif /* AUXV6_AUX_MENU_H */
