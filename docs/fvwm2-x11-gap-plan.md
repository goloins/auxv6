# fvwm2 Port — X11 Gap Plan
*Date: 2026-04-19*

## Purpose

fvwm2 is ported as an X11 gap-discovery vehicle.  The goal is **not** to ship
a polished fvwm2 build; it is to identify holes in our `user/x11.c` / `libX11`
implementation, then fill them so downstream apps (and future WMs) work.

The port lives at `ports/fvwm-fvwm2-stable/` with:
- `Makefile.auxv6`  — hand-crafted cross-build makefile (canonical: `ports/makefiles/fvwm2.Makefile`)
- `config.h`        — hand-crafted feature-flag header (no autoconf)

---

## Config strategy

`config.h` disables optional deps to reduce noise on the first bringup pass:

| Flag | State | Reason |
|------|-------|--------|
| `SHAPE` | **ON** | Core WM decoration path; we have most Shape symbols |
| `HAVE_XINERAMA` | **ON** | All four Xinerama syms are in libX11 |
| `HAVE_XRENDER` | off | Many Render syms missing; re-enable after Tier-R work |
| `HAVE_XFT` | off | We export 23 of 60 Xft syms fvwm2 needs; missing: `XftInit`, `XftFontSet*`, `XftPattern{Add,Get,Dup,Find}`, `XftObjectSet*`, `XftListFonts*`, `XftNameParse`, `XftValueDestroy/List/Print`, etc. |
| `HAVE_PNG` | off | No libpng |
| `HAVE_XOUTPUT_METHOD` | off | OM path (XOpenOM etc.) not yet in libX11 |
| `HAVE_NLS` / `HAVE_BIDI` | off | gettext/fribidi not ported |
| `SESSION` | off | ICE/SM not ported |

---

## X11 gap list

Functions called by fvwm2 that are **absent** from `user/libX11.a`.
Grouped by implementation priority.

### Tier 1 — Core WM / event loop  *(implement first — needed for any functional session)*

| Function | Xlib description | Notes |
|----------|-----------------|-------|
| `XIfEvent` | Block until event matches predicate | fvwm event loop; critical |
| `XPeekIfEvent` | Peek for matching event, no remove | event coalescing |
| `XQLength` | Queued event count (no I/O) | event drain loop |
| `XAutoRepeatOff` / `XAutoRepeatOn` | Toggle keyboard autorepeat | needed during grab |
| `XSetIOErrorHandler` | Fatal I/O error callback | robustness; WMs must set this |
| `XSynchronize` | Enable/disable sync mode | debug path but called unconditionally |
| `XScreenOfDisplay` | Return `Screen*` for screen number | used everywhere `DisplayWidth()/DisplayHeight()` macros go |
| `XDisplayString` | Return connection string of open Display | e.g. `:0` — different from `XDisplayName` |
| `XMapSubwindows` | Map all mapped children | used in icon/window restore |
| `XStringToKeysym` | Parse keysym name → `KeySym` | config/binding parser |
| `XLookupKeysym` | Translate XKeyEvent → KeySym | key dispatch |
| `XCopyGC` | Copy GC fields from src to dst | drawing setup |
| `XVisualIDFromVisual` | `Visual*` → `VisualID` | visual matching |

### Tier 2 — Color management

| Function | Xlib description | Notes |
|----------|-----------------|-------|
| `XAllocColorCells` | Allocate r/w color cells | colormaps, colorset module |
| `XFreeColors` | Free previously allocated pixels | colormaps |
| `XQueryColor` | Read single color from colormap | color parsing |
| `XQueryColors` | Read multiple colors from colormap | batch color operations |
| `XStoreColors` | Write colors into r/w colormap | colorset animations |

### Tier 3 — Text / font

Status (2026-04-19): implemented in-tree in `user/x11.c` and headers (`XDrawImageString`, `XExtentsOfFontSet`, `XGetFontProperty`, `XGetNormalHints`, `XGetSizeHints`).

