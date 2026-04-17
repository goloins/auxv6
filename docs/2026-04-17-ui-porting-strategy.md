# UI Porting and Desktop Coherence Strategy
**Date**: April 17, 2026  
**Objective**: Make porting plausible while enforcing a coherent classic Mac-style desktop (System 7-9 inspired), without chasing broad mainstream app compatibility.  
**Status**: Strategy baseline (Phase 1A WM bootstrap in progress)

---

## Executive Summary

This project is explicitly optimizing for:

1. A coherent native desktop look/behavior controlled by auxv6.
2. A practical porting path for simple graphical applications (terminal emulators, basic editors, lightweight utilities).
3. Incremental expansion from a constrained compatibility subset to a broader (still curated) subset over time.

This project is explicitly not optimizing for:

1. Running arbitrary upstream Linux desktop applications unchanged.
2. Full GTK/Qt parity.
3. Broad "mainstream app" compatibility in the near or medium term.

Core principle:

- Build and own a focused toolkit/runtime surface and desktop contract that fits auxv6 goals.
- Avoid maintaining deep long-lived forks of multiple large third-party toolkits where possible.
- Patch/tool adapters only at high-leverage integration points when needed.

---

## Strategy Model

This plan uses a three-lane model that can scale from constrained compatibility (Phase 1) to broader curated compatibility (Phase 2+) without a full rewrite.

### Lane 1: Native Shell and WM Authority

Own and enforce desktop behavior at the WM/shell layer:

1. Server-side decorations (title bar, borders, controls).
2. Window lifecycle policy (map/configure/focus/stack/modality/transients).
3. Global top menubar ownership and composition.
4. Desktop visual contract (fonts, metrics, colors, interaction patterns).

Outcome:

- Coherent desktop identity independent of individual app toolkit choices.

### Lane 2: Toolkit Integration Adapters

Use toolkit-specific adapters/patches only at high-value choke points:

1. Disable client-side decoration paths.
2. Export menu trees/actions to global menubar service.
3. Honor platform metrics/theme contract.
4. Route platform dialogs/settings through auxv6-native policy.

Outcome:

- Ported apps behave coherently without rewriting each app.

### Lane 3: Focused Compatibility Subimplementation

Build a constrained compatibility surface for target app classes:

1. Implement only required APIs/behaviors used by selected ports.
2. Stub/no-op unsupported features intentionally.
3. Track support explicitly in a compatibility matrix.

Outcome:

- Lower long-term maintenance than deep forking many large upstream codebases.

---

## Non-Goals and Scope Guardrails

To prevent scope creep, these are hard boundaries for now.

### Hard Non-Goals (Current Horizon)

1. Full GTK/Qt API or ABI compatibility.
2. Full ICCCM/EWMH parity beyond what target apps require.
3. Complex app classes (modern browsers, IDEs, Electron-scale stacks, heavy OpenGL UI suites).
4. "Run arbitrary X11 desktop software" as a success criterion.

### Allowed Scope (Current Horizon)

1. Small to medium traditional desktop apps.
2. Conventional widgets and menu-driven command surfaces.
3. Deterministic "known subset" behavior over speculative broad compatibility.

---

## Compatibility Promise

The compatibility promise must be explicit and versioned.

### Promise v1 (Initial)

"auxv6 supports porting selected simple graphical applications through a constrained, documented compatibility subset. Unsupported features may be stubbed or omitted."

### Promise v2 (Future, Optional)

"auxv6 supports a broader curated subset of desktop applications by expanding the compatibility matrix and toolkit adapters, while preserving coherent desktop behavior as top priority."

This establishes 1 as a stepping stone to 2, not a contradictory direction.

---

## Architecture Contract

### 1. WM and Shell Own Non-Client UI

The WM is authoritative for:

1. Reparent/frame policy for managed toplevel windows.
2. Title bar layout and controls.
3. Active/inactive visual state.
4. Move/resize/focus behaviors.
5. Modal/transient policy.

### 2. Menu Ownership Model

Global menubar is a system service.

1. Active application publishes menu model via integration adapter.
2. Menubar service renders top-of-screen menu.
3. Command dispatch routes back to the app/toolkit.

Important constraint:

- Menus can be centralized reliably.
- Arbitrary in-window controls cannot be generically "hoisted" without deeper app cooperation.

### 3. Toolkit Role

Toolkits are integration points, not desktop authorities.

1. Toolkit adapters supply semantics to the desktop (menus, hints, roles).
2. Toolkit adapters consume platform contract (theme metrics, dialogs, policy).
3. Toolkit internals should not override shell-level policy.

---

## Phased Execution Plan

## Phase 0: Contracts, Metrics, and Test Baseline

### Goals

1. Freeze desktop contract before implementation drift.
2. Define measurable acceptance criteria.

### Deliverables

1. Window chrome spec (pixel metrics, title control placement, active/inactive states).
2. Interaction spec (focus, drag, resize, modality, close/zoom behavior).
3. Menu protocol spec (export format and command return path).
4. Initial compatibility matrix template.

### Exit Criteria

1. Every planned implementation task references a contract section.
2. Every contract section has at least one validation test case.

---

## Phase 1: Simple Apps First (Primary Near-Term Target)

Execution note (updated April 17, 2026):

1. Immediate priority is WM core usability and infrastructure completion.
2. Visual fidelity should stay close to the contract, but pixel-perfect polish is explicitly deferred.
3. Menubar logic remains core, but WM infrastructure should be driven as far as possible before menubar completion becomes the blocker.

