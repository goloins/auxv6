# termcheck(1)

## Name
termcheck - Terminal capability verification.

## Synopsis
```
termcheck
```

## Duty
Verify terminal behavior and PTY functionality. Tests PTY allocation,
shell session management over a PTY, signal handling through the terminal
layer, and terminal control sequence processing.

## Options
None.

## Tests Performed
1. **PTY allocation** — Opens a PTY master/slave pair.
2. **Shell session** — Runs a shell through the PTY and verifies output.
3. **Signal delivery** — Tests SIGINT delivery through the terminal.
4. **Control sequences** — Verifies cursor movement and erase sequences.

## Examples
```
termcheck
```

## Source Audit
- Source file: user/termcheck.c
- Last updated: 2026-04-02
