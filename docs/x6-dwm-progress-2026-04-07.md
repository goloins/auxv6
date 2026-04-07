# x6 + dwm Progress Snapshot (2026-04-07)

## What Is Done So Far

### Foundation and launch path
- `x6` userspace display server skeleton is in place and accepts local loopback clients.
- `xinit`/`startx` flow is wired for auxv6, with compiled `startx` launcher (`user/startx.c`) invoking `/bin/xinit`.
- Staging policy is corrected:
  - `dwm` installs to `/usr/bin/dwm`
  - `startx` installs to `/bin/startx` and `/usr/bin/startx`
  - explicit `chmod 0755` is enforced during staging for `startx`

### WM-critical protocol behavior in x6
- Root `SubstructureRedirect` ownership gating is implemented (`REQUEST_REDIRECT`), including single-WM denial behavior.
- Client-side `MAP`/`CONFIGURE` now become pending requests when redirect is owned.
- WM-side resolution path is implemented (`WM_MAP`, `WM_UNMAP`, `WM_CONFIGURE`).
- Focus transition events exist (`FocusIn`, `FocusOut`) through `SET_FOCUS`.
- Keyboard-grab control path exists (`GRAB_KEYBOARD`, `UNGRAB_KEYBOARD`).

### Properties and atoms baseline
- Per-window property storage is implemented in `x6`.
- `SET_PROPERTY` and `GET_PROPERTY` protocol commands are live.
- X11 shim maps common atom/property operations used by dwm paths.

### X11 shim expansion (`user/x11.c`)
- Event parser supports:
  - `MapRequest`
  - `ConfigureRequest`
  - `FocusIn`/`FocusOut`
  - `DestroyNotify`
  - `KeyPress`
  - `ButtonPress`/`ButtonRelease`
  - `MotionNotify`
- `XNextEvent`, `XMaskEvent`, and `XCheckMaskEvent` now include pending-event buffering and mask-aware selection.
- WM role tracking is present so WM-side operations call WM-specific protocol commands.

### dwm integration wiring
- auxv6-specific dwm build lane is present (`ports/dwm-6.8/Makefile.auxv6`).
- Root `Makefile` `_dwm` path depends on shim object updates so relinking occurs after `x11.c` changes.

## Large Tranche Just Completed

### Tranche: real keyboard ingress into x6 runtime loop
Implemented in `user/x6.c`:
- `x6` now multiplexes client socket + stdin with `poll()` instead of blocking forever on socket reads.
- Console bytes are translated into `KeyPress` events and queued into the x6 event stream.
- Event routing target is chosen as:
  1. keyboard-grab owner (if active), else
  2. focused window (if set), else
  3. root window
- ESC prefix behavior is added to synthesize `Mod1Mask` on the next key, enabling terminal-driven Alt-style dwm shortcuts in this interim model.
- Return and Backspace are mapped to X keysyms (`XK_Return`, `XK_BackSpace`), while printable bytes map directly.

This converts keyboard input from synthetic-only (`INJECT_KEY`) to a real runtime source.

### Tranche: first visible rendering path (bar/layout paint surfaces)
Implemented across `user/x11.c`, `user/x6.c`, and `ports/dwm-6.8/drw-auxv6.c`:
- X11 shim GC state is now tracked enough to honor foreground color for draw operations.
- `XFillRectangle`/`XDrawRectangle` now send concrete draw commands over the x6 protocol.
- x6 now handles `DRAW_RECT` and renders to a lightweight ANSI-backed canvas tied to pixel-cell mapping.
- Auxv6 drw backend now performs real rectangle painting and text-block rendering (placeholder glyph band) instead of no-op returns.

This makes dwm paint activity visible in the active display path, even before a full framebuffer compositor lands.

### Tranche: control-plane stabilization and handshake deadlock fix
- Verified end-to-end launch path: `startx -> xinit -> dwm -> XOpenDisplay` is now live.
- Fixed xinit probe deadlock by explicitly detaching readiness probe clients so x6 can accept dwm as a second client.
- Added targeted runtime tracing in xinit/x6/x11/dwm to isolate startup stalls quickly.

### Tranche: framebuffer takeover scaffold (server7-independent)
- Added x6 backend selection (`-B auto|ansi|fb`) and automatic framebuffer probe path.
- Added `/dev/fb0` device-node provisioning in `devman` (console-major framebuffer minor).
- Added minimal framebuffer ioctls in console driver:
  - `FBIOGET_VSCREENINFO` (0x4600)
  - `FBIOGET_FSCREENINFO` (0x4602)
- Added raw pixel write path for `/dev/fb0` via console framebuffer memory with flush-region calls.

This is the first explicit handoff mechanism from text-console-only behavior toward real X scanout writes.

## What Is Still To Go

### Tranche A (highest user-visible impact): visible rendering path
- Validate `/dev/fb0` availability and ioctl success in guest (`x6: framebuffer backend active ...`).
- Verify dwm draw rectangles produce real pixel changes on scanout.
- If needed, add explicit ownership/focus arbitration around fb writes so tty and x6 do not fight.

### Tranche B: real pointer (mouse) ingress
- Current button/motion are protocol-injectable but not sourced from hardware input stream.
- Need pointer event capture and routing into `ButtonPress`/`ButtonRelease`/`MotionNotify` delivery.

### Tranche C: key state fidelity
- Modifier tracking is still simplified.
- Need proper key press/release state model and consistent modifier propagation.

### Tranche D: broaden X11 behavior for client compatibility
- Additional ICCCM/EWMH and geometry/query semantics are still thin/stubbed.
- Important for broader app compatibility after base interactivity is stable.

## Recommended Next Run Order
1. Validate fb backend activation and visible scanout takeover (`/dev/fb0` path).
2. Harden fb handoff semantics (ownership, restore-to-tty, crash cleanup).
3. Land hardware pointer ingress.
4. Refine keyboard/modifier fidelity and remove temporary ALT-prefix assumptions where possible.
5. Expand ICCCM/EWMH/query behavior based on first real client failures.
