# schedperf(1)

## Name
schedperf - Scheduler and process-table stress and performance scoring tool.

## Synopsis
```
schedperf
```

## Duty
Run scheduler-centric stress checks and emit both regression status and a
normalized performance score. This tool exercises fork/wait load, scheduler
fairness behavior, pipe wakeups, alarm delivery, signal return-path handling,
and process-table capacity headroom.

## Options
None.

## Behavior
- Runs a sequence of sub-tests and prints `[PASS]` / `[FAIL]` per test.
- Emits `[PERF]` lines with measured throughput for key paths.
- Prints a final weighted score in the form `schedperf score: X/100`.
- Prints a `profile=...` marker so target revisions are traceable.

## Score Interpretation
- `>= 75/100` - Meets current kernel performance target.
- `60..74/100` - Functional but below target; investigate regressions.
- `< 60/100` - Significant regression or heavy contention suspected.

## Tests Performed
- `fork-storm` - burst process creation/reap throughput
- `yield-storm` - scheduler handoff rate under concurrent yielding
- `pipe-wakeup` - sleep/wakeup throughput under pipe traffic
- `alarm-counter` - alarm firing under active alarm-tracking fast path
- `signal-fast-path` - signal delivery return-path throughput
- `idle-responsiveness` - basic non-hang check when CPUs idle
- `sched-spread` - no-starvation concurrent worker completion
- `proc-table-limit` - verifies raised process-table headroom

## Notes
- Throughput units are derived from `uptime()` ticks (100 ticks/sec).
- Use comparative runs on the same QEMU profile for trend tracking.
- Keep `[PASS]`/`[FAIL]` as the correctness gate; score is the performance ruler.
- When tuning targets, update both this manpage and `user/schedperf.c` in the same change.

## Examples
```
schedperf
```

## Source Audit
- Source file: user/schedperf.c
- Last updated: 2026-04-03
