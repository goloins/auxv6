/*
 * 6wm/6wm.h — shared types, metrics, and global state declaration
 *
 * Metric baseline: docs/ui-window-contract.md (v1 locked)
 * All geometry constants here are normative; changes require updating
 * the contract document and relevant tests.
 */

#ifndef WM6_H
#define WM6_H

#include "types.h"
#include "X11/Xlib.h"
#include "X11/Xutil.h"
#include "X11/Xatom.h"

/* ------------------------------------------------------------------ *
 * Debug tracing — compile with -DWM6_DEBUG to enable                 *
 * All output goes to fd 2 (console/serial) so it appears even when   *
 * there is no display connection yet.                                 *
 * ------------------------------------------------------------------ */
#ifdef WM6_DEBUG
#  include "stdio.h"
#  define WM6DBG(fmt, ...) \
       dprintf(2, "6wm:dbg %s: " fmt "\n", __func__, ##__VA_ARGS__)
#else
#  define WM6DBG(fmt, ...) ((void)0)
#endif

/* ------------------------------------------------------------------ *
 * Frame metrics (ui-window-contract.md §4, v1 locked)                *
 * ------------------------------------------------------------------ */

#define FRAME_BORDER      2    /* 1px inner + 1px outer bevel (§4.1)  */
#define TITLE_H           19   /* document title bar height, px (§4.1)*/
#define TITLE_H_UTIL      15   /* utility compact title bar height, px (§4.5) */
#define CTRL_SIZE         11   /* document close/zoom box, px (§4.2)  */
#define CTRL_SIZE_UTIL    9    /* utility close/zoom box, px (§4.5)   */
#define CTRL_MARGIN       5    /* left edge → first control, doc (§4.2) */
#define CTRL_MARGIN_UTIL  4    /* left edge → first control, util (§4.5) */
#define CTRL_GAP          4    /* gap between controls, px (§4.2)     */
#define DRAG_THRESHOLD    4    /* px before drag initiates (§6.3)     */

/* ------------------------------------------------------------------ *
 * Window roles (§2)                                                   *
 * ------------------------------------------------------------------ */

typedef enum {
    ROLE_DOCUMENT  = 0,   /* primary app content window               */
    ROLE_UTILITY   = 1,   /* tool palettes, inspectors, doodads       */
    ROLE_DIALOG    = 2,   /* app-scoped transient prompt              */
    ROLE_MODAL     = 3,   /* blocks owner app interaction             */
    ROLE_UNMANAGED = 4,   /* menus, tooltips — WM does not decorate   */
} WmRole;

/* ------------------------------------------------------------------ *
 * Visual states (§5)                                                  *
 * ------------------------------------------------------------------ */

typedef enum {
    STATE_INACTIVE = 0,   /* lower contrast, controls dimmed          */
    STATE_ACTIVE   = 1,   /* strong contrast, controls fully visible  */
} WmState;

/* ------------------------------------------------------------------ *
 * Stack layers (§6.2)                                                 *
 * ------------------------------------------------------------------ */

typedef enum {
    LAYER_DOCUMENT = 0,   /* bottom layer                             */
    LAYER_UTILITY  = 1,   /* above owning-app document windows        */
    LAYER_MODAL    = 2,   /* top — blocks owner input                 */
} WmLayer;

/* ------------------------------------------------------------------ *
 * Drag mode (§6.3)                                                    *
 * ------------------------------------------------------------------ */

typedef enum {
    DRAG_NONE   = 0,
    DRAG_MOVE   = 1,
    DRAG_RESIZE = 2,
} DragMode;

/* ------------------------------------------------------------------ *
 * Client: one tracked managed toplevel                                *
 * ------------------------------------------------------------------ */

typedef struct Client {
    Window          win;              /* client XID                    */
    Window          frame;            /* WM-owned frame XID            */

    WmRole          role;
    WmState         state;
    WmLayer         layer;

    int             x, y;            /* frame position (root-relative) */
    int             w, h;            /* total frame size incl. chrome  */
    int             cw, ch;          /* client window size             */

    /* Zoom bookkeeping (§8). Stores pre-zoom geometry in client coords. */
    int             zoomed;
    int             restore_x, restore_y;
    int             restore_cw, restore_ch;

    char            title[256];      /* last known WM_NAME or empty    */

    /* Drag/resize scratch */
    DragMode        drag_mode;
    int             drag_start_x, drag_start_y;
    int             drag_orig_x,  drag_orig_y;
    int             drag_orig_w,  drag_orig_h;

    /* Computed control hit regions — set during frame_draw (§14) */
    int             close_x, close_y;
    int             zoom_x,  zoom_y;

    /* Protocol flags */
    int             has_wm_delete;   /* WM_DELETE_WINDOW supported?   */

    /* Transient/modal ownership */
    Window          transient_for;
    Window          app_leader;      /* app identity (group leader)   */
    int             modal_owner_scope; /* modal override: owner-only   */

    struct Client  *next;            /* intrusive singly-linked list  */
} Client;

/* ------------------------------------------------------------------ *
 * Global WM state                                                     *
 * ------------------------------------------------------------------ */

typedef struct {
    Display        *dpy;
    int             screen;
    Window          root;
    int             sw, sh;          /* screen resolution              */

    int             running;

    Client         *clients;         /* head of managed client list   */
    Client         *focused;         /* currently focused client      */

    /* Draw resources (owned by draw.c, valid after draw_init()) */
    GC              gc;
    XFontStruct    *font;

    /* Standard WM atoms */
    Atom            a_wm_protocols;
    Atom            a_wm_delete_window;
    Atom            a_wm_state;
    Atom            a_wm_transient_for;

    /* _NET type hints for role classification (§9) */
    Atom            a_net_wm_window_type;
    Atom            a_net_wm_window_type_normal;
    Atom            a_net_wm_window_type_dialog;
    Atom            a_net_wm_window_type_utility;
    Atom            a_net_wm_window_type_menu;
    Atom            a_net_wm_window_type_tooltip;
    Atom            a_net_wm_state;
    Atom            a_net_wm_state_modal;
    Atom            a_wm_client_leader;

    /* _NET_ACTIVE_WINDOW for menubar focus tracking */
    Atom            a_net_active_window;

    /* AUX menu protocol atoms (ui-menu-protocol.md §3.1) */
    Atom            a_aux_menu_version;
    Atom            a_aux_menu_serial;
    Atom            a_aux_menu_model;
    Atom            a_aux_menu_state;
    Atom            a_aux_menu_caps;
    Atom            a_aux_menu_command;
    Atom            a_aux_menu_command_text;
    Atom            a_aux_menubar_window;
    Atom            a_aux_menubar_height;
    Atom            a_aux_modal_scope_owner;

    /* Menubar service registration (menu.c) */
    Window          menubar_win;
    int             menubar_h;       /* px reserved at top for menubar */
} WmGlobal;

extern WmGlobal g_wm;

#endif /* WM6_H */
