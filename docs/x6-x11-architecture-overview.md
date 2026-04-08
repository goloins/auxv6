# x6/x11 Architecture Overview

## File Organization

### Source Files

| File | Location | Purpose |
|------|----------|---------|
| **x6.c** | [user/x6.c](../user/x6.c) | Core display server implementation (~2100 lines) |
| **x11.c** | [user/x11.c](../user/x11.c) | X11 protocol client library (~1100 lines) |
| **xinit.c** | [user/xinit.c](../user/xinit.c) | Session launcher; bridges `startx` → x6 + client |
| **startx.c** | [user/startx.c](../user/startx.c) | User-facing launcher alias |

### Header Files

| File | Location | Purpose |
|------|----------|---------|
| **u6gfx.h** | [include/u6gfx.h](../include/u6gfx.h) | Minimal X11-compatible API stubs |
| **X11/Xlib.h** | [include/X11/Xlib.h](../include/X11/Xlib.h) | X11 type definitions and constants |
| **X11/Xutil.h** | [include/X11/Xutil.h](../include/X11/Xutil.h) | X11 utility structures |
| **X11/Xproto.h** | [include/X11/Xproto.h](../include/X11/Xproto.h) | X11 protocol constants |

### Documentation

```
docs/
  x6-dwm-progress-2026-04-07.md            [Current status]
  x6-dwm-minimal-x11-roadmap.md            [MVP requirements]
  x6-phase2.1-mvp-wm.md                    [Event model spec]
  x6-framebuffer-takeover-path.md          [Framebuffer integration]
  graphics-integration-guide.md            [Display ownership]
```

---

## Overall Architecture

### Three-Tier Design

```
┌─────────────────────────────────────────────────────────────┐
│ Client Applications (dwm, xterm, etc.)                      │
│  - Uses x11.c / u6gfx.h API                                 │
│  - Communicates via X11 protocol calls                       │
│  - Sends CREATE, MAP, DRAW_RECT, CONFIGURE commands         │
│  - Receives EVENT notifications asynchronously              │
└───────────────────┬─────────────────────────────────────────┘
                    │ TCP loopback (port 6006)
                    │ Line-oriented text protocol
┌───────────────────▼─────────────────────────────────────────┐
│ x6 Display Server (user/x6.c)                               │
│  - Listens on 127.0.0.1:6006                                │
│  - Manages window state (128 max windows)                   │
│  - Maintains event queues per client                        │
│  - Coordinates with display ownership (/proc/server7)       │
│  - Renders to framebuffer or ANSI terminal                  │
│  - Handles input devices (keyboard, mouse)                  │
└───────────────────┬─────────────────────────────────────────┘
                    │ ioctl + /dev/fb0 (framebuffer)
                    │ /dev/input/mice (mouse)
                    │ stdin (keyboard)
┌───────────────────▼─────────────────────────────────────────┐
│ Kernel Graphics & Input Subsystems                          │
│  - Console framebuffer device (/dev/fb0)                    │
│  - Mouse input device                                       │
│  - ANSI terminal emulation (fallback)                       │
│  - /proc/server7 (display ownership negotiation)            │
└─────────────────────────────────────────────────────────────┘
```

### Initialization Flow (from startx)

```
startx
  ↓
xinit (user/xinit.c)
  ├─ Fork x6 process (-f foreground, -p 6006)
  ├─ x6 claims display via /proc/server7
  ├─ x6 initializes framebuffer backend (or ANSI fallback)
  ├─ x6 opens TCP socket on 127.0.0.1:6006
  ├─ x6 sets up input handlers (keyboard, mouse)
  ├─ xinit probes x6 readiness (HELLO x6/1 handshake)
  ├─ xinit forks client process (dwm)
  │   └─ Client links against x11.c / u6gfx.h
  │   └─ Client calls XOpenDisplay() → connects to x6
  │   └─ Client handshakes with HELLO x6/1
  │   └─ Client enters main event loop
  └─ xinit waits for both processes
```

---

