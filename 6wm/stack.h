/*
 * 6wm/stack.h — layer-based stacking (§6.2)
 */

#ifndef WM6_STACK_H
#define WM6_STACK_H

#include "6wm.h"

/*
 * Restack all managed frame windows according to their layer.
 * Layer order (bottom → top): LAYER_DOCUMENT, LAYER_UTILITY, LAYER_MODAL.
 * Within a layer, windows keep their relative order.
 * Call after any map, unmap, or layer-change event.
 */
void stack_restack(void);

/*
 * Raise 'c' to the top of its layer, then call stack_restack().
 * Used by focus_set() and on ButtonPress.
 */
void stack_raise(Client *c);

#endif /* WM6_STACK_H */
