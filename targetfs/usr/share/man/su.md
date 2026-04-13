# su(1)

## Name
su - Switch user identity.

## Synopsis
```
su [user]
```

## Duty
Switch to another user account. Prompts for the target user's password,
then sets the process UID/GID and exec's the user's login shell.

## Options
None.

## Arguments
- `user` — Username to switch to. Defaults to the current user if omitted
  (useful for re-authenticating).

## Notes
- Password is verified against `/etc/passwd`.
- UID, GID, and environment are set to the target user's values.
- The user's shell from `/etc/passwd` is executed.

## Examples
```
su
su root
su operator
```

## Source Audit
- Source file: user/su.c
- Last updated: 2026-04-02
