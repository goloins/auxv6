# Graphics Subsystem Implementation - Integration Guide

## Purpose

This document tracks the current state of auxv6 graphics support as it exists in the tree today, and the concrete work still required to reach a true framebuffer console. The companion architecture document in `docs/graphics-subsystem-design.md` remains the long-range design reference; this file is the implementation reality check.

## Current Implementation Snapshot (2026-04-02)

### Landed Components

**Build and boot integration:**
- `Makefile` already links `kernel/graphics/framebuffer.o`, `kernel/graphics/display.o`, `kernel/graphics/font.o`, `kernel/graphics/render.o`, and `kernel/driver/virtio_gpu.o` into `aux.kern`.
- `kernel/core/main.c` already initializes `display_init()`, `pci_init()`, and `virtio_gpu_init()` before `consoleinit()`.

**Framebuffer core:**
- `kernel/graphics/framebuffer.c` provides DMA-backed framebuffer allocation.
- Dirty-rectangle tracking, fill, blit, set-pixel, and sync-for-device hooks are implemented.
- Several helper surfaces remain stubbed, especially format conversion and userspace mapping helpers.

**Font and render path:**
- `kernel/graphics/font.c` provides a builtin 8x16 monospace font and glyph lookup.
- `kernel/graphics/render.c` provides a minimal VT surface, dirty-cell tracking, cell-to-pixel rendering, and cursor drawing.
- The current font path is ASCII-oriented and intentionally minimal.

**Display core:**
- `kernel/graphics/display.c` provides device registration, framebuffer allocation dispatch, scanout dispatch, and flush routing.
- The registry and common wrappers are usable for the current virtio-gpu path.
- Full connector, mode, master-control, and probing semantics are still incomplete.

**VirtIO-GPU:**
- `kernel/driver/virtio_gpu.c` probes the PCI device, negotiates features, creates control and cursor queues, registers an IRQ handler, and registers a display device.
- Resource creation, backing attach, scanout selection, transfer-to-host, and flush commands are implemented and used by the current console mirror path.
- The command path is still synchronous polling rather than a fully asynchronous response or fence model.

**Console integration:**
- `kernel/driver/console.c` can create a `640x400` `PIXFMT_XRGB8888` framebuffer on the primary display device.
- The active tty is mirrored into a VT surface and flushed to the display through the generic display ops.
- `/proc/gfxstats` exposes framebuffer mirror counters (`sync_calls`, `cells_changed`, `cells_rendered`, `flush_calls`, `flush_pixels`).

### What This Means Right Now

- auxv6 does have a live virtio-gpu-backed framebuffer path.
- auxv6 does not yet have a true framebuffer-native console.
- The authoritative console state is still the existing text-mode or per-tty shadow screen state in `console.c`; the framebuffer is a derived mirror of that state.
- The graphics path is currently kernel-owned and terminal-first. There is no working `/dev/fb0`, `/dev/dri/card0`, or `libu6gfx` implementation yet.

## Gap Analysis

### 1. Presentation Ownership

The normal console path still writes through the legacy CGA-style text path and then mirrors into the framebuffer. That means the framebuffer is not the source of truth, which blocks any claim that the system has a true framebuffer console.

### 2. Real Display Geometry

The current console graphics path hardcodes a `640x400` framebuffer. The virtio-gpu driver has a `GET_DISPLAY_INFO` request, but current mode sizing does not yet flow from real device-reported scanout geometry.

### 3. Honest Display Model

The display layer is sufficient for one backend and one simple scanout path, but it is not yet a truthful minimal KMS-style core. `display_probe_all()`, master arbitration, rich connector reporting, and complete mode management are not finished.

### 4. Terminal Rendering Correctness

The VT renderer is good enough for the current mirror path, but still limited:
- builtin font only
- ASCII-first glyph coverage
- no full width model
- no grapheme-cluster semantics
- no full cursor or scrollback correctness for framebuffer-first terminal semantics

### 5. Userspace ABI

Headers for DRM-like and fb-like ioctls exist, but the kernel character-device implementation and userspace graphics library do not. That work should remain after the kernel console migration, not before it.

### 6. Recovery Semantics

The legacy text path still implicitly acts as the practical fallback. A true framebuffer console needs an explicit rule: framebuffer owns normal presentation, while panic and recovery can still fall back to a narrow, deterministic text-mode output path if needed.

