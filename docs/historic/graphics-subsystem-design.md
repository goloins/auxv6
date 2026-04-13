# Graphics Subsystem Architecture for auxv6

## Executive Summary

This document outlines a Unix-like graphics subsystem for auxv6 inspired by Linux and BSD architectures but designed from first principles for a terminal-first OS moving toward GUI support. The subsystem supports VirtIO-GPU as the primary driver but provides a generic hardware abstraction enabling future display device support.

**Status note (2026-04-02):** this document is the target architecture, not a precise implementation status log. The current tree is already past the original scaffolding stage: `framebuffer.c`, `display.c`, `font.c`, `render.c`, and `virtio_gpu.c` are built into the kernel, `main()` wires `display_init()` plus `virtio_gpu_init()`, and `console.c` can mirror the active tty into a virtio-gpu-backed framebuffer. Manual QEMU validation now shows a readable, stable framebuffer console through boot, login, and shell use. For live status and the current closure plan toward a true framebuffer console, see `docs/graphics-integration-guide.md` and `docs/framebuffer-implementation-vt-summary.md`.

**Current transport note:** the virtio core now has an initial modern virtio-pci capability path in addition to the older legacy PCI I/O path. This was necessary because current QEMU virtio-gpu devices were observed exposing MMIO capabilities without the expected legacy BAR0 I/O interface.

**Design Philosophy:**
- Kernel provides primitives, userspace provides policy
- Minimize coupling between terminal/console and graphics
- Keep display server optional (system boots without it)
- Preserve X Window System compatibility where practical
- Build incrementally: text rendering → basic 2D → client protocol

## Architecture Layers

```
┌─────────────────────────────────────────────────────┐
│ Userspace Display Applications                      │
│ (terminal emulator, X server, wayland compositor)   │
└────────────────────────────────────────────────────┤
│ Userspace Graphics API (X11/xlib compatibility)    │
├─────────────────────────────────────────────────────┤
│ Kernel Graphics Character Device Interface          │
│ (/dev/dri/*, /dev/fb*, ioctls)                    │
├─────────────────────────────────────────────────────┤
│ Framebuffer + Rendering Core                       │
│ (mode setting, buffer management, text rendering)  │
├─────────────────────────────────────────────────────┤
│ Display Device Driver Layer                         │
│ (virtio-gpu, vesa, virtio-vga)                     │
├─────────────────────────────────────────────────────┤
│ Hardware Abstraction (PCI, DMA, MMIO)              │
├─────────────────────────────────────────────────────┤
│ Platform Hardware (Framebuffer, VRAM)              │
└─────────────────────────────────────────────────────┘
```

## Design Details

### Layer 0: Hardware Abstraction

**Status:** Already exists
- PCI enumeration and BAR mapping
- DMA memory allocation (`dma_alloc`, `dma_virt_to_phys`)
- Memory mapping infrastructure
- Interrupt routing

### Layer 1: Display Device Driver Interface

**Purpose:** Abstraction over different GPU/display hardware

**Key Components:**
- `struct display_device` - represents a physical display device
- Mode negotiation (resolution, refresh rate, pixel format)
- Buffer allocation and management
- Scanout buffer presentation
- Interrupt handling

**Implementation Types:**
1. **VirtIO-GPU** (primary target)
2. **Simple Framebuffer** (fallback for VESA/GOP)
3. **Virtual Display** (for headless systems)

**File:** `include/graphics/display.h`

### Layer 2: Framebuffer Core

**Purpose:** Generic framebuffer abstraction independent of driver

**Key Components:**
- Software framebuffer management
- Double-buffering support
- Dirty rectangle tracking
- Blit and fill operations with clipping
- Scanout management

**Features:**
- Linear framebuffer support (primary)
- Tile-based rendering support (future)
- Multiple pixel format support via conversion routines
- Memory coherency guarantees via fence/flush

**File:** `include/graphics/framebuffer.h`

### Layer 3: Rendering Pipeline

**Purpose:** Terminal text rendering optimized for performance

