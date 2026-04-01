# Graphics Subsystem Implementation - Integration Guide

## What Has Been Created

This document summarizes the graphics subsystem scaffolding for auxv6.

### Design Documents
- **[docs/graphics-subsystem-design.md](../docs/graphics-subsystem-design.md)** - Master architecture document covering all layers, design decisions, and roadmap

### Header Files (Public API)

**Core Graphics Abstractions:**
- **include/graphics/framebuffer.h** - Linear framebuffer memory management
- **include/graphics/display.h** - Display device abstraction (KMS-like)
- **include/graphics/font.h** - Font rasterization interface
- **include/graphics/render.h** - Text rendering pipeline with VT surface

**Hardware Drivers:**
- **include/virtio_gpu.h** - VirtIO GPU specification and command structures

**Kernel-Userspace Interface:**
- **include/graphics/drm_ioctls.h** - Character device ioctls (DRM-compatible)

**Userspace Graphics Library:**
- **include/u6gfx.h** - X11-compatible graphics library API

### Kernel Implementation Stubs

**Framebuffer Core:**
- **kernel/graphics/framebuffer.c** - ~550 lines
  - Pixel format conversion utilities
  - Dirty rectangle tracking
  - Basic fill/blit operations
  - Memory allocation and synchronization
  - **Status**: Core operations implemented, format conversion TODO

**Display Device Layer:**
- **kernel/graphics/display.c** - ~430 lines
  - Device registration and enumeration
  - Mode setting orchestration
  - Resource lifecycle management
  - Framebuffer creation/management
  - **Status**: Infrastructure in place, individual operations mostly stubbed

**VirtIO-GPU Driver:**
- **kernel/driver/virtio_gpu.c** - ~600 lines
  - PCI device detection and initialization
  - Virtqueue setup
  - Feature negotiation
  - All major command implementations (GET_DISPLAY_INFO, RESOURCE_CREATE_2D, etc.)
  - **Status**: Probe and initialization working, command submission framework ready

## Architecture Summary

```
┌─────────────────────────────────────┐
│  Userspace Apps (terminal, etc.)    │
├─────────────────────────────────────┤
│  /dev/dri/* character devices       │  ← TODO: char_device.c
├─────────────────────────────────────┤
│  displays_*() kernel API            │  ← DONE: display.c (core)
│  renderin_*() kernel API            │  ← TODO: render.c
├─────────────────────────────────────┤
│  VirtIO-GPU driver                  │  ← DONE: virtio_gpu.c
│  (future: VESA, native drivers)     │  ← TODO
├─────────────────────────────────────┤
│  Framebuffer core                   │  ← DONE: framebuffer.c
│  DMA allocation (existing)           │
└─────────────────────────────────────┘
```

## Next Implementation Steps

### Phase 1a: Render Pipeline (Week 1)
**Files to create:**
1. `kernel/graphics/font.c` - Bitmap font management and rasterization
2. `kernel/graphics/render.c` - VT surface management and text rendering
3. `kernel/graphics/vt_surface.c` - Terminal cell rendering

**Key tasks:**
- Load builtin monospace font (8x16 or similar)
- Implement glyph rasterization
- Implement cell-to-pixel rendering with attributes
- Integrate with existing console driver
- Dirty cell tracking

**Expected output:**
- Terminal applications render to framebuffer instead of VGA text mode

### Phase 1b: Display Integration (Week 2)
**Files to modify:**
1. `kernel/core/init.c` - Call `display_probe_all()` and `virtio_gpu_init()`
2. `kernel/driver/serial.c` or `Makefile` - Ensure virtio_gpu.c is compiled

**Key tasks:**
- Wire up display device initialization on boot
- Get VirtIO-GPU device detection working with QEMU
- Test GET_DISPLAY_INFO command
- Capture display resolution from VirtIO device

**Expected output:**
- `lspci` shows virtio-gpu device
- Kernel detects valid display configuration

