# UI Compatibility Matrix
**Date**: April 17, 2026  
**Status**: Draft v1 (Phase 1A execution focus active)  
**Scope**: Curated compatibility subset for simple graphical application ports

---

## 1. Purpose

Track what is supported, in progress, or intentionally unsupported for the UI porting surface.

This matrix is authoritative for:

1. Port go/no-go decisions.
2. Prioritizing implementation work.
3. Preventing accidental scope creep.

Status legend:

1. Yes: implemented and validated
2. Partial: implemented with known limits
3. No: not implemented
4. N/A: intentionally out of scope for current phase

---

## 2. Target Profiles (Phase 1)

Current execution focus (April 17, 2026):

1. Prioritize WM core infrastructure and usability before menubar completion.
2. Keep visual style close to contract; defer non-blocking polish.
3. Near-term success target: `st` rendered as a managed window with basic decorations.

Phase 1 targets:

1. Terminal emulator class
2. Basic text editor class
3. Lightweight utility apps (menu/dialog-centric)

Phase 1 non-targets:

1. Browser-class applications
2. IDE-class applications
3. Electron-class stacks
4. Heavy OpenGL UI apps

---

## 3. Core Behavior Matrix

| Area | Feature | Phase 1 Requirement | Status | Owner | Notes |
|---|---|---:|---|---|---|
| WM | Reparented server-side decoration | Must | Partial | WM/Shell | Validate map/configure/unmap edge cases |
| WM | Active/inactive frame states | Must | Partial | WM/Shell | Visual snapshots required |
| WM | Drag move | Must | Partial | WM/Shell | Threshold and bounds tests pending |
| WM | Resize | Must | Partial | WM/Shell | Per-role constraints pending |
| WM | Modal transient blocking | Must | Partial | WM/Shell | App-scoped blocking plus explicit owner-window override hook are implemented; edge-policy refinement remains |
| WM | Zoom toggle behavior | Should | Partial | WM/Shell | Basic maximize/restore toggle implemented; policy refinement pending |
| WM | Utility/palette stacking policy | Should | Partial | WM/Shell | Family-aware raise improves owner-app utility ordering; full policy refinement still pending |
| Menubar | Active app switch | Must | Partial | Shell/Menu | WM now publishes active window; menubar service integration still pending |
| Menubar | Command dispatch callback | Must | No | Shell/Menu + Adapter | v1 protocol implementation |
| Menubar | Enabled/checked state refresh | Should | No | Shell/Menu + Adapter | Required for editor actions |
| Toolkit Adapter | Disable CSD path | Must | No | Adapter | Required for coherent frame ownership |
| Toolkit Adapter | Menu model export | Must | No | Adapter | First adapter defines baseline |
| Toolkit Adapter | Platform theme metrics ingestion | Should | No | Adapter | Prevent spacing drift |
| Rendering | Basic text and rect drawing path | Must | Partial | Graphics/X stack | Keep within simple-app needs |
| Input | Keyboard focus correctness | Must | Partial | WM + Input | Focus handoff plus WM key actions (Alt+Tab docs, Alt+Ctrl+Tab/Alt+Esc all windows, Alt+Q close) implemented; validation still pending |
| Input | Pointer button/motion routing | Must | Partial | WM + Input | Drag/resize reliability gate |

---

## 4. API/Behavior Subset Matrix

| Subset Item | Priority | Phase 1 | Phase 2 | Status | Notes |
|---|---:|---:|---:|---|---|
| Toplevel create/map/configure | P0 | Yes | Yes | Partial | Must be deterministic |
| Reparent + frame lifecycle | P0 | Yes | Yes | Partial | Core WM behavior |
| Focus/set-focus/focus-events | P0 | Yes | Yes | Partial | Includes active visuals |
| WM close protocol path | P0 | Yes | Yes | Partial | Graceful close preferred |
| WM zoom toggle path | P1 | Should | Yes | Partial | Basic toggle path implemented; multi-head/policy refinement pending |
| Menu publish/dispatch protocol | P0 | Yes | Yes | Partial | Atoms + focus/property bridge scaffolded in 6wm; adapter dispatch path pending |
| Dialog/transient ownership | P1 | Yes | Yes | Partial | App identity, transient ownership inference, and owner-scope modal override hook implemented; edge-policy refinement still needed before xedit gate |
| Clipboard basic text | P1 | Should | Yes | No | Add once core stable |
| Drag-and-drop | P2 | No | Optional | No | Deferred |
| Rich accessibility semantics | P2 | No | Optional | No | Deferred |
| OpenGL-integrated UI widgets | P3 | No | No | N/A | Out of scope |

