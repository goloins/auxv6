# top(1)

## Name
top - display and monitor processes in real time

## Synopsis
```
top
```

## Duty
Provides a full-screen, continuously-updated view of the running system.
Displays a system summary (uptime, load averages, memory) followed by a
process table sorted by CPU usage.  The display refreshes every ~1 second.

## Data Sources
- `/proc/ps` — per-process snapshot: PID, state, virtual size, cumulative CPU
  ticks (`CTICKS`), name
- `/proc/meminfo` — physical memory totals (MemTotal, MemFree)
- `/proc/loadavg` — 1-, 5-, 15-minute load averages; running/total process count
- `uptime()` syscall — elapsed system ticks (100 Hz) used for CPU% calculation

## Output Columns
- `PID` — Process ID
- `USER` — Effective user name (from `/etc/passwd`, falls back to numeric UID)
- `STAT` — Process state: `running`, `sleep`, `runnable`, `stopped`, `zombie`
- `VIRT` — Virtual address space size (K/M/G suffix)
- `CPU%` — Percentage of one CPU consumed since last refresh
- `TIME+` — Cumulative CPU time charged to the process (mm:ss)
- `COMMAND` — Process name

## Keyboard Bindings
- `q` / `Q` — Quit
- `r` / `R` / `Space` — Force immediate refresh
- `h` / `?` — Toggle keyboard help overlay

## CPU Percentage
CPU% is derived from the delta in each process's cumulative CPU tick counter
(`cticks`, incremented per timer interrupt per CPU) divided by elapsed system
ticks across the refresh interval.  This represents the fraction of a single
CPU consumed.  On an SMP system, a process using two CPUs simultaneously can
appear above 100%.

## Load Averages
The kernel computes 1-, 5-, and 15-minute exponential moving averages of the
count of RUNNABLE+RUNNING processes, sampled every 5 seconds.  These follow
the Linux algorithm (`FSHIFT=11`, coefficients `1884/2014/2037`) and are
exported via `/proc/loadavg`.

## Examples
```
top
```

## See Also
ps(1), free(1)

## Source Audit
- Source files: user/top.c, user/libterm.c
- Header: include/libterm.h
- Last updated: 2026-04-05
