/*
 * 6wm/6wm.c — main entry point and X event loop
 *
 * Startup sequence:
 *   1. XOpenDisplay
 *   2. Install error handler (detect existing WM via BadAccess)
 *   3. XSelectInput on root: SubstructureRedirect + SubstructureNotify
 *   4. atoms_init(), draw_init()
 *   5. Scan existing mapped windows (adopt pre-existing clients)
 *   6. Run event loop until g_wm.running == 0
 */

#include "6wm.h"
#include "atoms.h"
#include "client.h"
#include "draw.h"
#include "focus.h"
#include "frame.h"
#include "menu.h"
#include "stack.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "X11/keysym.h"

/* ------------------------------------------------------------------ *
 * Global state                                                        *
 * ------------------------------------------------------------------ */

WmGlobal g_wm;

/* ------------------------------------------------------------------ *
 * Error handling                                                      *
 * ------------------------------------------------------------------ */

static int g_another_wm;

#define WM_KEY_CYCLE_FWD XK_Tab
#define WM_KEY_CLOSE     XK_q

static Client *
cycle_target(Client *from, int dir)
{
    Client *order[256];
    int n = 0;
    int layer;
    int i;
    int idx = -1;
    Client *scan;

    /* Deterministic cycle order: documents, then utilities, then modals. */
    for (layer = (int)LAYER_DOCUMENT; layer <= (int)LAYER_MODAL; layer++) {
        for (scan = g_wm.clients; scan && n < 256; scan = scan->next) {
            if (!scan->frame)
                continue;
            if ((int)scan->layer != layer)
                continue;
            order[n++] = scan;
        }
    }

    if (n == 0)
        return NULL;

    if (!from)
        return order[0];

    for (i = 0; i < n; i++) {
        if (order[i] == from) {
            idx = i;
            break;
        }
    }

    if (idx < 0)
        return order[0];

    if (dir >= 0)
        return order[(idx + 1) % n];
    return order[(idx + n - 1) % n];
}

static void
grab_key_variant_set(int keycode, unsigned int mods, unsigned int numlock_mask)
{
    unsigned int v[4];
    int i;

    v[0] = mods;
    v[1] = mods | LockMask;
    v[2] = mods | numlock_mask;
    v[3] = mods | LockMask | numlock_mask;

    for (i = 0; i < 4; i++) {
        XGrabKey(g_wm.dpy, keycode, v[i],
                 g_wm.root, True, GrabModeAsync, GrabModeAsync);
    }
}

static unsigned int
wm_detect_numlock_mask(void)
{
    XModifierKeymap *modmap;
    KeyCode numlock;
    unsigned int mask = 0;
    int mod;
    int k;

    numlock = XKeysymToKeycode(g_wm.dpy, XK_Num_Lock);
    if (numlock == 0)
        return 0;

    modmap = XGetModifierMapping(g_wm.dpy);
    if (!modmap)
        return 0;

    for (mod = 0; mod < 8; mod++) {
        for (k = 0; k < modmap->max_keypermod; k++) {
            KeyCode kc = modmap->modifiermap[mod * modmap->max_keypermod + k];
            if (kc == numlock) {
                mask = (unsigned int)(1U << mod);
                break;
            }
        }
        if (mask)
            break;
    }

    XFreeModifiermap(modmap);
    return mask;
}

static void
wm_grab_keys(void)
{
    int tab;
    int q;
    unsigned int numlock_mask;

    XUngrabKey(g_wm.dpy, AnyKey, AnyModifier, g_wm.root);

    numlock_mask = wm_detect_numlock_mask();

    tab = (int)XKeysymToKeycode(g_wm.dpy, WM_KEY_CYCLE_FWD);
    q = (int)XKeysymToKeycode(g_wm.dpy, WM_KEY_CLOSE);

    if (tab > 0) {
        grab_key_variant_set(tab, Mod1Mask, numlock_mask);
        grab_key_variant_set(tab, Mod1Mask | ShiftMask, numlock_mask);
    }
    if (q > 0)
        grab_key_variant_set(q, Mod1Mask, numlock_mask);
}

static int
error_handler_check_wm(Display *dpy, XErrorEvent *e)
{
    (void)dpy;
    if (e->error_code == BadAccess)
        g_another_wm = 1;
    return 0;
}

static int
error_handler_default(Display *dpy, XErrorEvent *e)
{
    dprintf(2, "6wm: X error: code=%d request=%d resource=%lu\n",
            (int)e->error_code, (int)e->request_code,
            (unsigned long)e->resourceid);
    (void)dpy;
    /* Non-fatal: continue running */
    return 0;
}

