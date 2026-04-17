/*
 * 6wm/menu.c — AUX menubar protocol — WM-integrated
 *
 * The WM owns the top bar entirely: reads _AUX_MENU_MODEL/_AUX_MENU_STATE
 * from the focused window, renders menu titles in the root chrome band,
 * opens a popup for item selection, and dispatches commands via
 * _AUX_MENU_COMMAND_TEXT + ClientMessage (ui-menu-protocol.md).
 */

#include "6wm.h"
#include "menu.h"
#include "draw.h"
#include "client.h"
#include "frame.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"

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

    XChangeProperty(g_wm.dpy, g_wm.root,
                    g_wm.a_net_active_window,
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&w, 1);

    menu_load_from_focused(w);
    menu_redraw_bar();

    /* Nudge external service if one is ever registered (no-op when none). */
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
    if (c != g_wm.focused) return;

    p = ev->atom;

    if (p == g_wm.a_aux_menu_model  ||
        p == g_wm.a_aux_menu_state  ||
        p == g_wm.a_aux_menu_serial ||
        p == g_wm.a_aux_menu_caps) {
        menu_load_from_focused(c->win);
        menu_redraw_bar();
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

/* ==================================================================
 * In-WM menu model, bar rendering, and popup
 * (ui-menu-protocol.md §4, §5, §6 — WM-integrated path)
 * ================================================================== */

#define MNU_FIELD_MAX   256
#define MNU_ITEM_MAX     64
#define MNU_MAX          32
#define MNU_MODEL_MAX  16384
#define MNU_STATE_MAX   4096

/* Popup geometry */
#define POPUP_ITEM_H    20
#define POPUP_PAD_X     10
#define POPUP_PAD_TOP    2
#define POPUP_MIN_W    120

/* Selection highlight colours */
#define PLT_SEL_BG   0x000080UL
#define PLT_SEL_TEXT 0xFFFFFFUL

typedef struct {
    char id[MNU_FIELD_MAX];
    char label[MNU_FIELD_MAX];
    char command_id[MNU_FIELD_MAX];
    char kind[32];    /* normal|check|radio|separator|submenu */
    int  enabled;
    int  checked;
} WmMenuItem;

typedef struct {
    char       id[MNU_FIELD_MAX];
    char       label[MNU_FIELD_MAX];
    int        ordinal;
    WmMenuItem items[MNU_ITEM_MAX];
    int        n_items;
    int        bar_x;  /* title x-offset in bar (set by menu_draw_titles) */
    int        bar_w;  /* title width in bar */
} WmMenu;

static WmMenu  s_menus[MNU_MAX];
static int     s_n_menus;
static long    s_serial    = -1;
static Window  s_model_win = None;

/* ---- internal helpers ------------------------------------------ */

static int
mnu_text_width(const char *s)
{
    if (!s || !*s) return 0;
    if (g_wm.font)
        return XTextWidth(g_wm.font, s, (int)strlen(s));
    return (int)strlen(s) * 8;
}

static int
mnu_parse_fields(const char *line, char out[][MNU_FIELD_MAX], int maxf)
{
    int n = 0;
    const char *p = line;
    while (n < maxf) {
        const char *sep = strchr(p, '|');
        size_t len = sep ? (size_t)(sep - p) : strlen(p);
        if (len >= MNU_FIELD_MAX) len = MNU_FIELD_MAX - 1;
        memmove(out[n], p, len);
        out[n][len] = '\0';
        n++;
        if (!sep) break;
        p = sep + 1;
    }
    return n;
}

static WmMenu *
mnu_find_by_id(const char *id)
{
    int i;
    for (i = 0; i < s_n_menus; i++)
        if (strcmp(s_menus[i].id, id) == 0)
            return &s_menus[i];
    return NULL;
}

static void
mnu_parse_model(const char *text)
{
    char line[MNU_FIELD_MAX * 8];
    char fields[8][MNU_FIELD_MAX];
    const char *p = text;
    int n;

    s_n_menus = 0;

    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len == 0) { p = nl ? nl + 1 : p + strlen(p); continue; }
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memmove(line, p, len);
        line[len] = '\0';

        n = mnu_parse_fields(line, fields, 8);

        /* MENU|menu_id|parent_id|label|ordinal */
        if (n >= 4 && strcmp(fields[0], "MENU") == 0) {
            if (s_n_menus < MNU_MAX && strcmp(fields[2], "root") == 0) {
                WmMenu *m = &s_menus[s_n_menus++];
                memset(m, 0, sizeof(*m));
                memmove(m->id,    fields[1], MNU_FIELD_MAX);
                memmove(m->label, fields[3], MNU_FIELD_MAX);
                m->ordinal = (n >= 5) ? atoi(fields[4]) : s_n_menus;
            }
        }
        /* ITEM|item_id|menu_id|label|command_id|kind */
        else if (n >= 5 && strcmp(fields[0], "ITEM") == 0) {
            WmMenu *m = mnu_find_by_id(fields[2]);
            if (m && m->n_items < MNU_ITEM_MAX) {
                WmMenuItem *it = &m->items[m->n_items++];
                memset(it, 0, sizeof(*it));
                memmove(it->id,         fields[1], MNU_FIELD_MAX);
                memmove(it->label,      fields[3], MNU_FIELD_MAX);
                memmove(it->command_id, fields[4], MNU_FIELD_MAX);
                memmove(it->kind, n >= 6 ? fields[5] : "normal", 32);
                it->enabled = 1;
            }
        }

        p = nl ? nl + 1 : p + strlen(p);
    }
}