## Key Data Structures

### In x6.c

#### Display State
```c
struct x6_fb_state {
  int fd;              // /dev/fb0 file descriptor
  int width;           // Framebuffer width (pixels)
  int height;          // Framebuffer height (pixels)
  int stride;          // Bytes per scanline
  int bpp;             // Bits per pixel (32 for 32-bit color)
  uint *rowbuf;        // Line buffer for batched writes
  int rowcap;          // Allocated size of rowbuf
};

static struct x6_fb_state x6_fb;           // Main framebuffer state
static uint *x6_fb_shadow;                 // Shadow framebuffer (optional)
static int x6_fb_shadow_w, x6_fb_shadow_h; // Shadow dimensions
```

**Backend selection:**
- `X6_BACKEND_AUTO` (0): Try framebuffer first, fall back to ANSI
- `X6_BACKEND_ANSI` (1): Force terminal rendering
- `X6_BACKEND_FB` (2): Force framebuffer mode

#### Window Management
```c
struct x6_window {
  int in_use;                                      // Allocated?
  uint id;                                         // Window ID (from client)
  int owner_fd;                                    // Socket descriptor of owner
  int x, y, w, h;                                  // Geometry on screen
  int mapped;                                      // Is this window visible?
  int cursor_set;                                  // Custom cursor installed?
  uint cursor;                                     // Cursor ID
  struct x6_property props[X6_MAX_PROPERTIES_PER_WINDOW];  // Property storage
  int prop_count;                                  // Number of properties
};

static struct x6_window wins[X6_MAX_WINDOWS];     // Max 128 windows
```

#### Event System
```c
struct x6_event {
  int type;        // X6_EVENT_MAP_REQUEST, X6_EVENT_BUTTON_PRESS, etc.
  uint wid;        // Target window ID
  int x, y, w, h;  // Geometry data (for configure requests)
  int keycode;     // For keyboard events
  int button;      // For mouse events (1-3)
  uint state;      // Modifier state (shift, ctrl, etc.)
};

struct x6_event_queue {
  struct x6_event events[X6_MAX_EVENTS_PER_CLIENT];  // Ring buffer (64 events)
  int head, tail;                                      // Circular pointers
};

// Event types defined
#define X6_EVENT_MAP_REQUEST        1
#define X6_EVENT_CONFIGURE_REQUEST  2
#define X6_EVENT_FOCUS_IN           3
#define X6_EVENT_FOCUS_OUT          4
#define X6_EVENT_DESTROY_NOTIFY     5
#define X6_EVENT_KEY_PRESS          6
#define X6_EVENT_BUTTON_PRESS       7
#define X6_EVENT_BUTTON_RELEASE     8
#define X6_EVENT_MOTION_NOTIFY      9
```

#### Cursor Overlay
```c
struct x6_cursor_overlay {
  int drawn;           // Currently drawn?
  int x, y;            // Position
  int w, h;            // Size
  uint saved[121];     // Saved framebuffer pixels (11×11 max)
};
```

#### Global WM and Focus State
```c
// WM (window manager) state - Phase 2.1b
static int wm_has_redirect;              // WM holds SubstructureRedirect on root?
static struct x6_event_queue *wm_event_queue;  // Separate event queue for WM
static int wm_client_fd;                 // FD of WM client

// Focus management - Phase 2.1c
static uint focus_wid;                   // Currently focused window (0 = none)
static uint keyboard_grab_owner;         // Exclusive keyboard grab owner
static int wm_has_kb_grab;               // WM holds keyboard grab?

// Input state
static int pointer_x, pointer_y;         // Cursor position
static uint pointer_state;               // Button down mask
static int pointer_grab_active;          // Pointer grab in effect?
static uint pointer_grab_window;         // Grab target window
```

#### Canvas (ANSI fallback)
```c
#define X6_CANVAS_ROWS 40
#define X6_CANVAS_COLS 120
#define X6_CELL_W      8       // Pixels per cell
#define X6_CELL_H      16

static uint canvas_pixels[X6_CANVAS_ROWS][X6_CANVAS_COLS];  // Framebuffer for ANSI mode
```

