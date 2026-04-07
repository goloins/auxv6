# x6 Phase 2.1: MVP Window Manager Substrate for dwm

## Objective

Enable dwm to start, claim root window, and manage basic window lifecycle (create, map, configure, focus) with minimal protocol overhead.

**Gate:** dwm launches and can:
- Claim SubstructureRedirect on root
- Create/map first test client window
- Process ConfigureRequest (move/resize)
- Set focus via SetInputFocus
- Receive window events (MapRequest, ConfigureRequest, FocusIn/Out)

## Core Requirements (Strict MVP)

### 1. Event Model Extension

x6 needs to deliver events to clients that request them. Current protocol is synchronous command-response only.

**Addition: Event stream after HELLO**

```
HELLO x6/1
-> X6/1 READY
-> OK proto=1 ...

[Client can now receive async events]

EVENT MapRequest wid=<client_wid> parent=1
EVENT ConfigureRequest wid=<wid> x=<x> y=<y> w=<w> h=<h>
EVENT FocusIn wid=<wid> new_state=<focus_mode>
```

Implementation:
- x6 maintains per-client event queue (ring buffer, ~64 events max).
- Non-blocking event delivery: client recv() on socket returns events immediately.
- Core events: MapRequest, ConfigureRequest, FocusIn, FocusOut, DestroyNotify.

### 2. SubstructureRedirect Claim Path

WM (dwm) needs to assert control over window layout on root.

**New protocol commands:**

```
# dwm (as WM) claims root SubstructureRedirect
REQUEST_REDIRECT 1          # Request SubstructureRedirect on root wid=1
-> OK redirect_granted      # Success; now WM intercepts MapRequest/Configure

# dwm denies focus/reparent and configures client
CONFIGURE <client_wid> 0 0 800 600    # Sent by WM in response to ConfigureRequest
-> OK configured

# dwm sets focus
SET_FOCUS <client_wid>
-> OK focused
```

Implementation:
- One WM can hold SubstructureRedirect on root at a time.
- When SubstructureRedirect is held, MapRequest/ConfigureRequest to client children are queued as events, not auto-accepted.
- Non-WM clients that issue ConfigureRequest get ERR permission-denied.

### 3. Window Events Under SubstructureRedirect

When WM holds SubstructureRedirect on root, child window operations queue events instead of auto-executing.

**Event triggers:**

| Client Action | Not Under Redirect | Under Redirect |
|---|---|---|
| Create window | Succeeds, no event | Window created, EVENT MapRequest sent to WM |
| Map window | Window maps immediately | EVENT MapRequest sent to WM, client waits |
| ConfigureWindow | Applied immediately | EVENT ConfigureRequest sent to WM, client waits |
| Destroy window | Window destroyed immediately | EVENT DestroyNotify sent to WM |

### 4. Focus Model (Minimal)

dwm uses keyboard grabs and focus to manage input routing.

**New commands:**

```
SET_FOCUS <wid>    # Direct focus to window wid
GRAB_KEYBOARD 0    # Request exclusive keyboard (wid=0 = root, revokes all others)
UNGRAB_KEYBOARD
```

Implementation:
- Only one WM can hold exclusive keyboard grab.
- Focus switches are communicated via FocusIn/FocusOut events.
- Initial focus defaults to root; dwm must explicitly SET_FOCUS to client.

### 5. Atom/Property Minimal Support

dwm reads/writes window properties via X protocol; x6 must store and query them.

**New commands:**

```
SET_PROPERTY <wid> <atom_name:str> <value:str>
GET_PROPERTY <wid> <atom_name:str>
-> VALUE <atom_name> <value>
-> ERROR no-property
```

**Atoms needed for dwm (minimal set):**
- `WM_NAME` — window title
- `WM_CLASS` — application class
- `WM_PROTOCOLS` — supported ICCCM protocols
- `WM_DELETE_WINDOW` — graceful close signal
- `_NET_WM_WINDOW_TYPE` — window type hint
- `_NET_WM_STATE` — window state flags

Implementation:
- Property store: per-window string key-value pairs (simple hash table, max 16 properties/window).
- dwm can query properties but initial support is read-only for system atoms.

## Implementation Plan (Phased)

### Phase 2.1a: Event Delivery Infrastructure (Est. 2-3h coding)

1. Extend handle_client() to operate on per-client event queue.
2. Add non-blocking event drain loop (recv/send alternating with event queue check).
3. Implement MapRequest/ConfigureRequest event queueing (async).
4. Test with x6test extended harness: verify events arrive at client.

### Phase 2.1b: SubstructureRedirect + WM Flow (Est. 3-4h coding)

1. Add REQUEST_REDIRECT command handler.
2. Track WM pid and root redirect state.
3. Modify CREATE/MAP/CONFIGURE handlers to check redirect state and queue events.
4. Implement CONFIGURE and SET_FOCUS as WM responses.
5. Test with manual dwm startup sketch: dwm claims redirect, dwm creates test client, verifies event flow.

### Phase 2.1c: Focus + Grab (Est. 1-2h coding)

1. Implement SET_FOCUS and GRAB_KEYBOARD commands.
2. Track focus owner and keyboard grabber.
3. Broadcast FocusIn/FocusOut events on focus change.
4. Verify focus transitions with test harness.

### Phase 2.1d: Atom/Property Store (Est. 2-3h coding)

1. Implement SET_PROPERTY and GET_PROPERTY handlers.
2. Add simple property hash map per window.
3. Pre-populate system atoms (WM_NAME, WM_CLASS, WM_PROTOCOLS, etc.).
4. Test read/write with x6test.

### Phase 2.1e: dwm Launch Test (Est. 1-2h integration)

1. Build dwm (minimal patchset, no Xft initially).
2. Start x6, run dwm as first client.
3. Verify dwm claims SubstructureRedirect.
4. Create test window via xterm/xclock.
5. Verify dwm can tile/focus/move.

## Risk Mitigation

**Protocol Fragility:**
Current text-line protocol will become brittle as we add events and state. Consider:
- Migrate to binary wire format after Phase 2.1c?
- For now, use strict newline+field delimiters; regex-based parsing.

**Event Loss:**
If client recv() is slower than event generation, buffer overflow is possible.
- Implement per-client event ring buffer (max 64 events).
- Drop oldest event if full (dwm must tolerate eventual event loss).

**Focus Semantics Mismatch:**
dwm expects ICCCM focus model; x6 MVP is simplified.
- Document deviation from ICCCM in x6 protocol spec.
- Plan ICCCM strict mode for Phase 3.

## Success Criteria

- [x] Phase 1 (skeleton) gate passed: protocol validated
- [ ] Phase 2.1a: Events deliverable to client in real-time
- [ ] Phase 2.1b: WM can claim SubstructureRedirect and receive MapRequest
- [ ] Phase 2.1c: Focus switches and keyboard grab work bidirectionally
- [ ] Phase 2.1d: Properties readable/writable without protocol errors
- [ ] Phase 2.1e: dwm launch, claim, and tile a window successfully

## Estimate

Total: **10-15 hours** of focused coding.

Checkpoint: After Phase 2.1c, we have a functional WM substrate suitable for dwm MVP.

---

**Next Step:**
Review and approve; then let's start Phase 2.1a.
