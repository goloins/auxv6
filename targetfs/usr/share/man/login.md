# login(1)

## Name
login - Authenticate user and start a session.

## Synopsis
```
login
```

## Duty
Prompt for a username and password, authenticate against `/etc/passwd`,
set up the user environment, and execute the user's login shell.

## Options
None.

## Notes
- `login` is normally started by `getty` and should not be invoked directly.
- Credentials are verified against the password field in `/etc/passwd`.
- On successful login, the process UID/GID are set to the user's values and
  the user's shell is exec'd.
- Password entry is hidden and supports erase/backspace editing.

## Examples
```
# Invoked automatically by getty on each virtual terminal.
login
```

## Source Audit
- Source file: user/login.c
- Last updated: 2026-04-02
