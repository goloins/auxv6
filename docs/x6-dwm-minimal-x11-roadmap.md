# x6 Minimal X11 Roadmap for Running dwm on auxv6

## Purpose

Define a practical, constrained path to run `dwm` on auxv6 with a new local display server (`x6`) that implements only the X11 surface needed for a comfortable dwm session.

Implementation progress snapshot for the current state is tracked in `docs/x6-dwm-progress-2026-04-07.md`.

This plan intentionally avoids "full Xorg" scope. The objective is:

- `startx /bin/dwm`-style workflow
- reliable window management and keyboard-driven interaction
- strict integration with the existing auxv6 framebuffer and input stack

## Scope and Non-Goals

### In Scope

- One local display server process (`x6`)
- One screen/root window at first
- Local client transport only
- Sufficient X11 core + minimal extensions for dwm
- Session launcher equivalent to `startx` behavior

### Out of Scope (Initial Milestones)

- Remote X11 over TCP
- GLX/OpenGL acceleration
- Full RandR/XInput2/Composite stack
- Full DRM/KMS parity with Linux
- Desktop environment services beyond WM/session launch

## Current auxv6 Context (Why This Is Feasible)

auxv6 already has key building blocks:

- virtio-gpu framebuffer path is live and manually validated
- display ownership control patterns exist (`/proc/server7`, claim/release)
- graphics observability exists (`/proc/gfxstats`)
- kernel render and display layers exist (`kernel/graphics/*`)

Remaining gap is userspace-facing graphics/input ABI plus X11 protocol compatibility.

## Target User Experience

The end state should look like:

1. User logs in on tty.
2. User runs `startx /bin/dwm`.
3. `xinit`-style launcher starts `x6`.
4. `x6` claims graphics ownership, initializes root window, accepts local clients.
5. launcher executes `/bin/dwm` as first client.
6. dwm manages child application windows through `x6`.
7. On dwm exit, session shuts down cleanly and tty is restored.

## Minimum X11 Surface Required for dwm

The list below is the compatibility baseline for unmodified or near-unmodified dwm.

### 1) Core Protocol and Resources

- connection setup/handshake
- resource IDs (window, pixmap, GC, atom)
- drawable model and basic visuals (TrueColor root visual)
- error replies and sequence handling

### 2) Window Lifecycle Operations

- `CreateWindow`, `DestroyWindow`
- `MapWindow`, `UnmapWindow`
- `ConfigureWindow`
- `ReparentWindow`
- `RaiseWindow`, `LowerWindow`
- `QueryTree`, `GetGeometry`, `TranslateCoordinates`
- `ChangeWindowAttributes` (event masks, border/background basics)

### 3) Event Model (Critical for WM Behavior)

- event mask subscription and filtering
- delivery for:
  - `MapRequest`
  - `ConfigureRequest`
  - `UnmapNotify`
  - `DestroyNotify`
  - `ConfigureNotify`
  - `PropertyNotify`
  - `EnterNotify`
  - `FocusIn`
  - `KeyPress`
  - `ButtonPress`
  - `MotionNotify`
- synthetic `ConfigureNotify` behavior close to ICCCM expectations

### 4) WM Ownership and Redirection

- root `SubstructureRedirect` and `SubstructureNotify`
- single-WM ownership semantics (second WM must fail cleanly)

### 5) Input and Focus Control

- key/button event delivery
- `GrabKey`/`UngrabKey`
- `GrabButton`/`UngrabButton`
- pointer grab for move/resize operations
- `SetInputFocus` and revert semantics

### 6) Atoms and Properties

- `InternAtom`, `GetAtomName`
- `ChangeProperty`, `GetProperty`, `DeleteProperty`
- property typing/format handling (8/16/32-bit formats)

### 7) ICCCM Essentials

- `WM_PROTOCOLS`
- `WM_DELETE_WINDOW`
- `WM_STATE`
- `WM_NORMAL_HINTS`
- `WM_HINTS`
- transient-for support (`WM_TRANSIENT_FOR`)

### 8) EWMH Subset for "Comfortable" Operation

Not all EWMH is required for dwm core, but these improve interoperability with common clients/tools:

- `_NET_SUPPORTED`
- `_NET_ACTIVE_WINDOW`
- `_NET_CLIENT_LIST`
- `_NET_WM_NAME`
- `_NET_WM_STATE`

### 9) Drawing Path for dwm Bar

Two viable tracks:

- Track A (faster): patch dwm build to avoid Xft dependency and use simpler core text path.
- Track B (more compatibility): implement enough `Render` extension for Xft-backed text path.

### 10) Multi-Monitor Info

- minimal `Xinerama` query extension if multi-head dwm behavior is desired.

## Kernel/Graphics Roadmap Prerequisites

If missing, these must be implemented before or during x6 bring-up.

### A) Userspace Framebuffer ABI

- device node (`/dev/fb0` or minimal `/dev/dri/card0` equivalent)
- query mode: width, height, stride, format, buffer size
- userspace mapping of framebuffer memory
- explicit present/flush operation

### B) Userspace Input ABI

- keyboard and pointer event queue
- event timestamp, keycode/button, motion, modifier state
- clear ownership of key-repeat policy (kernel or x6)

### C) Graphics Ownership and Recovery

- single active graphics owner arbitration
- reclaim on owner exit/crash
- deterministic fallback back to tty path

### D) Session/VT Integration

- handoff semantics between tty console and x6 owner
- clean teardown path restoring text console state

## x6 Server Architecture (Minimal, Maintainable)

Organize x6 in layers to contain complexity:

1. Backend layer
   - framebuffer map/query/flush
   - input event ingestion
2. Protocol core
   - client connections
   - request decode/dispatch
   - resource database
3. WM semantics layer
   - redirection, configure negotiation, focus, grabs
4. Extension layer
   - start with none, then Xinerama, then Render if needed
5. Session integration layer
   - readiness handshake for launcher
   - clean shutdown and ownership release

## Phased Execution Plan

## Phase 0: ABI Hardening

Deliverables:

- userspace framebuffer query/map/present API
- userspace input queue API
- owner arbitration and crash-safe release

Gate:

- a small userspace test app draws and receives keyboard/mouse events reliably

## Phase 1: x6 Protocol Skeleton

Deliverables:

- local socket transport
- setup handshake and resource allocation
- root window creation and basic event loop
- basic create/map/unmap/configure window path

Gate:

- trivial xlib client creates a window and receives expose/input events

## Phase 2: WM-Critical Semantics for dwm

Deliverables:

- `SubstructureRedirect` ownership path
- map/configure request negotiation
- grabs, focus transitions, enter/focus events
- atom/property core

Gate:

- dwm starts and can tile/focus/move basic test windows

## Phase 3: ICCCM + EWMH Comfort Layer

Deliverables:

- ICCCM essentials implemented
- small EWMH subset exported
- robust property notifications and client list tracking

Gate:

- terminal + launcher + common test clients run without major WM protocol issues

## Phase 4: Bar/Text Compatibility

Track A (recommended first):

- use non-Xft dwm path or minimal patchset

Track B:

- implement minimal `Render` extension for Xft text path

Gate:

- dwm bar text renders correctly at usable performance

## Phase 5: startx-Compatible Session Launcher

Deliverables:

- `xinit`-like userspace helper:
  - start x6
  - wait for x6 ready signal
  - exec requested client (`/bin/dwm`)
  - session cleanup on either side exit
- `startx` wrapper script

Gate:

- `startx /bin/dwm` works repeatedly across restart/crash cycles

## Protocol Risks to Test Early

Most likely failure mode is incorrect ordering/semantics for focus, grabs, and WM events.

Create focused conformance tests for:

- `MapRequest` to map flow under `SubstructureRedirect`
- `ConfigureRequest` negotiation and synthetic `ConfigureNotify`
- focus transitions on map/unmap/destroy
- passive key grabs across modifier combinations
- property change notification ordering

## Suggested Milestone Checklist

- [ ] framebuffer userspace ABI (map/query/flush) is stable
- [ ] input userspace ABI is stable
- [ ] x6 accepts local clients and creates root window
- [ ] core map/configure/unmap event flow works
- [ ] dwm can claim WM ownership
- [ ] dwm keybindings via grabs are reliable
- [ ] dwm manages tiled/floating windows correctly
- [ ] ICCCM close/focus hints work with common clients
- [ ] bar text path is stable (Track A or Track B)
- [ ] `startx /bin/dwm` lifecycle is reliable

## Recommended Initial Implementation Strategy

To reduce risk and time-to-first-desktop:

1. Implement Track A first (no Xft/Render dependency).
2. Keep single-head only until core WM semantics are proven.
3. Add Xinerama and Render only after dwm session is stable.
4. Keep panic/recovery tty fallback narrow and explicit.

This gives a usable dwm environment quickly while preserving a clean path to broader X11 compatibility later.