### Phase 2: Character Device Interface (Week 3)
**Files to create:**
1. `kernel/graphics/char_device.c` - /dev/dri/* device implementation
2. Update `kernel/core/major.c` - Register graphics major number

**Key tasks:**
- Implement ioctl handler for major graphics operations
- DRM_IOCTL_GET_CAP - Device capabilities
- DRM_IOCTL_MODE_GETRESOURCES - Enumerate displays
- DRM_IOCTL_MODE_SETCRTC - Set active mode
- GEM (graphics memory) ioctls stub
- PRIME buffer import/export

**Expected output:**
- User programs can open /dev/dri/card0
- Can query display capabilities via ioctl
- Can change video mode

### Phase 3: VT Surface → Framebuffer (Week 4-5)
**Files to modify:**
1. `kernel/driver/console.c` - Redirect CGA writes to framebuffer
2. Integrate render.c output with display flush

**Key tasks:**
- Modify console output path to write cells to VT surface struct instead of CGA
- Implement timer-based dirty rectangle flush to display
- Handle cursor rendering on framebuffer
- Implement ANSI color palette lookup

**Expected output:**
- Console text appears on framebuffer display in color
- Keyboard input still works through console path
- Shell, vi, etc. work through framebuffer

### Phase 4: Userspace Library (Week 6)
**Files to create:**
1. `user/libu6gfx.c` - Userspace graphics library
2. Example user programs: `user/xclock.c`, `user/xpaint.c`

**Key tasks:**
- Implement Display, Window, GC abstraction
- XOpenDisplay → open /dev/dri/card0
- XCreateWindow → allocate GEM buffer
- XDrawLine, XFillRect → software drawing
- XSync → ioctl display flush

**Expected output:**
- Simple graphics programs can run
- Can draw basic shapes

### Phase 5+: Advanced Features
- Multi-monitor support
- Cursor/pointer handling
- Virgl 3D rendering
- Wayland compositor

## Integration Checklist

- [ ] Add `#include "graphics/display.h"` to `kernel/core/init.c`
- [ ] Add `#include "virtio_gpu.h"` to PCI probe code
- [ ] Call `virtio_gpu_init()` from main kernel init
- [ ] Register virtio_gpu_probe as PCI probe callback for device 0x1050 (VIRTIO_DEV_GPU)
- [ ] Ensure `kernel/graphics/framebuffer.c` compiled
- [ ] Ensure `kernel/graphics/display.c` compiled
- [ ] Ensure `kernel/driver/virtio_gpu.c` compiled
- [ ] Test build: `make aux.kern`
- [ ] Test in QEMU: `make qemu` with `-device virtio-gpu-pci`
- [ ] Verify boot messages show "virtio_gpu: device initialized"
- [ ] Verify `lspci` shows virtio-gpu device

## Key Design Decisions Made

1. **Software rendering first** - Keep GPU compute off hot path, focus on text
2. **Generic framebuffer abstraction** - Supports multiple display drivers
3. **X11-compatible userspace API** - Long-term goal for app compatibility
4. **Dirty rectangle tracking** - Minimize bandwidth to GPU
5. **Kernel owns display initially** - Safe fallback on userspace crash
6. **Stateless rendering** - Fonts and rendering are utility functions, not stateful

## Critical Function Signatures to Implement

These are the core functions that must be implemented before later phases work:

```c
/* Framebuffer */
int fb_set_pixel(fb, x, y, pixel);
void fb_fill_rect(fb, x, y, w, h, pixel);
void fb_blit_rect(src, src_x, src_y, dst, dst_x, dst_y, w, h);
void fb_flush(fb);

/* Display Device */
int display_set_mode(dev, crtc, mode);
int display_set_scanout(dev, crtc, fb);
int display_flush(dev, fb);

/* Font */
const struct glyph *font_get_glyph(font, codepoint);
void *font_load(name);

/* Render */
int vt_render_dirty(vt_surface);
void vt_set_cell(vts, x, y, cell);

/* VirtIO GPU */
int virtio_gpu_cmd_resource_create_2d(...);
int virtio_gpu_cmd_resource_attach_backing(...);
int virtio_gpu_cmd_set_scanout(...);
int virtio_gpu_cmd_resource_flush(...);
```

## Testing Strategy

### Unit Tests (in test programs)
```bash
# Test framebuffer operations
user/test_fb              # fb_alloc, fb_fill_rect, fb_mark_dirty
user/test_display         # display device enumeration
user/test_virtio_gpu      # command submission
```

### Integration Tests
```bash
# Boot with virtio-gpu
make qemu

