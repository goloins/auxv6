# QEMU Guest Test Template

This document describes the reusable guest automation template for auxv6.

## Purpose

Use one harness for many subsystem smoke tests instead of writing one-off expect blocks.

## Files

- `tools/qemu-guest-test.exp`: generic expect harness.
- `tools/tests/*.cmds`: simple command scripts run after guest login.

## Make Targets

- `make qemu-guesttest-template AUXV6_QEMU_TARGET=<target> AUXV6_TEST_SCRIPT=<cmd-file>`
- `make test-virtioblk-smoke`
- `make test-virtioblk-negative`

## Required Variables

- `AUXV6_TEST_SCRIPT`: guest command script file.

## Optional Variables

- `AUXV6_QEMU_TARGET` (default: `qemu-nox`)
- `AUXV6_EXPECT_TIMEOUT` (default: `240` seconds)
- `AUXV6_HALT` (default: `1`, set `0` to skip `halt`)
- `AUXV6_CHECK_RC` (default: `0`; set `1` only on shells that support `$?`)
- `AUXV6_LOGIN_USER` (default: `root`)
- `AUXV6_LOGIN_PASS` (default: `root`)
- `AUXV6_PROMPT` (default: `#`)

## Command Script Format

- One command per line.
- Empty lines ignored.
- Lines starting with `#` are comments.
- Each command must return to the shell prompt.
- Lines starting with `EXPECT ` are regex assertions checked against the
  output of the most recent command.

When `AUXV6_CHECK_RC=1`, non-zero command status fails the run.
On the default/basic-shell path (`AUXV6_CHECK_RC=0`), use `EXPECT` lines for
deterministic assertions without shell feature dependencies.

For auxv6's basic shell path, prefer `EXPECT` assertions and keep
`AUXV6_CHECK_RC=0` (default).

Example:

```
# storage smoke
lsblk
vblktest 2
cat /proc/vblk_flush
EXPECT unsupp=
EXPECT admin_last_op=1
EXPECT admin_last_rc=-2
```

## Extending To New Areas

1. Add a new `tools/tests/<name>.cmds` file.
2. Reuse an existing QEMU make target or add one for required hardware.
3. Invoke `qemu-guesttest-template` with those two inputs.

This keeps automated tests consistent across storage, networking, tty, and userland utilities.
