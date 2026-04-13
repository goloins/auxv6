# sh(1)

## Name
sh - Command interpreter (shell).

## Synopsis
```
sh
sh script [args...]
```

## Duty
An interactive command shell and script interpreter. Supports pipelines,
I/O redirection, background jobs, variable expansion, and profile loading.

## Options
None. Shell behavior is controlled through the interactive session or
environment variables.

## Arguments
- `script` — Path to a shell script to execute non-interactively.
- `args...` — Arguments passed to the script as `$1`, `$2`, etc.

## Builtin Commands
- `cd [dir]` — Change the current directory. Defaults to `/` if omitted.
- `exit [status]` — Exit the shell with optional exit status.
- `jobs` — List background and stopped jobs.
- `fg [%jobid]` — Bring a background or stopped job to the foreground.
- `bg [%jobid]` — Resume a stopped job in the background.

## Features
- **Pipelines**: `cmd1 | cmd2`
- **Redirection**: `cmd > file`, `cmd < file`, `cmd >> file`
- **Background**: `cmd &`
- **Variable expansion**: `$VAR`, `$PATH`
- **Profile**: Reads `/etc/profile` on startup if it exists
- **History**: Up/Down arrow recall of recent commands
- **Quick recall**: Typing `!!` on an empty prompt replaces the line with the previous command for editing or re-run
- **Persistent history**: Stores recent commands in `$HOME/.6sh_history`

## Notes
- `sh` is the login shell started by `login`.
- Job control uses process groups (SIGSTOP/SIGCONT).
- History stores up to 64 most-recent non-empty commands and avoids
	consecutive duplicates.
- `!!` only rewrites the current input line; it does not execute the command
	automatically.

## Examples
```
sh
sh /etc/rc.local
ls | grep sh
jobs
fg %1
```

## Source Audit
- Source file: user/sh.c
- Last updated: 2026-04-03
