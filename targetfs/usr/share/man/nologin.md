# nologin(1)

## Name
nologin - Refuse interactive login shells.

## Synopsis
```
nologin
```

## Description
Prints an account-unavailable message and exits with status 1.

## Notes
- Intended for use as a user's login shell in `/etc/passwd`.
- Always fails and never starts an interactive shell.

## Source Audit
- Source file: user/nologin.c
- Last updated: 2026-04-15
