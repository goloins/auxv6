# UI Window Contract
**Date**: April 17, 2026  
**Status**: Draft v1 (Phase 0 scaffold in progress)  
**Scope**: Managed toplevel windows in the auxv6 graphical desktop

---

## 1. Purpose

This document defines the authoritative behavior and visual contract for window management and decoration.

Goals:

1. Keep desktop behavior coherent and predictable.
2. Ensure server-side control of non-client UI (title bar, borders, window controls).
3. Provide testable rules for implementation and regression checks.

Non-goals (for this version):

1. Pixel-perfect historical reproduction of every System 7-9 variant.
2. Broad support for complex modern desktop effects.

Execution note (Phase 1A, April 17, 2026):

1. This file remains the normative v1 contract target.
2. During Phase 1A `st` bootstrap, zoom behavior may be implemented at basic level before full policy refinement.
3. During Phase 1A `st` bootstrap, modal/utility behaviors may be partial where they are non-blocking for `st` usability.
4. Any temporary deltas must be tracked in the compatibility matrix as explicit limitations.

---

## 2. Window Taxonomy

All toplevel windows must classify into one of the following roles.

1. Document Window
- Primary app content window.
- Standard full title bar and controls.

2. Utility/Palette Window
- Tool palettes and floating utility surfaces.
- Uses compact title treatment with no title text.
- Stacks above document windows of same app unless modal constraints apply.

Utility compact treatment definition (v1 locked):

1. A reduced-height title bar and tighter border insets used for small helper windows.
2. Same control semantics as document windows unless explicitly disabled by role policy.
3. Intended for tools like doodads, inspectors, and tiny utility surfaces where full document chrome is visually heavy.

3. Dialog Window
- App-scoped transient prompts.
- Usually fixed/minimally resizable.

4. Modal Dialog
- Blocks interaction with owner window (or app scope if owner absent).

5. Override/Unmanaged
- WM does not decorate or reparent.
- Reserved for menus, tooltips, and explicitly unmanaged popups.

---

## 3. Ownership Rules

1. WM owns all non-client UI for managed windows.
2. Managed windows are reparented into frame windows.
3. Client-side title bars/controls must not be considered authoritative.
4. Toolkit requests that conflict with shell policy may be ignored.
5. Unmanaged windows must be explicitly classified and audited.

---

## 4. Default Geometry and Metrics (v1 Baseline)

These values are locked v1 defaults targeting a classic Mac OS 9-style look/feel. Changes require updating tests and screenshots.

### 4.1 Frame Metrics (Document Window)

1. Border thickness: 1 px inner + 1 px outer bevel (2 px total visual edge)
2. Title bar height: 19 px
3. Content inset top (below title): 19 px
4. Content inset left/right/bottom: 2 px
5. Corner radius: 0 px (rectangular baseline for now)

### 4.2 Title Controls

Controls are left-aligned in title bar, vertically centered.

1. Close box size: 11x11 px
2. Zoom box size: 11x11 px
3. Collapse box: disabled in v1 baseline
4. Control horizontal gap: 4 px
5. Left margin from frame edge to first control: 5 px

### 4.3 Title Text

1. Font: Chicago-like bitmap UI face (fallback to closest available bitmap sans)
2. Size: 12 px equivalent baseline
3. Weight: bold for active window, normal for inactive
4. Horizontal alignment: centered in remaining title bar region
5. Truncation: ellipsis at end when overflow occurs

Utility-window title text rule:

1. Utility windows do not render title text in v1.
2. Utility title strip is purely structural/interactive chrome.

### 4.4 Palette and Bevel Baseline

1. Window chrome uses a platinum-style gray palette.
2. Title bar and border rendering must use dual-edge bevels (light top/left, dark bottom/right).
3. Active title text and controls must have stronger contrast than inactive state.

### 4.5 Utility Window Compact Metrics (v1 Locked)

1. Utility title bar height: 15 px
2. Utility border thickness: 1 px inner + 1 px outer bevel (same bevel model)
3. Utility control size: 9x9 px
4. Utility left margin to first control: 4 px
5. Utility title text: none

---

## 5. Visual States

Each managed window has two primary states.

1. Active
- Strong title contrast.
- Controls fully visible.

2. Inactive
- Lower contrast title treatment.
- Controls dimmed.

Optional future states:

1. Attention requested (visual pulse/stripe)
2. Modified document indicator

---

## 6. Input and Focus Behavior

### 6.1 Focus Model

1. Click-to-focus.
2. Focusing a window raises it within its stack class.
3. Focus follows WM policy, not toolkit preference, for managed windows.

### 6.2 Raise/Stack

1. Document windows stack in document layer.
2. Utility windows stack above owning app document windows by default.
3. Modal dialogs stack above their owner and block owner input.

### 6.3 Drag and Resize

1. Drag initiates on title bar press + move beyond threshold.
2. Resize initiates from resize affordance/hot edges according to role.
3. Dialog windows may restrict resize based on hints/policy.

### 6.4 Modal Scope Policy (v1 Locked)

1. Default modal scope is app-wide.
2. If owner-window relationship is explicit and policy requires narrower blocking, owner-window scope is allowed as an override.
3. For Phase 1 target apps, app-wide default is authoritative unless a clear breakage requires owner-window fallback.

---

## 7. Lifecycle Contract

### 7.1 Map Path (Managed)

1. Client creates toplevel.
2. WM intercepts map/configure intent.
3. WM classifies role.
4. WM creates frame.
5. WM reparents client into frame.
6. WM maps frame and client in stable order.
7. WM sends required synthetic notifications.

### 7.2 Configure Path

1. Client requests are interpreted through WM policy.
2. WM applies final geometry.
3. Client receives resolved geometry notifications.

