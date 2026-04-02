# uname(1)

## Name
uname - Print system name and hostname.

## Synopsis
```
uname
```

## Duty
Print the kernel release name and the system hostname read from
`/etc/hostname`.

## Options
None.

## Output Format
```
auxv6 hostname
```
- `auxv6` — Kernel/OS name (fixed)
- `hostname` — System hostname read from `/etc/hostname`

## Examples
```
uname
# auxv6 myhost
```

## Source Audit
- Source file: user/uname.c
- Last updated: 2026-04-02