**Components:**
1. **Font Manager**
   - Bitmap font rasterization
   - Glyph cache
   - Unicode grapheme handling

2. **Cell Renderer**
   - Per-cell dirty tracking
   - Attribute handling (colors, bold, italic, underline)
   - ANSI/VT100 rendering

3. **VT Surface**
   - Terminal-like view (position, size, content)
   - Scrollback buffer (optional)
   - Cursor management
   - Linked to framebuffer region

**Files:**
- `include/graphics/font.h`
- `include/graphics/render.h`
- VT surface declarations currently live in `include/graphics/render.h`

### Layer 4: Display Core (Kernel)

**Purpose:** Kernel-level display management

**Responsibilities:**
- Display device probing and initialization
- Mode setting orchestration
- Buffer ownership and lifetime
- Pageflipping/scanout presentation
- Panic console fallback

**Structures:**
- `struct display_device` - a physical display
- `struct display_mode` - resolution/format/refresh
- `struct display_buffer` - scanout/shadow buffer
- `struct display_surface` - rendered content (VT, graphics window)

**Key operations:**
- `display_set_mode()` - change resolution/format
- `display_get_buffer()` - allocate scanout buffer
- `display_present()` - flip to new buffer
- `display_blit()` - software rendering to buffer

**File:** `kernel/graphics/display.c`, `include/graphics/display.h`

### Layer 5: VirtIO-GPU Driver

**Purpose:** Implement VirtIO GPU device specification

**Architecture:**
- Control queue for mode setting, resource creation
- Cursor queue for pointer updates
- Scanout queue (optional, for 2D accelerated rendering future)
- Error handling and resource lifecycle

**Feature Set (Phase 1):**
- Basic 2D resource creation
- Simple framebuffer management
- Scanout configuration
- Flush/transfer operations

**Feature Set (Phase 2):**
- Virgl rendering contexts (optional)
- DMA-BUF buffer sharing
- Multi-monitor support

**File:** `kernel/driver/virtio_gpu.c`, `include/virtio_gpu.h`

### Layer 6: Display Character Device Interface

**Purpose:** Userspace API for graphics operations

**Device Nodes:**
- `/dev/dri/card0` - render device (primary GPU)
- `/dev/dri/controlD64` - control device
- `/dev/fb0` - framebuffer device (legacy)
- `/dev/input/event*` - input events

**Ioctls (initial set):**
```c
DRMIOC_GET_CAP              // Query device capabilities
DRMIOC_MODE_GETRESOURCES    // Get available modes/connectors
DRMIOC_MODE_GETCONNECTOR    // Detailed connector info
DRMIOC_MODE_GETENCODER      // Encoder info
DRMIOC_MODE_GETCRTC         // CRTC state
DRMIOC_MODE_SETCRTC         // Set active mode
DRMIOC_GEM_CREATE           // Allocate GPU memory object
DRMIOC_GEM_MMAP             // Map GEM object to userspace
DRMIOC_PRIME_HANDLE_TO_FD   // Export buffer as FD
DRMIOC_PRIME_FD_TO_HANDLE   // Import buffer from FD
```

**File:** `kernel/graphics/char_device.c`, `include/graphics/drm_ioctls.h`

### Layer 7: Userspace Graphics Library

**Purpose:** High-level graphics API (X11 compatibility target)

**Components:**
1. **Display connection** (`Display *`)
2. **Drawable abstraction** (Window, Pixmap)
3. **Graphics context** (GC with pen, fill, font)
4. **Event loop**
5. **Color management**

**API Compatibility:**
- X11-style function names where practical
- Internal implementation via `/dev/dri/` ioctls
- No direct X protocol server overhead (single-user terminal first)

**Files:**
- `user/libu6gfx.c` - library implementation
- `include/u6gfx.h` - public header

## Implementation Phases (Status Updated 2026-04-02)

