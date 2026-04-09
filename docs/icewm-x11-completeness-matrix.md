# IceWM-Driven X11 Completeness Matrix

Date: 2026-04-08
Status: Active execution baseline

## Goal

Use upstream IceWM as a real workload to measure X completeness in auxv6.

This document tracks gaps at three layers:

1. Header/API surface in include/X11.
2. Xlib/Xft shim behavior in user/x11.c.
3. Server/protocol behavior in user/x6.c.

Imlib2 is treated as a first-class dependency and must be ported fully (static linking is acceptable and preferred for now).

Scope constraints for this roadmap:

- IceWM and Imlib2 live in `ports/` and are built as static ports.
- Do not integrate Imlib2 into the base system.
- Do not add compatibility hacks that diverge from expected X11 behavior.
- Expand/complete real X11 surface and behavior in `include/X11`, `user/x11.c`, and `user/x6.c` so ports build and run.

## Baseline (Measured)

- IceWM upstream X-call surface discovered: 222 symbols.
- X symbols implemented in user/x11.c (automated extraction): 92 symbols.
- IceWM symbols missing from local X header declarations: 119 symbols.
- IceWM symbols not found in user/x11.c definitions: 138 symbols.

Notes:

- The implementation extraction is conservative and may over-report some symbols because of multiline C signatures and parsing limits.
- Treat this as a strict starting point, then confirm per symbol while implementing.

## Hard Blockers

### A. Missing extension/session headers

The following IceWM-required headers are currently missing from include/:

- X11/extensions/Xinerama.h
- X11/extensions/Xcomposite.h
- X11/extensions/XShm.h
- X11/extensions/Xrender.h
- X11/extensions/Xdamage.h
- X11/extensions/shape.h
- X11/extensions/Xrandr.h
- X11/extensions/Xfixes.h
- X11/xpm.h
- X11/Xresource.h
- X11/SM/SMlib.h
- X11/ICE/ICElib.h

Representative IceWM include usage:

- src/yxapp.cc: Xinerama, Xcomposite, XShm
- src/ylib.h: Xfixes, Xcomposite, Xdamage, Xrender
- src/ypaint.h: shape, Xrandr
- src/ysmapp.cc: SMlib
- src/yximage.cc and src/ycursor.cc: xpm

### B. Imlib2 not present

IceWM currently uses at least the following Imlib2 APIs (31 unique symbols found):

- imlib_context_set_display
- imlib_context_set_visual
- imlib_context_set_colormap
- imlib_context_set_drawable
- imlib_context_set_image
- imlib_context_set_mask
- imlib_context_set_blend
- imlib_context_set_anti_alias
- imlib_context_set_mask_alpha_threshold
- imlib_context_get_mask
- imlib_load_image
- imlib_load_image_immediately_without_cache
- imlib_create_image
- imlib_create_image_from_drawable
- imlib_create_cropped_image
- imlib_create_cropped_scaled_image
- imlib_image_get_data
- imlib_image_get_data_for_reading_only
- imlib_image_put_back_data
- imlib_image_get_width
- imlib_image_get_height
- imlib_image_has_alpha
- imlib_image_set_has_alpha
- imlib_image_set_format
- imlib_render_image_on_drawable
- imlib_render_image_part_on_drawable_at_size
- imlib_render_pixmaps_for_whole_image
- imlib_free_pixmap_and_mask
- imlib_free_image
- imlib_save_image
- imlib_set_cache_size

## Gap Matrix (Initial)

