# X11 Compatibility Status (x6/x11 shim)

Date: 2026-04-08

## Summary
This document records the recent x11/x6 compatibility work done for dwm and st integration, including what was implemented, what remains stubbed or partial, and likely reasons for current runtime issues (slow rendering, input failures, mapping/tag anomalies, color anomalies).

## Files Changed In This Tranche
- include/X11/Xlib.h
- user/x11.c
- user/x6.c
- ports/st-0.9.3/x-auxv6.c

Approximate diff size:
- 793 insertions
- 113 deletions

## Implemented

### 1) Server-backed window attributes/focus plumbing
Implemented in x6 protocol and shim wiring:
- GET_WINDOW_ATTR
- SET_BORDER_WIDTH
- SET_BORDER_COLOR
- SET_OVERRIDE_REDIRECT
- GET_FOCUS

x11 shim behavior now:
- XGetWindowAttributes queries x6 (instead of fake screen-sized attrs).
- XGetInputFocus queries x6.
- XSetWindowBorder and XChangeWindowAttributes forward to x6 commands.

### 2) Configure and move semantics corrections
- XConfigureWindow now preserves unspecified geometry fields and handles border width updates.
- XMoveWindow no longer forces 1x1 size; it preserves current width/height.

### 3) Passive key grabs and keyboard target routing
Added passive key grab support in x6:
- GRAB_KEY
- UNGRAB_KEY

Added shim calls:
- XGrabKey
- XUngrabKey

Key routing order in x6 now:
1. active keyboard grab
2. passive key grab match
3. explicit focused client
4. pointer-hit mapped window
5. WM/root fallback

Also changed focus handling:
- SET_FOCUS to PointerRoot/root clears explicit focused client window.

### 4) Synthetic event delivery path
Added x6 queue commands and shim support:
- QUEUE_CONFIGURE_NOTIFY
- QUEUE_CLIENT_MESSAGE
- EVENT ClientMessage parsing/dispatch

XSendEvent now actively queues supported event types:
- ConfigureNotify
- ClientMessage

### 5) Property subsystem expansion
x6 property handling:
- SET_PROPERTY now preserves full values (including spaces).
- DELETE_PROPERTY implemented.

Shim/property-backed helpers added:
- XDeleteProperty
- XSetWMNormalHints / XGetWMNormalHints
- XSetTransientForHint / XGetTransientForHint
- XSetClassHint / XGetClassHint
- XSetWMHints / XGetWMHints
- XGetWMProtocols

### 6) st-side hack reduction
In st aux backend:
- Replaced manual keycode-to-char mapping with XLookupString path.
- Fixed color index handling for st special/default indices 256..259 so defaults are not low-byte truncated.

In shim/header:
- XLookupString declaration added to include/X11/Xlib.h.
- XLookupString implementation added in user/x11.c.

### 7) Debug instrumentation cleanup
Removed temporary high-frequency debug output from x6/x11/st paths and removed temporary dwm diagnostics previously added.

## Remaining Stubbed / Partial Behavior (Known Gaps)
Primary in user/x11.c:
- XRaiseWindow: no-op
- XLowerWindow: no-op
- XGrabButton / XUngrabButton: no-op
- XAllowEvents: no-op
- XCopyArea: no-op
- Some lifecycle/util APIs remain minimal:
  - XFreeCursor
  - XFreePixmap
  - XSetLineAttributes
  - XKillClient
  - XRefreshKeyboardMapping
- XSendEvent only serializes specific event types (ConfigureNotify, ClientMessage), not generic event payloads.

## Why Runtime Issues May Still Exist

### Slow rendering
Likely still constrained by current draw architecture:
- synchronous command/reply behavior for draw ops
- no XCopyArea backing implementation
- no pixmap/composite acceleration path
- potential overdraw and high syscall volume in x6 backend

### Input still failing/intermittent
Potential causes still open:
- event-mask and passive-grab interaction edge cases
- focus transitions under map/unmap/tag operations
- ordering differences from standard X server behavior

### Mapping/tag anomalies (omnipresence, wrong draw region)
Potential causes still open:
- incomplete map/unmap/stacking semantics parity
- remaining root/window routing differences in edge paths

### Color anomalies still possible
Even after index fix, channel/pixel packing mismatches can remain if framebuffer format assumptions differ from runtime backend expectations.

## Suggested Next Work (If Continuing)
1. Implement XCopyArea with a real x6 backing path.
2. Implement button grabs and XAllowEvents semantics.
3. Implement stack ordering semantics for raise/lower and restack handling.
4. Add stricter event-mask filtering at x6 dispatch boundaries.
5. Add framebuffer format normalization/probe to guarantee color channel correctness.
6. Add a compatibility test matrix for dwm/st call patterns against shim behavior.

## Note
This document is an implementation/status record, not a claim of full X11 conformance.