| Function | Xlib description | Notes |
|----------|-----------------|-------|
| `XDrawImageString` | Draw string + fill background | menu / title bar labels |
| `XExtentsOfFontSet` | Overall extents of font set | text layout |
| `XGetFontProperty` | Read an atom-indexed font property | font info |
| `XGetNormalHints` | Deprecated wrapper for `XGetWMNormalHints` | ICCCM compat; may be a macro |
| `XGetSizeHints` | Deprecated size hints fetch | ICCCM compat |

### Tier 4 — Image / bitmap

Status (2026-04-19): implemented in-tree in `user/x11.c` and headers (`XAddPixel`, `XGetSubImage`, `XCreateBitmapFromData`, `XReadBitmapFile`, `XQueryBestTile`).

| Function | Xlib description | Notes |
|----------|-----------------|-------|
| `XAddPixel` | Add constant to every pixel in XImage | image recoloring |
| `XGetSubImage` | Copy sub-rectangle of server image to client XImage | icon capture |
| `XCreateBitmapFromData` | Create 1-bit Pixmap from static data | cursors, icons |
| `XReadBitmapFile` | Load XBM file → Pixmap + dimensions | icon loading |
| `XQueryBestTile` | Ask server for preferred tile size | stipple/tile GC ops |

### Tier 5 — Shape extension completions

Status (2026-04-19): implemented in-tree in `user/x11.c` and headers (`XShapeCombineRegion`, `XShapeGetRectangles`, `XShapeInputSelected`, `XShapeOffsetShape`).

We have: `XShapeCombineMask`, `XShapeCombineRectangles`, `XShapeCombineShape`,
`XShapeQueryExtension`, `XShapeQueryExtents`, `XShapeQueryVersion`, `XShapeSelectInput`.

Tier 5 additions now present: `XShapeCombineRegion`, `XShapeGetRectangles`,
`XShapeInputSelected`, `XShapeOffsetShape`.

### Tier 6 — Keyboard / screen metadata

Status (2026-04-19): implemented in-tree in `user/x11.c` and headers (`XGetKeyboardControl`, `XGetMotionEvents`, `XListDepths`, `XScreenResourceString`).

| Function | Description | Notes |
|----------|-------------|-------|
| `XGetKeyboardControl` | Query autorepeat, bell, LED state | used before `XAutoRepeatOff` |
| `XGetMotionEvents` | Retrieve stored pointer motion history | optional; motion compression |
| `XListDepths` | Return list of supported pixel depths | visual setup |
| `XScreenResourceString` | Per-screen resource database string | resource merging |

### Tier 7 — ICCCM / property helpers

| Function | Description | Notes |
|----------|-------------|-------|
| `XGetCommand` | Read `WM_COMMAND` property | window restart |
| `XStringListToTextProperty` | Build `XTextProperty` from string list | WM_COMMAND, WM_NAME |

### Tier 8 — Misc GC / region

| Function | Description | Notes |
|----------|-------------|-------|
| `XSetClipOrigin` | Set GC clip-mask origin | pixmap tiling |
| `XSetGraphicsExposures` | Enable/disable GraphicsExpose events on GC | CopyArea flows |
| `XSetRegion` | Set GC clip to an `XRegion` | combined with Shape |
| `XSetStipple` | Set GC stipple pixmap | patterned fills |
| `XDestroyRegion` | Free an `XRegion` | region cleanup |

### Tier R — XRender (deferred; re-enable `HAVE_XRENDER` after these land)

| Function | Description |
|----------|-------------|
| `XRenderQueryExtension` | Check Render presence |
| `XRenderQueryVersion` | Get Render version |
| `XRenderQueryFormats` | Enumerate all picture formats |
| `XRenderFindFormat` | Find format by template |
| `XRenderChangePicture` | Modify picture attributes |
| `XRenderFillRectangles` | Fill multiple rects with Render color |
| `XRenderSetPictureClipRectangles` | Set rect-list clip on Picture |
| `XRenderSetPictureClipRegion` | Set region clip on Picture |

