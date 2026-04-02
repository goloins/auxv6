# free(1)

## Name
free - Report memory usage.

## Synopsis
```
free [-h]
```

## Duty
Display total, used, and free memory by reading `/proc/meminfo`.

## Options
- `-h` — Human-readable output. Sizes are shown with `K`, `M`, or `G` suffixes
  instead of raw kilobyte counts.

## Output Columns
- `Total` — Total physical memory installed
- `Used` — Memory currently in use
- `Free` — Available free memory

## Examples
```
free
free -h
```

## Source Audit
- Source file: user/free.c
- Last updated: 2026-04-02
