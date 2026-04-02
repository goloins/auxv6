# ahcitest(1)

## Name
ahcitest - AHCI mount, persistence, and fault-injection smoke tests.

## Synopsis
- ahcitest [-i]

## Duty
Run an in-guest regression pass against the AHCI-backed disk (`/dev/hdd`). The suite verifies `/proc/ahci_tune` availability, a mount/write/read/persist cycle, and (when enabled) a bounded timeout-injection recovery check.

## Options
- -i: enable fault-injection checks via `/proc/ahci_tune`.

## Notes
- The default run skips injection to avoid altering AHCI tuning on shared runs.
- The test creates temporary files under `/mnt/ahcitest` and `/mnt/ahciinj`.

## Examples
- ahcitest
- ahcitest -i

## Expected Output
- Lines prefixed with `[PASS]`, `[FAIL]`, or `[SKIP]`, followed by a final summary.

## Source Audit
- Source file: user/ahcitest.c
- Last updated: 2026-04-02