static void
mnu_apply_state(const char *text)
{
    char line[MNU_FIELD_MAX * 3];
    char fields[4][MNU_FIELD_MAX];
    const char *p = text;
    int n, i, j;

    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len == 0) { p = nl ? nl + 1 : p + strlen(p); continue; }
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memmove(line, p, len);
        line[len] = '\0';
        n = mnu_parse_fields(line, fields, 4);
        if (n >= 3) {
            int val    = atoi(fields[2]);
            int is_chk = (strcmp(fields[0], "CHECKED") == 0);
            for (i = 0; i < s_n_menus; i++)
                for (j = 0; j < s_menus[i].n_items; j++)
                    if (strcmp(s_menus[i].items[j].command_id, fields[1]) == 0) {
                        if (is_chk) s_menus[i].items[j].checked = val;
                        else        s_menus[i].items[j].enabled = val;
                    }
        }
        p = nl ? nl + 1 : p + strlen(p);
    }
}

static char *
mnu_read_prop(Window w, Atom prop, int maxbytes)
{
    Atom ac; int fmt; unsigned long n, ba; unsigned char *d = NULL;
    char *out;

    if (XGetWindowProperty(g_wm.dpy, w, prop, 0, (maxbytes / 4) + 1,
                           False, AnyPropertyType,
                           &ac, &fmt, &n, &ba, &d) != Success || !d)
        return NULL;
    if (n == 0) { XFree(d); return NULL; }
    out = malloc(n + 1);
    if (!out) { XFree(d); return NULL; }
    memmove(out, d, n);
    out[n] = '\0';
    XFree(d);
    return out;
}

static long
mnu_read_cardinal(Window w, Atom prop)
{
    Atom ac; int fmt; unsigned long n, ba; unsigned char *d = NULL;
    long val = -1;
    if (XGetWindowProperty(g_wm.dpy, w, prop, 0, 1, False, XA_CARDINAL,
                           &ac, &fmt, &n, &ba, &d) == Success && d) {
        if (n >= 1) val = (long)*(unsigned long *)d;
        XFree(d);
    }
    return val;
}

/* ---- public: load model from focused window -------------------- */

