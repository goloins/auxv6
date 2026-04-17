/*
 * 6wm/focus.c — click-to-focus model and active/inactive transitions
 *
 * Implements §6.1 (Focus Model), §6.2 (Raise/Stack), §6.4 (Modal Scope).
 */

#include "6wm.h"
#include "focus.h"
#include "client.h"
#include "frame.h"
#include "stack.h"
#include "menu.h"

/* ------------------------------------------------------------------ *
 * Internal helpers                                                    *
 * ------------------------------------------------------------------ */

/*
 * Check modal scope (§6.4): if another client blocks 'c', return the
 * blocking modal window instead.
 *
 * Priority order:
 *   1) explicit owner-window scope modal targeting this window
 *   2) direct owner relationship (transient_for == c->win)
 *   3) app-wide modal in the same app family
 */
static Client *
modal_constraint(Client *c)
{
    Client *m;
    Client *owner_match = NULL;
    Client *app_match = NULL;

    if (!c) return NULL;

    for (m = g_wm.clients; m; m = m->next) {
        if (m == c)
            continue;
        if (m->role != ROLE_MODAL)
            continue;
        if (!m->frame)
            continue;

        if (m->modal_owner_scope) {
            if (m->transient_for != None && m->transient_for == c->win) {
                owner_match = m;
                break;
            }
            continue;
        }

        /* Prefer direct owner relationship over broad app-wide matching. */
        if (m->transient_for != None && m->transient_for == c->win) {
            if (!owner_match)
                owner_match = m;
            continue;
        }

        if (client_same_app(m, c) && !app_match)
            app_match = m;
    }

    if (owner_match)
        return owner_match;
    if (app_match)
        return app_match;
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

    /* Raise active app family before setting focus (§6.2) */
    stack_raise_family(target);

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

    /* Prefer modal -> utility -> document for revert target. */
    for (c = g_wm.clients; c; c = c->next) {
        if (c->layer == LAYER_MODAL && c->frame) {
            best = c;
            break;
        }
    }
    if (!best) {
        for (c = g_wm.clients; c; c = c->next) {
            if (c->layer == LAYER_UTILITY && c->frame) {
                best = c;
                break;
            }
        }
    }
    if (!best) {
        for (c = g_wm.clients; c; c = c->next) {
            if (c->layer == LAYER_DOCUMENT && c->frame) {
                best = c;
                break;
            }
        }
    }

    if (!best) {
        for (c = g_wm.clients; c; c = c->next) {
            if (c->frame) {
                best = c;
                break;
            }
        }
    }

    if (best && best->role == ROLE_MODAL && best->transient_for != None) {
        Client *owner = client_find(best->transient_for);
        if (owner && !client_same_app(best, owner))
            best = owner;
    }

    if (best) {
        Client *constraint = modal_constraint(best);
        if (constraint)
            best = constraint;
    }

    if (best && best->frame)
        focus_set(best);
    else
        focus_set(NULL);
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