### Phase 1: Foundation [mostly landed]
- [x] Abstraction layers defined
- [x] Framebuffer core compiled and usable in the kernel
- [x] Display device abstraction compiled and used by the current path
- [x] Builtin font and text rendering pipeline landed
- [ ] Remaining work is depth and correctness, not initial existence

### Phase 2: VirtIO-GPU Driver [partially landed]
- [x] PCI detection and initialization
- [x] Resource creation, attach-backing, scanout, transfer, and flush path
- [x] Boot integration with the current framebuffer mirror path
- [x] Initial mode or connector state driven from `GET_DISPLAY_INFO`
- [x] Console framebuffer allocation consumes discovered geometry
- [x] Initial modern virtio-pci transport support for QEMU gpu bring-up
- [ ] Terminal grid and tty consumers use discovered geometry end-to-end
- [ ] Asynchronous completion, fence, or richer present semantics if needed later

### Phase 3: Kernel Display Core [partially landed]
- [x] Display registry and boot wiring
- [x] Current kernel-owned framebuffer mirror path
- [ ] Truthful mode management and master control
- [ ] Character-device implementation for userspace access

### Phase 4: True Framebuffer VT [not complete]
- [ ] Make the VT or logical cell buffer authoritative
- [ ] Remove the normal-path dependency on CGA writes
- [ ] Preserve an explicit panic or recovery fallback
- [ ] Validate shell, editor, and curses-style behavior on the framebuffer path

### Phase 5: Userspace API [not started]
- [ ] Implement a minimal `/dev/fb0` or `/dev/dri/card0` surface
- [ ] Implement `libu6gfx` only after the kernel console path is stable
- [ ] Add small graphics test programs once the kernel ABI is real

### Phase 6: Advanced Features [future]
- [ ] Multi-monitor support
- [ ] Cursor or pointer handling
- [ ] Damage-tracking optimization beyond the current minimal path
- [ ] Virgl rendering contexts
- [ ] Display-server bring-up

## Key Design Decisions

### 1. Software Rendering First
Kernel handles text rendering via software blits. Virtio-GPU remains on hot path only for:
- Mode changes (rare)
- Flush operations (batched)
- Resource allocation

Benefits:
- Simpler first implementation
- Avoids GPU<->CPU sync bottleneck
- Easier debugging and development

### 2. Dirty Rectangle Tracking
Per-cell marking in terminal, coalesced into dirty rects for presentation.

```c
struct vt_surface {
    ushort *content;           // cell content
    uchar *attr;               // attributes per cell
    uchar *dirty;              // dirty flags per cell
    int dirty_top, dirty_left;  // dirty rect bounds
    int dirty_bot, dirty_right;
};
```

Benefits:
- Minimal bandwidth to GPU
- Scales to large displays
- Cooperative with Virgl future

### 3. X11-Style Device Model
Following Linux DRM architecture rather than Windows/macOS models.

- Multiple devices supported
- Per-device resource namespace
- Standard ioctl interface
- Userspace library abstraction

Rationale:
- X11 has decades of proven design
- Community knows the paradigm
- Browser engines understand X11

### 4. Kernel Display Ownership
Boot console owns display first, optionally hands to userspace display server.

Ownership transitions:
1. **Initial Boot:** Kernel console renders to `/dev/dri/card0`
2. **Login:** Display server gains master via ioctl
3. **Session Loss:** Kernel recovers display for panic/ttys
4. **Logout:** Restore kernel console rendering

Benefits:
- Fallback always available
- No trusted kernel compositor required
- Fail-safe in case of userspace crash

## Memory Management Strategy

### Framebuffer Allocation
```
Physical VRAM (from GPU):
  [Scanout Buffer 0] [Scanout Buffer 1] [VT Texture Atlas] [Cache]

Kernel-owned:
  - Scanout buffers (size = width * height * bytes-per-pixel)
  - Font/glyph cache (shared, locked in VRAM)
  - Temporary rendering surfaces (DMA-coherent)

Userspace-accessible (via mmap):
  - Framebuffer region for read-only copies
  - Shared memory regions for client-server communication
```

