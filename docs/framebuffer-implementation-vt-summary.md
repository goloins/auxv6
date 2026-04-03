# Framebuffer + VT/TTY Implementation Summary for auxv6

## Purpose

This document captures a comprehensive implementation overview for introducing a framebuffer-based terminal stack in auxv6, integrating it with VT/TTY and PTY plans, and keeping a clean path toward a future display server.

The key architectural rule is: **treat this as a terminal architecture migration first, and a graphics expansion second**.

## Current Baseline in auxv6

- The tty and line-discipline baseline is already strong and must be preserved: `VMIN/VTIME`, `TCSADRAIN/TCSAFLUSH`, `SIGTTIN`/`SIGTTOU`, winsize ioctls, `SIGWINCH`, `ISTRIP`, `ECHOCTL`, and `IUTF8`-aware erase are all in play.
- Runtime tty count is still clamped to single-tty (`CONSOLE_NTTY = 1`) for stability.
- The tree now has a dedicated graphics path: `framebuffer.c`, `display.c`, `font.c`, `render.c`, and `virtio_gpu.c` are built into the kernel.
- Boot initializes the display registry and virtio-gpu before `consoleinit()`.
- The shared virtio core now has an initial modern PCI capability path in addition to the older legacy BAR0 I/O path, because QEMU's gpu device presentation did not actually provide the expected legacy interface.
- The DMA allocator now supports contiguous multi-page framebuffer backing, which removes the earlier immediate failure on any framebuffer allocation larger than one page.
- `consoleinit()` now clears inherited BIOS VGA text and replays buffered kernel output instead of treating the raw pre-console VGA contents as the initial tty state.
- Kernel debug output remains on the VGA or UART side; the framebuffer mirror is driven from the tty path instead of from `cprintf()`, which keeps display-driver bring-up separate from early boot logging.
- The framebuffer mirror is now enabled only after `kinit2()`, because pre-`kinit2()` memory was too small for reliable full-screen framebuffer allocation during lazy console mirror startup.
- Echoed input and normal tty output now share the same OPOST-aware tty emission path, so newline handling stays consistent after separating kernel `cprintf()` from tty state updates.
- Canonical erase echo and signal echo now also stay on the tty-aware path, which fixes framebuffer-visible backspace and control-character display drift.
- Normal tty ANSI mutation now updates the per-tty logical screen and cursor directly instead of routing through the shared offscreen CGA surface shim. VGA hardware remains the fallback or debug projection of tty state rather than the normal interactive write path.
- `console.c` can create a framebuffer sized from the primary display mode when discovered, choose a readable boot tty size from that geometry, mirror the active tty into a VT surface, and flush it through virtio-gpu.
- Once the framebuffer path is live, normal active-tty refresh now prefers the framebuffer path rather than continuing to treat VGA text refresh as part of every interactive flush.
- The framebuffer mirror now scales terminal cells for readability in higher-resolution modes, and boot-time winsize now follows the discovered mode closely enough that the current QEMU `1200x800` virtio-gpu scanout is largely filled by the initial console grid instead of showing a smaller centered region.
- The terminal stack now has two builtin 8x16 bitmap fonts: the original classic asset and a new original Monaco-inspired variant named `Montecarlo`.
- `Montecarlo` is now the default terminal and framebuffer-console font; the original `builtin-8x16` asset remains available as a classic fallback.
- Larger on-screen text still comes from cell scaling rather than from multiple point sizes, and Chicago-style UI typography remains deferred until much later display-server work.
- `/proc/gfxstats` now provides visibility into counters plus mode, framebuffer, tty, VT, and viewport state for the current mirror path.
- A critical mirror regression has been fixed: the framebuffer is no longer cleared on every sync, only on initial allocation or VT resize. That restored stable text and significantly reduced redraw overhead.
- The important limitation is still architectural: normal tty mutation is now owned by the per-tty logical screen and cursor rather than the old shared CGA shim, but the framebuffer still remains a mirror of tty state instead of the primary presentation model.

## Current Manually Validated Milestone

- Manual QEMU bring-up now shows a readable, stable virtio-gpu framebuffer console through boot, login, and interactive shell use.
- Display discovery, modern virtio-pci transport, multi-page DMA backing, delayed post-`kinit2()` mirror activation, display-derived geometry, readable cell scaling, the tty-local logical-surface refactor, and the per-sync clear regression fix now work together as one coherent path.
- Canonical erase display and control-character echo now stay aligned with the framebuffer-visible console state, and active interactive refresh is now framebuffer-first once the graphics path is live.
- The framebuffer console is visibly faster after the sync-clear fix because stable pixels are preserved between dirty updates instead of being erased every frame.
- The latest flush batching plus dirty-region present path (2026-04-03) produced an immediate, high-confidence manual responsiveness improvement in normal shell interaction, making this one of the strongest performance wins so far in the mirror phase.

## What Must Stay Stable During The Migration

- Existing tty and termios behavior.
- Job-control and foreground-process-group behavior.
- Signal routing tied to terminal access.
- Kernel panic and emergency output availability.
- The single-tty fallback until framebuffer presentation is authoritative.

## Gap To A True Framebuffer Terminal

### 1. Presentation Ownership

