# bcachestress

## Name
`bcachestress` - concurrent filesystem stress utility for buffer-cache integrity

## Synopsis
```sh
bcachestress [options]
```

## Description
`bcachestress` drives high-rate, concurrent filesystem I/O intended to hammer
`bread()`/`bget()` paths and expose buffer-cache or allocator corruption under
load.

For each round, the parent forks worker processes. Each worker creates files in
a scratch directory, writes patterned payload data, stats files, reads data
back for spot verification, and unlinks files. After workers finish, the parent
reads `/proc/bcache_health` and fails immediately if any corruption counters are
non-zero.

On first corruption detection, the tool also dumps `/proc/vmstat` and
`/proc/meminfo` snapshots to help correlate cache integrity failures with memory
allocator pressure.

## Options
| Option | Description |
|--------|-------------|
| `-w <n>` | Worker processes per round (default: 4, max: 16) |
| `-r <n>` | Number of rounds (default: 50) |
| `-f <n>` | Files per worker per round (default: 8, max: 32) |
| `-k <n>` | KiB written per file (default: 8) |
| `-d <dir>` | Scratch directory (default: `/tmp/bcs`) |
| `-v` | Verbose mode: print `/proc/bcache_health` each round |

## Output
Typical startup line:

```text
bcachestress: workers=8 rounds=200 files=16 file_kb=32 dir=/tmp/bcs
```

Progress is printed periodically and a PASS/FAIL summary is emitted at the end.
On detected corruption, output includes the offending `bcache_health` fields and
context dumps.

## Exit Status
- `0` - completed all rounds with no corruption fields and no worker failures
- `1` - worker I/O failures without detected bcache corruption
- `2` - corruption detected in `/proc/bcache_health`

## Notes
- This tool is intentionally aggressive and may trigger latent kernel bugs.
- Higher values of `-w`, `-f`, and `-k` increase contention and cache churn.
- Repro recipe used during investigation:

```sh
bcachestress -w 8 -r 200 -f 16 -k 32
```

## See Also
`fsregress(1)`, `stressfs(1)`, `kallocstress(1)`