### DMA Coherency
- `dma_sync_for_device()` before GPU access
- `dma_sync_for_cpu()` after GPU write
- Caching modeled after Linux's DMA coherency framework

## X11 Compatibility Strategy

**Full X11 server:** Not planned (too complex)

**Compatibility layer:** Subset of X11 Xlib API
- `XOpenDisplay()` → open `/dev/dri/card0`
- `XCreateWindow()` → allocate buffer resource
- `XDrawLine()`, `XDrawPoint()` → software rendering to buffer
- `XSync()` → flush to GPU via ioctl
- `XNextEvent()` → read from `/dev/input/` or kernel event queue

**Target Applications:**
- Terminal emulator (xterm-compatible)
- Simple graphics apps (xclock, xpaint)
- No heavyweight clients (netscape, GIMP)

## Input Handling

### Subsystem Integration
Graphics driver does NOT handle input directly.

```
Keyboard/Mouse Hardware
    ↓
[Input Drivers: keyboard.c, mouse.c]
    ↓
[Input Event Queue]
    ↓
[Reader: Graphics Server or Console]
```

### Event Structure
```c
struct input_event {
    int type;      // EV_KEY, EV_REL, EV_ABS
    int code;      // KEY_ENTER, REL_X, etc.
    int value;     // keycode, displacement, position
    uint timestamp; // us since boot
};
```

### Routing
1. Kernel console: reads events for console control chars
2. Graphics server: reads events when master
3. Fallback: keyboard interrupt handler queues to console buffer

## Testing Strategy

### Unit Tests
- Dirty rectangle coalescing
- Pixel format conversion
- Font rasterization
- Buffer memory management

### Integration Tests
- VirtIO-GPU device enumeration
- Mode setting and scanout
- Text rendering to framebuffer
- Concurrent console and graphics output

### System Tests
- Boot with framebuffer console
- Transition to graphics server
- Panic recovery with graphics active
- Multiple VTs on framebuffer
- Long-running applications (vi, shells)

## Future Extensions

### Virgl Support
- Rendering contexts via virtio-gpu Virgl
- 3D graphics capability
- Separated from text path

### DMA-BUF / GEM
- Shareable graphics buffers
- Userspace driver support
- Zero-copy display operations

### Wayland Support
- Alternative to X11
- Simpler compositor model
- Planned for Phase 6+

### Hardware Drivers
- VESA/GOP fallback for UEFI systems
- AMD/Intel native drivers (complex, long term)
- ARM framebuffer drivers

## Reference Materials

**VirtIO Specification:**
- https://docs.oasis-open.org/virtio/virtio/v1.1/csd01/virtio-v1.1-csd01.html
- Section 5.7: Virtual GPU Device

**Linux DRM Subsystem:**
- `drivers/gpu/drm/` (kernel)
- `libdrm/` userspace library
- `/Documentation/gpu/` design docs

**X Window System:**
- X11R7.7 source (xorg)
- X11 Protocol Specification
- Xlib Programming Manual

**BSD Graphics:**
- NetBSD: `sys/dev/pci/drm/`
- OpenBSD: similar drm structure
- Simpler than Linux, good learning reference

## Signal Handling Map

```
Signal      Source              Handler
───────────────────────────────────────────
SIGWINCH    Mode change         Console/App
SIGTERM     Server shutdown     Graceful unmap
SIGCONT     Recovery from       Repaint screen
            graphics crash
```

## Summary

This architecture provides:
1. **Portability** - generic display/framebuffer abstraction
2. **Simplicity** - text-first, software rendering initially
3. **Safety** - kernel fallback, resource isolation
4. **Extensibility** - clear layers for GPU drivers and userspace
5. **Unix Compatibility** - X11/DRM paradigm familiar to developers

Implementation priority:
1. Framebuffer core + text rendering (foundation)
2. VirtIO-GPU driver (primary hardware)
3. Display device integration (kernel wiring)
4. VT surface adaptation (console move to framebuffer)
5. Userspace library (applications)
6. Advanced features (multi-monitor, Virgl, Wayland)
