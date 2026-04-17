/*
 * 6wm/stack.c — layer-based window stacking (§6.2)
 *
 * Layer order: LAYER_DOCUMENT < LAYER_UTILITY < LAYER_MODAL.
 * Within a layer, windows are ordered by their position in g_wm.clients
 * (head = most recently managed/raised).
 */

#include "6wm.h"
#include "stack.h"
#include "client.h"
#include "stdlib.h"

/* Maximum number of simultaneously managed windows for stack building. */
#define STACK_MAX 256

/* ------------------------------------------------------------------ *
 * stack_restack                                                        *
 * ------------------------------------------------------------------ */

void
stack_restack(void)
{
    Window  wins[STACK_MAX];
    int     n = 0;
    Client *c;
    int     layer;

    /*
     * Build the restacking array bottom-to-top:
     *   pass 0: LAYER_DOCUMENT
     *   pass 1: LAYER_UTILITY
     *   pass 2: LAYER_MODAL
     *
     * Within each pass we walk g_wm.clients in list order (head =
     * most-recently-raised, placed last within the layer so it sits
     * on top of older peers).
     *
     * We first collect per-layer counts for a two-pass approach, but
     * a simple three-pass scan over the list is clearer and fast
     * enough given the small window count expected.
     */
    for (layer = (int)LAYER_DOCUMENT; layer <= (int)LAYER_MODAL; layer++) {
        /* Collect frames for this layer, in reverse list order so that
         * the list head ends up last (topmost within its layer). */
        int  layer_start = n;
        int  layer_n     = 0;

        /* First count */
        for (c = g_wm.clients; c; c = c->next) {
            if ((int)c->layer == layer && c->frame)
                layer_n++;
        }

        if (layer_n == 0)
            continue;

        /* Ensure we won't overflow */
        if (n + layer_n > STACK_MAX)
            break;

        /* Fill in reverse: list tail first, head last */
        {
            Client *scan;
            int     pos = layer_start + layer_n - 1;

            for (scan = g_wm.clients; scan; scan = scan->next) {
                if ((int)scan->layer == layer && scan->frame)
                    wins[pos--] = scan->frame;
            }
        }
        n += layer_n;
    }

    if (n > 0)
        XRestackWindows(g_wm.dpy, wins, n);
}

/* ------------------------------------------------------------------ *
 * stack_raise                                                          *
 * ------------------------------------------------------------------ */

void
stack_raise(Client *c)
{
    Client **pp;
    Client  *found = NULL;

    if (!c) return;

    /* Remove c from its current position */
    for (pp = &g_wm.clients; *pp; pp = &(*pp)->next) {
        if (*pp == c) {
            found   = *pp;
            *pp     = c->next;
            break;
        }
    }

    if (!found)
        return;

    /* Prepend to list (head = topmost within layer) */
    c->next      = g_wm.clients;
    g_wm.clients = c;

    /* Re-apply full stacking order */
    stack_restack();
}

void
stack_raise_family(Client *c)
{
    Client *scan;
    Client *related_util[STACK_MAX];
    Client *related_modal[STACK_MAX];
    int util_n = 0, modal_n = 0;
    int i;

    if (!c) return;

    /* First raise the target itself in its own layer. */
    stack_raise(c);

    /* Collect app-related utility and modal windows. */
    for (scan = g_wm.clients; scan; scan = scan->next) {
        if (scan == c)
            continue;
        if (!scan->frame)
            continue;
        if (!client_same_app(scan, c))
            continue;

        if (scan->layer == LAYER_UTILITY) {
            if (util_n < STACK_MAX)
                related_util[util_n++] = scan;
        } else if (scan->layer == LAYER_MODAL) {
            if (modal_n < STACK_MAX)
                related_modal[modal_n++] = scan;
        }
    }

    /* Raise same-app utility windows near top of utility layer. */
    for (i = 0; i < util_n; i++)
        stack_raise(related_util[i]);

    /* Ensure same-app modals remain top-most. */
    for (i = 0; i < modal_n; i++)
        stack_raise(related_modal[i]);
}
