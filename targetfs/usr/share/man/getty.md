# getty(1)

## Name
getty - Terminal login service.

## Synopsis
```
getty
```

## Duty
Attach to available virtual terminals, prompt for login, and spawn the login
process. Queries `/dev/console` for the number of available TTYs and
automatically respawns crashed login processes.

## Options
None.

## Notes
- `getty` is normally started by `init` and should not be run directly.
- One `getty` process is spawned per virtual terminal.
- If a login process exits, `getty` restarts it on that terminal.

## Examples
```
getty
```

## Source Audit
- Source file: user/getty.c
- Last updated: 2026-04-02
