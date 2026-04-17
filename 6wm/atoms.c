/*
 * 6wm/atoms.c — intern all atoms used by 6wm
 */

#include "6wm.h"
#include "atoms.h"

void
atoms_init(void)
{
    Display *dpy = g_wm.dpy;

    /* Standard WM */
    g_wm.a_wm_protocols       = XInternAtom(dpy, "WM_PROTOCOLS",       False);
    g_wm.a_wm_delete_window   = XInternAtom(dpy, "WM_DELETE_WINDOW",   False);
    g_wm.a_wm_state           = XInternAtom(dpy, "WM_STATE",           False);
    g_wm.a_wm_transient_for   = XInternAtom(dpy, "WM_TRANSIENT_FOR",   False);

    /* _NET type hints (role classification, §9) */
    g_wm.a_net_wm_window_type =
        XInternAtom(dpy, "_NET_WM_WINDOW_TYPE",         False);
    g_wm.a_net_wm_window_type_normal =
        XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NORMAL",  False);
    g_wm.a_net_wm_window_type_dialog =
        XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG",  False);
    g_wm.a_net_wm_window_type_utility =
        XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    g_wm.a_net_wm_window_type_menu =
        XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_MENU",    False);
    g_wm.a_net_wm_window_type_tooltip =
        XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_TOOLTIP", False);
    g_wm.a_net_wm_state =
        XInternAtom(dpy, "_NET_WM_STATE",               False);
    g_wm.a_net_wm_state_modal =
        XInternAtom(dpy, "_NET_WM_STATE_MODAL",         False);
    g_wm.a_wm_client_leader =
        XInternAtom(dpy, "WM_CLIENT_LEADER",            False);

    /* Focus reporting for menubar service */
    g_wm.a_net_active_window =
        XInternAtom(dpy, "_NET_ACTIVE_WINDOW",          False);

    /* AUX menu protocol (ui-menu-protocol.md §3.1) */
    g_wm.a_aux_menu_version =
        XInternAtom(dpy, "_AUX_MENU_VERSION",           False);
    g_wm.a_aux_menu_serial =
        XInternAtom(dpy, "_AUX_MENU_SERIAL",            False);
    g_wm.a_aux_menu_model =
        XInternAtom(dpy, "_AUX_MENU_MODEL",             False);
    g_wm.a_aux_menu_state =
        XInternAtom(dpy, "_AUX_MENU_STATE",             False);
    g_wm.a_aux_menu_caps =
        XInternAtom(dpy, "_AUX_MENU_CAPS",              False);
    g_wm.a_aux_menu_command =
        XInternAtom(dpy, "_AUX_MENU_COMMAND",           False);
    g_wm.a_aux_menu_command_text =
        XInternAtom(dpy, "_AUX_MENU_COMMAND_TEXT",      False);
    g_wm.a_aux_modal_scope_owner =
        XInternAtom(dpy, "_AUX_MODAL_SCOPE_OWNER",      False);
}
