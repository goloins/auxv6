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
- `AUXV6_LOGIN_USER` (default: `root`)
- `AUXV6_LOGIN_PASS` (default: `root`)
- `AUXV6_PROMPT` (default: `# `)

## Command Script Format

- One command per line.
- Empty lines ignored.
- Lines starting with `#` are comments.
- Each command must return to the shell prompt.

Example:

```
# storage smoke
lsblk
vblktest 2
cat /proc/vblk_flush
```

## Extending To New Areas

1. Add a new `tools/tests/<name>.cmds` file.
2. Reuse an existing QEMU make target or add one for required hardware.
3. Invoke `qemu-guesttest-template` with those two inputs.

This keeps automated tests consistent across storage, networking, tty, and userland utilities.
