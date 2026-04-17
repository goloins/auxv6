# UI Window Contract
**Date**: April 17, 2026  
**Status**: Draft v1 (Phase 0 baseline)  
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

---

## 2. Window Taxonomy

All toplevel windows must classify into one of the following roles.

1. Document Window
- Primary app content window.
- Standard full title bar and controls.

2. Utility/Palette Window
- Tool palettes and floating utility surfaces.
- May use compact title treatment.
- Stacks above document windows of same app unless modal constraints apply.

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

These values are the initial baseline and can be tuned later. Changes require updating tests and screenshots.

### 4.1 Frame Metrics (Document Window)

1. Border thickness: 2 px
2. Title bar height: 18 px
3. Content inset top (below title): 18 px
4. Content inset left/right/bottom: 2 px
5. Corner radius: 0 px (rectangular baseline for now)

### 4.2 Title Controls

Controls are left-aligned in title bar, vertically centered.

1. Close box size: 11x11 px
2. Zoom box size: 11x11 px
3. Collapse box size: 11x11 px (optional in v1, may be disabled)
4. Control horizontal gap: 4 px
5. Left margin from frame edge to first control: 5 px

### 4.3 Title Text

1. Font: system UI bitmap/serif choice from platform font contract
2. Size: 12 px equivalent baseline
3. Weight: medium/bold for active window, normal for inactive
4. Horizontal alignment: centered in remaining title bar region
5. Truncation: ellipsis at end when overflow occurs

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

3. Collapse box (if enabled)
- Collapses content region while retaining title bar presence.

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

1. Exact font family/bitmap selection for the title bar.
2. Final decision on collapse box in v1.
3. Utility window compact title metrics.
4. Modal scope default (owner-window vs app-wide).