### Tier X — Xft (deferred; re-enable `HAVE_XFT` after these land)

fvwm2 renders text without Xft using `XmbDrawString` + `XFontSet` and
`XDrawString` + `XFontStruct` — both already in libX11.  So a working binary
is achievable with `HAVE_XFT` off.  Xft adds antialiased/TrueType rendering;
it is a real gap but not blocking first-pass work.

We currently export 23 of the 60 Xft symbols fvwm2 calls.  The 37 missing:

**Font set / list management**

| Function | Description |
|----------|-------------|
| `XftInit` | Initialize Xft subsystem |
| `XftFontSetCreate` | Allocate an empty `XftFontSet` |
| `XftFontSetAdd` | Append pattern to font set |
| `XftFontSetMatch` | Find best match in font set |
| `XftFontSetPrint` | Debug-print a font set |
| `XftFontOpenXlfd` | Open font by XLFD name |
| `XftListFontSets` | List all font sets |
| `XftListFontsPatternObjects` | List fonts matching pattern for given properties |
| `XftGlyphExists` | Test if glyph exists in font |

**Pattern manipulation**

| Function | Description |
|----------|-------------|
| `XftNameParse` | Parse Xft font name string into pattern |
| `XftPatternAdd` | Add typed value to pattern |
| `XftPatternAddBool` | Add Bool value |
| `XftPatternAddDouble` | Add Double value |
| `XftPatternAddInteger` | Add Integer value |
| `XftPatternAddMatrix` | Add Matrix value |
| `XftPatternAddString` | Add String value |
| `XftPatternDuplicate` | Deep-copy a pattern |
| `XftPatternFind` | Find value in pattern by object name |
| `XftPatternGet` | Get typed value from pattern |
| `XftPatternGetBool` | Get Bool value |
| `XftPatternGetDouble` | Get Double value |
| `XftPatternGetMatrix` | Get Matrix value |
| `XftPatternGetString` | Get String value |
| `XftPatternPrint` | Debug-print pattern |
| `XftPatternVaBuild` | Build pattern from va_list |

**Object set**

| Function | Description |
|----------|-------------|
| `XftObjectSetCreate` | Allocate empty object set |
| `XftObjectSetAdd` | Add property name to set |
| `XftObjectSetDestroy` | Free object set |
| `XftObjectSetVaBuild` | Build object set from va_list |

**Config / defaults**

| Function | Description |
|----------|-------------|
| `XftConfigSubstitute` | Apply font config substitutions |
| `XftDefaultHasRender` | Test if Render is available for default visual |
| `XftDefaultSet` | Override Xft default properties |

**Draw**

| Function | Description |
|----------|-------------|
| `XftDrawCreateBitmap` | Create Xft draw target on 1bpp Pixmap |

**Value / value list (used by pattern internals)**

| Function | Description |
|----------|-------------|
| `XftValueDestroy` | Free a single `XftValue` |
| `XftValueListDestroy` | Free a value list |
| `XftValuePrint` | Debug-print a value |

---

## Implementation notes for `user/x11.c`

### `XScreenOfDisplay`
`DisplayWidth(dpy,scr)` and `DisplayHeight(dpy,scr)` macros expand to
`ScreenOfDisplay(dpy,scr)->width` — meaning `Screen` struct pointers are
dereferenced everywhere.  Our `Display` struct needs a `Screen screens[]` array
(or pointer) that is accessible through this macro.  This is the single most
structural gap.  Implement by embedding a `Screen` struct in our `Display`
typedef and returning `&dpy->screens[scr]` from `XScreenOfDisplay`.

### `XIfEvent` / `XPeekIfEvent`
These take a `Bool (*predicate)(Display*, XEvent*, XPointer)` callback.
Implement as iterating the event queue; `XIfEvent` removes the matching event,
`XPeekIfEvent` does not.  Both must handle the flush/block loop correctly.