static void
toggle_zoom(Client *c)
{
    int work_x, work_y, work_w, work_h;
    int title_h;
    int target_cw, target_ch;

    if (!c)
        return;

    if (c->zoomed) {
        frame_configure(c, c->restore_x, c->restore_y,
                        c->restore_cw, c->restore_ch);
        c->zoomed = 0;
        return;
    }

    c->restore_x  = c->x;
    c->restore_y  = c->y;
    c->restore_cw = c->cw;
    c->restore_ch = c->ch;

    work_x = 0;
    work_y = g_wm.menubar_h;
    work_w = g_wm.sw;
    work_h = g_wm.sh - g_wm.menubar_h;

    title_h = (c->role == ROLE_UTILITY) ? TITLE_H_UTIL : TITLE_H;
    target_cw = work_w - 2 * FRAME_BORDER;
    target_ch = work_h - FRAME_BORDER - title_h - FRAME_BORDER;

    if (target_cw < 16) target_cw = 16;
    if (target_ch < 16) target_ch = 16;

    frame_configure(c, work_x, work_y, target_cw, target_ch);
    c->zoomed = 1;
}

/* ------------------------------------------------------------------ *
 * Event handlers                                                      *
 * ------------------------------------------------------------------ */

/*
 * MapRequest (§7.1) — client wants to map a new toplevel.
 */
static void
on_map_request(XMapRequestEvent *e)
{
    Client *c;

    /* Already managed? */
    c = client_find(e->window);
    if (c) {
        if (c->frame == None)
            return;
        XMapWindow(g_wm.dpy, c->frame);
        XMapWindow(g_wm.dpy, c->win);
        return;
    }

    c = client_manage(e->window);

    if (!c) {
        /* Unmanaged or classify failure: let it map directly */
        XMapWindow(g_wm.dpy, e->window);
        return;
    }

    frame_create(c);
    if (!c->frame) {
        /* Frame creation failed; map client bare */
        XMapWindow(g_wm.dpy, c->win);
        client_unmanage(c);
        return;
    }

    /* §7.1 steps 6-7: map client then frame, set focus */
    XMapWindow(g_wm.dpy, c->win);
    XMapWindow(g_wm.dpy, c->frame);

    frame_draw(c);
    focus_set(c);
    stack_restack();
}

/*
 * ConfigureRequest (§7.2) — client requests a geometry or stacking change.
 */
static void
on_configure_request(XConfigureRequestEvent *e)
{
    Client        *c;

    c = client_find(e->window);

    if (c) {
        /* Managed: interpret through WM policy */
        int new_x = (e->value_mask & CWX)      ? e->x      : c->x;
        int new_y = (e->value_mask & CWY)      ? e->y      : c->y;
        int new_w = (e->value_mask & CWWidth)  ? e->width  : c->cw;
        int new_h = (e->value_mask & CWHeight) ? e->height : c->ch;
        if (e->value_mask & (CWX | CWY | CWWidth | CWHeight))
            c->zoomed = 0;
        frame_configure(c, new_x, new_y, new_w, new_h);
    } else {
        /* Unmanaged: pass through unchanged */
        XWindowChanges wc;
        wc.x            = e->x;
        wc.y            = e->y;
        wc.width        = e->width;
        wc.height       = e->height;
        wc.border_width = e->border_width;
        wc.sibling      = e->above;
        wc.stack_mode   = e->detail;
        XConfigureWindow(g_wm.dpy, e->window, e->value_mask, &wc);
    }
}

/*
 * UnmapNotify — client (or its frame) was unmapped.
 */
static void
on_unmap_notify(XUnmapEvent *e)
{
    Client *c;

    c = client_find(e->window);
    if (!c) return;

    /* Ignore frame-only unmaps; client teardown is keyed on client unmap. */
    if (e->window == c->frame)
        return;

    /* Ignore synthetic unmaps generated by our own reparent (§7.1) */
    if (e->send_event) return;

    /* Reparent side-effect: old parent (root) unmap while adopting client. */
    if (c->frame && e->window == c->win && e->event == g_wm.root)
        return;

    frame_destroy(c);
    client_unmanage(c);
    focus_revert();
    stack_restack();
}

/*
 * DestroyNotify — client window was destroyed.
 */
static void
on_destroy_notify(XDestroyWindowEvent *e)
{
    Client *c;

    c = client_find(e->window);
    if (!c) return;

    /* Frame may already be gone; clear to avoid double-destroy */
    c->frame = None;
    client_unmanage(c);
    focus_revert();
    stack_restack();
}

