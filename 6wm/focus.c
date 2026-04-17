/*
 * 6wm/focus.c — click-to-focus model and active/inactive transitions
 *
 * Implements §6.1 (Focus Model), §6.2 (Raise/Stack), §6.4 (Modal Scope).
 */

#include "6wm.h"
#include "focus.h"
#include "frame.h"
#include "stack.h"
#include "menu.h"

/* ------------------------------------------------------------------ *
 * Internal helpers                                                    *
 * ------------------------------------------------------------------ */

/*
 * Check modal scope (§6.4): if another client blocks 'c', return the
 * blocking modal window instead.  v1 uses app-wide scope — any modal
 * window whose transient_for matches c (or shares the same app) wins.
 *
 * This is the v1 default; owner-window fallback is deferred.
 */
static Client *
modal_constraint(Client *c)
{
    Client *m;

    if (!c) return NULL;

    for (m = g_wm.clients; m; m = m->next) {
        if (m->role == ROLE_MODAL
            && m->transient_for == c->win)
            return m;
    }
    return NULL;
}

/*
 * Deactivate the currently focused client without moving focus.
 * Redraws its frame in inactive state.
 */
static void
deactivate_current(void)
{
    Client *prev = g_wm.focused;

    if (!prev) return;
    if (prev->state == STATE_INACTIVE) return;

    prev->state  = STATE_INACTIVE;
    if (prev->frame)
        frame_draw(prev);
}

/* ------------------------------------------------------------------ *
 * focus_set (§6.1)                                                    *
 * ------------------------------------------------------------------ */

void
focus_set(Client *c)
{
    Client *target;

    /* Resolve modal constraints */
    if (c) {
        target = modal_constraint(c);
        if (!target) target = c;
    } else {
        target = NULL;
    }

    /* Deactivate previous */
    deactivate_current();

    if (!target) {
        g_wm.focused = NULL;
        XSetInputFocus(g_wm.dpy, g_wm.root,
                       RevertToPointerRoot, CurrentTime);
        menu_on_focus(NULL);
        return;
    }

    g_wm.focused  = target;
    target->state = STATE_ACTIVE;

    /* Raise within layer before setting focus (§6.2) */
    stack_raise(target);

    /* Deliver input focus to client window (§6.1) */
    XSetInputFocus(g_wm.dpy, target->win,
                   RevertToPointerRoot, CurrentTime);

    /* Redraw frame in active state */
    if (target->frame)
        frame_draw(target);

    /* Notify menubar service of active app change (ui-menu-protocol.md §5.2) */
    menu_on_focus(target);
}

/* ------------------------------------------------------------------ *
 * focus_revert                                                        *
 * ------------------------------------------------------------------ */

void
focus_revert(void)
{
    Client *best = NULL;
    Client *c;

    /*
     * Find the topmost document-layer client as the revert target.
     * List is in insertion order; scanning for a LAYER_DOCUMENT client
     * gives a reasonable approximation until a proper Z-order list is
     * maintained.
     */
    for (c = g_wm.clients; c; c = c->next) {
        if (c->layer == LAYER_DOCUMENT) {
            best = c;
            break;
        }
    }

    focus_set(best);
}

/* ------------------------------------------------------------------ *
 * focus_refresh_decorations                                           *
 * ------------------------------------------------------------------ */

void
focus_refresh_decorations(void)
{
    Client *c;

    for (c = g_wm.clients; c; c = c->next) {
        WmState expected = (c == g_wm.focused)
                               ? STATE_ACTIVE
                               : STATE_INACTIVE;
        if (c->state != expected) {
            c->state = expected;
            if (c->frame)
                frame_draw(c);
        }
    }
}