void
menu_load_from_focused(Window w)
{
    long serial;
    char *model, *state;

    if (w == None || w == g_wm.root) {
        s_n_menus   = 0;
        s_serial    = -1;
        s_model_win = None;
        return;
    }

    serial = mnu_read_cardinal(w, g_wm.a_aux_menu_serial);
    if (serial != -1 && w == s_model_win && serial == s_serial)
        return; /* nothing changed */

    model = mnu_read_prop(w, g_wm.a_aux_menu_model, MNU_MODEL_MAX);
    if (!model) {
        s_n_menus   = 0;
        s_model_win = None;
        s_serial    = -1;
        return;
    }

    mnu_parse_model(model);
    free(model);

    state = mnu_read_prop(w, g_wm.a_aux_menu_state, MNU_STATE_MAX);
    if (state) { mnu_apply_state(state); free(state); }

    s_serial    = (serial >= 0) ? serial : 0;
    s_model_win = w;
}

/* ---- public: draw menu titles on root bar --------------------- */

void
menu_draw_titles(void)
{
    int x, i, ty;
    int h = g_wm.menubar_h;

    if (!g_wm.font || h <= 0)
        return;

    ty = h / 2 + g_wm.font->ascent -
         (g_wm.font->ascent + g_wm.font->descent) / 2;

    if (s_n_menus == 0) {
        /* Fallback: system label */
        static const char *lbl = "auxv6";
        XSetForeground(g_wm.dpy, g_wm.gc, PLT_TEXT_ACTIVE);
        XDrawString(g_wm.dpy, g_wm.root, g_wm.gc, 8, ty, lbl, 5);
        return;
    }

    x = POPUP_PAD_X;
    for (i = 0; i < s_n_menus; i++) {
        WmMenu *m = &s_menus[i];
        int tw = mnu_text_width(m->label);
        int bw = tw + POPUP_PAD_X;

        m->bar_x = x - POPUP_PAD_X / 2;
        m->bar_w = bw;

        if (g_wm.menu_popup_idx == i) {
            draw_rect(g_wm.root, PLT_SEL_BG,
                      m->bar_x, 0, m->bar_w, h - 1);
            XSetForeground(g_wm.dpy, g_wm.gc, PLT_SEL_TEXT);
        } else {
            XSetForeground(g_wm.dpy, g_wm.gc, PLT_TEXT_ACTIVE);
        }
        XDrawString(g_wm.dpy, g_wm.root, g_wm.gc,
                    x, ty, m->label, (int)strlen(m->label));
        x += bw;
    }
}

/* ---- public: full bar redraw (background + bevel + titles) ---- */

void
menu_redraw_bar(void)
{
    int h = g_wm.menubar_h;

    if (h <= 0 || h > g_wm.sh)
        return;

    draw_rect(g_wm.root, PLT_FRAME_BG, 0, 0, g_wm.sw, h);
    draw_bevel(g_wm.root, 0, 0, g_wm.sw, h);
    draw_rect(g_wm.root, PLT_BLACK, 0, h - 1, g_wm.sw, 1);
    menu_draw_titles();
    XFlush(g_wm.dpy);
}

/* ---- popup ---------------------------------------------------- */

static int
popup_item_width(WmMenu *m)
{
    int i, w = POPUP_MIN_W;
    for (i = 0; i < m->n_items; i++) {
        int tw = mnu_text_width(m->items[i].label) + POPUP_PAD_X * 3;
        if (tw > w) w = tw;
    }
    return w;
}