## Plan To Reach A True Framebuffer Console

### Phase 1: Make The VT Surface Authoritative

Primary target: move normal console presentation authority out of CGA memory and into the logical VT or cell buffer.

Work items:
- Change `console.c` so tty-visible state is committed to the VT or logical cell model first.
- Render from that model into the framebuffer on the normal path.
- Keep a minimal text-mode panic or emergency path isolated from the normal console flow.
- Preserve existing termios, signal, and tty semantics while changing only the presentation backend.

Definition of done:
- Normal shell and login output no longer require CGA writes to stay visually correct.

### Phase 2: Drive Framebuffer Size From The Display Driver

Primary target: replace the fixed `640x400` mirror surface with a device-reported scanout configuration.

Work items:
- Use virtio-gpu `GET_DISPLAY_INFO` during bring-up.
- Populate display device mode or connector state from the actual scanout report.
- Allocate the console framebuffer from the selected mode rather than a constant size.
- Keep a conservative fallback mode if discovery fails.

Definition of done:
- Console framebuffer dimensions match the active virtio-gpu scanout instead of a hardcoded size.

### Phase 3: Finish Terminal Rendering For Real Workloads

Primary target: make framebuffer terminal rendering correct enough for everyday shell, editor, and curses-style usage.

Work items:
- Improve glyph fallback and palette handling.
- Add width accounting for non-ASCII text.
- Add grapheme-aware cursor and erase boundaries.
- Tighten dirty-region coalescing, cursor redraw, and scroll behavior.
- Keep `/proc/gfxstats` useful for measuring real redraw behavior.

Definition of done:
- Full-screen terminal programs no longer depend on VGA-era assumptions.

### Phase 4: Make The Display Layer Truthful

Primary target: keep the generic display abstraction, but narrow it to semantics the kernel actually provides.

Work items:
- Finish the connector or mode state needed by the virtio-gpu backend.
- Either implement or explicitly defer `set_mode`, `wait_vsync`, fence, and master-control behavior.
- Keep virtio-gpu as the only active display backend until the abstraction is proven.

Definition of done:
- The display layer describes real kernel behavior instead of a larger aspirational API surface.

### Phase 5: Add A Minimal Userspace Graphics ABI

Primary target: expose framebuffer or display information to userspace only after the kernel-owned console path is stable.

Work items:
- Decide whether the first ABI should be `/dev/fb0` or a very small `/dev/dri/card0` surface.
- Support information query, buffer access or mapping, and explicit present or flush.
- Keep compositing, windows, and display-server policy out of scope.

Definition of done:
- Userspace can query and use the kernel graphics path without destabilizing the console migration.

### Phase 6: Re-enable Rich VT And Session Features

Primary target: only expand beyond the current single-safe-path console once framebuffer ownership is stable.

Work items:
- Revisit multi-tty or getty fan-out after the framebuffer terminal is authoritative.
- Add VT activation or switching semantics on top of the framebuffer path.
- Only then consider display master arbitration and later display-server handoff.

Definition of done:
- The framebuffer console remains stable as tty or session complexity increases.

## Recommended File Order

1. `kernel/driver/console.c`
2. `kernel/graphics/render.c`
3. `kernel/graphics/font.c`
4. `kernel/driver/virtio_gpu.c`
5. `kernel/graphics/display.c`
6. userspace ABI files after the kernel console path is stable

## Suggested Validation Points

- Boot and login text render correctly with the framebuffer path enabled.
- `cat /proc/gfxstats` shows ongoing sync and flush activity during shell use.
- Editor and curses-style workloads render correctly without depending on CGA writes.
- Virtio-gpu still provides a deterministic fallback if display-info probing fails.
- Panic or early-recovery output remains available even if the framebuffer path regresses.

## Status Summary

- **Framebuffer core:** usable for the current path, with helper stubs still present.
- **Font and render:** live and integrated, but deliberately minimal.
- **Display core:** enough for one backend and one scanout path, not yet a complete minimal KMS layer.
- **VirtIO-GPU:** good enough for the current mirror path, not yet a full userspace-facing graphics stack.
- **Userspace graphics ABI:** planned only; not implemented.

**Current state:** framebuffer support is real and in use, but the system is still in the mirror phase rather than the true framebuffer-console phase.