| Area | Header Surface | x11 Shim | x6 Backend | Status |
|---|---|---|---|---|
| Core event loop | Partial | Partial | Partial | In progress |
| Window/GC basics | Partial | Partial | Partial | In progress |
| Pixel image pipeline (XImage) | Missing/partial | Missing/partial | Missing | Blocked |
| FontSet + i18n Xlib path | Missing/partial | Missing/partial | N/A | Blocked |
| Render extension | Missing | Missing | Missing | Blocked |
| Composite extension | Missing | Missing | Missing | Blocked |
| Damage extension | Missing | Missing | Missing | Blocked |
| Shape extension | Missing | Missing | Missing | Blocked |
| RandR extension | Missing | Missing | Missing | Blocked |
| Xinerama extension | Missing | Missing | Missing | Blocked |
| XFixes extension | Missing | Missing | Missing | Blocked |
| X resource manager | Partial | Partial | N/A | In progress |
| Session management (SM/ICE) | Missing | Missing | N/A | Blocked |
| Xpm helpers | Missing | Missing | N/A | Blocked |
| Context API (XUniqueContext, etc.) | Partial | Partial | N/A | In progress |
| Selection/clipboard | Partial | Partial | Partial | In progress |
| Property/ICCCM/EWMH | Partial | Partial | Partial | In progress |
| Imlib2 full library | Missing | Missing | N/A | Blocked |

## High-Priority X Symbols (IceWM-Relevant Initial Target Set)

This was the first working set to attack before deeper polish.

Many symbols in this set have now been implemented across Iteration 1 and Iteration 2; keep this section as historical targeting context and use the Iteration Log for current per-symbol landing status.

### Extension and rendering path

- XRenderCreatePicture
- XRenderFreePicture
- XRenderComposite
- XRenderFillRectangle
- XRenderFindStandardFormat
- XRenderFindVisualFormat
- XRenderSetPictureFilter
- XRenderSetPictureTransform
- XCompositeRedirectWindow
- XDamageCreate
- XDamageDestroy
- XDamageSubtract
- XShapeCombineMask
- XShapeCombineRectangles
- XShapeCombineShape
- XShapeQueryExtents
- XShapeSelectInput
- XRRQueryExtension
- XRRQueryVersion
- XRRGetScreenResources
- XRRGetOutputInfo
- XRRGetCrtcInfo
- XRRSelectInput

### Image and pixmap pipeline

- XCreateImage
- XDestroyImage
- XGetImage
- XPutImage
- XSubImage
- XGetPixel
- XPutPixel
- XCreatePixmapCursor
- XReadBitmapFileData

### Font and text path

- XCreateFontSet
- XFontsOfFontSet
- XFreeFontSet
- XLoadQueryFont
- XTextPropertyToStringList
- XTextWidth

### Context and event plumbing

- XUniqueContext
- XSaveContext
- XFindContext
- XDeleteContext
- XCheckIfEvent
- XCheckTypedWindowEvent
- XCheckWindowEvent
- XMaskEvent
- XWindowEvent

## Tranche Plan

### Tranche 0: Scaffolding and compile unblock

- Add missing extension/session/resource headers with real type/constants required by IceWM.
- Add declaration coverage for the first tranche of missing symbols.
- Keep stubs explicit and marked as not-yet-implemented where behavior is not wired.

Exit criteria:

- IceWM translation units compile through header phase without missing include/type errors.

### Tranche 1: XImage and fontset primitives

- Implement XCreateImage, XDestroyImage, XGetImage, XPutImage, XSubImage, XGetPixel, XPutPixel.
- Implement FontSet path needed by IceWM text handling.

Exit criteria:

- IceWM image and cursor/icon codepaths link against real symbols.

### Tranche 2: Render/Composite/Damage/Shape/RandR minimum viability

- Implement minimum extension behavior in x11 shim and x6 backend.
- Add protocol verbs/state to x6 for extension-backed operations.

Exit criteria:

- IceWM can run without extension-symbol crashes and can render/tray reasonably.

### Tranche 2 Tooling Track: Ports-only diagnostics

Target diagnostics are optional helper ports to validate X behavior while keeping base system unchanged:

- RandR: xrandr
- Render/composite diagnostics: xdpyinfo, xprop, xwininfo
- Resource manager path: xrdb
- Event/protocol inspection: xev, xlsclients

Policy for this track:

- Keep diagnostics in `ports/` (same policy as IceWM/Imlib2).
- Do not add these tools to base-system `UPROGS` unless explicitly requested later.
- Use them as validation workloads for X11 surface/behavior fidelity.

### Tranche 3: Imlib2 static port

- Port full Imlib2 needed by IceWM with static linkage model.
- Validate PNG/JPEG/XPM loader paths used by IceWM themes/icons.