### In x11.c

#### Display Wrapper
```c
typedef struct _XDisplay {
  int fd;      // TCP socket to x6
  int screen;  // Screen number (always 0)
  Window root; // Root window (always 1)
  int width, height;  // Display dimensions
  int depth;   // Color depth (always 32)
} _XDisplay;
```

#### GC (Graphics Context) Management
```c
struct x11_gc_state {
  int in_use;
  GC id;
  unsigned long fg;  // Foreground color
};

static struct x11_gc_state g_gcs[X11_MAX_GCS];  // Max 128 GCs
```

#### Atom (Symbol) Management
```c
typedef struct {
  Atom atom;          // Numeric atom ID
  char name[64];      // Atom name string
} atom_entry;

static atom_entry g_atoms[X11_MAX_ATOMS];  // Max 256 atoms
```

#### Event Buffering
```c
static XEvent g_pending_event;    // Lookahead event buffer
static int g_has_pending_event;   // Is there a buffered event?
```

---

## Main Event Loops

### x6 Main Loop (from main() in x6.c)

**Lines 1900-2050 (approx)**

```c
int main(int argc, char **argv) {
  // Argument parsing (-f foreground, -p port, -B backend)
  // Daemonize if not -f
  // Claim display via /proc/server7
  // Initialize backend (framebuffer or ANSI)
  
  // Socket setup
  fd = socket() → bind(127.0.0.1:6006) → listen()
  
  // Signal handlers
  signal(SIGTERM/SIGINT) → on_term() → keep_running = 0
  
  // Main accept loop
  while(keep_running) {
    cfd = accept(fd)
    handle_client(cfd)  // [See below]
    close(cfd)
  }
  
  // Cleanup
  close(fd)
  x6_fb_shutdown()
  x6_release_display()
}
```

### x6 Client Handler Loop (from handle_client() in x6.c)

**Lines 1747-1890 (approx)**

```c
static void handle_client(int cfd) {
  struct x6_event_queue q;
  
  // Initialize per-client event queue
  x6_event_queue_init(&q)
  current_event_queue = &q
  x6_send_line(cfd, "X6/1 READY\n")
  
  while(keep_running) {
    // ┌─────────────────────────────────────────┐
    // │ PHASE 1: Dequeue & send pending events  │
    // └─────────────────────────────────────────┘
    while(!x6_event_queue_empty(&q)) {
      x6_event_queue_dequeue(&q, &evt)
      // Serialize event to text format
      snprintf(eventbuf, "EVENT MapRequest wid=%u\n", evt.wid)
      x6_send_line(cfd, eventbuf)
    }
    
    // ┌─────────────────────────────────────────┐
    // │ PHASE 2: Set up polling                 │
    // └─────────────────────────────────────────┘
    pfds[0] = {cfd, POLLIN}           // Client socket
    pfds[1] = {x6_mouse_fd, POLLIN}   // Mouse input (if available)
    // Note: Console input checked separately
    
    int pr = poll(pfds, nfds, 50)  // 50ms timeout
    
    // ┌─────────────────────────────────────────┐
    // │ PHASE 3: Handle input devices           │
    // └─────────────────────────────────────────┘
    if(x6_console_input_pending() > 0)
      x6_pump_console_input()         // Read stdin, queue KEY/MOTION events
    
    if(pfds[mouse_idx].revents & POLLIN)
      x6_pump_mouse()                 // Read /dev/input/mice, queue BUTTON/MOTION
    
    // ┌─────────────────────────────────────────┐
    // │ PHASE 4: Handle client command          │
    // └─────────────────────────────────────────┘
    if((pfds[0].revents & POLLIN) == 0)
      continue
    
    if(x6_recv_line(cfd, line, sizeof(line)) < 0)
      break  // Client disconnected
    
    handle_one_command(cfd, line)
    
    // Check for QUIT/DETACH to exit loop
    if(strncmp(line, "QUIT", 4) == 0 || strncmp(line, "DETACH", 6) == 0)
      break
  }
  
  // Cleanup
  if(wm_event_queue == &q) {
    wm_event_queue = NULL
    wm_has_redirect = 0
  }
  x6_cursor_hide()
  current_event_queue = NULL
}
```

