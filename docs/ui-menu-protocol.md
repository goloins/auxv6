# UI Menu Protocol
**Date**: April 17, 2026  
**Status**: Draft v1 (Phase 0 baseline)  
**Scope**: Global top menubar integration between applications/toolkit adapters and shell service

---

## 1. Purpose

Define a stable, minimal protocol so applications can publish menu semantics to the global menubar and receive command activation callbacks.

Design goals:

1. Keep protocol small and deterministic.
2. Avoid requiring per-application bespoke integration.
3. Work through standard X-compatible primitives (properties, atoms, client messages).
4. Allow adapters for multiple toolkits.

---

## 2. Model Overview

Actors:

1. App Process
- Owns actual command handlers.
- Uses toolkit adapter to publish menu model.

2. Toolkit Adapter
- Extracts semantic menu tree from toolkit internals.
- Publishes to protocol fields.

3. Menubar Service (Shell)
- Renders top menu bar.
- Tracks active app/window.
- Dispatches selected commands back to app.

4. WM Integration
- Provides active-window/app transitions to menubar service.

---

## 3. Transport and Encoding (v1)

### 3.1 Transport

Protocol uses X atoms/properties on each app toplevel window.

Required atoms:

1. _AUX_MENU_VERSION (CARDINAL)
2. _AUX_MENU_SERIAL (CARDINAL)
3. _AUX_MENU_MODEL (UTF8_STRING)
4. _AUX_MENU_STATE (UTF8_STRING)
5. _AUX_MENU_CAPS (UTF8_STRING, optional)
6. _AUX_MENU_COMMAND (ClientMessage dispatch channel)
7. _AUX_MENU_COMMAND_TEXT (UTF8_STRING transient command payload)

### 3.2 Encoding

v1 uses UTF-8 text payload with line-delimited records for implementation simplicity.

Benefits:

1. Easy to debug from tracing tools.
2. Minimal parser complexity.
3. Stable enough for simple app targets.

Future v2 can introduce compact binary encoding if needed.

---

## 4. Data Schema

### 4.1 _AUX_MENU_MODEL (Structure)

Record types:

1. MENU
- Fields: menu_id, parent_id, label, ordinal

2. ITEM
- Fields: item_id, menu_id, label, command_id, kind
- kind: normal | check | radio | separator | submenu

3. SUBMENU_LINK
- Fields: item_id, submenu_menu_id

Example (illustrative):

MENU|m_file|root|File|0
ITEM|i_new|m_file|New|cmd.new|normal
ITEM|i_open|m_file|Open...|cmd.open|normal
ITEM|i_sep1|m_file|-|none|separator
ITEM|i_quit|m_file|Quit|cmd.quit|normal

### 4.2 _AUX_MENU_STATE

Dynamic state for enablement/checked values.

Record types:

1. ENABLED
- Fields: command_id, 0|1

2. CHECKED
- Fields: command_id, 0|1

3. RADIO_GROUP
- Fields: group_id, command_id_active

Example:

ENABLED|cmd.paste|0
CHECKED|cmd.wrap|1

### 4.3 _AUX_MENU_SERIAL

Monotonic integer incremented on every model/state change.

Rules:

1. Menubar service must only apply newest serial.
2. Adapter must update serial atomically with payload updates.

---

## 5. Lifecycle

### 5.1 Publish

1. Adapter populates _AUX_MENU_VERSION, _AUX_MENU_MODEL, _AUX_MENU_STATE, _AUX_MENU_SERIAL.
2. Adapter emits PropertyNotify by updating properties.

### 5.2 Activate App

1. WM focus change identifies active toplevel.
2. Menubar service reads active window menu properties.
3. Menubar service renders new menu model.

### 5.3 Dynamic Update

1. App state changes (selection/edit mode/etc).
2. Adapter updates _AUX_MENU_STATE and increments serial.
3. Menubar service re-renders enabled/checked state.

### 5.4 Command Dispatch

1. User selects menu item in global menubar.
2. Menubar service sends ClientMessage with _AUX_MENU_COMMAND atom to active app window.
3. Payload includes command_id token.
4. Adapter routes command to toolkit action/callback.

---

## 6. Command Dispatch Format

ClientMessage payload v1 fields:

1. protocol_version
2. serial_seen
3. command_id_string_property_ref
4. reserved
5. reserved

v1 decision:

1. Command identifiers are string-based (human-readable IDs such as `cmd.open` or `cmd.quit`).
2. Menubar service writes the selected command ID into `_AUX_MENU_COMMAND_TEXT` on the active window.
3. Menubar service emits `_AUX_MENU_COMMAND` ClientMessage as the dispatch trigger.
4. Adapter reads `_AUX_MENU_COMMAND_TEXT` and dispatches by exact string match.

Rationale:

1. Easier tracing/debugging during early iteration.
2. Lower maintenance cost than hash/token maps during frequent schema changes.
3. Avoids token synchronization bugs across menu service and adapters.

---

## 7. Error Handling

Menubar service behavior when data is missing/invalid:

1. Missing model: render system fallback menu only.
2. Parse error: ignore invalid update and keep last valid serial.
3. Unknown command on dispatch: drop action and log trace.
4. Stale serial: ignore update.

Adapter behavior:

1. If dispatch token unknown, no-op and log.
2. Never crash app on malformed menu protocol events.

---

## 8. Capability Flags (_AUX_MENU_CAPS)

Optional line-delimited flags:

1. CAPS|check-items
2. CAPS|radio-items
3. CAPS|dynamic-labels
4. CAPS|command-ids-64bit
5. CAPS|command-ids-string

Menubar may use these to enable richer behavior progressively.

---

## 9. Security and Trust Model (Local Desktop)

v1 assumes trusted local processes in the graphical session.

Hardening options for later:

1. Session-scoped ownership checks.
2. Per-client command namespace isolation.
3. Menubar service validation of active window ownership before dispatch.

---

## 10. Conformance Requirements

An adapter is v1-conformant if it:

1. Publishes required atoms/properties.
2. Maintains monotonic serial updates.
3. Handles _AUX_MENU_COMMAND dispatch reliably.
4. Keeps model/state synchronized for active app windows.

Menubar service is v1-conformant if it:

1. Switches menu contents on active app transition.
2. Honors serial monotonicity.
3. Dispatches command activation to active app.
4. Handles invalid/missing protocol state safely.

---

## 11. Test Plan

Required tests:

1. Publish/read model round-trip.
2. Serial update ordering.
3. Active app switch updates top menubar.
4. Enable/disable and check-state updates.
5. Command dispatch to adapter callback.
6. Invalid payload robustness.

---

## 12. Open Items

1. Define max command string length and overflow behavior.
2. Define max payload sizes and chunking rules.
3. Decide whether per-window or per-app shared menu root is preferred in v1.
4. Add optional accelerator/shortcut metadata fields.