Exit criteria:

- IceWM starts with themed assets via real Imlib2 paths.

## Tracking Checklist

- [x] Tranche 0.1: Extension/session/resource header set added.
- [x] Tranche 0.2: Missing declarations added for tranche-1 symbols.
- [x] Tranche 1.1: XImage core implemented and build-validated.
- [x] Tranche 1.2: FontSet primitives implemented and build-validated.
- [x] Tranche 2.1: Render extension minimum path wired.
- [x] Tranche 2.2: Composite + Damage minimum path wired.
- [x] Tranche 2.3: Shape extension minimum path wired.
- [x] Tranche 2.4: RandR extension minimum path wired.
- [ ] Tranche 2.F1: Extension event/fidelity semantics pass.
- [ ] Tranche 2.F2: Extension backend/protocol fidelity pass in x6.
- [ ] Tooling Track T1: xrandr port build/run validation.
- [ ] Tooling Track T2: xrdb + xprop + xwininfo port build/run validation.
- [ ] Tooling Track T3: xdpyinfo + xev + xlsclients port build/run validation.
- [ ] Tranche 3.1: Imlib2 static build integrated.
- [ ] Tranche 3.2: Imlib2 runtime paths validated with IceWM assets.

## Iteration Log

### Iteration 0 (2026-04-08)

- Established baseline metrics from upstream IceWM symbol extraction.
- Confirmed hard blockers: missing extension/session headers and missing Imlib2.
- Defined tranche structure and first high-priority symbol sets.

### Iteration 1 (current)

Target:

- Complete Tranche 0.1 by adding required header surface for extension/session/resource includes.
- Start Tranche 0.2 by adding first declaration batch for missing Xlib symbols.

Definition of done:

- IceWM translation units no longer fail on missing include files listed in Hard Blockers section A.

Delivered:

- Added headers:
	- include/X11/Xresource.h
	- include/X11/xpm.h
	- include/X11/extensions/Xinerama.h
	- include/X11/extensions/Xcomposite.h
	- include/X11/extensions/XShm.h
	- include/X11/extensions/Xrender.h
	- include/X11/extensions/Xdamage.h
	- include/X11/extensions/shape.h
	- include/X11/extensions/Xrandr.h
	- include/X11/extensions/Xfixes.h
	- include/X11/SM/SMlib.h
	- include/X11/ICE/ICElib.h
- Expanded include/X11/Xlib.h with tranche-0 declaration/types for:
	- XImage pipeline declarations
	- FontSet/font declarations
	- Context API declarations
	- Additional GC/event/window/property declarations used by IceWM
- Added include/X11/extensions/XRes.h with XRes declaration surface used by IceWM.
- Closed remaining real declaration deltas from the IceWM symbol diff.
- Implemented Tranche 1.1 XImage primitives in user/x11.c:
	- XCreateImage
	- XInitImage
	- XDestroyImage
	- XGetPixel
	- XPutPixel
	- XSubImage
	- XGetImage
	- XPutImage
- Added local client-side pixmap backing store in user/x11.c for pixmap image reads/writes.
- Implemented Tranche 1.2 FontSet runtime in user/x11.c:
	- XCreateFontSet
	- XFontsOfFontSet
	- XFreeFontSet
	- XFreeFont
	- Internal FontSet object now binds to existing font metrics/state.
- Started Tranche 2.1 (XRender minimum viability) in user/x11.c:
	- XRenderFindVisualFormat
	- XRenderFindStandardFormat
	- XRenderCreatePicture
	- XRenderFreePicture
	- XRenderComposite (minimum src/over path via existing XCopyArea)
	- XRenderFillRectangle
	- XRenderSetPictureFilter (no-op placeholder)
	- XRenderSetPictureTransform (no-op placeholder)
	- Added local picture-state tracking for render picture handles.
- Started Tranche 2.2 (XComposite + XDamage minimum viability) in user/x11.c and extension headers:
	- XCompositeQueryExtension
	- XCompositeQueryVersion
	- XCompositeRedirectWindow
	- XCompositeUnredirectWindow
	- XCompositeNameWindowPixmap
	- XDamageQueryExtension
	- XDamageQueryVersion
	- XDamageCreate
	- XDamageDestroy
	- XDamageSubtract
	- Added local damage and composite redirect state tracking.