**Key timing:** 50ms poll timeout ensures interactive response even when no events pending.

### Client Read Loop (from x11.c)

**Reading events (x11_read_event):**

```c
int x11_read_event(Display *display, XEvent *event) {
  char line[X6_BUF_SIZE]
  
  // Blocking loop: read lines until we parse an EVENT
  while(1) {
    if(x11_read_line(display->fd, line, sizeof(line)) < 0)
      return -1
    
    if(x11_parse_event_line(display, line, event) == 0)
      return 0  // Parsed successfully
  }
}
```

**Blocking I/O:** Client blocks reading from socket until x6 sends EVENT line.
xlib-compatible `XNextEvent()` is blocking.

---

## Command Protocol (Text-based)

### Handshake
```
Client ──→ X6: HELLO x6/1
X6 ──────→ C: OK proto=1 transport=tcp-loopback screen=0 root=1 visual=truecolor depth=32 width=1024 height=768
```

### Window Lifecycle Commands

| Command | Format | Response | Purpose |
|---------|--------|----------|---------|
| **CREATE** | `CREATE <id> <x> <y> <w> <h>` | `OK create` | Allocate window with given ID and geometry |
| **MAP** | `MAP <id>` | `OK map` or `PENDING map` | Make window visible (queued if WM redirect active) |
| **UNMAP** | `UNMAP <id>` | `OK unmap` | Hide window |
| **CONFIGURE** | `CONFIGURE <id> <x> <y> <w> <h>` | `OK configure` or `PENDING configure` | Resize/move window |
| **DESTROY** | `DESTROY <id>` | `OK destroy` | Deallocate window |

### Rendering Commands
```
DRAW_RECT <wid> <x> <y> <w> <h> <color>
  → OK draw
  
DRAW_TEXT <wid> <x> <y> <color> <len> <text>
  → OK text
```

**Performance note:** Each command is synchronous (request-response). No command pipelining.

### WM Protocol Commands (Phase 2.1b)

| Command | Format | Purpose |
|---------|--------|---------|
| **REQUEST_REDIRECT** | `REQUEST_REDIRECT <root_wid>` | WM claims SubstructureRedirect |
| **WM_MAP, WM_CONFIGURE, WM_UNMAP** | WM-specific responses | WM approves child window operations |

### Event Delivery Format

Events are sent asynchronously by x6 to clients:

```
EVENT MapRequest wid=<id>
EVENT ConfigureRequest wid=<id> geom=<x>,<y> <w>x<h>
EVENT KeyPress wid=<id> keycode=<code> state=<mask> time=<t>
EVENT ButtonPress wid=<id> button=<1-3> state=<mask> x=<px> y=<py> time=<t>
EVENT MotionNotify wid=<id> x=<px> y=<py> state=<mask> time=<t>
```

---

## Rendering Paths

### Framebuffer Backend (Performance-Critical)

**Entry point:** `x6_canvas_fill_pixels(int x, int y, int w, int h, uint pixel)`
**Lines:** ~470-550 in x6.c

```
Input: Coordinates and color value
  ↓
Clipping: Bound to framebuffer dimensions
  ↓
Row Buffer Allocation: Allocate/resize rowbuf if needed
  ↓
Color Preparation: Mask to 24-bit RGB
  ↓
Main Write Loop (per scanline):
  1. Seek to byte offset: y * stride + x * 4
  2. Write prepared pixels (lseek + write syscall)
  3. Update shadow framebuffer (optional, for cursor)
  ↓
Cursor Refresh: Hide/redraw cursor overlay
```

