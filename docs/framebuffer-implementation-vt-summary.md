# Framebuffer + VT/TTY Implementation Summary for auxv6

## Purpose

This document captures a comprehensive implementation overview for introducing a framebuffer-based terminal stack in auxv6, integrating it with VT/TTY and PTY plans, and keeping a clean path toward a future display server.

The key architectural rule is: **treat this as a terminal architecture migration first, and a graphics expansion second**.

## Current Baseline in auxv6

- Console rendering is still VGA text memory at `0xB8000`, with per-tty shadow screen buffers and ANSI parser state in the console driver.
- Runtime tty count is currently clamped to single-tty (`CONSOLE_NTTY = 1`) for stability.
- Significant termios and line-discipline behavior is already implemented and should be preserved during migration:
  - `VMIN/VTIME` noncanonical reads
  - `TCSADRAIN/TCSAFLUSH` deferred apply semantics while output is busy
  - `SIGTTIN/SIGTTOU` background access behavior
  - `TIOCGWINSZ/TIOCSWINSZ` + `SIGWINCH`
  - `ISTRIP`, `ECHOCTL`, `IUTF8`-aware erase improvements
- UTF-8 decode exists, but visual correctness is limited by cell-per-byte VGA model and glyph fallback.
- There is no dedicated framebuffer/graphics driver stack yet (no VESA/GOP/virtio-gpu-like display path in current kernel drivers).

## Architectural Objectives

1. Keep POSIX-like tty semantics stable while replacing presentation backend.
2. Introduce a VT ownership model that decouples input focus, tty binding, and visible surface.
3. Add framebuffer rendering suitable for terminal workloads first.
4. Re-enable multi-tty/getty only after VT ownership and rendering semantics are stable.
5. Add PTY master/slave for multi-session correctness.
6. Provide kernel/user graphics and input interfaces that enable a future display server.

## Comprehensive Requirements

### 1) Boot and Graphics Discovery

- Add early boot graphics mode contract and metadata handoff to kernel:
  - framebuffer base physical address
  - width/height
  - stride
  - bits per pixel
  - pixel format
- Keep VGA text fallback active for panic and recovery paths until fb path is proven.

### 2) Kernel Display Core (Minimal KMS-Like)

- Introduce display abstractions:
  - display device
  - mode
  - scanout buffer
- Start with one fixed format (`XRGB8888`) and one active mode path.
- Provide deterministic mode-set and buffer-set sequencing to avoid visual races.

### 3) Framebuffer Memory Management

- Support kernel-owned scanout and optional shadow/back buffers.
- Add coherent blit/flush path with clipping and dirty-rect updates.
- Define ownership transitions:
  - boot console owns display initially
  - display server can acquire/release master ownership later
  - safe fallback to kernel console on crash/logout

### 4) VT Presentation Layer

- Introduce explicit VT objects:
  - visible text surface
  - scrollback/state
  - bound tty endpoint
- Replace direct CGA cell writes with framebuffer text rendering.
- Keep existing ANSI parser behavior but retarget final draw operations to fb renderer.
- Implement robust VT switch policy and state restore.

### 5) Text Rendering Stack

- Font atlas based renderer (bitmap font first, scalable later).
- Cell metadata per terminal cell:
  - codepoint/grapheme info
  - attributes/colors
  - dirty state
- Efficient redraw strategy:
  - per-cell dirty tracking
  - line/region coalescing

### 6) Unicode Correctness Beyond Byte-Aware UTF-8

- Add width model (`wcwidth`-like): width 0/1/2 behavior.
- Add combining mark handling and grapheme-aware cursor/edit semantics.
- Normalize malformed UTF-8 policy across input, echo, and output.
- Ensure erase/kill/edit operations operate on logical display units, not raw bytes.

### 7) Input Subsystem Refactor

- Decouple keyboard ISR from direct console mutation.
- Introduce input event queue/layer:
  - keyboard driver emits events
  - active VT consumes events by focus
  - future display server can capture events when master
- Preserve existing control character and signal behavior through tty path.

### 8) PTY and Per-TTY Isolation

- Implement `/dev/ptmx` and `/dev/pts/N` lifecycle.
- Per-tty/per-pty state isolation:
  - termios
  - winsize
  - fg process group
  - signal routing
- Ensure select/poll readiness semantics work for pty endpoints.
- Keep controlling-tty/session rules POSIX-shaped.

### 9) Userspace ABI for Graphics/VT

- Add device interface and ioctls for:
  - mode query/set
  - framebuffer info query
  - buffer map/flip (or equivalent)
  - VT query/activate
  - display master acquire/release
- Permission model should align with session/control terminal semantics.

### 10) Display Server Readiness

- Kernel should provide primitives, not policy:
  - modesetting + scanout/buffer objects
  - input event stream
  - VT arbitration
- Userspace display server should own:
  - compositing
  - window management
  - client protocol and rendering policy
- Preserve fallback text console for maintenance and recovery.

### 11) Unix-Compatibility Requirements (Must-Have)

To remain broadly Unix-like and compatible:

- PTY semantics comparable to standard Unix systems.
- Reliable job-control behavior tied to controlling tty/pty.
- Correct `TIOCGWINSZ/TIOCSWINSZ` and `SIGWINCH` propagation.
- Stable canonical/noncanonical termios behavior across backend swap.
- ANSI/VT behavior sufficient for vi/vim and curses-style apps.
- Correct `isatty/ttyname` semantics for tty and pty descriptors.

### 12) Reliability and Observability

- Panic-safe fallback output path independent from complex fb stack.
- Runtime fallback if display path fails.
- Instrumentation counters:
  - redraw/flush metrics
  - dropped input events
  - VT switch latency
  - PTY queue pressure
- Regression matrix for tty semantics, ANSI traces, editor workflows, fg/bg job control.

## Recommended Integration Order for auxv6

1. Freeze current tty semantics as regression baseline.
2. Introduce VT ownership objects while temporarily retaining existing rendering backend.
3. Land framebuffer boot discovery and minimal pixel renderer.
4. Redirect VT rendering from VGA text path to framebuffer renderer.
5. Stabilize Unicode width/grapheme behavior in editing/cursor logic.
6. Re-enable multi-tty/getty fan-out after VT/rendering stability.
7. Land PTY subsystem and per-tty isolation.
8. Add display master + userland graphics ABI for compositor/display-server bring-up.

## Minimum Graphics Scope to Start

- Linear framebuffer setup and mapping
- Single pixel format
- Software text renderer
- Dirty-rect refresh
- Basic mode/framebuffer query ioctl

## Minimum Scope for Future Display Server Enablement

- Kernel buffer allocation and userspace mapping
- Basic mode-setting API
- Buffer flip/present API
- Display ownership arbitration
- Input event channel usable by userspace server

## Primary Technical Risk

The main failure mode is coupling tty semantics to rendering internals.

Mitigation: keep line discipline, job control, and PTY semantics strictly independent from pixel pipeline decisions.

## Exit Criteria for This Undertaking

- vi/vim-like full-screen editing works reliably on framebuffer VT.
- tty/termios/job-control behavior remains stable or improved vs current baseline.
- Multi-tty and PTY sessions are isolated and standards-shaped.
- Kernel can hand display/input master control to a userspace display server and safely reclaim it.
- Recovery path exists (panic/fallback console) when advanced display path fails.
