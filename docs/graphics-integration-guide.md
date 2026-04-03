# Graphics Subsystem Implementation - Integration Guide

## Purpose

This document tracks the current state of auxv6 graphics support as it exists in the tree today, and the concrete work still required to reach a true framebuffer console. The companion architecture document in `docs/graphics-subsystem-design.md` remains the long-range design reference; this file is the implementation reality check.

## Current Implementation Snapshot (2026-04-02)

### Landed Components

**Build and boot integration:**
- `Makefile` already links `kernel/graphics/framebuffer.o`, `kernel/graphics/display.o`, `kernel/graphics/font.o`, `kernel/graphics/render.o`, and `kernel/driver/virtio_gpu.o` into `aux.kern`.
- `kernel/core/main.c` already initializes `display_init()`, `pci_init()`, and `virtio_gpu_init()` before `consoleinit()`.
- `Makefile` now defaults QEMU graphics to `-vga none -device virtio-gpu-pci,disable-modern=on,xres=1200,yres=800`, which suppresses the default QEMU VGA window, requests a modestly larger default virtio-gpu scanout from the host, and keeps the visible graphics device aligned with the same virtio-backed framebuffer path auxv6 uses.
- Recent QEMU still exposes that gpu transport through PCI capabilities rather than a pure legacy BAR0 I/O path, so auxv6 relies on the modern virtio-pci capability support added in `kernel/driver/virtio.c`.
- `kernel/driver/virtio.c` now has an initial modern virtio-pci capability path so auxv6 can probe that gpu transport instead of failing immediately on the missing legacy BAR0 path.

**Framebuffer core:**
- `kernel/graphics/framebuffer.c` provides DMA-backed framebuffer allocation.
- The DMA allocator now supports contiguous multi-page allocations, which removes the earlier hard stop where framebuffer creation failed for any surface larger than one page.
- Dirty-rectangle tracking, fill, blit, set-pixel, and sync-for-device hooks are implemented.
- Several helper surfaces remain stubbed, especially format conversion and userspace mapping helpers.

**Font and render path:**
- `kernel/graphics/font.c` provides a builtin 8x16 monospace font and glyph lookup.
- `kernel/graphics/render.c` provides a minimal VT surface, dirty-cell tracking, cell-to-pixel rendering, and cursor drawing.
- Framebuffer cell metrics can now scale up to 2x for readability in higher-resolution modes while still using the existing builtin font.
- `kernel/graphics/font.c` now ships two builtin terminal bitmaps: the original `builtin-8x16` asset and a new original Monaco-inspired variant named `montecarlo-8x16`.
- `montecarlo-8x16` is now the default terminal and framebuffer-console font, while the original `builtin-8x16` asset remains available as a classic fallback.
- Montecarlo is intentionally terminal-only work: GUI-facing fonts such as Chicago-style UI typography remain out of scope until much later graphics or window-server work.
- Glyph drawing now uses row-span fills instead of a per-pixel foreground path, which reduces framebuffer write overhead.
- The current font path is ASCII-oriented and intentionally minimal.

**Display core:**
- `kernel/graphics/display.c` provides device registration, framebuffer allocation dispatch, scanout dispatch, and flush routing.
- The registry and common wrappers are usable for the current virtio-gpu path.
- Full connector, mode, master-control, and probing semantics are still incomplete.

**VirtIO-GPU:**
- `kernel/driver/virtio_gpu.c` probes the PCI device, negotiates features, creates control and cursor queues, registers an IRQ handler, and registers a display device.
- Initial scanout discovery now uses `GET_DISPLAY_INFO` to populate one preferred virtio-gpu mode in the display-device state.
- Resource creation, backing attach, scanout selection, transfer-to-host, and flush commands are implemented and used by the current console mirror path.
- The shared virtio core now includes initial modern PCI capability discovery plus common or notify or ISR MMIO access, which removes the earlier hard blocker where QEMU gpu probing died at `virtio: no I/O base in BAR0`.
- The command path is still synchronous polling rather than a fully asynchronous response or fence model.
- Manual QEMU validation is still pending for the new modern transport path. The authoritative runtime signal for this work should come from the graphical framebuffer console, not from an automated guest harness.