- Started Tranche 2.4 (XRandR minimum viability) in user/x11.c:
	- XRRQueryExtension
	- XRRQueryVersion
	- XRRGetScreenResources
	- XRRFreeScreenResources
	- XRRGetOutputInfo
	- XRRFreeOutputInfo
	- XRRGetCrtcInfo
	- XRRFreeCrtcInfo
	- XRRSelectInput
	- XRRUpdateConfiguration
- Started Tranche 2.3 (XShape minimum viability) in user/x11.c:
	- XShapeCombineMask
	- XShapeCombineShape
	- XShapeCombineRectangles
	- XShapeQueryExtents
	- XShapeSelectInput
	- XShapeQueryExtension
	- XShapeQueryVersion
	- Added per-window shape-state tracking for bounding/clip extents and selected shape event mask.
- Added extension-compat probe/runtime floors in user/x11.c:
	- XineramaQueryExtension
	- XineramaQueryVersion
	- XineramaIsActive
	- XineramaQueryScreens
	- XFixesQueryExtension
	- XFixesQueryVersion
	- XShmQueryExtension
	- XShmCreateImage
	- XShmAttach
	- XShmDetach
	- XShmPutImage
	- XShmGetImage
	- XResQueryClientIds
	- XResGetClientPid
	- XResClientIdsDestroy
- Started Tranche 2.F1 extension event/fidelity semantics pass:
	- Extension query functions now return stable nonzero event/error bases for:
		- Composite
		- Damage
		- RandR
		- Shape
		- XFixes
		- Xinerama
	- Shape operations now emit synthetic ShapeNotify events into the local event queue when selected via XShapeSelectInput.
	- RandR select input now tracks per-window masks and emits an initial synthetic screen-change event when RRScreenChangeNotifyMask is selected.
	- Damage objects now emit synthetic extension events when tracked drawables are mutated (XPutImage/XRender paths and damage create/subtract transitions).
	- Extension event mask routing now includes ShapeNotify and RandR change masks in local event-mask resolution.
	- XRRUpdateConfiguration now returns success only for RandR extension events.
	- Synthetic extension events now carry consistent serial/send-event metadata.
	- Synthetic ShapeNotify events now include monotonically increasing synthetic timestamps.
- Started Tranche 2.F2 backend/protocol fidelity pass in x6:
	- x6 now enqueues Expose notifications for window draw mutations driven by:
		- DRAW_RECT (window targets)
		- DRAW_TEXT (window targets)
		- COPY_AREA destination window updates
	- Expose delivery is gated by the window's registered ExposureMask in x6 event-mask state.

Validation evidence:

- Build: make _dwm _st _x6 -> success (exit 0).
- Header smoke compile (all new headers included in one TU) -> success (exit 0).
- Forced rebuild: make -B _st -> success (exit 0).
- Forced rebuild: make -B _dwm -> success (exit 0).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post FontSet tranche).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post FontSet tranche).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post XRender tranche start).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post XRender tranche start).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post XRender tranche start).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post Composite/Damage tranche start).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post Composite/Damage tranche start).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post Composite/Damage tranche start).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post RandR tranche start).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post RandR tranche start).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post Shape tranche start).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post Shape tranche start).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post Shape tranche start).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post Xinerama/XFixes/Shape-query compatibility pass).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post Xinerama/XFixes/Shape-query compatibility pass).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post Xinerama/XFixes/Shape-query compatibility pass).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post XRes/XShm compatibility pass).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post XRes/XShm compatibility pass).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post XRes/XShm compatibility pass).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post XShm API expansion pass).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post XShm API expansion pass).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post XShm API expansion pass).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post extension event/fidelity semantics pass).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post extension event/fidelity semantics pass).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post extension event/fidelity semantics pass).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post damage/randr event-fidelity pass).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post damage/randr event-fidelity pass).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post damage/randr event-fidelity pass).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post synthetic event metadata pass).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post synthetic event metadata pass).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post synthetic event metadata pass).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post typed extension-event payload pass).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post typed extension-event payload pass).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post typed extension-event payload pass).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post XShm extension-availability pass).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post XShm extension-availability pass).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post XShm extension-availability pass).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post XShm completion-event pass).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post XShm completion-event pass).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post XShm completion-event pass).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post x6 expose-region clipping pass).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post x6 expose-region clipping pass).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post x6 expose-region clipping pass).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post x6 expose coalescing/order pass).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post x6 expose coalescing/order pass).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post x6 expose coalescing/order pass).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post backend damage-notify path pass).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post backend damage-notify path pass).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post backend damage-notify path pass).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post backend shape-notify path pass).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post backend shape-notify path pass).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post backend shape-notify path pass).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post backend damage-region fidelity pass).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post backend damage-region fidelity pass).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post backend damage-region fidelity pass).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post backend randr-notify path pass).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post backend randr-notify path pass).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post backend randr-notify path pass).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post mixed-event ordering/de-dup pass).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post mixed-event ordering/de-dup pass).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post mixed-event ordering/de-dup pass).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post randr map/unmap coverage pass).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post randr map/unmap coverage pass).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post randr map/unmap coverage pass).
- Forced rebuild: make -B _st -> ST_EXIT:0 (post unsolicited ShapeNotify regression fix pass).
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post unsolicited ShapeNotify regression fix pass).
- Forced rebuild: make -B _x6 -> X6_EXIT:0 (post unsolicited ShapeNotify regression fix pass).
- Forced rebuild: make -B _x6 -> X6_EXIT:2 then fixed (`win` initialization in x6 command handler), re-run -> X6_EXIT:0.
- Forced rebuild: make -B _dwm -> DWM_EXIT:0 (post x6 expose-fidelity pass).
- Declaration diff now only shows parser-noise tokens (XFV/XIV/XSV/XRectangle/XRenderColor), no real missing function declarations.

Remaining in this iteration:

- Complete Tranche 2.F1/F2 fidelity work: extension event semantics and x6 backend behavior alignment.
- Continue providing X functionality needed by downstream image libraries (Imlib2 dependency path) while keeping Imlib2 in ports.

### Iteration 2 (2026-04-09)

Direction lock-in:

- Confirmed roadmap policy: Imlib2 and IceWM remain ports-only static builds.
- Confirmed non-goal: no base-system Imlib2 integration.
- Confirmed quality bar: no non-X11 compatibility hacks; fix behavior to match expected X11 semantics.

Delivered:

- Added Xlib surface for extension probing:
	- `XQueryExtension` declaration in `include/X11/Xlib.h`.
	- `XQueryExtension` implementation in `user/x11.c` with stable metadata for Composite, Damage, RandR, Shape, Xinerama, XFixes, MIT-SHM, Render, and XRes.
- Added X resource manager runtime surface (`Xrm`) for ports-side consumers:
	- `include/X11/Xresource.h` now declares `XrmInitialize`, `XResourceManagerString`, `XrmGetStringDatabase`, `XrmDestroyDatabase`, `XrmGetResource`, `XrmPutStringResource`, `XrmMergeDatabases`.
	- `user/x11.c` now implements the same APIs with an in-memory Xrm database parser/lookup and root `RESOURCE_MANAGER` retrieval via X properties.
- Added missing core/Xutil utility surfaces used by downstream X11 consumers:
	- `XCreatePixmapCursor` implemented in `user/x11.c` with stable client-side cursor id allocation.
	- `XTextPropertyToStringList` implemented in `user/x11.c` with proper NUL-split list expansion.
	- `XReadBitmapFileData` implemented in `user/x11.c` with XBM `#define` metadata parsing and hex payload decode.
- Added event/context primitives needed by toolkit-style consumers:
	- `XCheckIfEvent` implemented in `user/x11.c` with predicate matching over queued and newly pending events.
	- `XUniqueContext`, `XSaveContext`, `XFindContext`, `XDeleteContext` implemented in `user/x11.c` with table-backed per-resource context storage.
