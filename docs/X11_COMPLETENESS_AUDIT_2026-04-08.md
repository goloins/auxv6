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
