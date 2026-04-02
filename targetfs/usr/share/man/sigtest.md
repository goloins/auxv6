# sigtest(1)

## Name
sigtest - Signal delivery regression test.

## Synopsis
```
sigtest
```

## Duty
Test signal registration, delivery, and handling. Verifies that signal
handlers are invoked correctly for signals sent from the same process and
from a child process.

## Options
None.

## Tests Performed
1. **Self-signal** — Registers a handler and sends a signal to itself.
2. **Child signal** — Forks a child that sends a signal to the parent.
3. **Handler invocation** — Confirms the handler was called with the correct
   signal number.

## Examples
```
sigtest
```

## Source Audit
- Source file: user/sigtest.c
- Last updated: 2026-04-02
