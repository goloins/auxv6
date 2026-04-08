# Broken Keyboard Input In x6/DWM

Date: 2026-04-07

## Goal
Get keyboard shortcuts working correctly in X session (x6 + x11 shim + dwm), specifically:
- Mod (Alt/Option) + number tags
- Mod + Shift + Return to spawn st
- Mod + p for dmenu (if present)

## Initial Symptoms
- `startx` originally hung around probe/HELLO stage.
- After handshake fixes, X launched, but keyboard combos still did not trigger dwm bindings.
- Plain characters appeared instead (`p`, `1`, `2`, `3`, newline), often with delay.
- Console output showed key events with `state=0x0` (modifiers missing).

## High-Level Architecture Reviewed
- IRQ path: `trap.c` -> `kbdintr()`
- Keyboard decode: `kernel/driver/kbd.c`
- Input discipline/event emission: `kernel/driver/console.c`
- Device readiness: `kernel/core/sysfile.c`
- X server shim: `user/x6.c` (reads `/dev/kbd0` and emits X events)
- Client side: `user/x11.c`
- WM key handling: `ports/dwm-6.8/dwm.c`

## Changes Attempted (Chronological)

### 1) Main build integration for st
- Wired `_st` into root `Makefile` and `UPROGS`.
- Verified `_st` builds and artifact exists.

### 2) x11 event subscription gap
- Implemented `XSelectInput()` command emission to x6 (`SELECT_EVENTS`).
- Added x6-side `SELECT_EVENTS` handling.
- Added `XSetInputFocus()` call in st aux backend startup.

### 3) KeySym mapping fixes for dwm compatibility
- Adjusted x11 keycode->keysym behavior so control keys map to expected X keysyms.
- Added missing keysym defines needed by that mapping.

### 4) Handshake regressions and fixes
- Added probe logging in `xinit` and HELLO send logging in `x6`.
- Found probe race where `EVENT ...` lines polluted probe reply parsing.
- Fixed probe/parser robustness in `xinit` (line-oriented state machine).
- Gated async event delivery in x6 until HELLO handshake is complete.

### 5) Introduced `/dev/kbd0` event path
- Added `CONSOLE_MINOR_KBD0`.
- Added `struct aux_kbd_event { keycode, state }` ABI.
- Added keyboard ring queue in console driver.
- Added `/dev/kbd0` poll/read handling.
- Added devman node creation and policy for `/dev/kbd0`.
- Switched x6 to consume `/dev/kbd0` (with fallback behavior in some iterations).

### 6) Source-selection issue discovered
- `Ctrl+P` behavior and logs indicated serial/console style characters were still entering X path.
- Added source guard in `consoleintr()` so `/dev/kbd0` queue is populated only when source is keyboard IRQ (`getc == kbdgetc`).
- Removed ESC-prefix Alt synthesis from `/dev/kbd0` path (kept correctness over terminal workaround).

### 7) Instrumentation added for end-to-end tracing
- x6 instrumentation:
  - raw `/dev/kbd0` events
  - emitted X key events with target and state
- dwm instrumentation:
  - incoming keycode/state
  - cleaned mask
  - resolved keysym
  - match/no-match on keybinding table

## Key Observations Captured
- When pressing Option combinations, logs showed:
  - keycodes for plain characters (`p`, `1`, `2`, `3`, `Return`)
  - `state=0x0` in x6 and dwm
  - dwm reported `key no-match` for all tested combos
- This means key events are present, but modifier state is absent by the time dwm handles them.

## Current Working Hypothesis
The primary remaining fault is in modifier-state fidelity for hardware keyboard events reaching `/dev/kbd0` and/or transformation of that state before dwm comparison (`CLEANMASK` path). Shortcuts fail because `Mod1Mask` is never present in event state at dwm keypress handling.

## Current Debug Status
- Debug logging remains enabled in:
  - `user/x6.c`
  - `ports/dwm-6.8/dwm.c`
- This is intentional for now to locate exact drop point for modifier bits.

## What To Verify Next (Actionable)
1. Confirm `/dev/kbd0` raw events include expected modifier bits for Alt/Shift.
2. Confirm x6 emits KeyPress with same modifier bits (no loss).
3. Confirm x11 shim parses event state unchanged.
4. Confirm dwm sees same `ev->state` and `CLEANMASK(ev->state)` includes `Mod1Mask` for Option/Alt.
5. If bits are absent at source, inspect keyboard decode/modifier tracking in `kbd.c` for this host/input mode.

## Notes
- Some intermediate changes were diagnostic and intentionally temporary.
- Handshake/startup is now functional enough to run dwm; keyboard modifiers remain the blocking issue.

## Architecture Decision (2026-04-07)
- `/dev/kbd0` must be sourced from raw keyboard decode events, not from console cooked character input.
- Console TTY semantics remain unchanged for existing shell/terminal programs.
- x6 keyboard ingress must be `/dev/kbd0` only; stdin/console byte fallback is removed.
- `/dev/kbd0` event ABI now includes `value` (press/release/repeat), in addition to keycode/state.

## Host/VMM Caveat (Documented, Deferred)
- QEMU host key capture is usually reliable and is not the first suspect.
- However, if host-side key combos (especially Option/Command mappings on macOS) are not forwarded as guest scancodes, no in-guest translation can recover missing modifier events.
- This remains a back-pocket check only after verifying in-guest raw event plumbing.