### 7.3 Unmap/Destroy Path

1. WM updates focus and stacking safely.
2. Frame/client resources are released without orphan state.
3. Transient/modal constraints are re-evaluated after teardown.

---

## 8. Window Controls Semantics

1. Close box
- Sends graceful close intent first when supported.
- Falls back to forced close policy only when required.

2. Zoom box
- Toggles between normal geometry and policy-defined zoomed geometry.
- Phase 1A implementation status: basic maximize/restore toggle is implemented; advanced policy refinement remains deferred.

3. Collapse box
- Disabled in v1 baseline (reserved for future expansion).

Control behavior must be role-aware (for example, dialogs may hide or disable unsupported controls).

---

## 9. Role Mapping Inputs

Role classification uses available hints/properties and policy:

1. Transient relationships.
2. Window type hints.
3. Size constraints.
4. Override/unmanaged flags.
5. Toolkit adapter hints.

When hints conflict, WM policy order must be deterministic and documented.

---

## 10. Accessibility and Keyboard Baseline

v1 baseline requirements:

1. Focus ring/active indication is visible.
2. Keyboard focus traversal remains functional within client content.
3. WM-level window actions have keyboard path (close/focus cycle).

Future expansion can add richer accessibility semantics.

---

## 11. Test Requirements

No behavior change is complete without tests covering:

1. Managed map -> frame creation -> reparent correctness.
2. Focus transitions (active/inactive state updates).
3. Drag/resize interaction correctness.
4. Modal blocking behavior.
5. Window control action semantics.
6. Unmap/destroy cleanup behavior.

Visual checks:

1. Active/inactive chrome snapshots.
2. Title truncation behavior.
3. Control placement conformance.

---

## 12. Change Control

Any contract change must include:

1. Updated metric/state definitions in this file.
2. Corresponding test updates.
3. Compatibility matrix impact note.
4. Brief rationale in commit message and docs changelog.

---

## 13. Open Items

1. Implement active title-bar stripe treatment for document windows to better match platinum-era appearance (currently solid fill in scaffold).
2. Integrate Chicago-like bitmap UI font for title rendering; current fallback face is functional but not contract-accurate.
3. Refine zoom policy details (for example multi-monitor/work-area policy and role-specific constraints) beyond basic maximize/restore behavior.
4. Formalize menubar service registration protocol/atom handshake so WM reserves menubar band and binds service deterministically.
5. Add WM-side ownership validation for menu command dispatch hardening before broader untrusted-client scenarios are considered.

---

## 14. Visual Appendix (v1 Reference Layouts)

This appendix provides implementation-facing ASCII sketches for chrome geometry and hit regions.

Legend:

1. `[X]` close box
2. `[+]` zoom box
3. `====` title strip area
4. `....` content area
5. `##` frame border/bevel edge

### 14.1 Document Window (v1)

Reference metrics:

1. Title bar height: 19 px
2. Border/bevel visual edge: 2 px
3. Control size: 11x11 px
4. Left margin to first control: 5 px
5. Control gap: 4 px

```
##==============================================================##
##  [X]  [+]                  Document Title...                 ##  <- 19 px title bar
##==============================================================##
##..............................................................##
##..............................................................##
##......................... client content .....................##
##..............................................................##
##..............................................................##
##################################################################
```

Document-window hit regions:

1. Drag region: title strip area excluding control hit boxes.
2. Control region: `[X]` and `[+]` bounding rectangles.
3. Resize region: frame edges/corners per WM policy.
4. Content region: area below title bar inset, routed to client.

### 14.2 Utility/Palette Window (v1)

Reference metrics:

1. Title bar height: 15 px
2. Border/bevel visual edge: 2 px
3. Control size: 9x9 px
4. Left margin to first control: 4 px
5. Title text: none

```
##==================================##
## [X]  [+]                         ##  <- 15 px compact title strip (no title text)
##==================================##
##..................................##
##........ utility content .........##
##..................................##
######################################
```

Utility-window hit regions:

1. Drag region: compact title strip excluding controls.
2. Control region: `[X]` and `[+]` compact control boxes.
3. Resize region: optional by role policy; disabled for fixed palettes.
4. Content region: utility client area.

### 14.3 Notes for Test Review

1. Utility windows must remain visually lighter than document windows.
2. Utility windows must never render title text in v1.
3. Active/inactive state transitions must preserve control placement and box sizes.
4. Any geometry delta must update both Section 4 metrics and this appendix.

---

## 15. Implementation Status Snapshot (2026-04-17)

1. Baseline frame metrics from Section 4 are wired into `6wm` constants and used by frame/content inset calculations.
2. Managed map path is scaffolded through classify -> frame create -> reparent -> map -> synthetic configure notification.
3. Active/inactive visual state switching is wired on focus transitions, with role-aware title text suppression for utility windows.
4. Drag move and basic resize affordance paths are scaffolded and bounded to screen/menubar limits.
5. Stacking uses explicit layer ordering (document, utility, modal) with per-focus raise behavior.
6. Basic zoom maximize/restore toggle path is implemented on the zoom box.
7. Modal/transient handling now includes app-scoped modal focus blocking and transient ownership inference from WM hints/properties.
8. Owner-window override hook is implemented via `_AUX_MODAL_SCOPE_OWNER` for explicit narrower modal blocking where needed.
9. Utility/modal family-aware raise behavior is implemented so same-app palettes/modals follow active app focus more coherently.
10. Late property/hint updates on managed windows now refresh role/layer/app identity/modal-scope policy without requiring remap.
11. Client-driven configure notifications are now synchronized back through WM frame geometry policy.
12. Modal policy remains partial because remaining edge cases still require refinement against Section 6.4.
