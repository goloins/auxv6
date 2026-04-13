# kernperf(1)

## Name
kernperf - general kernel performance ruler for before/after tuning.

## Synopsis
```sh
kernperf [-n runs]
```

## Duty
Run system-agnostic kernel microbenchmarks that cover core execution paths:
- syscall round-trip overhead
- process create/wait churn
- pipe IPC throughput (payload-based ping-pong)
- VM page-touch throughput
- filesystem read/write throughput

The tool is intended for comparing kernel revisions, not for absolute host-to-host
performance ranking.

## Options
- `-n runs`  Number of repeated runs (default: 3, max: 20)
- `-h`       Show usage

## Behavior
- Prints `[PERF]` lines for each test in each run.
- Prints `[RUN]` score lines and a final `[SUMMARY]` section.
- Reports a normalized score in the range `0..100` per run.
- Prints `[GATE]` lines for per-test and per-domain health checks.
- Returns non-zero only on functional test failures.

## Output Contract
- `kernperf: profile=... runs=...`
- `[PERF] run=N test=... value=... target=... score=A/B`
- `[RUN] N score=S/100`
- `[SUMMARY] avg-score=.../100`
- `[SUMMARY] test=... avg=... target=...`
- `[GATE] test=... status=PASS|FAIL ...`
- `[GATE] domain=core|ipc|vm|fs status=PASS|FAIL ...`

## Notes
- Throughput uses `uptime()` ticks (100 ticks/sec).
- Keep QEMU profile and host load consistent when comparing revisions.
- For trend tracking, run with `-n 3` or higher.
- `kernperf` intentionally avoids subsystem-specific procfs counters so it remains
  broadly useful across kernel configuration changes.
- v2 uses calibrated default targets and explicit gates so weak subsystems are
  visible even when the composite score is acceptable.

## Examples
```sh
kernperf
kernperf -n 5
```

## Source Audit
- Source file: user/kernperf.c
- Last updated: 2026-04-06