The normal console path no longer depends on the shared offscreen CGA shim for tty ANSI mutation, but the framebuffer is still rendered by mirroring tty logical state. The framebuffer is visible, but not yet authoritative.

### 2. Real Display Geometry

The virtio-gpu path now has initial display discovery, the current graphics console allocates its framebuffer from that geometry, and the boot tty grid now derives from display size plus scaled framebuffer cell metrics instead of a hardcoded fixed model. The remaining issue is ownership: VGA remains the fallback and viewport path, and the framebuffer VT is still not the source of truth for normal console presentation.

### 3. Rendering Correctness

The existing render path is intentionally minimal. It still needs:
- width accounting beyond byte-oriented text assumptions
- grapheme-aware editing boundaries
- stronger cursor and scroll behavior for terminal workloads
- better glyph fallback beyond the current ASCII-first path
- tighter dirty-region coalescing and render-side change detection under ANSI-heavy workloads

### 4. VT And Session Layering

Re-enabling richer tty fan-out or VT switching before the framebuffer path owns presentation would make the migration harder to reason about. The ordering needs to stay strict.

### 5. Explicit Recovery Rules

The system still benefits from the legacy path as a safety net. That fallback should remain explicit and narrow rather than accidentally staying on the hot path forever.

### 6. Manual Bring-Up Validation

For this subsystem, the relevant guest-side evidence may appear on the graphical framebuffer console rather than on serial. QEMU bring-up validation should therefore remain manual and framebuffer-observed until the new transport path is confirmed stable.

## Architectural Objectives

1. Keep POSIX-like tty semantics stable while replacing the presentation backend.
2. Make the framebuffer path authoritative before increasing tty or VT complexity.
3. Drive console geometry from the display driver rather than from fixed constants.
4. Preserve a narrow, reliable fallback path for panic and recovery.
5. Re-enable richer VT or session behavior only after framebuffer correctness is stable.
6. Expose a userspace graphics ABI only after the kernel-owned framebuffer console is trustworthy.

## True-Framebuffer Execution Order

### 1) Make The Logical VT State Authoritative

- Commit console-visible state to the VT or logical cell model first.
- Render from that state into the framebuffer on the normal path.
- Leave text-mode writes only for panic or emergency fallback.

### 2) Drive Geometry From The Active Display

- Use the discovered virtio-gpu display information to choose framebuffer dimensions.
- Size the console VT grid from the real display mode instead of the current fixed 80x25 surface.
- Keep a deterministic fallback mode if display discovery fails.

### 3) Strengthen Terminal Rendering Correctness

- Tighten dirty-cell to dirty-rect behavior.
- Add width and grapheme-aware cursor or erase rules.
- Improve glyph fallback and palette correctness.
- Validate shell, editor, and curses-style usage on the framebuffer path.

### 4) Keep Recovery And Debug Paths Explicit

- Preserve a known-good text fallback for panic and bring-up.
- Keep `/proc/gfxstats` and related instrumentation honest.
- Treat recovery output as a separate path, not as the normal renderer.

### 5) Re-enable Richer VT Behavior After The Console Is Stable

- Only revisit multi-tty fan-out once framebuffer presentation is authoritative.
- Then layer VT switching, tty binding rules, and later session or display-server handoff semantics.

### 6) Add Userspace Graphics ABI Last

- Introduce `/dev/fb0` or a minimal `/dev/dri/card0` only after the kernel framebuffer console is stable.
- Keep window management, compositing, and general display-server policy out of scope during the migration.

## Recommended Integration Order for auxv6

1. Freeze current tty semantics as the regression baseline.
2. Make the VT or cell buffer authoritative while retaining a narrow text fallback.
3. Replace the fixed console framebuffer size with display-reported geometry.
4. Stabilize width, cursor, erase, and redraw correctness on the framebuffer path.
5. Re-enable richer tty or VT behavior only after framebuffer presentation is trustworthy.
6. Add userspace graphics ABI and later display-server handoff only after the kernel console path is stable.

## Immediate Definition Of Done For “True FB”

- Normal console output is no longer visually dependent on CGA writes.
- Console framebuffer size and visible terminal grid both come from the active display path rather than local constants.
- Shell and editor workloads render correctly through the framebuffer renderer.
- Existing tty, termios, and job-control semantics remain stable.
- Panic or emergency output still has a deterministic fallback.

## Future Display-Server Scope After The Console Migration

- Kernel buffer allocation and userspace mapping.
- Basic mode or scanout query surface.
- Buffer present or flip semantics.
- Display ownership arbitration.
- Input or VT control channels usable by userspace.

## Primary Technical Risk

The main failure mode is still coupling tty semantics to rendering internals.

Mitigation: keep line discipline, signal behavior, job control, and tty state transitions independent from the pixel pipeline and from the virtio-gpu backend.

## Exit Criteria For This Undertaking

- `vi` or similar full-screen editing works reliably on the framebuffer console.
- tty, termios, and job-control behavior remain stable or improve versus the current baseline.
- Multi-tty or richer VT behavior is added only after the framebuffer console is authoritative.
- Kernel can later expose a userspace graphics ABI without reopening basic console correctness.
- Recovery path exists when the advanced display path fails.
