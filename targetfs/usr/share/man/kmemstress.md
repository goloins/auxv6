# kmemstress(1)

## Name
kmemstress - broad kernel memory/API stress utility with continuous diagnostics.

## Synopsis
```sh
kmemstress [options]
```

## Description
`kmemstress` continuously exercises allocator-adjacent kernel interfaces under
mixed pressure and prints round-by-round diagnostics to help catch racey memory
corruption.

Each round drives these coverage buckets:
- VM/fork pressure (`fork`, `waitpid`, `sbrk`, page-touch churn)
- IPC/descriptor churn (`pipe`, `poll`, `read`, `write`, `close`)
- VFS/file-path churn (`open`, `write`, `read`, `lseek`, `ftruncate`, `stat`, `unlink`)
- procfs and directory interfaces (`/proc/*`, `getdents`)
- network socket memory paths (`socket`, `bind`, `connect`, `send`, `recvtimeout`)
- kernel metadata APIs (`mountinfo`, `netifinfo`, `routeinfo`, `arpinfo`, `getrlimit`, `kmsgread`)

A per-round line reports pass/fail counts by bucket and key memory counters,
followed by procfs snapshots (`/proc/meminfo`, `/proc/vmstat`, and optional
verbose snapshots).

## Options
| Option | Description |
|--------|-------------|
| `-n <rounds>` | Number of rounds. `0` means run forever (default: `0`). |
| `-L` | Lite preset (`-w 4 -p 8 -f 16 -s 16`). |
| `-M` | Balanced preset (`-w 8 -p 12 -f 32 -s 32`). |
| `-H` | High preset (`-w 16 -p 24 -f 64 -s 64`). |
| `-w <workers>` | Fork workers per round (default: `8`, max: `32`). |
| `-p <pages>` | Pages touched per child worker (default: `12`). |
| `-f <ops>` | File/proc/fd operations per round (default: `32`). |
| `-s <ops>` | UDP socket operations per round (default: `32`). |
| `-d <dir>` | Scratch directory (default: `/tmp/kmemstress`). |
| `-v` | Verbose diagnostics: extra proc snapshots each round. |

## Exit Status
- `0` - all rounds completed with zero bucket failures
- `1` - one or more stress buckets reported failures

## Examples
```sh
# Continuous stress with default settings
kmemstress

# High preset (shell-friendly short form)
kmemstress -H -v

# 200 rounds with higher churn
kmemstress -n 200 -w 16 -p 24 -f 64 -s 64 -v
```

## Notes
- This tool is intentionally noisy and aggressive.
- Use `-n 0` for long soaks and collect serial logs for panic correlation.
- If kernel panics, capture `eip`, `cr2`, and the nearest prior round line.

## Source
- Source file: user/kmemstress.c
- Profile marker: 2026-04-06-r1
