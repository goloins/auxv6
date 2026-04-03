# uname(1)

## Name
uname - Print system information.

## Synopsis
```
uname [-asnrmv]
```

## Duty
Print information about the running system.  When no flags are given prints
only the OS name (equivalent to `-s`).

## Options
- `-a` — Print all fields in order: sysname nodename release version machine.
- `-s` — Print the kernel/OS name (e.g. `a/ux86`).
- `-n` — Print the network node hostname (from `/etc/hostname`).
- `-r` — Print the kernel release string.
- `-v` — Print the kernel version string (same as `-r` on auxv6).
- `-m` — Print the machine hardware name (e.g. `i686`).

Multiple flags may be combined.  Fields are printed in POSIX order
(`sysname nodename release version machine`) separated by spaces.

## Examples
```
uname          # a/ux86
uname -a       # a/ux86 myhost aux86 aux86 i686
uname -s       # a/ux86
uname -n       # myhost
uname -r       # aux86
uname -m       # i686
uname -srm     # a/ux86 aux86 i686
```

## Notes
- The nodename is read from `/etc/hostname`; defaults to `localhost` if absent.
- The kernel version (`-v`) returns the same string as the release (`-r`).

## Source Audit
- Source file: user/uname.c
- Last updated: 2026-04-03
