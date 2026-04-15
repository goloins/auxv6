# su(1)

## Name
su - Switch user identity.

## Synopsis
```
su [options] [-] [user [args...]]
```

## Duty
Switch to another user account. Prompts for the target user's password,
then sets process credentials and executes the selected shell.

## Options
- `-`, `-l`, `--login` — Start a login shell.
- `-c`, `--command` `CMD` — Pass command to shell as `-c CMD`.
- `--session-command` `CMD` — Compatibility alias for `--command`.
- `-s`, `--shell` `SHELL` — Use an alternate shell.
- `-m`, `-p`, `--preserve-environment` — Preserve current environment.
- `-w`, `--whitelist-environment` `LIST` — In login mode, preserve listed
  comma-separated environment variable names.
- `-g`, `--group` `GROUP` — Set primary group by name or numeric GID.
- `-G`, `--supp-group` `GROUP` — Add supplementary group (repeatable).
- `-f`, `--fast` — Pass `-f` to the target shell.
- `-P`, `--pty` — Accepted for compatibility (PTY allocation is not yet implemented).
- `-h`, `--help` — Show usage.
- `-V`, `--version` — Show version.

## Arguments
- `user` — Username to switch to. Defaults to `root`.
- `args...` — Additional arguments passed to the shell when `-c/--command`
  is not used.

## Notes
- Password is verified against `/etc/shadow` when available, otherwise `/etc/passwd`.
- Without `-g/-G`, supplementary groups are initialized from group database entries.
- `-g` and `-G` are restricted to root callers.
- Login mode clears most environment variables and sets `HOME`, `SHELL`,
  `USER`, `LOGNAME`, and `PATH`.

## Examples
```
su
su root
su - alice
su -c "id"
su -s /bin/sh operator
su operator
```

## Source Audit
- Source file: user/su.c
- Last updated: 2026-04-15
