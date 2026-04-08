# IceWM-Driven X11 Completeness Matrix

Date: 2026-04-08
Status: Initial baseline

## Goal

Use upstream IceWM as a real workload to measure X completeness in auxv6.

This document tracks gaps at three layers:

1. Header/API surface in include/X11.
2. Xlib/Xft shim behavior in user/x11.c.
3. Server/protocol behavior in user/x6.c.

Imlib2 is treated as a first-class dependency and must be ported fully (static linking is acceptable and preferred for now).

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
| X resource manager | Missing | Missing | N/A | Blocked |
| Session management (SM/ICE) | Missing | Missing | N/A | Blocked |
| Xpm helpers | Missing | Missing | N/A | Blocked |
| Context API (XUniqueContext, etc.) | Missing/partial | Missing/partial | N/A | Blocked |
| Selection/clipboard | Partial | Partial | Partial | In progress |
| Property/ICCCM/EWMH | Partial | Partial | Partial | In progress |
| Imlib2 full library | Missing | Missing | N/A | Blocked |

## High-Priority Missing X Symbols (IceWM-Relevant)

This is the first working set to attack before deeper polish:

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

### Tranche 3: Imlib2 static port

- Port full Imlib2 needed by IceWM with static linkage model.
- Validate PNG/JPEG/XPM loader paths used by IceWM themes/icons.

Exit criteria:

- IceWM starts with themed assets via real Imlib2 paths.

## Tracking Checklist

- [ ] Tranche 0.1: Extension/session/resource header set added. (IN PROGRESS)
- [ ] Tranche 0.2: Missing declarations added for tranche-1 symbols.
- [ ] Tranche 1.1: XImage core implemented and tested.
- [ ] Tranche 1.2: FontSet primitives implemented and tested.
- [ ] Tranche 2.1: Render extension minimum path wired.
- [ ] Tranche 2.2: Composite + Damage minimum path wired.
- [ ] Tranche 2.3: Shape extension minimum path wired.
- [ ] Tranche 2.4: RandR extension minimum path wired.
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

Definition of done:

- IceWM translation units no longer fail on missing include files listed in Hard Blockers section A.

## Update Rules

When a change lands:

1. Update this matrix row status.
2. Move symbols from missing lists to implemented lists in the relevant tranche notes.
3. Record build and runtime verification command/results.
4. Keep this document current as the source of truth.