static void
popup_render(void)
{
    WmMenu *m;
    int i, pw, ph, item_y, ty;

    if (g_wm.menu_popup == None || g_wm.menu_popup_idx < 0 ||
        g_wm.menu_popup_idx >= s_n_menus)
        return;

    m  = &s_menus[g_wm.menu_popup_idx];
    pw = popup_item_width(m);
    ph = m->n_items * POPUP_ITEM_H + POPUP_PAD_TOP * 2;

    draw_rect(g_wm.menu_popup, PLT_FRAME_BG, 0, 0, pw, ph);
    draw_bevel(g_wm.menu_popup, 0, 0, pw, ph);

    for (i = 0; i < m->n_items; i++) {
        WmMenuItem *it = &m->items[i];
        item_y = POPUP_PAD_TOP + i * POPUP_ITEM_H;
        ty = item_y + (POPUP_ITEM_H +
                       (g_wm.font ? g_wm.font->ascent : 12)) / 2;

        if (strcmp(it->kind, "separator") == 0) {
            draw_rect(g_wm.menu_popup, PLT_FRAME_BG,
                      1, item_y, pw - 2, POPUP_ITEM_H);
            XSetForeground(g_wm.dpy, g_wm.gc, PLT_DARK_GRAY);
            XDrawLine(g_wm.dpy, g_wm.menu_popup, g_wm.gc,
                      POPUP_PAD_X, item_y + POPUP_ITEM_H / 2,
                      pw - POPUP_PAD_X, item_y + POPUP_ITEM_H / 2);
            continue;
        }

        if (g_wm.menu_popup_hover == i) {
            draw_rect(g_wm.menu_popup, PLT_SEL_BG,
                      1, item_y, pw - 2, POPUP_ITEM_H);
            XSetForeground(g_wm.dpy, g_wm.gc,
                           it->enabled ? PLT_SEL_TEXT : PLT_DARK_GRAY);
        } else {
            draw_rect(g_wm.menu_popup, PLT_FRAME_BG,
                      1, item_y, pw - 2, POPUP_ITEM_H);
            XSetForeground(g_wm.dpy, g_wm.gc,
                           it->enabled ? PLT_TEXT_ACTIVE : PLT_DARK_GRAY);
        }
        XDrawString(g_wm.dpy, g_wm.menu_popup, g_wm.gc,
                    POPUP_PAD_X, ty,
                    it->label, (int)strlen(it->label));

        if (it->checked) {
            XDrawString(g_wm.dpy, g_wm.menu_popup, g_wm.gc,
                        2, ty, "v", 1);
        }
    }

    XFlush(g_wm.dpy);
}

void
menu_popup_close(void)
{
    if (g_wm.menu_popup != None) {
        XDestroyWindow(g_wm.dpy, g_wm.menu_popup);
        g_wm.menu_popup = None;
    }
    g_wm.menu_popup_idx   = -1;
    g_wm.menu_popup_hover = -1;
    menu_redraw_bar();
}

static void
popup_open(int menu_idx)
{
    WmMenu *m;
    int pw, ph, px, py;
    XSetWindowAttributes swa;
    unsigned long vmask;

    if (menu_idx < 0 || menu_idx >= s_n_menus)
        return;
    m = &s_menus[menu_idx];
    if (m->n_items == 0)
        return;

    pw = popup_item_width(m);
    ph = m->n_items * POPUP_ITEM_H + POPUP_PAD_TOP * 2;
    px = m->bar_x;
    py = g_wm.menubar_h;
    if (px + pw > g_wm.sw) px = g_wm.sw - pw;
    if (px < 0) px = 0;

    if (g_wm.menu_popup != None)
        XDestroyWindow(g_wm.dpy, g_wm.menu_popup);

    vmask = CWOverrideRedirect | CWBackPixel | CWEventMask;
    swa.override_redirect = True;
    swa.background_pixel  = PLT_FRAME_BG;
    swa.event_mask        = ExposureMask | ButtonReleaseMask |
                            PointerMotionMask | LeaveWindowMask;

    g_wm.menu_popup = XCreateWindow(g_wm.dpy, g_wm.root,
                                    px, py,
                                    (unsigned)pw, (unsigned)ph,
                                    0, CopyFromParent, InputOutput,
                                    CopyFromParent, vmask, &swa);
    g_wm.menu_popup_idx   = menu_idx;
    g_wm.menu_popup_hover = -1;

    XMapRaised(g_wm.dpy, g_wm.menu_popup);
    popup_render();
    menu_redraw_bar(); /* re-draw bar so title shows highlight */
}

