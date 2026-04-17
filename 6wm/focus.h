/*
 * 6wm/focus.h — click-to-focus model (§6.1)
 */

#ifndef WM6_FOCUS_H
#define WM6_FOCUS_H

#include "6wm.h"

/*
 * Focus a client.
 *
 * Policy (§6.1, §6.4):
 *   - If c is NULL, revert focus to root.
 *   - Modal constraints are checked; if a modal dialog blocks c's
 *     owning app, the modal window receives focus instead.
 *   - The previous focused window is set to STATE_INACTIVE and its
 *     frame is redrawn.
 *   - c is set to STATE_ACTIVE, raised within its layer, and
 *     XSetInputFocus is called.
 *   - The menubar service is notified via menu_on_focus().
 */
void focus_set(Client *c);

/*
 * Called from focus_revert() to find and focus the best available
 * client (highest document-layer window), or revert to root if none.
 */
void focus_revert(void);

/*
 * Re-apply decorations to ensure active/inactive state matches
 * g_wm.focused.  Call after state changes that may have drifted
 * (e.g. after a restack).
 */
void focus_refresh_decorations(void);

#endif /* WM6_FOCUS_H */
