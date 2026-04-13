# gfxperf

## Name
`gfxperf` - framebuffer console performance probe using `/proc/gfxstats`

## Synopsis
`gfxperf [-l lines] [-r rounds] [-w width] [-q|-Q] [-P progress_lines]`

## Description
`gfxperf` runs a deterministic terminal-output workload, samples `/proc/gfxstats`
before and after, and prints delta counters plus derived metrics.

Derived metrics:
- `pixels_per_flush = flush_pixels / flush_calls`
- `cells_per_sync = cells_changed / sync_calls`
- `render_efficiency = cells_rendered / cells_changed`

The tool emits `[PASS]` or `[FAIL]` checks and returns non-zero on failure.

## Options
- `-l lines`: output lines per round (default: `600`)
- `-r rounds`: number of rounds (default: `1`)
- `-w width`: payload width per line (default: `72`)
- `-q`, `--quick`: quick preset (`-l 120 -r 1 -w 64`)
- `-Q`, `--smoke`: smoke preset (`-l 60 -r 1 -w 48`)
- `-P`, `--progress`: print interim metrics every N emitted lines
- `-h`, `--help`: show usage

## Exit Status
- `0`: all checks passed
- `1`: setup/parsing failure or one or more checks failed

## Examples
Run a short probe:

```sh
gfxperf -l 300 -r 1 -w 64
```

Fast iterative probe:

```sh
gfxperf -q
```

Probe with interim snapshots every 25 lines:

```sh
gfxperf -q -P 25
```

Heavier ANSI-heavy workload:

```sh
gfxperf -l 1200 -r 2 -w 96
```

## Notes
- The metrics are based on `/proc/gfxstats` counters and are best compared across
  runs with the same workload options.
- Use this together with manual framebuffer observation when tuning console
  rendering behavior.
- Press `Ctrl-C` to stop early and still print partial metrics collected so far.