/*
 * ButtonPress — click-to-focus, drag initiation, and control actions.
 */
static void
on_button_press(XButtonEvent *e)
{
    Client *c;
    int     hit;

    c = client_find(e->window);
    if (!c) return;

    /* Focus and raise on any click (§6.1) */
    if (c != g_wm.focused)
        focus_set(c);

    /* Only handle button 1 for WM actions on the frame */
    if (e->window != c->frame || e->button != Button1)
        return;

    hit = frame_hittest(c, e->x, e->y);

    switch (hit) {

    case HITTEST_CLOSE:
        client_close(c);
        break;

    case HITTEST_ZOOM:
        toggle_zoom(c);
        break;

    case HITTEST_DRAG:
        c->drag_mode    = DRAG_MOVE;
        c->drag_start_x = e->x_root;
        c->drag_start_y = e->y_root;
        c->drag_orig_x  = c->x;
        c->drag_orig_y  = c->y;
        XGrabPointer(g_wm.dpy, c->frame, False,
                     ButtonReleaseMask | PointerMotionMask,
                     GrabModeAsync, GrabModeAsync,
                     None, None, CurrentTime);
        break;

    case HITTEST_RESIZE:
        c->drag_mode    = DRAG_RESIZE;
        c->drag_start_x = e->x_root;
        c->drag_start_y = e->y_root;
        c->drag_orig_w  = c->cw;
        c->drag_orig_h  = c->ch;
        XGrabPointer(g_wm.dpy, c->frame, False,
                     ButtonReleaseMask | PointerMotionMask,
                     GrabModeAsync, GrabModeAsync,
                     None, None, CurrentTime);
        break;

    default:
        break;
    }
}

/*
 * ButtonRelease — end drag or resize.
 */
static void
on_button_release(XButtonEvent *e)
{
    Client *c;

    (void)e;

    /* End any active drag on the focused client */
    c = g_wm.focused;
    if (c && c->drag_mode != DRAG_NONE) {
        c->drag_mode = DRAG_NONE;
        XUngrabPointer(g_wm.dpy, CurrentTime);
    }
}

/*
 * KeyPress — WM keyboard action path (§10 baseline).
 * Alt+Tab           : cycle focus forward
 * Alt+Shift+Tab     : cycle focus backward
 * Alt+Q             : close focused window
 */
static void
on_key_press(XKeyEvent *e)
{
    KeySym ks;

    if (!(e->state & Mod1Mask))
        return;

    ks = XKeycodeToKeysym(g_wm.dpy, e->keycode, 0);

    if (ks == WM_KEY_CYCLE_FWD) {
        Client *target;
        int dir = (e->state & ShiftMask) ? -1 : 1;
        target = cycle_target(g_wm.focused, dir);
        if (target)
            focus_set(target);
        return;
    }

    if (ks == WM_KEY_CLOSE) {
        if (g_wm.focused)
            client_close(g_wm.focused);
        return;
    }
}

/*
 * MotionNotify — drag/resize update (§6.3).
 */
static void
on_motion_notify(XMotionEvent *e)
{
    Client *c;
    int     dx, dy;

    c = g_wm.focused;
    if (!c || c->drag_mode == DRAG_NONE)
        return;

    dx = e->x_root - c->drag_start_x;
    dy = e->y_root - c->drag_start_y;

    /* Only start moving once we've exceeded DRAG_THRESHOLD (§6.3) */
    if (dx * dx + dy * dy < DRAG_THRESHOLD * DRAG_THRESHOLD
        && c->drag_mode == DRAG_MOVE)
        return;

    if (c->drag_mode == DRAG_MOVE) {
        int nx = c->drag_orig_x + dx;
        int ny = c->drag_orig_y + dy;

        /* Clamp to screen bounds */
        if (nx < 0) nx = 0;
        if (ny < g_wm.menubar_h) ny = g_wm.menubar_h;
        if (nx + c->w > g_wm.sw) nx = g_wm.sw - c->w;
        if (ny + c->h > g_wm.sh) ny = g_wm.sh - c->h;

        c->x = nx;
        c->y = ny;
        c->zoomed = 0;
        XMoveWindow(g_wm.dpy, c->frame, nx, ny);
        frame_send_configure(c);

    } else if (c->drag_mode == DRAG_RESIZE) {
        int nw = c->drag_orig_w + dx;
        int nh = c->drag_orig_h + dy;
        int min_w = ((c->role == ROLE_UTILITY)
                ? (CTRL_MARGIN_UTIL + 2 * CTRL_SIZE_UTIL + CTRL_GAP + CTRL_MARGIN_UTIL + 16)
                : (CTRL_MARGIN + 2 * CTRL_SIZE + CTRL_GAP + CTRL_MARGIN + 16));
        int min_h = 16;

        if (nw < min_w) nw = min_w;
        if (nh < min_h) nh = min_h;

        c->zoomed = 0;
        frame_configure(c, c->x, c->y, nw, nh);
    }
}

