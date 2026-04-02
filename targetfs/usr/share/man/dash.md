# dash(1)

## Name
dash - POSIX-compliant command interpreter (Debian Almquist Shell).

## Synopsis
```
dash [-aCefnuvxoOption] [-c command_string] [command_file [argument...]]
```

## Duty
A lightweight POSIX shell providing full POSIX `sh` compatibility. Used as
`/bin/sh` for scripts and interactive sessions. Ported from the upstream
`dash` project (via Debian Linux) as part of the auxv6 ports collection.

## Options
- `-a` — Export all variables assigned during execution.
- `-c command_string` — Execute the commands in `command_string` instead of\
  reading from a file or stdin.
- `-e` — Exit immediately if any command exits with a non-zero status.
- `-f` — Disable pathname expansion (globbing).
- `-n` — Read commands without executing them (syntax check).
- `-u` — Treat unset variables as an error.
- `-v` — Print shell input lines to stderr as they are read.
- `-x` — Print each command to stderr before executing it (trace mode).
- `-o option` — Set the named option. Common options: `noclobber`, `noglob`,
  `errexit`, `nounset`.

## Arguments
- `command_file` — Script file to execute. If omitted with no `-c`, reads
  from stdin.
- `argument...` — Arguments to the script, available as `$1`, `$2`, etc.

## Builtins (selection)
`cd`, `exit`, `export`, `read`, `set`, `unset`, `shift`, `test`/`[`,
`echo`, `printf`, `pwd`, `source`/`.`, `trap`, `wait`, `exec`, `eval`.

## Notes
- This is the ported upstream `dash`; source for this port is in `ports/`.
- For the native `sh` built directly from `user/sh.c`, see `man sh`.

## Examples
```
dash
dash -c 'echo hello'
dash /etc/rc.local
dash -x /etc/rc.local
```

## Source Audit
- Source file: ports/ (upstream dash; source analysis not performed)
- Last updated: 2026-04-02