- Added additional core Xlib utility coverage for compatibility and correctness:
	- `XEventsQueued` implemented in `user/x11.c` using queued-event accounting with non-blocking pending refresh.
	- `XCreateRegion` and `XUnionRectWithRegion` implemented in `user/x11.c` with simple region-extents union semantics.
	- `XBlackPixel` and `XWhitePixel` implemented in `user/x11.c` with canonical 24-bit defaults.
	- `XBell`, `XAddToSaveSet`, and `XRemoveFromSaveSet` implemented in `user/x11.c` as explicit compatibility no-op success paths.
- Expanded GC state/ops coverage for compatibility with toolkit draw pipelines:
	- `XChangeGC` implemented in `user/x11.c` with valuemask-driven state updates.
	- `XSetBackground`, `XSetFillStyle`, `XSetFunction`, `XSetTile`, `XSetTSOrigin`, `XSetClipMask`, `XSetClipRectangles`, and `XSetDashes` implemented in `user/x11.c` with tracked GC state.
	- `XCreateGC` now consumes compatible `XGCValues` fields from `valuemask` at creation time.
- Added missing drawing/clearing helper surface using existing x6 primitives:
	- `XDrawLine`, `XDrawLines`, `XDrawSegments`, `XDrawPoint` implemented as compatibility wrappers on top of rectangle rasterization.
	- `XDrawArc`, `XDrawRectangles`, `XFillArc`, `XFillArcs`, `XFillPolygon`, `XFillRectangles` implemented with rectangle-based fallback behavior.
	- `XClearArea` and `XClearWindow` implemented using drawable-size-aware clear rectangle emission.
	- `XCopyPlane` implemented as `XCopyArea` compatibility wrapper.
- Added geometry/state query helpers that higher-level clients commonly depend on:
	- `XGetGCValues` implemented in `user/x11.c` with valuemask-driven reads from tracked GC state.
	- `XGetGeometry` implemented in `user/x11.c` for pixmaps and windows via existing drawable/window attribute paths.
	- `XTranslateCoordinates` implemented in `user/x11.c` and refined to translate through source/destination root-space origins from live window attributes.
	- `XTranslateCoordinates` now also reports `child_return` for root-destination queries via server-backed topmost hit-testing at translated coordinates.
	- `XTranslateCoordinates` now also reports `child_return` for non-root destination queries via parent-scoped child hit-testing (`QUERY_CHILD_AT`) at translated root coordinates.
- Improved behavior fidelity for key draw primitives (replacing coarse rectangle fallbacks):
	- `XDrawLine` now uses integer Bresenham rasterization.
	- `XDrawArc` and `XFillArc` now use integer ellipse rasterization/fill paths with angle-sector clipping via X11 `angle1`/`angle2` units (1/64 degree).
	- `XFillPolygon` now uses scanline edge-intersection filling instead of bounding-box fill.
- Hardened WM redirect ownership stability in x6:
	- `REQUEST_REDIRECT` is now idempotent for the current WM client fd, preventing WM-mode demotion during repeated redirect claims.
- Added hierarchy-aware x6 window query semantics needed for non-root child resolution:
	- `CREATE` now accepts/records parent id in `user/x6.c` (with root fallback for invalid parents).
	- Added `QUERY_CHILD_AT <parent> <x> <y>` in `user/x6.c` and wired `user/x11.c` to use it from `XTranslateCoordinates`.
- Added usermode runtime validation utility for startup-console verification:
	- Added `xwmselftest` in `user/xwmselftest.c` to exercise `XTranslateCoordinates` root/non-root child-hit semantics with PASS/FAIL console output.
	- Wired `targetfs/root/.xinitrc` to run `xwmselftest` before WM launch so validation is possible without additional target ports.
- Fixed startup/runtime regressions found by `xwmselftest`:
	- `XCloseDisplay` in `user/x11.c` now sends `DETACH` (not `QUIT`) so closing one client does not terminate the x6 server before WM startup.
	- `x6` hit-testing (`x6_pick_window_at` / `x6_pick_child_at`) now resolves window geometry in root-space using parent-chain origins, fixing non-root child lookup in `QUERY_CHILD_AT`.

Validation evidence:

- Forced rebuild: `make -B _x6 _dwm _st _xinit` -> success (exit 0) after XQueryExtension surface addition.
- Forced rebuild: `make -B _x6 _dwm _st _xinit` -> success (exit 0) after Xrm surface addition.
- Forced rebuild: `make -B _x6 _st _xinit` -> success (exit 0) after `XCreatePixmapCursor` / `XTextPropertyToStringList` / `XReadBitmapFileData` addition.
- Forced rebuild: `make -B _x6 _st _xinit` -> success (exit 0) after `XCheckIfEvent` / context API (`XUniqueContext`/`XSaveContext`/`XFindContext`/`XDeleteContext`) addition.
- Forced rebuild: `make -B _x6 _st _xinit` -> success (exit 0) after `XEventsQueued` / region helpers (`XCreateRegion`/`XUnionRectWithRegion`) / utility APIs (`XBlackPixel`/`XWhitePixel`/`XBell`/save-set) addition.
- Forced rebuild: `make -B _x6 _st _xinit` -> success (exit 0) after GC API tranche (`XChangeGC`, `XSetBackground`, `XSetFillStyle`, `XSetFunction`, `XSetTile`, `XSetTSOrigin`, `XSetClipMask`, `XSetClipRectangles`, `XSetDashes`) addition.
- Forced rebuild: `make -B _x6 _st _xinit` -> success (exit 0) after draw-helper tranche (`XDrawLine`, `XDrawLines`, `XDrawSegments`, `XDrawPoint`, `XDrawArc`, `XDrawRectangles`, `XFillArc`, `XFillArcs`, `XFillPolygon`, `XFillRectangles`, `XClearArea`, `XClearWindow`, `XCopyPlane`) addition.
- Forced rebuild: `make -B _x6 _st _xinit` -> success (exit 0) after geometry/query tranche (`XGetGCValues`, `XGetGeometry`, `XTranslateCoordinates`) addition.
- Forced rebuild: `make -B _x6 _st _xinit` -> success (exit 0) after fidelity rasterization pass (`XDrawLine` Bresenham, `XDrawArc`/`XFillArc` ellipse paths, `XFillPolygon` scanline fill).
- Forced rebuild: `make -B _x6 _st _xinit` -> success (exit 0) after angle-aware arc clipping pass (`XDrawArc`/`XFillArc` honoring `angle1`/`angle2`).
- Forced rebuild: `make -B _x6 _st _xinit` -> success (exit 0) after `XTranslateCoordinates` semantic refinement (source/destination root-space offset translation).
- Forced rebuild: `make -B _x6 _st _xinit` -> success (exit 0) after `XTranslateCoordinates` root child hit-testing refinement (`child_return` via `QUERY_WINDOW_AT`).
- Forced rebuild: `make -B _x6 _st _xinit` -> success (exit 0) after parent-aware create + non-root child hit-testing refinement (`QUERY_CHILD_AT` path in `XTranslateCoordinates`).
- Forced rebuild: `make -B _xwmselftest _xinit` -> success (exit 0) after adding xinitrc-driven usermode child-hit self-test utility.
- Forced rebuild: `make -B _x6 _xwmselftest _xinit _dwm` -> success (exit 0) after fixing `XCloseDisplay` detach semantics and root-space child hit-testing.
- Forced rebuild: `make -B _x6 _dwm _st _xinit` -> success (exit 0) after redirect idempotency fix.
- Image staging: `make test_ext2.img` -> success (exit 0) after redirect idempotency fix.

Current Tranche 2.1 status:

- Complete for minimum path: shim-side symbol/runtime floor implemented.
- Pending: protocol/backend fidelity and focused runtime validation with an IceWM workload.

Current Tranche 2.2 status:

- Complete for minimum path: shim-side symbol/runtime floor implemented.
- Pending: backend behavior fidelity for redirected window content and damage event semantics.

Current Tranche 2.4 status:

- Complete for minimum path: shim-side symbol/runtime floor implemented.
- Pending: mode/output modeling fidelity and event emission semantics.

Current Tranche 2.3 status:

- Complete for minimum path: shim-side symbol/runtime floor implemented.
- Pending: backend/event-path fidelity for full shape notify semantics.

