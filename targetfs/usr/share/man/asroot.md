# asroot(1)

## Name
asroot - Minimal sudo-style command wrapper.

## Synopsis
```
asroot command [args ...]
```

## Description
Executes `command` when run from an already-root shell. This is intentionally
minimal and does not implement policy, PAM, or password prompting.

## Notes
- `/bin/sudo` is provided as a symlink to `asroot` for compatibility.
- Non-root callers are rejected.

## Source Audit
- Source file: user/asroot.c
- Last updated: 2026-04-06
