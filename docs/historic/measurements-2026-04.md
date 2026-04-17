# VM Refactor Baseline Measurements (2026-04)

Date: 2026-04-10
Scope: Phase 0 baseline lock-in for VM/allocator decomposition
Owner: auxv6 VM project

## Build Context

- Host build command: `sudo make aux.kern`
- Build policy: all builds use sudo
- Guest boot/testing: manual by user (no automated boot from assistant)

## Commit/Tree Context

Record before every measurement batch:

- Commit SHA:
- Working tree status (`git status --short`):
- Kernel size line (`kernel-size:` from build output):
- Notes:

## Mandatory Baseline Suite

Run each command for 10 iterations unless otherwise noted.

1. `kallocstress -n 10`
2. `schedperf -n 10`
3. `fsperf -n 10`
4. `stackgrowtest -n 10`

## Results Table

| Batch | Date/Time | Commit | kallocstress | schedperf | fsperf | stackgrowtest | Notes |
|-------|-----------|--------|--------------|-----------|--------|---------------|-------|
| B0    |           |        |              |           |        |               |       |
| B1    |           |        |              |           |        |               |       |
| B2    |           |        |              |           |        |               |       |

## Procfs Snapshot Capture

Capture after each batch:

- `/proc/vmstat` summary:
- `/proc/meminfo` summary:
- Any notable counters (cache hits/misses, refill/drain, ref/deferred frees):

## Acceptance Gate (Phase 0)

- Baseline values captured for all mandatory suite commands.
- Results attached to a specific commit/tree state.
- No unexplained outliers without notes.

## Regression Gate (for next phases)

Trigger pause/investigation if any condition occurs:

- >10% regression vs baseline in primary score for any mandatory suite command.
- New panic/hang during benchmark runs.
- Free-page/accounting drift observed across repeated steady-state runs.

## Notes Log

- 2026-04-10: Document created as baseline ledger.
