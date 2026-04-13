# passwd(1)

## Name
passwd - Change a user account password.

## Synopsis
```
passwd [user]
```

## Duty
Prompt for a new password and update the `/etc/passwd` file. Without
an argument, changes the password of the current user.

## Options
None.

## Arguments
- `user` — Username whose password to change. Defaults to the current
  user if omitted.

## Notes
- The new password is read interactively (no echo to terminal).
- The password is stored directly in `/etc/passwd`. No shadow file is used.
- Only root can change another user's password.

## Examples
```
passwd
passwd root
```

## Source Audit
- Source file: user/passwd.c
- Last updated: 2026-04-02