---

## 5. Candidate Application Matrix

This table tracks first concrete port targets.

| App Candidate | Profile | Toolkit Class | Phase Target | Port Status | Blocking Gaps | Notes |
|---|---|---|---|---|---|---|
| st | Terminal | Xlib/lightweight | Phase 1A | In Progress | Remaining WM interaction hardening | Baseline terminal target and immediate bring-up gate |
| xedit | Editor | Xaw/Xlib | Phase 1 | Not Started | Dialog transients, menu dispatch, state sync | Spartan editor target with simple menu model |
| xeyes | Utility | Xlib/lightweight | Phase 1 | Not Started | Utility role policy, unmanaged/popup policy validation | Doodad utility target for event/lifecycle coverage |
| xfw | Utility | FOX toolkit | Deferred | Not Started | Toolkit subsystem maturity (FOX adapter work) | Deferred to avoid rushing toolkit integration layer |

Action required:

1. Add required feature rows specific to `st`, `xedit`, and `xeyes`.
2. Re-evaluate `xfw` after first adapter path is stable.

### 5.1 Per-App Required Feature Checklist

| App | Requirement | Priority | Status | Validation Notes |
|---|---|---:|---|---|
| st | Managed reparented frame with Mac-style chrome | P0 | Partial | Must show active/inactive transitions correctly |
| st | Keyboard focus and input reliability | P0 | Partial | No dropped focus after window raise/lower |
| st | Resize behavior with live content redraw | P0 | Partial | Verify no geometry desync after repeated resize |
| st | Close path (graceful then fallback) | P1 | Partial | WM close action should terminate cleanly |
| st | Zoom behavior | P2 | Partial | Basic zoom toggle path exists; advanced policy refinement deferred |
| xedit | Managed frame + title truncation behavior | P0 | Partial | Long filenames should ellipsize in title bar |
| xedit | Menu publish/dispatch integration | P0 | No | File/Edit basics routed through global menubar |
| xedit | Dialog transient + modal behavior | P0 | No | Open/Save/Confirm prompts block app scope correctly |
| xedit | Enabled/disabled menu state sync | P1 | No | State updates reflect selection/editability |
| xeyes | Utility-window role classification | P0 | No | Classified as utility/doodad role, not document |
| xeyes | Utility stacking policy correctness | P0 | No | Utility layer ordering remains stable |
| xeyes | Unmanaged/popup policy sanity | P1 | No | No accidental reparent/decorate of transient doodad surfaces |
| xeyes | Motion/event lifecycle stability | P1 | Partial | Continuous event updates do not starve WM event loop |

### 5.2 Phase 1 Smoke Test Scripts

The following smoke scripts are intended for manual run/verification in a graphical session.

#### st Smoke Script

1. Launch `st`.
2. Verify frame uses Mac-style chrome and control placement.
3. Move, resize, and refocus repeatedly.
4. Type continuously while switching focus between windows.
5. Trigger close via WM control and verify clean exit.

Phase 1A pass condition for this script:

1. `st` is visible in a managed frame with basic WM decorations.
2. Move/resize/focus interactions are stable enough for normal terminal use.

#### xedit Smoke Script

1. Launch `xedit`.
2. Verify title rendering and truncation behavior for long titles.
3. Open File/Edit menus from global menubar.
4. Trigger a dialog (open/save/confirm) and confirm modal blocking behavior.
5. Verify command dispatch reaches app actions from menubar.

#### xeyes Smoke Script

1. Launch `xeyes`.
2. Verify utility-role frame policy (or intentional unmanaged handling) is applied consistently.
3. Move and restack relative to document windows.
4. Confirm motion updates remain smooth and do not destabilize WM input/focus paths.
5. Close and relaunch repeatedly to check lifecycle cleanup.

---

## 6. Gating Criteria

### Phase 0 Exit Gates

1. This matrix has owners for all P0 features.
2. Every P0 feature has a test plan reference.

### Phase 1A Exit Gates (Current)

