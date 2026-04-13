# vmprobe(1)

## Name
vmprobe - targeted VM/scheduler slowdown hypothesis probe

## Synopsis
vmprobe [-r rounds] [-f forks] [-s sleepers] [-i sleep_iters]

## Description
`vmprobe` is a focused diagnostic utility for long-run performance drift.
It runs repeatable micro-phases and correlates throughput with kernel counters
from `/proc/vmstat` and `/proc/schedstat`.

Per round, it executes:

1. `fork/wait` burst (context-switch and VM-path pressure)
2. tick-sleeper fanout with `sleep(1)` workers (low sleeper count)
3. tick-sleeper fanout with doubled sleepers (high sleeper count)

It prints per-phase deltas for:

- `vm_sync_calls`, `vm_sync_entries`
- `vm_pde_repairs`, `vm_master_repairs`, `vm_bad_pte_drops`
- `wake_ticks_calls`, `wake_scanned`
- timing-based tick fanout scale ratio (`high_vs_low`)

The summary reports first-vs-last `fork_ops` slope to flag runtime decline.

## Options
- `-r rounds`
  Number of rounds to run. Default: `8`.
- `-f forks`
  `fork/wait` operations per round. Default: `96`.
- `-s sleepers`
  Base sleeper count for tick fanout. Default: `8`.
- `-i sleep_iters`
  `sleep(1)` iterations per child. Default: `24`.
- `-h`, `--help`
  Show usage.

## Output Interpretation
- Falling `fork_ops` with stable workload suggests long-run switch-path drift.
- Rising `entries_per_fork` suggests heavier VM sync work per operation.
- Increasing repair deltas (`vm_pde_repairs`, `vm_master_repairs`) suggests
  ongoing page-table divergence/repair activity.
- `tick_scale_ratio` significantly above 200% when sleepers double indicates
  non-linear wakeup overhead.

## Examples
Run default probe:

```sh
vmprobe
```

Longer run with stronger fork pressure:

```sh
vmprobe -r 16 -f 160 -s 8 -i 24
```

Tick-heavy scaling check:

```sh
vmprobe -r 8 -f 64 -s 12 -i 32
```

## Notes
- `vmprobe` is intended for comparative diagnosis and correlation, not as a
  pass/fail benchmark.
- Pair output with `kallocstress` runs over uptime to validate slope behavior.
