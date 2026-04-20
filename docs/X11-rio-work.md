# X11/rio Port Work Plan and Progress

Date: 2026-04-19
Owner: ongoing implementation session
Scope: auxv6 `x6` server + `user/x11.c` Xlib shim needed for plan9port `rio`

## Goals

1. Port plan9port `rio` on auxv6 using the existing ports infrastructure.
2. Close missing Xlib/X11 behavior gaps in `x6` + Xlib shim that block `rio` startup/runtime.
3. Implement changes in slices to reduce regression risk and keep behavior verifiable.

## Ground Rules

1. Edit authoritative headers only under `include/`.
2. Do not manually edit `targetfs` header mirrors (staged/generated from source tree).
3. Keep changes incremental and behavior-focused.
4. Validate after each slice with diagnostics and build checks.

## Implementation Plan

### Slice A: API/Symbol Coverage (Completed)

Purpose: eliminate immediate unresolved/missing Xlib surface used by `rio`.

Planned items:
1. Add missing declarations in `include/X11/Xlib.h`.
2. Implement missing shim functions in `user/x11.c`.
3. Keep placeholder behavior only where protocol backing is not yet added.

Completed:
1. Added declaration for `XCreatePixmapFromBitmapData`.
2. Implemented/added in shim:
   - `XChangeActivePointerGrab`
   - `XSetWindowBorderWidth`
   - `XSetWindowBackgroundPixmap` (initially placeholder, then protocol-backed in Slice B)
   - `XCreatePixmapFromBitmapData`
   - `XInstallColormap` (initially placeholder, then protocol-backed in Slice B)
   - `XGetErrorText`
   - `XGetErrorDatabaseText`

Outcome:
1. Slice A symbol/API coverage no longer blocks basic rio compilation paths.

### Slice B: Behavioral Correctness (In Progress; major pieces landed)

Purpose: implement server-backed behavior for areas rio depends on.

Planned items:
1. Background pixmap/pixel semantics for clear operations.
2. Colormap install tracking + notify behavior.
3. Redirect conflict semantics (`BadAccess`-style behavior when WM redirect already owned).
4. Missing event emissions and parsing (`CreateNotify`, `ReparentNotify`, `ColormapNotify`).

Progress in this session:
1. Header/type support extended in source tree:
   - Added `XReparentEvent` and `XColormapEvent` in `include/X11/Xlib.h`.
   - Added `XEvent` union members for reparent/colormap events.
   - Added `ColormapChangeMask`, `ColormapInstalled`, `ColormapUninstalled`.
   - Added `X_ChangeWindowAttributes` in `include/X11/Xproto.h` for request-code mapping.
2. `x6` protocol/server behavior present for:
   - `SET_WINDOW_BACKGROUND_PIXMAP`
   - `SET_WINDOW_BACKGROUND_PIXEL`
   - `CLEAR_AREA` background-aware clear path
   - `SET_WINDOW_COLORMAP`
   - `INSTALL_COLORMAP`
   - Event queue/flush support for `CreateNotify`, `ReparentNotify`, `ColormapNotify`
3. `user/x11.c` shim behavior updated for:
   - Parsing `CreateNotify`, `ReparentNotify`, `ColormapNotify`
   - Event mask mapping + event window routing updates
   - `XSelectInput` redirect-in-use path dispatches `BadAccess` through error handler
   - Protocol-backed implementations:
     - `XSetWindowBackgroundPixmap`
     - `XSetWindowBackground`
     - `XChangeWindowAttributes` handling for `CWBackPixmap`, `CWBackPixel`, `CWColormap`
     - `XClearArea` using `CLEAR_AREA`
     - `XInstallColormap` using `INSTALL_COLORMAP`
4. Reparent event targeting parity improved:
   - Event payload now distinguishes child-structure and parent-substructure observers.

## Current File-Level Progress

Actively modified in working tree:
1. `include/X11/Xlib.h`
2. `user/x11.c`

Additional behavioral code paths verified present in tree:
1. `user/x6.c`
2. `include/X11/Xproto.h`

## Validation Status

1. Editor diagnostics on touched files: clean (`get_errors` reports none).
2. Non-sudo build check attempted (`make aux.kern`) failed due local toolchain environment (`i386-*` toolchain check), not due compile diagnostics in touched files.
3. Preferred full validation command remains:
   - `sudo make aux.kern`
   - Use same terminal session to reuse sudo auth.

## Remaining Work (Next Slice Steps)

1. Rio-focused runtime verification pass:
   - Confirm WM claim/fail behavior exactly matches rio startup expectations.
   - Confirm event ordering around create/reparent/map in rio paths.
2. Tighten event fidelity as needed after real rio traces:
   - Reparent/Create field parity against real Xlib expectations.
   - Colormap notify state transitions for edge cases.
3. Add targeted regression checks (small harness/tests or trace assertions) for:
   - Redirect conflict (`BadAccess` callback)
   - Background clear semantics
   - Colormap notify delivery
4. Start integration pass for plan9port `rio` in ports flow once runtime behavior is confirmed.

## Quick Resume Checklist

1. Run `sudo make aux.kern` in the usual terminal.
2. Boot/run with current tree and exercise rio startup path.
3. Capture any event/protocol mismatches from logs.
4. Apply minimal corrective patch in `user/x11.c` / `user/x6.c`.
5. Re-run diagnostics + build check.
