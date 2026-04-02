# ahcitest(1)

## Name
ahcitest - AHCI disk mount, persistence, and fault-injection smoke tests.

## Synopsis
```
ahcitest [-i]
```

## Duty
Run an in-guest regression pass against the AHCI-backed disk (`/dev/hdd`).
Verifies `/proc/ahci_tune` availability, a mount/write/read/persist cycle,
and optionally a bounded timeout-injection recovery check.

## Options
- `-i` — Enable fault-injection checks via `/proc/ahci_tune`. Injects
  artificial command timeouts and verifies the driver recovers correctly.
  Skipped by default to avoid altering AHCI tuning on shared runs.

## Notes
- Creates temporary files under `/mnt/ahcitest` and `/mnt/ahciinj`.
- Output lines are prefixed with `[PASS]`, `[FAIL]`, or `[SKIP]`,
  followed by a final summary.

## Examples
```
ahcitest
ahcitest -i
```

## Source Audit
- Source file: user/ahcitest.c
- Last updated: 2026-04-02