### `XStringToKeysym`
Reverse map of `XKeysymToString`.  Build a static sorted table of the keysym
name strings (mirroring what `XKeysymToString` returns) and binary-search it.

### `XAutoRepeatOff` / `XAutoRepeatOn`
Sends `ChangeKeyboardControl` request with `KBAutoRepeatMode`.  Stub with a
`x6_ioctl(X6_IOCL_KEYBOARD_AUTOREPEAT, ...)` call; surface the no-op path
cleanly so the WM doesn't crash on startup.

### Color cells (`XAllocColorCells`, `XFreeColors`, `XStoreColors`, `XQueryColor/s`)
Our colormap is currently pseudo-static (TrueColor model).  fvwm2 uses
read/write colormaps for colorset animations.  For a TrueColor display stub:
- `XAllocColorCells` → return success with dummy pixels (no-op on TrueColor)
- `XFreeColors` → no-op
- `XQueryColor/s` → decompose pixel to RGB using our depth/mask tables
- `XStoreColors` → no-op on TrueColor (log a note)

### Shape extension completions
`XShapeCombineRegion` converts an `XRegion` (list of rectangles) to the
rectangle-list form and calls our existing `XShapeCombineRectangles`.
`XShapeGetRectangles` returns a stub empty list.
`XShapeInputSelected` returns False.
`XShapeOffsetShape` sends `ShapeOffset` request — stub as no-op.

### `XGetNormalHints` / `XGetSizeHints`
Legacy Xlib 10 wrappers.  Implement as:
```c
Status XGetNormalHints(Display *d, Window w, XSizeHints *h) {
    long supplied;
    return XGetWMNormalHints(d, w, h, &supplied);
}
Status XGetSizeHints(Display *d, Window w, XSizeHints *h, Atom prop) {
    /* simplified: only handle XA_WM_NORMAL_HINTS */
    long supplied;
    return XGetWMNormalHints(d, w, h, &supplied);
}
```

### `XCreateBitmapFromData`
Creates a 1-bit-deep Pixmap from packed row-major bitmap data.  Wire to
`XCreatePixmap(1bpp)` + `XPutImage` with a 1bpp XImage.

---

## Build / iteration flow

```
# First build attempt (expect link errors exposing the gaps):
make -C ports/fvwm-fvwm2-stable -f Makefile.auxv6 all 2>&1 | tee /tmp/fvwm2-build.log
grep 'undefined reference' /tmp/fvwm2-build.log | sort -u

# After implementing gaps in user/x11.c, rebuild libX11.a:
sudo make aux.kern          # rebuilds user/ including libX11.a

# Re-run fvwm2 build to verify progress:
make -C ports/fvwm-fvwm2-stable -f Makefile.auxv6 clean all 2>&1 | grep -c 'undefined reference'
```

---

## Gap count summary

| Tier | Count | Status |
|------|-------|--------|
| T1 Core WM / event loop | 13 | implemented in-tree (2026-04-19), pending runtime verification |
| T2 Color management | 5 | implemented in-tree (2026-04-19), pending runtime verification |
| T3 Text / font | 5 | implemented in-tree (2026-04-19), pending runtime verification |
| T4 Image / bitmap | 5 | implemented in-tree (2026-04-19), pending runtime verification |
| T5 Shape completions | 4 | implemented in-tree (2026-04-19), pending runtime verification |
| T6 Keyboard / screen meta | 4 | implemented in-tree (2026-04-19), pending runtime verification |
| T7 ICCCM / property helpers | 2 | implement with T5 |
| T8 Misc GC / region | 5 | implement with T5 |
| TR XRender (deferred) | 8 | deferred — re-enable `HAVE_XRENDER` after |
| TX Xft (deferred) | 37 | deferred — core fonts work without; re-enable `HAVE_XFT` after |
| **Total (active T1–T8)** | **43** | |
| **Total (deferred TR+TX)** | **45** | |
