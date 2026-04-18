/*
 * 6wm/menu.h — AUX menu protocol — WM-integrated menubar
 *
 * The WM owns the top bar entirely:
 *   - Reads _AUX_MENU_MODEL / _AUX_MENU_STATE from the focused window.
 *   - Renders menu titles in the reserved root chrome band.
 *   - Opens an override-redirect popup window for item selection.
 *   - Dispatches _AUX_MENU_COMMAND + _AUX_MENU_COMMAND_TEXT to the
 *     focused window on item activation.
 */

#ifndef WM6_MENU_H
#define WM6_MENU_H

#include "6wm.h"

/* --- bar geometry / registration --------------------------------- */
void menu_register_bar(Window bar_win, int bar_h);
int  menu_sync_registration_from_root(void);
void menu_publish_registration_to_root(void);

/* --- focus / property bridge (called by 6wm.c event handlers) --- */
void menu_on_focus(Client *c);
void menu_on_property(Client *c, XPropertyEvent *ev);
void menu_dispatch_command(XClientMessageEvent *ev);

/* --- model loading (called after focus change or nudge) ---------- */
void menu_load_from_focused(Window w);

/* --- bar rendering (called from wm_draw_root_chrome) ------------- */
void menu_redraw_bar(void);
void menu_draw_titles(void);

/* --- popup management (called from 6wm.c event routing) ---------- */
void menu_popup_close(void);
void menu_handle_bar_press(int x, int y);
void menu_handle_popup_event(XEvent *ev);

/* --- fallback system menu shortcuts (no active window mode) ----- */
int  menu_handle_global_shortcut(KeySym ks);

#endif /* WM6_MENU_H */