Current Tranche 2.F1 status:

- In progress: extension query and event-base semantics are now stable; synthetic shape/randr/damage event delivery is wired.
- Progress: Damage and RandR synthetic events now use base+opcode event typing and carry typed payload fields (damage id/drawable/area/geometry/timestamp/level and RandR screen-change root/geometry/timestamps).
- Progress: XShm extension query now reports availability, aligning advertised capability with implemented XShmCreateImage/XShmPutImage/XShmGetImage wrapper behavior.
- Progress: XShm completion semantics now include event base API (`XShmGetEventBase`) plus synthetic ShmCompletion emission on `XShmPutImage(..., send_event=True)` with tracked shm segment metadata.
- Pending: align extension event delivery ordering and backend-coupled semantics with deeper real-world client expectations.

Current Tranche 2.F2 status:

- In progress: x6 now emits Expose events for core draw/copy mutation paths with event-mask gating.
- Progress: x6 Expose event regions are now clipped to per-window local bounds before delivery, reducing overbroad/out-of-window invalidation reports.
- Progress: x6 now coalesces pending Expose events per window into bounding unions when no newer MapNotify/ConfigureNotify transition intervenes.
- Progress: on MapNotify/ConfigureNotify enqueue paths, stale pending Expose events for that window are cleared and a transition-ordered full-window Expose is queued afterward.
- Progress: x6 now emits backend-originated `DamageNotify` events for window draw mutations (DRAW_RECT, DRAW_TEXT, COPY_AREA), and x11 maps those wire events into XDamage extension notifications.
- Progress: x6 now emits backend-originated `ShapeNotify` events for map/unmap/configure/override transitions, and x11 maps those wire events into shape-state updates plus extension ShapeNotify delivery.
- Progress: backend `DamageNotify` rectangle payloads are now preserved into XDamage event areas in x11 (with clipping/fallback handling), instead of always collapsing to full-drawable area.
- Progress: x6 now emits backend-originated `RandRNotify` events on mapped geometry reconfiguration, and x11 maps those into subscription-aware RandR screen-change extension events.
- Progress: mixed core/extension queue semantics now clear stale extension notifications before map/configure transitions, merge pending Damage rectangles, and upsert pending RandR notifications per window.
- Progress: backend RandR notify coverage now includes map/unmap transitions in addition to configure-time updates.
- Progress: fixed unsolicited `ShapeNotify` handling so x11 no longer logs/queues uninitialized type-0 events and no longer risks re-entrant command traffic while parsing backend extension events.
- Pending: complete focused runtime validation pass and close out Tranche 2.F1/2.F2 status gates.

Runtime closeout checklist for Tranche 2.F1/2.F2 (user-run in guest/QEMU console):

- [ ] Start X stack and WM path (`x6`, `dwm`, and a client workload such as `st`) and confirm no startup extension errors in console/debug logs.
- [ ] Exercise resize/configure churn on at least one mapped client window and confirm:
	- [ ] ConfigureNotify arrives before post-transition redraw notifications.
	- [ ] RandR screen-change updates are observed for mapped geometry transitions.
- [ ] Exercise repeated draw operations on the same window and confirm:
	- [ ] Expose and Damage notifications are coalesced/de-duplicated under burst updates.
	- [ ] Damage areas reflect backend rectangle payloads (not always full drawable).
- [ ] Exercise map/unmap transitions and confirm:
	- [ ] Shape notifications reflect shaped/unshaped transition semantics.
	- [ ] RandR notify path remains stable across map/unmap cycles.
- [ ] Exercise clipboard/property activity and confirm no regressions in PropertyNotify / Selection events while extension event traffic is active.
- [ ] Capture serial/debug evidence for one complete run and append summary outcomes here.

Completion gate:

- When all checklist items above are green with captured evidence, mark both `Tranche 2.F1` and `Tranche 2.F2` as complete in the Tracking Checklist.

## Update Rules

When a change lands:

1. Update this matrix row status.
2. Move symbols from missing lists to implemented lists in the relevant tranche notes.
3. Record build and runtime verification command/results.
4. Keep this document current as the source of truth.