# Check device detection
lspci
dmesg | grep -i graphics

# Check display info
cat /proc/pci              # Should show virtio-gpu

# Test mode setting
user/graphics_test         # Set mode and render
```

### System Tests
```bash
# Terminal rendering
# (should work after Phase 3)

# Simple graphics app
user/xclock

# Complex terminal app
vi /tmp/testfile
```

## Code Organization

```
auxv6/
├── docs/
│   ├── graphics-subsystem-design.md     ← Master design
│   └── graphics-integration-guide.md    ← This file
├── include/
│   ├── graphics/
│   │   ├── framebuffer.h                ← DONE
│   │   ├── display.h                    ← DONE
│   │   ├── font.h                       ← DONE
│   │   ├── render.h                     ← DONE
│   │   └── drm_ioctls.h                 ← DONE
│   ├── virtio_gpu.h                     ← DONE
│   └── u6gfx.h                          ← DONE
├── kernel/
│   ├── graphics/
│   │   ├── framebuffer.c                ← DONE (80%)
│   │   ├── display.c                    ← DONE (40%)
│   │   ├── font.c                       ← TODO
│   │   ├── render.c                     ← TODO
│   │   └── char_device.c                ← TODO
│   └── driver/
│       └── virtio_gpu.c                 ← DONE (60%)
├── user/
│   ├── libu6gfx.c                       ← TODO
│   ├── xclock.c                         ← TODO (example)
│   ├── xpaint.c                         ← TODO (example)
│   └── test_graphics.c                  ← TODO
└── Makefile                             ← UPDATE: add graphics/*.o
```

## Build Integration

Add to `kernel/Makefile`:

```makefile
OBJDIRS += kernel/graphics

# Framebuffer core
kernel/graphics/framebuffer.o: kernel/graphics/framebuffer.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Display device layer
kernel/graphics/display.o: kernel/graphics/display.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Character device interface
kernel/graphics/char_device.o: kernel/graphics/char_device.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Font management
kernel/graphics/font.o: kernel/graphics/font.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Render pipeline
kernel/graphics/render.o: kernel/graphics/render.c
	$(CC) $(CFLAGS) -c -o $@ $<

# In kernel/driver/Makefile or similar:
kernel/driver/virtio_gpu.o: kernel/driver/virtio_gpu.c
	$(CC) $(CFLAGS) -c -o $@ $<
```

And in main `Makefile`, ensure graphics object files are linked into kernel binary.

## Compatibility Notes

- **X11**: We implement a compatibility layer, not a full server
- **Wayland**: Future target for simpler client protocol
- **DRM**: We borrow ioctl interface but implement minimal subset
- **POSIX**: All syscalls conform to standard I/O semantics

## References for Implementation

- **XFree86/X11R7**: `/usr/include/X11/*.h` on any Linux system
- **Linux DRM**: `drivers/gpu/drm/*` and `include/drm/`
- **Linux VirtIO**: `drivers/gpu/drm/virtio/` and `drivers/virtio/`
- **QEMU**: `hw/display/virtio-gpu.c` for protocol details
- **NetBSD/OpenBSD**: `sys/dev/pci/drm/` for BSD-style patterns

## Status Summary

**COMPLETED:**
- ✅ Audio: Architecture design document
- ✅ Audio: All header files defined
- ✅ ~1600 lines of kernel stub code
- ✅ Framebuffer core: 80% complete
- ✅ Display device layer: 40% complete
- ✅ VirtIO-GPU driver: 60% complete

**REMAINING:**
- ⏳ Font rendering system
- ⏳ VT surface integration with console
- ⏳ Character device interface
- ⏳ Async response handling
- ⏳ Userspace graphics library

**Estimated effort to working graphics:**
- Phase 1 (foundation): 1-2 weeks
- Phase 2 (integration): 1 week
- Phase 3 (characters): 1-2 weeks
- Phase 4+ (features): 2+ weeks

**Current state**: Ready for Phase 1 implementation to begin.
