# X11 Completeness Audit (April 8, 2026)

## Scope

Audit and implementation pass across:
- user/x11.c (Xlib/Xft/FontConfig shim)
- user/x6.c (display server command handling)
- include/X11/* headers (surface area check)

Goal: move from compile-only stubs to runtime-usable behavior for st/dwm.

## Implemented In This Pass

### 1. Text rendering path is now functional through Xft

Updated in user/x11.c:
- XftDrawRect now maps to XSetForeground + XFillRectangle
- XftDrawGlyphFontSpec now emits drawable text for each glyph spec
- XftDrawStringUtf8 now maps to XDrawString
- XftTextExtentsUtf8 now uses font metrics instead of hardcoded 8x16

Result:
- Xft calls are no longer no-ops in client shim.
- st/dwm draw paths now issue real draw commands.

### 2. Pixmap text rendering gap closed in x6

Updated in user/x6.c:
- Added x6_draw_text_pixmap() using user_font glyphs into pixmap pixel buffer
- DRAW_TEXT handler now renders when target drawable is a pixmap (not just windows)

Result:
- st draw-to-pixmap + XCopyArea model can now carry text to window output.

### 3. XParseColor expanded for real-world st config values

Updated in user/x11.c:
- Supports #RGB, #RRGGBB, #RRRRGGGGBBBB
- Supports named colors including red/green/blue/yellow/magenta/cyan
- Supports X-style intensity variants (red3, blue2, etc.)
- Supports grayNN/greyNN and numeric grayscale percentages
- Supports simple numeric RGB forms: "r g b" and "r,g,b"

Result:
- st color table entries like red3/green3/blue2/gray90 now parse.

### 4. Font pattern state is now minimally modeled

Updated in user/x11.c:
- Added internal x11_fc_pattern structure
- FcNameParse/FcPatternDuplicate/FcFontMatch/FcFontSetMatch now preserve state
- FcPatternAddDouble/GetDouble/Del support FC_PIXEL_SIZE and FC_SIZE
- FcPatternAddInteger + XftPatternGetInteger support FC_SLANT and FC_WEIGHT
- XftXlfdParse now returns a parsed pattern object
- XftDefaultSubstitute and FcDefaultSubstitute fill default pixel size
- XftFontOpenPattern/XftFontOpenName derive metrics from pattern/name rather than hardcoded constants

Result:
- Font setup logic in upstream st/dwm has consistent values and fewer fake defaults.

### 5. IM/text property stubs now have useful behavior

Updated in user/x11.c:
- XSetWMName/XSetTextProperty now write real properties via XChangeProperty
- Xutf8TextListToTextProperty now allocates/populates XTextProperty
- XSetWMIconName now sets WM_ICON_NAME
- XSetLocaleModifiers stores and returns modifier string
- XOpenIM/XCreateIC/XVaCreateNestedList now return valid non-null handles
- XmbLookupString now forwards to XLookupString
- XParseGeometry now parses common geometry strings

Result:
- WM/text metadata and input plumbing are less stubbed and more compatible.

### 6. Pixmap ID synchronization bug fixed

Updated in user/x11.c:
- XCreatePixmap now parses server reply (OK create_pixmap pmid=...) and stores server ID
- XCopyArea now returns success/failure semantics (0/-1) instead of raw read length

Result:
- Offscreen drawables are now aligned between client and x6 server.

## Remaining Gaps (Post-Pass)

These are still incomplete relative to full X11/Xft behavior:

1. Selection/clipboard ownership and transfer remains simplified
- XSetSelectionOwner/XGetSelectionOwner/XConvertSelection are still placeholders.
- Enough to avoid crashes, not full ICCCM selection behavior.

2. Clip handling in Xft remains no-op
- XftDrawSetClipRectangles/XftDrawSetClip are accepted but not enforced.

3. Keyboard/mouse grab details remain simplified
- XGrabButton/XUngrabButton/XAllowEvents are currently minimal stubs.

4. Window stacking operations are no-op
- XRaiseWindow/XLowerWindow do not yet translate to z-order updates in server.

5. Full IME and XKB support is still absent
- XKBlib is still minimal stub territory.
- No real compose/IME stack beyond basic key translation.

6. Font fallback and non-ASCII shaping remain limited
- Current text rendering is pragmatic for bitmap/ASCII path.
- No advanced shaping, fallback chains, or anti-aliased glyph rasterization.

7. Protocol parity vs full X11 remains partial by design
- Current implementation targets st/dwm compatibility first, not full X.Org-level semantics.

## Build Verification

Verified in workspace:
- make _x6 _dwm _xinit (success)
- make _st (up-to-date success)

No static errors reported for:
- user/x11.c
- user/x6.c

## Recommended Next Targets

If you want to continue toward broader X11 completeness, best ROI next:

1. Implement real selection/clipboard data flow end-to-end
2. Add clip rectangle enforcement in Xft draw path
3. Implement XRaiseWindow/XLowerWindow as server-side z-order commands
4. Flesh out XGrabButton/XAllowEvents semantics for stricter WM behavior
5. Expand glyph path for broader UTF-8 coverage and fallback behavior

## Session Follow-Up: Prompt Still Missing

Observed after first pass:
- dwm bar text renders correctly
- st window appears and font rendering is active
- shell prompt text remained invisible in st content area

Root-cause candidate addressed in this follow-up:
- x6 COPY_AREA path was writing pixel coordinates into canvas cell coordinates and never issuing proper framebuffer row blits for pixmap-to-window copies.
- st depends on pixmap backbuffer + COPY_AREA for final present.

Fix implemented:
- In user/x6.c, COPY_AREA now:
	- Performs clipped row blits from pixmap source to destination window coordinates on framebuffer backend.
	- Writes rows to /dev/fb0 and updates shadow framebuffer coherently.
	- Preserves cursor overlay hide/show behavior while blitting.
	- Uses a reasonable cell-mapped fallback path on ANSI backend.
	- Uses strict typed command parsing for COPY_AREA fields (`%u/%d`) to avoid varargs format mismatches and destination ID corruption risk.

Additional completeness work after that:
- Added real stacking order tracking (`z`) in x6 windows.
- MAP/WM_MAP now raise mapped windows to top of stack.
- Added protocol commands `RAISE`/`LOWER` in x6.
- Implemented XRaiseWindow/XLowerWindow in user/x11.c to send those protocol commands.
- Updated pointer hit-testing (`x6_pick_window_at`) to honor stacking order.

Why this is important:
- It restores correctness of the present step for st's draw pipeline.
- Without it, text can be rendered to the pixmap correctly but never make it to the visible window surface.

Immediate next completeness steps from here:
1. Validate runtime prompt visibility after COPY_AREA fix.
2. If prompt is still absent, instrument XftDrawGlyphFontSpec glyph-to-character mapping path next.
3. Add clip enforcement for XftDrawSetClipRectangles / XftDrawSetClip.
4. Implement stacking semantics (XRaiseWindow/XLowerWindow) in x6 protocol.

## Latest Tranche: "Implement All Remaining Stubs" (Same Session)

Additional completeness work landed after the follow-up section above:

1. Selection ownership and conversion are no longer pure no-ops
- XSetSelectionOwner/XGetSelectionOwner now track per-selection owner state in user/x11.c.
- XConvertSelection now synthesizes a SelectionNotify event and attempts a best-effort property transfer from owner to requestor when data exists.
- This is still a simplified ICCCM model, but no longer placeholder-only behavior.

2. Xft clip APIs are now enforced in draw paths
- XftDraw now carries clip state (`has_clip` + clip rectangle) in include/X11/Xft/Xft.h.
- XftDrawSetClipRectangles computes an effective clip extents box.
- XftDrawSetClip clears clipping.
- XftDrawRect, XftDrawGlyphFontSpec, and XftDrawStringUtf8 now honor clip state before drawing.

3. Button grabs now have real protocol semantics
- user/x11.c now maps XGrabButton/XUngrabButton to x6 protocol commands.
- x6 server now supports GRAB_BUTTON/UNGRAB_BUTTON command handling and stores passive button grabs per client.
- Pointer event routing consults passive button grabs for ButtonPress delivery.
- Pointer events now carry composed state (keyboard modifiers + button mask bits), improving WM input behavior.

4. Grab wildcard modifier semantics aligned for key/button grabs
- AnyModifier is translated by user/x11.c into x6 wildcard modifier encoding.
- Prevents silent mismatch between Xlib-side wildcard requests and x6-side matching.

Build status after this tranche:
- make _x6 _dwm _xinit: success
- make _st: success

## Follow-Up Fixes (Post Tranche)

1. Repaired clip helper regression
- `x11_point_in_clip` in `user/x11.c` had an accidental `return ok;` after refactor.
- Fixed to `return 1;` when point is inside clip, restoring successful compile under `-Werror`.

2. Corrected WM property status propagation
- `XSetWMProperties` in `user/x11.c` now returns aggregated `ok` status instead of unconditional success.
- This preserves caller-visible failure semantics when subordinate property writes fail.

3. Header surface completion for property querying
- Added `AnyPropertyType` definition in `include/X11/Xlib.h` to match callers that request unconstrained property type fetches.

Build status after follow-up fixes:
- make _x6 _dwm _xinit: success
- make _st: success

## Next Tranche Implemented: Selection Routing + Event API Coverage

This tranche focused on deeper event and clipboard semantics that matter for WM/terminal interoperability.

1. Selection ownership moved into x6 server state (cross-client)
- Added x6 protocol commands:
	- `SET_SELECTION_OWNER <selection> <owner> <time>`
	- `GET_SELECTION_OWNER <selection>`
	- `CONVERT_SELECTION <selection> <target> <property> <requestor> <time>`
	- `QUEUE_SELECTION_NOTIFY <requestor> <selection> <target> <property> <time>`
- `XSetSelectionOwner` / `XGetSelectionOwner` / `XConvertSelection` in `user/x11.c` now use those commands.
- Selection ownership is now global to x6 rather than process-local shim state only.

2. Selection events now flow through event queues
- x6 now emits/queues:
	- `SelectionClear` when owner changes
	- `SelectionRequest` on convert requests when an owner exists
	- `SelectionNotify` when owner/application sends it via `XSendEvent`
- `XSendEvent` now forwards `SelectionNotify` through `QUEUE_SELECTION_NOTIFY`.
- `x11_parse_event_line` now parses SelectionClear/SelectionRequest/SelectionNotify and fills XEvent selection fields.

3. Enter/Leave crossing events implemented
- x6 now tracks pointer window crossings during motion and emits:
	- `EnterNotify`
	- `LeaveNotify`
- Delivery honors window event masks for Enter/Leave.
- x11 parser now maps these into `XCrossingEvent` fields.

4. Missing event helper API coverage added in shim
- Implemented in `user/x11.c`:
	- `XPeekEvent`
	- `XPutBackEvent`
	- `XCheckTypedEvent`
	- `XCheckTypedWindowEvent`
	- `XCheckWindowEvent`
	- `XWindowEvent`
- Added corresponding declarations in `include/X11/Xlib.h`.

5. Header completeness for selection clear event
- Added `XSelectionClearEvent` definition and `xselectionclear` union arm in `include/X11/Xlib.h`.

Build status after this tranche:
- make `_x6 _dwm _xinit`: success
- make `_st`: success

## Final Tranche (This Request): Stricter Selection + Event Queue Robustness + Richer Crossing/Focus

1. Stricter selection conversion semantics
- Selection ownership is now validated server-side during conversion:
	- rejects invalid requestor windows
	- rejects empty/invalid target/property payloads
	- applies owner-time stale checks (`request_time < owner_time` => `SelectionNotify` with property `NONE`)
- Selection ownership is cleared on owner client disconnect to avoid stale owners.

2. Buffered event queue semantics in x11 shim
- Replaced single pending-event behavior with a bounded buffered queue (`X11_MAX_EVENTS`) to improve behavior under bursty event traffic.
- Updated event APIs to consume/search buffered events properly:
	- `XNextEvent`, `XPeekEvent`, `XPutBackEvent`
	- `XMaskEvent`, `XCheckMaskEvent`
	- `XCheckTypedEvent`, `XCheckTypedWindowEvent`, `XCheckWindowEvent`, `XWindowEvent`
- Added a raw wire-read path so blocking APIs avoid re-consuming buffered non-matching events.

3. Richer crossing/focus detail fields
- x6 now carries/serializes additional crossing/focus metadata:
	- crossing: `mode`, `detail`, `focus`, `same_screen`
	- focus: `mode`, `detail`
- x11 parser now decodes those fields into `XCrossingEvent` and `XFocusChangeEvent`.

Build status after this tranche:
- make `_x6 _dwm _xinit`: success (`XSTACK_OK`)
- make `_st`: success (`ST_OK`)

## Next Correct Interface Piece Landed: Property Format/Type Semantics (st clipboard-critical)

Why this was next:
- `st` selection/clipboard flows touch `XChangeProperty` / `XGetWindowProperty` with non-8-bit payloads (not just plain strings).
- The prior shim path only supported format=8 strings, leaving a protocol-compat gap for atom lists and typed payloads.

What was implemented:
1. Expanded property encoding/decoding in `user/x11.c`
- Added binary-safe property packing/unpacking helpers with explicit metadata for:
	- property type
	- format (`8/16/32`)
	- element count
	- payload bytes (hex-encoded)
- `XChangeProperty` now supports `PropModeReplace`, `PropModeAppend`, and `PropModePrepend` for compatible format/type payloads.
- `XGetWindowProperty` now decodes typed payloads, returns accurate `actual_type`, `actual_format`, `nitems`, and `bytes_after`, and handles `req_type` mismatch semantics.

2. Increased protocol/property buffer headroom
- `user/x11.c`: increased line buffer size used for command/reply handling.
- `user/x6.c`: expanded per-property value capacity and temporary GET/SET property buffers to carry encoded payloads.

3. Selection robustness tie-in
- Existing selection routes now benefit from typed property transport instead of string-only fallbacks.

Build status after this tranche:
- make `_x6 _dwm _xinit`: success (exit 0)
- make `_st`: success (exit 0)

## Event Semantics Tranche (Continuing Completeness)

Additional X11 behavior implemented beyond drag/resize concerns:

1. KeyRelease event path is now wired end-to-end
- x6 now emits key release events from `/dev/kbd0` (`AUX_KBD_VALUE_RELEASE`) instead of only press/repeat.
- Event stream now includes `EVENT KeyRelease ...` records.
- x11 shim now parses `KeyRelease` and exposes it as X11 `KeyRelease` events.

2. PropertyNotify semantics now work for selected windows
- x6 now queues PropertyNotify events when `SET_PROPERTY` or `DELETE_PROPERTY` updates window properties.
- Delivery respects window event selection (`PropertyChangeMask`).
- x11 shim now parses `PropertyNotify` lines, interns atom names, and fills `XPropertyEvent` fields (`atom`, `state`, `time`).

3. Pointer/button event state fidelity improved
- Pointer event payloads now consistently carry combined modifier+button state bits, reducing WM/client ambiguity in button handler logic.

Build status after this tranche:
- make _x6 _dwm _xinit: success
- make _st: success
