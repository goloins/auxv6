# x6 Framebuffer Takeover Path (No server7 Dependency)

## Goal

Enable `x6` to switch from ANSI terminal rendering to direct framebuffer output (`/dev/fb0`) so `startx /usr/bin/dwm` behaves like a real display-server takeover path.

## Current State

- Launch/control plane is working:
  - `startx` launches `xinit`
  - `xinit` launches `x6` and `dwm`
  - `dwm` reaches `run()`
  - `x11` handshake succeeds
- Draw protocol is active (`DRAW_RECT` telemetry confirms paint commands flow).
- `x6` currently reports `backend=ansi (framebuffer unavailable)` in guest.

## Implemented in This Tranche

- `x6` backend selector added:
  - `-B auto|ansi|fb`
- `x6` framebuffer backend probe added:
  - opens `/dev/fb0`
  - queries fb geometry/ioctl metadata
  - writes pixel rows when active
- `devman` now provisions `/dev/fb0` (console-major framebuffer minor).
- `console` driver now supports minimal framebuffer ABI:
  - `FBIOGET_VSCREENINFO` (0x4600)
  - `FBIOGET_FSCREENINFO` (0x4602)
  - raw write path on framebuffer minor that updates framebuffer memory and flushes regions.

## Remaining Work

1. Guest validation of `/dev/fb0` node lifecycle
- Confirm `devman` creates `/dev/fb0` at boot/session bring-up.
- Confirm permissions and ownership are correct for x6 launch context.

2. Takeover/restore semantics
- Define and implement deterministic mode transitions:
  - tty active -> x6 active
  - x6 exit/crash -> tty restored
- Ensure text-console state is preserved/restored cleanly.

3. Isolation from tty stream
- Prevent ANSI/tty writes from visually fighting framebuffer output while x6 owns display.
- Add explicit ownership checks around console flush paths.

4. Input coupling
- Route real keyboard/mouse input to x6 event queue while in framebuffer-owner mode.

5. Hardening + observability
- Add explicit backend-active logs and counters in `/proc/gfxstats`/debug output.
- Add simple smoke test utility to verify `/dev/fb0` open/ioctl/write behavior.

## Suggested Execution Order

1. Validate `/dev/fb0` availability and x6 `backend=fb` activation in guest.
2. Implement takeover ownership + restore on exit.
3. Gate tty flush while x6 owns output.
4. Land pointer ingress.
5. Remove temporary ANSI fallback dependence for normal x6 sessions.
