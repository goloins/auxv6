# Framebuffer + VT/TTY Implementation Summary for auxv6

## Purpose

This document captures a comprehensive implementation overview for introducing a framebuffer-based terminal stack in auxv6, integrating it with VT/TTY and PTY plans, and keeping a clean path toward a future display server.

The key architectural rule is: **treat this as a terminal architecture migration first, and a graphics expansion second**.

## Current Baseline in auxv6

- The tty and line-discipline baseline is already strong and must be preserved: `VMIN/VTIME`, `TCSADRAIN/TCSAFLUSH`, `SIGTTIN`/`SIGTTOU`, winsize ioctls, `SIGWINCH`, `ISTRIP`, `ECHOCTL`, and `IUTF8`-aware erase are all in play.
- Runtime tty count is still clamped to single-tty (`CONSOLE_NTTY = 1`) for stability.
- The tree now has a dedicated graphics path: `framebuffer.c`, `display.c`, `font.c`, `render.c`, and `virtio_gpu.c` are built into the kernel.
- Boot initializes the display registry and virtio-gpu before `consoleinit()`.
- `console.c` can create a framebuffer on the primary display device, mirror the active tty into a VT surface, and flush it through virtio-gpu.
- `/proc/gfxstats` provides visibility into the current framebuffer mirror path.
- The important limitation is still architectural: the normal console source of truth remains the legacy text-mode or shadow-screen state, and the framebuffer remains a mirror of that state.

## What Must Stay Stable During The Migration

- Existing tty and termios behavior.
- Job-control and foreground-process-group behavior.
- Signal routing tied to terminal access.
- Kernel panic and emergency output availability.
- The single-tty fallback until framebuffer presentation is authoritative.

## Gap To A True Framebuffer Terminal

### 1. Presentation Ownership

The normal console path still flows through the legacy text renderer and then mirrors into the framebuffer. The framebuffer is visible, but not authoritative.

### 2. Real Display Geometry

The current graphics console path uses a fixed `640x400` framebuffer. A real framebuffer terminal needs its geometry to come from the active display device rather than a console-local constant.

### 3. Rendering Correctness

The existing render path is intentionally minimal. It still needs:
- width accounting beyond byte-oriented text assumptions
- grapheme-aware editing boundaries
- stronger cursor and scroll behavior for terminal workloads
- better glyph fallback beyond the current ASCII-first path

### 4. VT And Session Layering

Re-enabling richer tty fan-out or VT switching before the framebuffer path owns presentation would make the migration harder to reason about. The ordering needs to stay strict.

### 5. Explicit Recovery Rules

The system still benefits from the legacy path as a safety net. That fallback should remain explicit and narrow rather than accidentally staying on the hot path forever.

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

- Use virtio-gpu display information to choose framebuffer dimensions.
- Size the console VT grid from the real display mode instead of a fixed `640x400` surface.
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
- Console framebuffer size comes from the active display path rather than a local constant.
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