### Phase 1A: WM Bootstrap Milestone (Current Focus)

Goals:

1. Finish core WM interaction and lifecycle paths needed for daily usability.
2. Validate first real app bring-up with minimal coherent chrome.

Deliverables:

1. Stable managed window map/configure/unmap flow with reparented frames.
2. Basic decorations rendered consistently (title bar, border, close/zoom affordances).
3. Reliable focus, move, and resize behavior for the first app target.

Exit criteria:

1. `st` launches and renders in a managed frame with basic decorations.
2. `st` remains usable under repeated focus, move, and resize operations.
3. Known visual deltas versus contract are documented as deferred polish items.
4. Zoom behavior may remain stubbed if it does not impact `st` baseline usability.
5. Modal/utility policy may remain partial when not required by `st` bring-up; gaps must be documented.

### Phase 1B: Menubar Integration Milestone

Goals:

1. Complete menubar protocol flow on top of stable WM core.
2. Validate app command dispatch for at least one adapter path.

### Goals

1. Make terminals and simple editors portable with coherent behavior.
2. Validate architecture and expose X/WM gaps quickly.

### Deliverables

1. Stable server-side decoration pipeline.
2. WM lifecycle correctness for target apps.
3. Global menubar path for at least one integration adapter.
4. Basic toolkit/platform compatibility subset sufficient for first targets.

### Target App Profiles

1. Terminal emulator.
2. Basic text editor.
3. Lightweight utility apps with standard menus/dialogs.

### Exit Criteria

1. At least 2 to 3 representative apps run with coherent chrome and interactions.
2. Top menubar works for these apps through adapter path.
3. Known unsupported features are documented, not hidden.

---

## Phase 2: Curated Expansion (Optional Step-Up)

### Goals

1. Broaden compatibility subset while preserving desktop coherence.
2. Expand from "few simple apps" to "curated set of practical apps".

### Deliverables

1. Expanded compatibility matrix with per-feature support states.
2. Additional adapter coverage (as justified by target apps).
3. Improved property/hint handling required by next app tier.

### Exit Criteria

1. New app ports require bounded, predictable adaptation effort.
2. No regression in WM/shell coherence from expanded compatibility work.

---

## Phase 3: Deferred Advanced Features (Long Horizon)

Deferred until earlier phases are stable:

1. Complex rendering paths.
2. High-complexity desktop integration features.
3. Broader standards coverage not required by curated targets.

---

## Maintenance Economics Plan

To avoid unsustainable maintenance costs, use these rules:

1. Prefer owning small compatibility surfaces over maintaining deep forks.
2. Patch upstream toolkits only where leverage is high and churn is manageable.
3. Keep patch sets minimal, layered, and well-documented.
4. Pin toolkit baselines when needed; update only on planned cadence.
5. Promote recurring patches into aux-owned integration abstractions over time.

Decision heuristic:

- If a behavior can be enforced at WM/shell level, do it there.
- If it requires app semantics shared by many apps, do it in toolkit adapter.
- If it is rare or app-specific, patch app or leave unsupported.

---

## Licensing and Risk Posture

General policy (non-legal advice):

1. Minimize deep long-lived forks to reduce legal and operational burden.
2. Keep third-party modifications isolated and traceable.
3. Maintain provenance for all imported/modified code.
4. Prefer clean, aux-owned integration interfaces for long-term stability.

---

## Required Artifacts

Track these docs as first-class project assets.

1. `docs/ui-window-contract.md` (window behavior + visuals).
2. `docs/ui-menu-protocol.md` (global menubar integration).
3. `docs/ui-compat-matrix.md` (feature/app support table).
4. `docs/ui-porting-guide.md` (how to port apps into the supported subset).

Note: These files may be added incrementally; this strategy doc defines their purpose.

---

## Validation and Gating

### Gating Principle

No new compatibility feature lands without:

1. Contract reference.
2. Test coverage (unit/integration/self-test as appropriate).
3. Matrix update.
4. Clear unsupported behavior notes.

### Suggested Test Buckets

1. WM lifecycle tests (map/configure/reparent/focus/unmap/destroy).
2. Menubar export/dispatch tests.
3. Visual consistency snapshots for window chrome.
4. Ported app smoke tests for each supported profile.

---

## Immediate Next Actions (Recommended)

1. Complete WM core interaction paths (focus, move, resize, close, lifecycle hardening).
2. Validate Phase 1A bring-up with `st` as the first real target.
3. Record any visual deltas as deferred polish, not blockers.
4. Begin menubar completion work only when WM core stability reaches daily-usable baseline.
5. After `st` baseline passes, proceed to `xedit` and `xeyes` smoke validation.

## Execution Updates

### 2026-04-17

1. Prioritized WM infrastructure completion as the immediate execution focus.
2. Defined Phase 1A bootstrap milestone (`st` with basic managed decorations) before full menubar completion.
3. Clarified that close visual fidelity is desired, while non-blocking polish remains deferred.

---

## Decision Record

Adopted strategy:

1. Three-lane model (native shell authority, toolkit adapters, focused compatibility subset).
2. "Simple apps first" as explicit initial target.
3. 1 is a stepping stone to 2 (curated expansion), not a competing objective.
4. Broad mainstream compatibility is out of scope.

This decision should be revisited only after Phase 1 exit criteria are met.
