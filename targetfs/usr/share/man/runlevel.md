# runlevel(1)

## Name
runlevel - Print the current system runlevel.

## Synopsis
```
runlevel
```

## Duty
Read and display the current and previous runlevel from `/etc/runlevel`.

## Options
None.

## Output Format
```
prev current
```
- `prev` — Previous runlevel (`N` if none)
- `current` — Current runlevel digit (e.g. `3`)

## Runlevels
| Level | Meaning                  |
|-------|-------------------------|
| 0     | Halt                    |
| 1     | Single-user mode        |
| 2-5   | Multi-user modes        |
| 6     | Reboot                  |
| S     | Single-user (rescue)    |

## Examples
```
runlevel
# N 3
```

## Source Audit
- Source file: user/runlevel.c
- Last updated: 2026-04-02