**Performance hotspots:**
- **Per-pixel I/O:** Seeks in `/dev/fb0` are slow; batched line writes via `rowbuf`
- **Shadow framebuffer:** Maintains in-memory copy for cursor hit-testing
- **Cursor overlay:** 11×11 pixels saved/restored (save old pixels, draw cursor, then restore)

### Text Rendering

**Entry point:** `x6_draw_text_pixels(int x, int baseline_y, uint color, const char *text, int len)`
**Lines:** ~520-600 in x6.c

```
Input: Text string, baseline Y, color
  ↓
Get font: user_font_builtin_montecarlo()
  ↓
Per-character loop:
  - Get glyph metrics (bearing, advance, height)
  - For each row in glyph bitmap:
    - For each column (bit in row):
      - If bit set, x6_fb_write_pixel() directly
```

**Font:** Built-in bitmap font (8×16 cells, 1-byte bitmap per row)

### ANSI Terminal Fallback

**Entry point:** `x6_canvas_fill_pixels()` → `x6_canvas_init()` / `x6_canvas_flush_rows()`
**Lines:** ~415-450 in x6.c

```
Canvas: 120 cols × 40 rows of uint (RGB pixels)
  ↓
Per cell, emit ANSI escape sequence:
  \033[R;1H        (position to row R, column 1)
  \033[48;2;R;G;Bm (background RGB color)
  (space char)
  ↓
Sent to stdout (dprintf(1, ...))
```

**Performance:** Terminal redraws can be slow; minimal optimization beyond row batching.

---

## Performance-Critical Operations

### 1. Event Dispatch (Per-client poll cycle)

**Timing:** 50ms poll timeout in handle_client

**Cost breakdown:**
- Poll syscall: ~1-2 µs (with 50ms timeout)
- Event dequeue & serialization: ~linear in queued events (usually <10)
- Client socket recv: ~CPU+network

**Bottleneck:** TCP round-trip latency for synchronous commands (CREATE, DRAW_RECT, etc.)
**Optimization:** Event batching helps; asynchronous rendering commands (DRAW_RECT) complete immediately.

### 2. Framebuffer Writes

**Per DRAW_RECT command (the HotPath):**

```c
// Allocate row buffer (amortized, reused across calls)
rowbuf = malloc(width * sizeof(uint))

// Per scanline:
lseek(fd, offset, SEEK_SET)    // ~1-2 µs
write(fd, rowbuf, width*4)     // ~100-500 µs (depends on FB device)
```

**Syscall cost:** Dominant. Batching line writes is critical.
**Shadow update:** Optional; adds linear memory copy but enables cursor overlay.

### 3. Input Device Polling

**Console input:** `x6_pump_console_input()` (lines ~730-800)
```c
n = read(0, buf, sizeof(buf))  // Non-blocking per terminal setup
// Parse escape sequences, queue KEY/MOTION events
```

**Mouse input:** `x6_pump_mouse()` (lines ~820-860)
```c
n = read(x6_mouse_fd, &evt, sizeof(evt))  // Blocked until mouse event
// Queue BUTTON_PRESS/RELEASE and MOTION_NOTIFY events
```

**Cursor overlay refresh (per mouse move):**
```c
x6_cursor_hide()  // Restore saved pixels
x6_move_pointer()
x6_cursor_show()  // Save pixels, draw cursor
```

### 4. Window Lookup

**`x6_pick_window_at(int px, int py)`** – O(N) linear scan (N ≤ 128)

Used for:
- Hit-testing pointer events
- Determining pointer grab target

**Optimization:** Could use spatial data structure (quadtree) if many windows.

### 5. Event Queue Management

**Ring buffer** (64 entries max per client):
```c
// Enqueue (O(1) amortized)
next_tail = (q->tail + 1) % X6_MAX_EVENTS_PER_CLIENT
if(next_tail == q->head) return -1  // Drop event if full
q->events[q->tail] = *evt
q->tail = next_tail

// Dequeue (O(1))
*evt = q->events[q->head]
q->head = (q->head + 1) % X6_MAX_EVENTS_PER_CLIENT
```

