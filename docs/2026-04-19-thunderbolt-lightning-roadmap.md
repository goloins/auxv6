# Thunderbolt and Lightning Support Plan (2026-04-19)

## Purpose

This document defines a practical landing path for Thunderbolt/USB4 and Apple
Lightning/iAP2 support in auxv6. It is intentionally scaffold-first: we add
observability and explicit runtime contracts before deep data-plane work.

## Current In-Tree Baseline

1. Thunderbolt scaffold
- `thunderbolt_init()` probes PCI Thunderbolt-class controllers.
- `/proc/thunderbolt` exposes discovered host-router metadata.

2. Lightning scaffold
- `lightning_init()` establishes a kernel-visible lifecycle anchor.
- `lightning_usb_observe(...)` is a placeholder observation hook for USB integration.
- `/proc/lightning` exposes scaffold state and counters.

3. Existing USB runtime model to leverage
- IRQ hint + consume + classify + deferred reason queue model is already in tree.
- That model is the preferred substrate for future Thunderbolt/Lightning event handling.

## Architecture Direction

### A) Thunderbolt/USB4

Near-term objective:
- Keep Thunderbolt as a controller-orchestration subsystem first.

Phases:
1. Discovery and visibility (landed)
- Probe controllers and expose `/proc/thunderbolt`.

2. Runtime event integration
- Add bounded runtime service for host-router event polling/consume.
- Reuse USB-style bounded deferred queue semantics where possible.

3. Security and policy surface
- Expose security-level and authorization-policy telemetry.
- Add policy-only enforcement stubs before enabling full tunneling.

4. Tunnel lifecycle scaffold
- Add tunnel object model and placeholder state transitions.
- Keep data movement disabled until validation path exists.

### B) Lightning/iAP2

Near-term objective:
- Treat Lightning as a USB-attached accessory protocol family.

Phases:
1. Observation and counters (landed scaffold)
- Keep `/proc/lightning` truthful and cheap.

2. USB core bridge
- Invoke `lightning_usb_observe(...)` from USB runtime attach/detach lifecycle.
- Capture VID:PID + interface tuple + endpoint hints for candidate sessions.

3. Session state scaffold
- Add minimal iAP2 session lifecycle states (none, detected, negotiating, active, failed).
- No user payload plane yet.

4. Control-plane boundaries
- Define where authentication/session policy lives between kernel and userland.

## Concrete Next Milestones

1. M1: Wire Lightning observation into USB attach/detach
- Trigger `lightning_usb_observe` for matching Apple USB candidates.
- Add counters for attach/detach candidate flow.

2. M2: Add Thunderbolt runtime service pulse
- Add timer-driven bounded runtime service similar to USB cadence.
- Export event counters and last-state snapshots.

3. M3: Add policy-only status reporting
- Thunderbolt security-level placeholders.
- Lightning session-state placeholders.

4. M4: Add regression checks
- Boot/procfs invariants for `/proc/thunderbolt` and `/proc/lightning`.
- Bounded runtime behavior checks for no-unbounded-work guarantees.

## Risks

1. Scope bleed
- Thunderbolt and Lightning can quickly expand into large transport stacks.

2. Security ambiguity
- Thunderbolt policy and Apple accessory authentication boundaries must be explicit.

3. Runtime churn
- Event-heavy attach/detach loops can destabilize scheduling if not bounded.

## Guardrails

1. Keep ISR work tiny and defer heavy work to runtime pulses.
2. Keep queueing bounded per controller/session.
3. Land telemetry before data-plane behavior.
4. Treat policy contracts as first-class interfaces, not comments.
