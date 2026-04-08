# st 0.9.3 auxv6 Integration Roadmap (2026-04-07)

## Current Status
- Build lane exists and compiles to `ports/st-0.9.3/_st` via `ports/st-0.9.3/Makefile.auxv6`.
- st is intentionally **not** wired into top-level make/image flow yet.
- Current lane uses temporary st-specific compatibility hacks to let terminal core integration/testing start before full X11 parity lands.

## st-Specific Hack Inventory (Rewire Targets)
- `ports/st-0.9.3/Makefile.auxv6`
  - `AUXV6_ST_HACK_NOPTY` compile define is enabled.
  - Rewire target: remove define once PTY + termios parity is implemented.
- `ports/st-0.9.3/x-auxv6.c`
  - `AUXV6_ST_HACK` backend replaces upstream `x.c` for now.
  - Rewire target: switch `SRC` back to upstream `x.c` when X11/Xft/XIM support is ready.
- `ports/st-0.9.3/st.c`
  - `AUXV6_ST_HACK_NOPTY` path replaces `openpty()` with pipe-based child I/O.
  - Rewire target: restore PTY path once openpty/pty plumbing lands.
- `ports/st-0.9.3/compat/wchar.h`
  - Minimal `wcschr()`/`wcwidth()` compatibility only.
  - Rewire target: delete once libc wchar coverage is complete.
- `ports/st-0.9.3/compat/stubs.c`
  - `tcsendbreak()` stub returns success.
  - Rewire target: remove when real termios `tcsendbreak()` exists.
- `ports/st-0.9.3/config.auxv6.h`
  - Reduced config excludes upstream shortcut/key tables requiring full X11 keysym set.
  - Rewire target: switch back to upstream `config.def.h`/normal config flow after keysym/header parity.

## X11 Feature Matrix: st Needs vs auxv6 Today

### Available Enough For Initial Bring-Up
- Basic display/window/event loop: `XOpenDisplay`, `XCreateWindow`, `XMapWindow`, `XSelectInput`, `XNextEvent`, `XPending`.
- Primitive drawing path: `XFillRectangle`, `XDrawString`, `XCreateGC`.
- Basic properties/atoms: `XInternAtom`, `XChangeProperty`, `XGetWindowProperty`.

### Missing or Partial (Blocks Upstream x.c)
- Xft + fontconfig stack (major blocker):
  - `X11/Xft/Xft.h` API surface and runtime behavior.
  - Fc* fontconfig object model and matching APIs.
- XIM/input method path:
  - `XOpenIM`, `XCreateIC`, `XSetIMValues`, `XmbLookupString`, `XFilterEvent`.
- Clipboard/selection semantics:
  - `XSetSelectionOwner`, `XConvertSelection`, `XGetSelectionOwner`, selection events.
- WM metadata helpers:
  - `XSetWMProtocols`, `XSetWMName`, `XSetWMIconName`, `Xutf8TextListToTextProperty`.
- Keyboard translation parity:
  - KeySym coverage and proper keycode->keysym conversion (`XLookupString`/equivalent).
- PTY/termios parity for terminal behavior:
  - `openpty` path and canonical terminal session behavior.

## Proper Implementation Roadmap

### Phase 1: Stabilize Hack Lane (Now)
- Keep `_st` buildable with aux backend and no-pty fallback.
- Validate startup, child shell launch, draw loop, and basic key input path.
- Add runtime tracing in `x-auxv6.c` for key events and redraw cadence if needed.

### Phase 2: PTY + Termios Correctness
- Add real PTY support (`openpty` or equivalent) in auxv6 user/kernel path.
- Remove `AUXV6_ST_HACK_NOPTY` branches from `st.c`.
- Delete `compat/stubs.c` once `tcsendbreak` is implemented correctly.

### Phase 3: X11 Core Completion For st
- Implement selection ownership and conversion semantics in `user/x11.c`.
- Implement WM protocol/title helper functions in `user/x11.c` + `include/X11/Xlib.h`.
- Add missing key/event helper coverage needed by upstream st key path.

### Phase 4: Font/Text Path Convergence
- Either:
  - implement enough Xft/fontconfig compatibility for upstream `x.c`, or
  - upstream/aux patch st to use auxv6 native text API instead of Xft.
- Preferred long-term: converge toward real Xft-compatible behavior if x6 roadmap includes that for broader app compatibility.

### Phase 5: Replace Hack Backend
- Switch `ports/st-0.9.3/Makefile.auxv6` `SRC` from `x-auxv6.c` back to upstream `x.c`.
- Drop `config.auxv6.h` reduction and restore full config/shortcut model.
- Remove compat `wchar` shim when libc coverage is available.

## Wiring Plan (When You Approve)
- Add `_st` target in root Makefile (like `_dwm`) but keep out of default `UPROGS` initially.
- Add staged install to `/usr/bin/st` in `tools/stage-ext2-root.sh`.
- Set optional `targetfs/root/.xinitrc` launch line to test `st` under `dwm` manually (not default).
