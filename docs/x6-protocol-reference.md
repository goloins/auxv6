# x6 Protocol Reference & Command Dispatch Map

## Protocol Overview

**Type:** Line-oriented text protocol over TCP
**Transport:** Loopback TCP (127.0.0.1, default port 6006)
**Connection model:** One client per connection (sequential);  future multi-client support planned
**Encoding:** ASCII strings terminated with `\n`

### Connection Lifecycle

```
1. Client connects to x6 TCP socket
2. x6 sends: X6/1 READY\n
3. Client sends: HELLO x6/1\n
4. x6 responds: OK proto=1 ...(metadata)...\n
5. [Command-response loop]
6. Client sends: DETACH\n or QUIT\n
7. x6 responds: BYE\n and closes connection
```

---

## Command Dispatch (from handle_one_command in x6.c, lines 1161-1950)

### Window Lifecycle Commands

#### CREATE
```
Syntax:  CREATE <id> <x> <y> <w> <h>
Example: CREATE 2 100 100 800 600

Response:
  OK create          ✓ window created
  ERR exists         ✗ window ID already in use
  ERR no-slots       ✗ global window table full (128 max)

Behavior:
  - Allocates new window with given ID
  - Sets geometry (x, y at screen origin; w, h > 0)
  - Initial state: unmapped, no custom cursor
  - owner_fd tracks which client created it (for redirect logic)
```