/*
 * Expose — redraw frame chrome.
 */
static void
on_expose(XExposeEvent *e)
{
    Client *c;

    /* Only redraw on the last Expose in a sequence */
    if (e->count > 0) return;

    c = client_find(e->window);
    if (c && c->frame == e->window)
        frame_draw(c);
}

/*
 * PropertyNotify — title change or AUX menu model/state update.
 */
static void
on_property_notify(XPropertyEvent *e)
{
    Client *c;
    WmRole old_role;
    WmLayer old_layer;
    Window old_leader;
    int old_modal_owner_scope;
    int needs_restack;
    int needs_redraw;
    int needs_focus_recheck;
    int title_h_old, title_h_new;

    c = client_find(e->window);
    if (!c) return;

    if (e->window != c->win)
        return;

    needs_restack = 0;
    needs_redraw = 0;
    needs_focus_recheck = 0;

    if (e->atom == XA_WM_NAME) {
        client_update_title(c);
        needs_redraw = 1;
    }

    if (e->atom == g_wm.a_wm_protocols) {
        client_check_protocols(c);
    }

    if (e->atom == XA_WM_HINTS
        || e->atom == g_wm.a_wm_client_leader
        || e->atom == g_wm.a_wm_transient_for
        || e->atom == g_wm.a_net_wm_window_type
        || e->atom == g_wm.a_net_wm_state
        || e->atom == g_wm.a_aux_modal_scope_owner) {

        old_role = c->role;
        old_layer = c->layer;
        old_leader = c->app_leader;
        old_modal_owner_scope = c->modal_owner_scope;
        title_h_old = (old_role == ROLE_UTILITY) ? TITLE_H_UTIL : TITLE_H;

        XGetTransientForHint(g_wm.dpy, c->win, &c->transient_for);
        client_update_app_identity(c);

        {
            WmRole new_role = client_classify_role(c->win);
            if (new_role != ROLE_UNMANAGED) {
                c->role = new_role;
                c->layer = client_role_to_layer(new_role);
            }
        }

        client_update_modal_scope(c);
        title_h_new = (c->role == ROLE_UTILITY) ? TITLE_H_UTIL : TITLE_H;

        if (c->role != old_role || title_h_new != title_h_old) {
            c->zoomed = 0;
            frame_configure(c, c->x, c->y, c->cw, c->ch);
            needs_redraw = 1;
        }

        if (c->layer != old_layer
            || c->app_leader != old_leader
            || c->modal_owner_scope != old_modal_owner_scope) {
            needs_restack = 1;
        }

        needs_focus_recheck = 1;
    }

    if (needs_restack) {
        if (g_wm.focused)
            stack_raise_family(g_wm.focused);
        else
            stack_restack();
    }

    if (needs_redraw && c->frame)
        frame_draw(c);

    if (needs_focus_recheck && g_wm.focused) {
        Client *constraint = g_wm.focused;
        focus_set(constraint);
    }

    /* Forward menu-related property changes to the menubar */
    menu_on_property(c, e);
}

/*
 * ConfigureNotify — sync managed frame to client-driven size changes.
 */
static void
on_configure_notify(XConfigureEvent *e)
{
    Client *c;

    c = client_find(e->window);
    if (!c)
        return;

    /* Only track client window configure notifications. */
    if (e->window != c->win)
        return;

    if (e->width <= 0 || e->height <= 0)
        return;

    /* Ignore no-op geometry notifications. */
    if (e->width == c->cw && e->height == c->ch)
        return;

    c->zoomed = 0;
    frame_configure(c, c->x, c->y, e->width, e->height);
}

/*
 * ClientMessage — WM_DELETE replies, AUX_MENU_COMMAND.
 */
static void
on_client_message(XClientMessageEvent *e)
{
    /*
     * _AUX_MENU_COMMAND sent to the WM itself (should not normally
     * happen in v1, but handle defensively).
     */
    if ((Atom)e->message_type == g_wm.a_aux_menu_command) {
        menu_dispatch_command(e);
        return;
    }

    /*
     * Check for menubar registration: a window sends a known
     * registration message to root declaring itself the menubar.
     *
     * TODO(menu): define a formal registration atom/protocol.
     * For now, menubar registration is done via menu_register_bar()
     * called from a dedicated startup path.
     */
}

