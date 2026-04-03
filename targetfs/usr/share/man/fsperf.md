# fsperf(1)

## Name
fsperf - Filesystem, inode-cache, and buffer-cache stress and scoring tool.

## Synopsis
```
fsperf
```

## Duty
Run filesystem stress checks focused on cache behavior and concurrency, then
emit a normalized performance score for use as a repeatable performance ruler.

## Options
None.

## Behavior
- Runs multiple filesystem sub-tests and prints `[PASS]` / `[FAIL]` lines.
- Emits `[PERF]` lines for measured operation rates.
- Prints a final weighted score in the form `fsperf score: X/100`.

## Score Interpretation
- `>= 75/100` - Meets current filesystem performance target.
- `60..74/100` - Functional but under target; tune cache/lock paths.
- `< 60/100` - Likely regression or severe contention.

## Tests Performed
- `fd-ceiling` and `fd-open-rate` - per-process fd capacity and open rate
- `inode-churn` - create/open/stat/unlink turnover throughput
- `bcache-sequential` - sequential read throughput across cache pressure
- `concurrent-openers` - shared-file open/read/close concurrency
- `parallel-writers` - independent concurrent write/read throughput
- `inode-limit` and `inode-limit-rate` - raised inode/open headroom check
- `hash-correctness` - repeated integrity verification under cache lookup load

## Notes
- Creates and removes temporary files under `/tmp`.
- Throughput units are based on `uptime()` ticks (100 ticks/sec).
- Compare scores on consistent VM settings for meaningful trend lines.

## Examples
```
fsperf
```

## Source Audit
- Source file: user/fsperf.c
- Last updated: 2026-04-03