**Code:** [x6.c:1261-1283](../user/x6.c#L1261-L1283)

#### MAP
```
Syntax:  MAP <wid>
Example: MAP 2

Response:
  OK map             ✓ window mapped immediately
  PENDING map        ✗ WM redirect active; queued for approval
  ERR not-found      ✗ window ID does not exist

Behavior:
  - If WM holds SubstructureRedirect and client !={WM, owner}:
      * Queue MapRequest event to WM
      * Return PENDING (client must wait for approval)
      * WM approves via WM_MAP command
  - Otherwise, set window.mapped=1 immediately
  
Note: "PENDING" means event queued; x6 accepts more commands from client
      WM must respond with WM_MAP to complete the operation
```

**Code:** [x6.c:1285-1307](../user/x6.c#L1285-L1307)

#### UNMAP
```
Syntax:  UNMAP <wid>
Example: UNMAP 2

Response:
  OK unmap           ✓ window hidden
  ERR not-found      ✗ window ID does not exist

Behavior:
  - Set window.mapped = 0 (immediate, no WM approval needed)
```

**Code:** [x6.c:1309-1319](../user/x6.c#L1309-L1319)

#### CONFIGURE (Client non-WM version)
```
Syntax:  CONFIGURE <wid> <x> <y> <w> <h>
Example: CONFIGURE 2 150 150 600 400

Response:
  OK configure       ✓ window resized/moved immediately
  PENDING configure  ✗ WM redirect active; queued for approval
  ERR not-found      ✗ window ID does not exist

Behavior:
  - Similar to MAP: if WM holds redirect, queue ConfigureRequest else apply immediately
  - Clamps w, h to minimum 1
  - Updates window.x, window.y, window.w, window.h
```

**Code:** [x6.c:1321-1352](../user/x6.c#L1321-L1352)

#### DESTROY
```
Syntax:  DESTROY <wid>
Example: DESTROY 2

Response:
  OK destroy         ✓ window deallocated

Behavior:
  - Immediately deallocate window (memset to zero)
  - client_addr note: No DestroyNotify event sent (MVP limitation)
```

**Code:** [x6.c:1354-1359](../user/x6.c#L1354-L1359)

---

### Rendering Commands

#### DRAW_RECT (Framebuffer Fill)
```
Syntax:  DRAW_RECT <wid> <x> <y> <w> <h> <color_rgb>
Example: DRAW_RECT 1 100 100 50 50 0xFF0000

Parameters:
  wid              Window ID (1 = root, or specific window ID)
  x, y             Offset within window coordinate space
  w, h             Rectangle dimensions (pixels)
  color_rgb        24-bit RGB color (0x00RRGGBB)

Response:
  OK draw            ✓ rectangle rendered

Behavior:
  - Size validation: 0 < w, h ≤ 4096
  - If wid != root (1), look up window and offset rendering
  - Call x6_canvas_fill_pixels(ox+x, oy+y, w, h, color)
  - Hide cursor, fill pixels, show cursor
  - Return immediately (no blocking)

Rendering backend:
  - Framebuffer: lseek + write per scanline to /dev/fb0
  - ANSI fallback: color cells to canvas buffer, flush ANSI sequences
```

**Code:** [x6.c:1361-1381](../user/x6.c#L1361-L1381)

#### DRAW_TEXT  
```
Syntax:  DRAW_TEXT <wid> <x> <y> <color_rgb> <len> <text>
Example: DRAW_TEXT 2 10 20 0xFFFFFF 5 Hello

Parameters:
  wid              Window ID
  x, y             Baseline position
  color_rgb        24-bit RGB
  len              Number of characters to render
  text             Text string (rest of line)

Response:
  OK text            ✓ text rendered

Behavior:
  - Parse: sscanf(cmd, "DRAW_TEXT %u %d %d %u %d%n", ...)
  - Extract text from after format args
  - Look up montecarlo bitmap font
  - Per character:
      * Get glyph metrics (bearing, advance)
      * Per row/column in glyph: x6_fb_write_pixel()
  - Baseline-based layout (accounting for font ascent/descent)
```

**Code:** [x6.c:1383-1411](../user/x6.c#L1383-L1411)

---

### Window Manager Protocol (Phase 2.1b)

#### REQUEST_REDIRECT
```
Syntax:  REQUEST_REDIRECT <root_wid>
Example: REQUEST_REDIRECT 1

Response:
  OK redirect_granted        ✓ WM claim granted
  ERR redirect-in-use        ✗ Another WM already holds redirect
  ERR invalid-window         ✗ wid != root window ID

Behavior:
  - Asserts that caller is the window manager
  - Sets wm_has_redirect = 1
  - Points wm_event_queue and wm_client_fd to caller
  - All subsequent child MAP/CONFIGURE requests by non-WM clients get queued
    to WM instead of being applied immediately
  - Only one WM can hold redirect at a time

WM responsibilities:
  - Receive MapRequest/ConfigureRequest events asynchronously
  - Approve/deny via WM_MAP / WM_CONFIGURE / WM_UNMAP commands
  - No hard timeout; clients remain PENDING until WM acts
```

**Code:** [x6.c:1413-1431](../user/x6.c#L1413-L1431)

#### WM_CONFIGURE (WM response)
```
Syntax:  WM_CONFIGURE <wid> <x> <y> <w> <h>
Example: WM_CONFIGURE 2 200 200 400 300

Response:
  OK configured      ✓ window state updated
  ERR not-wm         ✗ Caller does not hold redirect
  ERR not-found      ✗ Window ID does not exist

Behavior:
  - Only callable by WM (wm_client_fd)
  - Applies geometry change to window
  - Unblocks client that sent CONFIGURE (was in PENDING state)

Note: Regular CONFIGURE from non-WM clients while redirect is active
      returns PENDING; WM must respond with WM_CONFIGURE to settle it
```

**Code:** [x6.c:1433-1457](../user/x6.c#L1433-L1457)

#### WM_MAP (WM response)
```
Syntax:  WM_MAP <wid>
Example: WM_MAP 2

Response:
  OK mapped          ✓ window is now visible
  ERR not-wm         ✗ WM redirect not held by caller
  ERR not-found      ✗ Window ID does not exist

Behavior:
  - Set window.mapped = 1 (completing MapRequest approval)
```

**Code:** [x6.c:1459-1475](../user/x6.c#L1459-L1475)

#### WM_UNMAP (WM response)
```
Syntax:  WM_UNMAP <wid>
Example: WM_UNMAP 2

Response:
  OK unmapped        ✓ window is now hidden
  ERR not-wm         ✗ WM redirect not held
  ERR not-found      ✗ Window ID does not exist

Behavior:
  - Set window.mapped = 0
```

**Code:** [x6.c:1477-1493](../user/x6.c#L1477-L1493)

---

### Focus & Keyboard Control (Phase 2.1c)

#### SET_FOCUS
```
Syntax:  SET_FOCUS <wid>
Example: SET_FOCUS 2

Response:
  (implicit)         Always succeeds

Behavior:
  - Queue FocusOut event to old focus window (if any)
  - Queue FocusIn event to new focus window
  - keyboard keypresses routed to focused window
  
Note: Both WM and clients can set focus (MVP simplification)
```

**Code:** [x6.c:1627-1650](../user/x6.c#L1627-L1650)

#### QUERY_POINTER
```
Syntax:  QUERY_POINTER

Response:
  OK pointer root=1 child=<wid> x=<px> y=<py> state=<button_mask>

Behavior:
  - Return current pointer coordinates and button state
  - child=1 (root) if no window under pointer
  - child=<wid> if pointer over a mapped window
```

**Code:** [x6.c:1652-1671](../user/x6.c#L1652-L1671)

---

### Property Storage (Phase 2.1d)

#### GET_PROPERTY
```
Syntax:  GET_PROPERTY <wid> <atom_name>
Example: GET_PROPERTY 2 WM_NAME

Response:
  OK property value=<value>

Behavior:
  - Look up window property by atom name
  - Return string value (max 256 chars)
  - Returns empty string if not found
```

**Code:** [x6.c:1673-1704](../user/x6.c#L1673-L1704)

#### SET_PROPERTY
```
Syntax:  SET_PROPERTY <wid> <atom_name> <value>
Example: SET_PROPERTY 2 WM_NAME "My Window"

Response:
  OK property_set

Behavior:
  - Store key=value pair on window
  - Max 16 properties per window
  - Max 64 bytes for atom name, 256 for value
  - Update existing or add new
```

**Code:** [x6.c:1706-1738](../user/x6.c#L1706-L1738)

---

### Utility Commands

#### HELLO (Handshake)
```
Syntax:  HELLO x6/1

Response:
  OK proto=1 transport=tcp-loopback screen=0 root=1 visual=truecolor depth=32 width=<w> height=<h>

Behavior:
  - Metadata about display (screen dimensions, root window ID, color depth)
  - width, height depend on backend:
      * Framebuffer: actual FB dimensions
      * ANSI: 120*8 × 40*16 (canvas size in pixels)
  - client calls XOpenDisplay() → sends HELLO → parses this response
```

**Code:** [x6.c:1215-1224](../user/x6.c#L1215-L1224)

#### PING
```
Syntax:  PING

Response:
  PONG

Behavior:
  - Liveness check; used by xinit to verify x6 readiness
```

**Code:** [x6.c:1226-1229](../user/x6.c#L1226-L1229)

#### LIST
```
Syntax:  LIST

Response:
  WIN id=<id> map=<0|1> geom=<x>,<y> <w>x<h>
  (repeated for each window)
  OK list count=<n>

Behavior:
  - Enumerate all allocated windows
  - Useful for debugging
```

**Code:** [x6.c:1565-1585](../user/x6.c#L1565-L1585)

#### DETACH
```
Syntax:  DETACH

Response:
  BYE

Behavior:
  - Client disconnect (same as QUIT but cleaner)
  - Closes connection, but x6 keeps running
```

**Code:** [x6.c:1231-1234](../user/x6.c#L1231-L1234)

#### QUIT
```
Syntax:  QUIT

Response:
  BYE

Behavior:
  - Same as DETACH but also sets keep_running = 0
  - Causes x6 daemon to exit
  - Used by xinit to shut down x6 after client exits
```

**Code:** [x6.c:1236-1240](../user/x6.c#L1236-L1240)

---

### Test/Debug Commands

#### QUEUE_EVENT
```
Syntax:  QUEUE_EVENT <event_type> <wid>

Response:
  OK queued / ERR queue-full

Behavior:
  - Directly enqueue event for testing
  - Used by x6test.c for protocol validation
```

**Code:** [x6.c:1495-1510](../user/x6.c#L1495-L1510)

#### INJECT_KEY
```
Syntax:  INJECT_KEY <wid> <keycode> <state>

Response:
  OK key_injected / ERR queue-full

Behavior:
  - Manually queue a KEY_PRESS event
  - keycode: X11 keycode
  - state: modifier mask (shift, ctrl, alt)
```

**Code:** [x6.c:1512-1532](../user/x6.c#L1512-L1532)

#### INJECT_BUTTON
```
Syntax:  INJECT_BUTTON <wid> <button> <state> <x> <y>

Response:
  OK button_injected

Behavior:
  - Manually queue BUTTON_PRESS event
```

**Code:** [x6.c:1534-1558](../user/x6.c#L1534-L1558)

#### INJECT_MOTION
```
Syntax:  INJECT_MOTION <wid> <x> <y> <state>

Response:
  OK motion_injected

Behavior:
  - Manually queue MOTION_NOTIFY event
```

---

## Event Delivery Format

**Sent asynchronously by x6 to client (as new lines):**

### Event: MapRequest
```
EVENT MapRequest wid=<wid>
```
Sent when non-WM client maps a window while WM holds redirect.

### Event: ConfigureRequest
```
EVENT ConfigureRequest wid=<wid> geom=<x>,<y> <w>x<h>
```
Sent when non-WM client moves/resizes a window while WM holds redirect.

### Event: FocusIn / FocusOut
```
EVENT FocusIn wid=<wid>
EVENT FocusOut wid=<wid>
```
Focus transitions caused by SET_FOCUS command.

### Event: KeyPress
```
EVENT KeyPress wid=<wid> keycode=<code> state=<mask> time=<t>
```
Keyboard input routed to focused window (or WM if keyboard grab active).
- keycode: X11 keycode
- state: modifier mask
- time: monotonic timestamp (incremented per event)

### Event: ButtonPress / ButtonRelease
```
EVENT ButtonPress wid=<wid> button=<1-3> state=<mask> x=<px> y=<py> time=<t>
EVENT ButtonRelease wid=<wid> button=<1-3> state=<mask> x=<px> y=<py> time=<t>
```
Mouse button input; delivered to window under pointer (or grab target).

### Event: MotionNotify
```
EVENT MotionNotify wid=<wid> x=<px> y=<py> state=<mask> time=<t>
```
Mouse movement; delivered to window under pointer.

---

## Command Dispatch Map (by Category)

### Window Lifecycle (3 commands)
- CREATE, MAP, UNMAP, CONFIGURE, DESTROY

### Rendering (2 commands)
- DRAW_RECT, DRAW_TEXT

### WM Control (4 commands)
- REQUEST_REDIRECT, WM_CONFIGURE, WM_MAP, WM_UNMAP

### Focus (2 commands)
- SET_FOCUS, QUERY_POINTER

### Properties (2 commands)
- GET_PROPERTY, SET_PROPERTY

### Session (2 commands)
- HELLO, PING, LIST, DETACH, QUIT

### Testing (4 commands)
- QUEUE_EVENT, INJECT_KEY, INJECT_BUTTON, INJECT_MOTION

---

## State Diagram: Client Window Lifecycle

```
[Created]                  CREATE <id> x y w h
  ├─ (unmapped state)
  ├─ window.mapped = 0
  │
  ├─ MAP <id>  or  CLIENT CONFIGURES while WM redirect active
  │  ├─ [WM redirect active?]
  │  │  ├─ YES: Queue MapRequest event → client gets PENDING
  │  │  │       WM receives MapRequest event
  │  │  │       WM sends WM_MAP <id>
  │  │  │       [Mapped]
  │  │  │
  │  │  └─ NO: Immediate map
  │  │         [Mapped]
  │  │
  │  └─ [Mapped]
  │     window.mapped = 1
  │
  ├─ UNMAP <id>
  │  └─ [Unmapped]
  │     window.mapped = 0
  │
  ├─ DESTROY <id>
  │  └─ [Destroyed]
  │     memset(window, 0)
  │
  └─ CONFIGURE <id> x y w h (while mapped)
     └─ Similar to MAP: queued if WM redirect, else immediate
```

---

## Error Responses

| Error | Commands Affected | Meaning |
|-------|-------------------|---------|
| `ERR exists` | CREATE | Window ID already allocated |
| `ERR no-slots` | CREATE | Window table full (128 max) |
| `ERR not-found` | MAP, UNMAP, CONFIGURE, DESTROY, GET_PROPERTY, SET_PROPERTY, etc. | Window ID does not exist |
| `ERR no-property-space` | SET_PROPERTY | Window already has 16 properties |
| `ERR redirect-in-use` | REQUEST_REDIRECT | Another WM holds redirect |
| `ERR invalid-window` | REQUEST_REDIRECT | wid != 1 (root) |
| `ERR not-wm` | WM_* commands | Caller does not hold redirect |
| `ERR bad-syntax` | SET_PROPERTY | Parse error |
| `ERR queue-full` | QUEUE_EVENT, INJECT_* | Event queue overflow (64 max) |
| `ERR not-ready` | QUEUE_EVENT, INJECT_* | No event queue initialized |

---

## Implementation Notes

### Bidirectional Communication
- **Client → x6:** Commands (CREATE, DRAW_RECT, etc.) → Responses (OK ..., ERR ...)
- **x6 → Client:** Asynchronous events (EVENT MapRequest, etc.)

### Command Synchrony
- All commands are **request-response synchronous**
- "PENDING" status means command queued at x6; client must wait
- No pipelining; client must wait for response before sending next command

### Event Asynchrony
- Within x6 event loop (50ms poll timeout):
  1. **Dequeue events** from ring buffer → serialize and send to client
  2. **Poll input** (mouse, console keyboard)
  3. **Accept client commands** → handle and respond
  - Events sent between command responses

### No Client Multiplexing
- One client connection at a time (future: per-client queues support multi-client)
- WM and regular clients use same socket

---

## See Also

- [x6 Main Loop](x6-x11-architecture-overview.md#x6-main-loop-from-main-in-x6c)
- [Command Parsing](x6-x11-architecture-overview.md#command-protocol-text-based)
- [Event Delivery](x6-x11-architecture-overview.md#event-delivery-format)