/* ------------------------------------------------------------------ *
 * Adopt pre-existing mapped windows at startup                        *
 * ------------------------------------------------------------------ */

static void
scan_existing_windows(void)
{
    Window        root_ret, parent_ret;
    Window       *children = NULL;
    unsigned int  nchildren, i;
    XWindowAttributes attrs;

    if (!XQueryTree(g_wm.dpy, g_wm.root,
                    &root_ret, &parent_ret,
                    &children, &nchildren))
        return;

    for (i = 0; i < nchildren; i++) {
        if (!XGetWindowAttributes(g_wm.dpy, children[i], &attrs))
            continue;
        if (attrs.override_redirect || attrs.map_state != IsViewable)
            continue;

        /* Adopt as a managed client */
        {
            Client *c = client_manage(children[i]);
            if (!c) continue;

            frame_create(c);
            if (!c->frame) { client_unmanage(c); continue; }

            XMapWindow(g_wm.dpy, c->win);
            XMapWindow(g_wm.dpy, c->frame);
            frame_draw(c);
        }
    }

    if (children)
        XFree(children);

    stack_restack();

    /* Focus the first document-layer window found */
    {
        Client *c;
        for (c = g_wm.clients; c; c = c->next) {
            if (c->layer == LAYER_DOCUMENT) {
                focus_set(c);
                break;
            }
        }
    }
}

/* ------------------------------------------------------------------ *
 * Event loop                                                          *
 * ------------------------------------------------------------------ */

static void
run(void)
{
    XEvent ev;

    while (g_wm.running) {
        XNextEvent(g_wm.dpy, &ev);

        switch (ev.type) {
        case MapRequest:
            on_map_request(&ev.xmaprequest);
            break;
        case ConfigureRequest:
            on_configure_request(&ev.xconfigurerequest);
            break;
        case UnmapNotify:
            on_unmap_notify(&ev.xunmap);
            break;
        case DestroyNotify:
            on_destroy_notify(&ev.xdestroywindow);
            break;
        case ButtonPress:
            on_button_press(&ev.xbutton);
            break;
        case KeyPress:
            on_key_press(&ev.xkey);
            break;
        case ButtonRelease:
            on_button_release(&ev.xbutton);
            break;
        case MotionNotify:
            /* Coalesce: only process the latest motion event */
            while (XCheckTypedEvent(g_wm.dpy, MotionNotify, &ev))
                ;
            on_motion_notify(&ev.xmotion);
            break;
        case Expose:
            on_expose(&ev.xexpose);
            break;
        case PropertyNotify:
            on_property_notify(&ev.xproperty);
            break;
        case MappingNotify:
            wm_grab_keys();
            break;
        case ConfigureNotify:
            on_configure_notify(&ev.xconfigure);
            break;
        case ClientMessage:
            on_client_message(&ev.xclient);
            break;
        default:
            break;
        }
    }
}

/* ------------------------------------------------------------------ *
 * main                                                                *
 * ------------------------------------------------------------------ */

int
main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    memset(&g_wm, 0, sizeof(g_wm));

    g_wm.dpy = XOpenDisplay(NULL);
    if (!g_wm.dpy) {
        dprintf(2, "6wm: cannot open display\n");
        return 1;
    }

    g_wm.screen = DefaultScreen(g_wm.dpy);
    g_wm.root   = RootWindow(g_wm.dpy, g_wm.screen);
    g_wm.sw     = DisplayWidth(g_wm.dpy, g_wm.screen);
    g_wm.sh     = DisplayHeight(g_wm.dpy, g_wm.screen);

    /* Detect another running WM via BadAccess on SubstructureRedirectMask */
    g_another_wm = 0;
    XSetErrorHandler(error_handler_check_wm);
    XSelectInput(g_wm.dpy, g_wm.root,
                 SubstructureRedirectMask
                 | SubstructureNotifyMask
                 | ButtonPressMask
                 | KeyPressMask
                 | PropertyChangeMask);
    XSync(g_wm.dpy, False);

    if (g_another_wm) {
        dprintf(2, "6wm: another window manager is already running\n");
        XCloseDisplay(g_wm.dpy);
        return 1;
    }

    XSetErrorHandler(error_handler_default);

    atoms_init();
    draw_init();
    wm_grab_keys();

    g_wm.running = 1;

    scan_existing_windows();

    run();

    draw_fini();
    XCloseDisplay(g_wm.dpy);
    return 0;
}
