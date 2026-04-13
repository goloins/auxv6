# kallocstress(1)

## Name
kallocstress - allocator-focused stress and regression scoring utility.

## Synopsis
```sh
kallocstress
```

## Duty
Exercise allocator-heavy kernel paths and report both correctness gates and a
performance score. This tool is intended to catch regressions in `kalloc`/
`kfree` behavior under fork/copyuvm and pipe churn pressure.

## Options
None.

## Behavior
- Runs allocator-focused sub-tests and prints `[PASS]` / `[FAIL]` lines.
- Emits `[PERF]` lines with throughput measurements.
- Prints a final weighted score in the form `kallocstress score: X/100`.
- Prints a `profile=...` marker so target revisions are traceable.

## Score Interpretation
- `>= 75/100` - Meets current allocator stress target.
- `60..74/100` - Functional but below target; investigate allocator contention.
- `< 60/100` - Significant regression or heavy instability risk.

## Tests Performed
- `fork-copyuvm-pressure` - repeated fork with child heap growth and page touch
- `pipe-page-churn` - repeated pipe create/read/write/close cycles
- `allocator-reclaim` - MemFree before/after churn to detect leak-style drops

## Notes
- `allocator-reclaim` uses `/proc/meminfo` and allows normal runtime variance;
  it only fails on large sustained memory drops.
- Throughput units are derived from `uptime()` ticks (100 ticks/sec).
- Compare runs on consistent QEMU settings for trend tracking.
- When tuning targets, update both this manpage and `user/kallocstress.c` in
  the same change.

## Examples
```sh
kallocstress
```

## Source Audit
- Source file: user/kallocstress.c
- Last updated: 2026-04-03 (profile r2 score normalization)