**Console integration:**
- `kernel/driver/console.c` now allocates its framebuffer from the discovered display mode when one is present, with `640x400` kept as a fallback.
- The active tty is mirrored into a VT surface sized from the current tty winsize, centered within the framebuffer, and flushed through the generic display ops.
- Boot winsize now derives directly from the discovered display mode plus scaled framebuffer cell metrics, with only the historical one-row reserve left in place. On the current QEMU `1200x800` default this lets the boot console fill the framebuffer much more closely instead of being capped to the older `100x30` baseline.
- VGA hardware remains the physical `80x25` surface, but it now tracks a viewport over the larger logical tty when the tty grid exceeds hardware text-mode dimensions.
- Console initialization now clears inherited BIOS VGA text and replays only buffered kernel output, which keeps early virtio-gpu bring-up noise from being mistaken for the initial tty state.
- Kernel debug output stays on the VGA or UART path; the framebuffer mirror is driven from the tty output path instead of from `cprintf()` itself. That avoids recursive display-driver activity during early boot logging.
- Framebuffer mirror activation is now deferred until after `kinit2()` so the first mirror allocation runs with the full post-bootstrap page pool instead of the tiny pre-`kinit2()` memory window.
- Echoed input now uses the same tty output path as normal writes, which keeps carriage-return and newline handling aligned between login prompts, shell command entry, and command output.
- Canonical erase echo and signal echo now also stay on the tty-aware path, which keeps backspace, `^C`, and `^Z` visible state aligned with the framebuffer mirror instead of only updating the legacy VGA side.
- Normal tty ANSI mutation now updates the per-tty logical screen and cursor directly instead of routing through the shared offscreen CGA surface shim. VGA hardware remains the fallback or debug projection of tty state rather than the hot path for normal interactive output.
- Once the framebuffer path is live, the active tty flush now treats the framebuffer as the primary refresh target. VGA text updates remain the pre-graphics and fallback path rather than part of every normal interactive flush.
- `/proc/gfxstats` now exposes framebuffer mirror counters plus mode, framebuffer, tty, VT, cursor, and viewport geometry.
- A major mirror regression in `console_gfx_ensure_locked()` has been fixed: the framebuffer is no longer cleared on every sync, only on initial allocation or VT resize. That restored stable text and removed the worst redraw slowdown during boot and shell use.

### What This Means Right Now

- auxv6 does have a live virtio-gpu-backed framebuffer path.
- Manual QEMU validation now shows a stable, readable virtio-gpu-backed framebuffer console through boot, login, and interactive shell use.
- auxv6 does not yet have a true framebuffer-native console.
- Normal tty mutation is now authoritative at the per-tty logical screen and cursor level in `console.c` rather than in the old shared CGA offscreen shim, but the framebuffer is still a derived mirror of tty state rather than the source of truth.
- Performance is materially better after removing the per-sync full-screen clear, but the virtio-gpu present path still uses whole-frame uploads for correctness.
- The graphics path is currently kernel-owned and terminal-first. There is no working `/dev/fb0`, `/dev/dri/card0`, or `libu6gfx` implementation yet.

## Gap Analysis

### 1. Presentation Ownership

The normal console path no longer routes tty ANSI mutation through the shared CGA-style offscreen surface, but the framebuffer is still populated by syncing from tty logical state rather than by owning presentation directly. That means the framebuffer is still not the source of truth, which blocks any claim that the system has a true framebuffer console.

### 2. Real Display Geometry

The virtio-gpu driver now records preferred scanout geometry from `GET_DISPLAY_INFO`, the framebuffer is allocated from that discovered mode, and boot tty sizing now derives from display geometry plus scaled framebuffer cell metrics. The remaining gap is ownership: the framebuffer geometry is now real, but the authoritative console path still lives in the legacy text or shadow-screen model rather than in a framebuffer-first VT.

### 3. Honest Display Model

