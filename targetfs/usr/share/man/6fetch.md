# 6fetch(1)

## Name
6fetch - print a concise auxv6 system summary.

## Synopsis
```sh
6fetch
```

## Description
`6fetch` prints a compact, screenfetch-style overview with a small ASCII logo and key host information:

- current user and hostname
- operating system name and kernel release
- machine architecture token
- uptime
- memory usage from `/proc/meminfo`

The command takes no options.

## Output Fields
- `user@host`: resolved from passwd and `/etc/hostname`
- `os`: system name from `uname(2)`
- `kernel`: release token from `uname(2)`
- `machine`: hardware token from `uname(2)`
- `uptime`: wall-clock uptime from `uptime()` ticks
- `memory`: used and total MiB derived from `/proc/meminfo`

## Examples
```sh
6fetch
```

## Source Audit
- Source file: user/6fetch.c
- Last updated: 2026-04-07