1. `st` launches in a managed reparented frame with basic decorations.
2. Focus, drag-move, resize, and close paths are stable for routine use.
3. Deferred visual-polish deltas are documented in this matrix or WM TODO backlog.
4. Zoom path is implemented at basic level (maximize/restore); advanced policy refinement remains a known limitation.
5. Modal and utility policy may be partial if `st` baseline behavior is unaffected.

### Phase 1B Exit Gates

1. Global menubar works for at least one adapter path.
2. Command dispatch and state refresh are functional for that path.

### Phase 1 Exit Gates

1. At least 2 representative apps pass smoke tests with coherent WM chrome.
2. Global menubar works for at least one adapter path.
3. All known unsupported behavior is documented as explicit limitations.

### Phase 2 Entry Gates

1. Phase 1 regressions are stable for two consecutive validation cycles.
2. Adapter architecture proven reusable for second app/toolkit profile.

---

## 7. Known Deferred Items

Deferred until post-Phase-1 stability:

1. Broad standards-completeness work not needed by target apps.
2. Complex compositing effects.
3. High-churn toolkit parity features.

### 7.1 Current Scaffold Snapshot (2026-04-17)

1. A first-party `6wm` skeleton now exists with modular files for atoms, drawing, client/frame lifecycle, focus, stack policy, and menu bridge.
2. Managed map/configure/unmap/destroy paths are wired with frame reparenting and synthetic configure notifications.
3. Click-to-focus, drag move, and basic resize interaction paths are implemented at scaffold level.
4. Menubar integration has initial `_NET_ACTIVE_WINDOW` publication and menu-property change nudge hooks.

### 7.2 TODO Backlog From 6wm Scaffold

1. `TODO(draw)`: replace active title solid fill with alternating platinum stripe treatment (`6wm/draw.h`).
2. `TODO(draw)`: replace fallback font with Chicago-like bitmap UI font once available (`6wm/draw.h`).
3. `TODO(menu)`: define formal menubar registration atom/protocol between menubar service and WM (`6wm/6wm.c`).
4. `TODO(menu)`: add WM-side ownership validation for `_AUX_MENU_COMMAND` dispatch hardening (`6wm/menu.c`).

---

## 8. Maintenance Rules

1. New feature requests must map to a target app profile.
2. No matrix row should be added without owner + planned validation.
3. Unsupported features are acceptable if explicitly documented.
4. Prefer deterministic subset growth over large speculative expansions.

---

## 9. Changelog

### 2026-04-17

1. Initial matrix created from UI Porting and Desktop Coherence Strategy.
2. Established Phase 1 target profiles and baseline feature ownership buckets.
3. Selected concrete Phase 1 app targets: `st`, `xedit`, and `xeyes`.
4. Marked `xfw` deferred until toolkit integration layer matures.
5. Added per-app requirement checklist and manual smoke-test scripts for Phase 1 targets.
6. Updated matrix statuses for the initial `6wm` scaffold and added an implementation TODO backlog sourced from in-tree code markers.
7. Reframed active execution to Phase 1A WM bootstrap with `st` managed-frame bring-up as the immediate gate.
8. Split gates into Phase 1A (WM usability baseline) and Phase 1B (menubar integration completion).
9. Marked zoom as explicitly stubbed for Phase 1A and tracked as deferred WM behavior.
10. Marked modal/transient ownership as acceptable partial coverage for `st`-first bring-up.
11. Updated WM zoom status from stubbed to partial after implementing a basic maximize/restore toggle path in `6wm`.
12. Implemented app-scoped modal focus blocking using WM hints/property-derived app identity; kept status partial pending owner-window override refinement.
13. Implemented explicit owner-window modal override hook (`_AUX_MODAL_SCOPE_OWNER`) and family-aware utility/modal raise behavior for active app focus.
14. Added dynamic policy refresh on late property/hint updates and configure-notify synchronization for managed windows to improve lifecycle stability.
15. Added WM keyboard action path baseline with Alt+Tab focus cycling and Alt+Q close for managed windows.
16. Hardened WM key grabs for lock/numlock modifier combinations and made focus-cycle ordering deterministic by layer.
17. Refined cycling policy for usability: document-first Alt+Tab, explicit all-window cycle paths via Alt+Ctrl+Tab and Alt+Escape.