static int
popup_hit_item(int py)
{
    WmMenu *m;
    int i, item_y;

    if (g_wm.menu_popup_idx < 0 || g_wm.menu_popup_idx >= s_n_menus)
        return -1;
    m = &s_menus[g_wm.menu_popup_idx];
    for (i = 0; i < m->n_items; i++) {
        item_y = POPUP_PAD_TOP + i * POPUP_ITEM_H;
        if (py >= item_y && py < item_y + POPUP_ITEM_H)
            return i;
    }
    return -1;
}

static void
popup_dispatch(int item_idx)
{
    WmMenu *m;
    WmMenuItem *it;
    XEvent ev;

    if (!g_wm.focused || g_wm.menu_popup_idx < 0 ||
        g_wm.menu_popup_idx >= s_n_menus)
        return;

    m = &s_menus[g_wm.menu_popup_idx];
    if (item_idx < 0 || item_idx >= m->n_items)
        return;

    it = &m->items[item_idx];
    if (!it->enabled) return;
    if (strcmp(it->kind, "separator") == 0) return;
    if (it->command_id[0] == '\0' ||
        strcmp(it->command_id, "none") == 0) return;

    /* Write command text onto the active window */
    XChangeProperty(g_wm.dpy, g_wm.focused->win,
                    g_wm.a_aux_menu_command_text,
                    XA_STRING, 8, PropModeReplace,
                    (unsigned char *)it->command_id,
                    (int)strlen(it->command_id));

    /* Send _AUX_MENU_COMMAND ClientMessage — data.l[0]=1 means dispatch */
    memset(&ev, 0, sizeof(ev));
    ev.type                 = ClientMessage;
    ev.xclient.window       = g_wm.focused->win;
    ev.xclient.message_type = g_wm.a_aux_menu_command;
    ev.xclient.format       = 32;
    ev.xclient.data.l[0]    = 1;
    XSendEvent(g_wm.dpy, g_wm.focused->win, False, NoEventMask, &ev);
    XFlush(g_wm.dpy);

    dprintf(2, "6wm: menu dispatch cmd='%s' win=0x%lx\n",
            it->command_id, (unsigned long)g_wm.focused->win);
}

/* ---- public: bar press hit-test ------------------------------- */

void
menu_handle_bar_press(int x, int y)
{
    int i;
    (void)y;

    for (i = 0; i < s_n_menus; i++) {
        if (x >= s_menus[i].bar_x &&
            x <  s_menus[i].bar_x + s_menus[i].bar_w) {
            /* Toggle: click same open menu closes it */
            if (g_wm.menu_popup_idx == i)
                menu_popup_close();
            else {
                menu_popup_close();
                popup_open(i);
            }
            return;
        }
    }

    /* Click outside all menu titles: close any open popup */
    if (g_wm.menu_popup != None)
        menu_popup_close();
}

/* ---- public: popup event routing ------------------------------ */

void
menu_handle_popup_event(XEvent *ev)
{
    if (!ev || g_wm.menu_popup == None) return;

    switch (ev->type) {
    case Expose:
        if (ev->xexpose.count == 0)
            popup_render();
        break;
    case MotionNotify: {
        int hi = popup_hit_item(ev->xmotion.y);
        if (hi != g_wm.menu_popup_hover) {
            g_wm.menu_popup_hover = hi;
            popup_render();
        }
        break;
    }
    case LeaveNotify:
        if (g_wm.menu_popup_hover != -1) {
            g_wm.menu_popup_hover = -1;
            popup_render();
        }
        break;
    case ButtonRelease:
        if (ev->xbutton.button == Button1) {
            int hi = popup_hit_item(ev->xbutton.y);
            popup_dispatch(hi);
            menu_popup_close();
        }
        break;
    default:
        break;
    }
}