The display layer is sufficient for one backend and one simple scanout path, but it is not yet a truthful minimal KMS-style core. `display_probe_all()`, master arbitration, rich connector reporting, and complete mode management are not finished.

### 3a. Transport Validation

The earlier transport blocker was architectural: auxv6 only implemented the legacy virtio PCI I/O register model, while QEMU's current gpu device shape exposed MMIO capabilities instead. That initial mismatch is now addressed in the shared virtio core, but the framebuffer-visible guest behavior still needs manual confirmation under `make qemu`.

### 3b. DMA Backing

Framebuffer backing now uses contiguous multi-page DMA allocation instead of the earlier single-page-only path. That was a real bring-up blocker once console framebuffer sizing started using the discovered display mode, because even the fallback framebuffer sizes were far larger than 4 KB.

### 3c. Boot Sequencing

Even after multi-page DMA allocation was fixed, lazy framebuffer mirror creation could still fail during early boot because auxv6 had not yet called `kinit2()` and therefore had only the small bootstrap allocator range available. The mirror path is now enabled only after full memory comes online.

### 4. Terminal Rendering Correctness

The VT renderer is good enough for the current mirror path, but still limited:
- builtin font only
- ASCII-first glyph coverage
- no full width model
- no grapheme-cluster semantics
- no full cursor or scrollback correctness for framebuffer-first terminal semantics

### 4a. Presentation Efficiency

The mirror is now fast enough for normal boot and shell use after removing the accidental full-frame clear on every sync, but `virtio_gpu_display_flush()` still performs whole-frame uploads for correctness. The next performance step should be to restore bounded dirty-region transfer semantics without regressing visible correctness.

### 5. Userspace ABI

Headers for DRM-like and fb-like ioctls exist, but the kernel character-device implementation and userspace graphics library do not. That work should remain after the kernel console migration, not before it.

### 6. Recovery Semantics

The legacy text path still implicitly acts as the practical fallback. A true framebuffer console needs an explicit rule: framebuffer owns normal presentation, while panic and recovery can still fall back to a narrow, deterministic text-mode output path if needed.

## Plan To Reach A True Framebuffer Console

### Phase 1: Make The VT Surface Authoritative

Primary target: move normal console presentation authority out of CGA memory and into the logical VT or cell buffer.

Work items:
- Finish changing `console.c` so tty-visible state is committed to the VT or logical cell model first. The per-tty logical screen and cursor now own normal ANSI mutation, but the framebuffer VT still rebuilds from that logical state rather than acting as the primary presentation model.
- Render from that model into the framebuffer on the normal path.
- Keep a minimal text-mode panic or emergency path isolated from the normal console flow.
- Preserve existing termios, signal, and tty semantics while changing only the presentation backend.

Definition of done:
- Normal shell and login output no longer require CGA writes to stay visually correct.

### Phase 2: Drive Framebuffer Size From The Display Driver

Primary target: replace the fixed `640x400` mirror surface with a device-reported scanout configuration.

Work items:
- Keep boot and default tty sizing derived from the selected display mode and scaled framebuffer cell metrics.
- Finish moving the normal console path from VGA-era assumptions to display-derived geometry end-to-end.
- Keep a conservative fallback mode if discovery fails.

Definition of done:
- Console framebuffer dimensions and terminal grid both match the active display path rather than local fixed constants.

### Phase 3: Finish Terminal Rendering For Real Workloads

Primary target: make framebuffer terminal rendering correct enough for everyday shell, editor, and curses-style usage.

Work items:
- Improve glyph fallback and palette handling.
- Add width accounting for non-ASCII text.
- Add grapheme-aware cursor and erase boundaries.
- Tighten dirty-region coalescing, cursor redraw, and scroll behavior.
- Replace the current whole-frame virtio-gpu upload path with correct dirty-region presentation once the readable baseline is locked in.
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

**Current state:** framebuffer support is real, readable, and manually validated on virtio-gpu. The console now consumes discovered framebuffer geometry, scales cells for readability, and exposes honest mirror diagnostics, but the system is still in the mirror phase rather than the true framebuffer-console phase.
