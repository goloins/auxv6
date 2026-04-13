# top utility and libterm

## Overview

`top(1)` is a real-time interactive process monitor for auxv6.  It is built
on a small, reusable terminal control library (`libterm`) that provides the
ANSI/VT100 primitives needed for full-screen TUI applications without a
dependency on terminfo or curses.

## Components

### `user/libterm.c` / `include/libterm.h`

Lightweight terminal control library.  Stateless except for a single
`struct termstate` the caller holds on the stack.  Provides:

- `term_init()` / `term_enter()` / `term_leave()` — lifecycle; enters/leaves
  raw mode and the alternate screen buffer.
- `term_update_size()` — `TIOCGWINSZ` wrapper; call from `SIGWINCH` handler.
- `term_move(row, col)` — 0-based cursor positioning.
- `term_clear()`, `term_clreol()`, `term_clreos()` — screen/line erase.
- `term_attr()`, `term_color()`, `term_highlight()`, `term_reset_attrs()` —
  SGR (bold, dim, reverse, fg/bg colour) without dynamic allocation.
- `term_hide_cursor()` / `term_show_cursor()`.
- `term_poll_key(ms)` — non-blocking input with millisecond timeout via
  `poll(2)`; returns raw byte or 0 on timeout.
- `term_fmt_uint()`, `term_fmt_lavg()` — string formatters for numbers and
  fixed-point load average values (saves pulling in `snprintf` for hot paths).

All escape sequences target ANSI/VT100; the auxv6 console and standard
emulators (`xterm`, `vte`, etc.) understand this subset.

### `user/top.c`

Full-screen process monitor.  Refresh cycle:

1. Read `/proc/ps` → parse into `proc_rec[]`.
2. Read `/proc/meminfo` → extract MemTotal/MemFree.
3. Read `/proc/loadavg` → parse 1/5/15-min averages and running/total counts.
4. Call `uptime()` syscall → elapsed ticks for CPU% denominator.
5. Merge snapshot into a persistent `disp_rec[]` table, computing
   `cpu_pct_x10 = (delta_cticks * 1000) / elapsed_ticks` per process.
6. Insertion-sort by CPU% descending.
7. Render header (uptime, load, memory) and process rows via libterm.
8. Poll for keyboard input in short slices against an uptime-tick budget
   until the 1-second interval expires or the user presses a key.

### Kernel changes

#### `cticks` — per-process CPU tick counter

`struct proc` (in `include/proc.h`) gains a `uint cticks` field that is
incremented on every hardware timer tick (100 Hz) for whichever process is
currently running on that CPU.  The increment happens in `kernel/core/trap.c`
outside the `cpuid() == 0` guard so every CPU accounts for its own workload.

`struct procinfo_k` (the userland-visible snapshot type) gains a matching
`uint cticks` field populated by `proc_snapshot()`.  `/proc/ps` now emits a
`CTICKS` column between `SZ` and `NAME`.

#### Load averages

`proc_tick_loadavg()` (called from the CPU-0 timer path every 500 ticks =
5 seconds) scans `ptable` under `ptable.lock`, counts RUNNABLE+RUNNING
processes, then updates three fixed-point EMAs stored in `lavg[3]`:

```
FSHIFT = 11, divisor = 2048
EXP_1  = 1884   ≈ exp(-5/60)
EXP_5  = 2014   ≈ exp(-5/300)
EXP_15 = 2037   ≈ exp(-5/900)
```

`proc_get_loadavg()` and `proc_count_active()` are exported through
`include/defs.h` and used by `/proc/loadavg` in `kernel/fs/procfs.c`.

`/proc/loadavg` outputs a Linux-compatible line:
```
0.12 0.04 0.01 2/10
```
(`running/total`; last-PID field omitted — we don't track it yet.)

## Files Changed

| File | Change |
|------|--------|
| `include/proc.h` | Add `cticks` to `struct proc` and `struct procinfo_k` |
| `include/defs.h` | Declare `proc_tick_loadavg`, `proc_get_loadavg`, `proc_count_active` |
| `include/libterm.h` | New: libterm public API |
| `kernel/core/proc.c` | Implement `proc_tick_loadavg`, `proc_get_loadavg`, `proc_count_active`; populate `cticks` in `proc_snapshot` |
| `kernel/core/trap.c` | Increment `myproc()->cticks` on timer tick (all CPUs); call `proc_tick_loadavg` every 500 ticks on CPU 0 |
| `kernel/fs/procfs.c` | Add `PROCFS_LOADAVG_INO`, `/proc/loadavg` read handler, `CTICKS` column in `/proc/ps`, directory entry |
| `user/libterm.c` | New: libterm implementation |
| `user/top.c` | New: top utility |
| `Makefile` | Add `libterm.o` to `LIBC_OBJS`; add `_top` target and `UPROGS` entry; extend `clean` |
| `.gitignore` | Add `user/top` |
| `targetfs/usr/share/man/top.md` | New: man page |

## Future Work

- **Sort modes**: allow sorting by VIRT, TIME+, PID via keyboard.
- **Kill**: `k` → prompt for PID, send SIGKILL.
- **Colour threshold tuning**: configurable CPU% thresholds.
- **Idle time tracking**: export per-CPU idle ticks to produce the second
  field of `/proc/uptime` in Linux format.
- **Thread awareness**: once kernel threads land, distinguish kernel threads
  from user processes.
- **`/proc/stat`**: Linux-compatible aggregate CPU stats (user/system/idle
  jiffies) needed by higher-level monitoring tools.