**No locking:** Single-threaded design; simple and fast.

---

## Display Ownership & Backend Selection

### /proc/server7 Interface

**Written by x6.c at startup:**
```
x6_claim_display() → write("/proc/server7", "claim\n")
x6_release_display() → write("/proc/server7", "release\n")
```

**Coordinate with kernel:** Kernel console suppresses output while x6 owns display.

### Framebuffer Backend Initialization

**Entry point:** `x6_fb_try_init()` (lines ~950-1000)

```
1. Open /dev/fb0 (create via mknod if needed)
2. ioctl(fd, FBIOGET_VSCREENINFO, &vinfo)  // Get variable screen info
3. ioctl(fd, FBIOGET_FSCREENINFO, &finfo)  // Get fixed screen info
4. Allocate row buffer and shadow framebuffer
5. Allocate shadow framebuffer (width × height × 4 bytes)
6. Return success
```

**Fallback:** If /dev/fb0 unavailable or ioctl fails, fall back to ANSI.

**Auto mode:**
```c
x6_init_backend() {
  if(x6_backend_pref == X6_BACKEND_ANSI) {
    x6_backend = X6_BACKEND_ANSI
    return 0
  }
  if(x6_fb_try_init() == 0)  // Try FB first
    return 0
  x6_backend = X6_BACKEND_ANSI  // Fall back to ANSI
  return 0
}
```

---

## Synchronization & Threading Model

**Single-threaded event-driven model:**
- One `handle_client()` call per connected client
- No concurrent clients (one-at-a-time architecture)
- No locks needed; WM event queue is per-process

**Future expansion (noted in comments):**
```c
struct x6_client {
  int fd;
  struct x6_event_queue queue;  // Per-client queue for multi-client support
};
```

---

## Summary: Key Performance-Sensitive Code Paths

| Operation | Location | Cost | Optimization |
|-----------|----------|------|--------------|
| Event dispatch | handle_client() | 50ms poll | Ring buffer, async cmd responses |
| Framebuffer write | x6_canvas_fill_pixels() | per-line lseek+write | Batches by row; row buffer |
| Text render | x6_draw_text_pixels() | per-pixel | Uses built-in bitmap font |
| Cursor refresh | x6_cursor_show/hide() | 11×11 pixel save/restore | Only 121 pixels max |
| Window lookup | x6_pick_window_at() | O(128) linear | Could use spatial index |
| Event queue | x6_event_queue_enqueue/dequeue() | O(1) | Ring buffer, no locking |
| Command parsing | handle_one_command() | string scanning | sscanf, then dispatch |

---

## Testing Infrastructure

### Test Command Injection (for MVP validation)

```c
QUEUE_EVENT <type> <wid>
INJECT_KEY <wid> <keycode> <state>
INJECT_BUTTON <wid> <button> <state> <x> <y>
INJECT_MOTION <wid> <x> <y> <state>
```

Used by `x6test.c` for protocol validation.

### Query Commands

```c
HELLO x6/1      → Handshake
PING            → Liveness check
LIST            → Enumerate visible windows
```

---

## Architecture Phases (Roadmap)

- **Phase 1 (MVP):** Window lifecycle + basic rendering (CURRENT)
- **Phase 2.1a:** Event delivery to WM
- **Phase 2.1b:** SubstructureRedirect semantics (WM redirects child MAP/CONFIGURE)
- **Phase 2.1c:** Focus management and keyboard grab
- **Phase 2.1d:** Window properties and atoms
- **Phase 2.2+:** Full X11 protocol, drawing ops, image handling

Current implementation is **Phase 2.1d** (properties/atoms added).

---

## See Also

- [Framebuffer Takeover Path](x6-framebuffer-takeover-path.md) – Display ownership flow
- [x6 + dwm Progress](x6-dwm-progress-2026-04-07.md) – Current status and blockers
- [Minimal X11 Roadmap](x6-dwm-minimal-x11-roadmap.md) – Next required features
