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
- `make test-virtioblk-retry-stress`
- `make test-termcheck-smoke`
- `make test-termcheck-full`
- `make test-termdemo-smoke`
- `make test-termcap-smoke`
- `make test-terminal-regression`

## Required Variables

- `AUXV6_TEST_SCRIPT`: guest command script file.

## Optional Variables

- `AUXV6_QEMU_TARGET` (default: `qemu-nox`)
- `AUXV6_MAKE_CMD` (default: `make`; on macOS `qemu-guesttest-template` defaults to `sudo -n make`)
- `AUXV6_EXPECT_TIMEOUT` (default: `240` seconds)
- `AUXV6_HALT` (default: `1`, set `0` to skip `halt`)
- `AUXV6_CHECK_RC` (default: `0`; set `1` only on shells that support `$?`)
- `AUXV6_LOG_USER` (default: `0`; set `1` to stream raw guest output live)
- `AUXV6_LOGIN_USER` (default: `root`)
- `AUXV6_LOGIN_PASS` (default: `root`)
- `AUXV6_PROMPT` (default: `#`)

Teardown behavior:
- Harness always attempts graceful guest shutdown (`halt` when `AUXV6_HALT=1`, `exit` when `AUXV6_HALT=0`).
- If QEMU does not exit promptly, harness falls back to monitor quit (`Ctrl-A x`) so tests do not hang.

Host terminal safety:
- Raw guest output echo is disabled by default (`AUXV6_LOG_USER=0`) to avoid host-terminal ANSI query/reply pollution.
- This prevents stray text like `n;6R...` from being injected into your host shell prompt after guest shutdown.

On macOS hosts, `qemu-guesttest-template` now defaults to interactive `sudo make` for launching QEMU targets from the expect harness. If prompted, enter your host sudo password once and the run will continue.

## Command Script Format

- One command per line.
- Empty lines ignored.
- Lines starting with `#` are comments.
- Each command must return to the shell prompt.
- Lines starting with `EXPECT ` are regex assertions checked against the
  output of the most recent command.
- `RAW <command>`: execute command and wait for prompt return directly (no completion marker injection). Use for commands that manipulate terminal modes/cursor state and can be disrupted by marker echoes.

Harness note:
- Command completion now uses an explicit in-guest completion marker (`__AUXV6_CMD_DONE__`) rather than relying only on prompt matching. This avoids false command boundaries when TUI/ANSI output includes prompt-like characters.
- On harness failure, `qemu-guesttest-template` now force-kills stale `qemu-system-i386` processes to avoid background leakage (`sudo -n killall -9 qemu-system-i386` on macOS, plain `killall` on non-macOS hosts).
- `EXPECT` checks now run against both raw command output and a normalized view with ANSI/control sequences removed, which makes TUI-heavy command assertions more stable.
- Command execution no longer depends on compound shell syntax (`; ...`) for completion tracking; the harness now queues a separate `echo` marker command, which is compatible with auxv6's minimal shell behavior.
- Completion matching is anchored to a standalone marker line, so echoed input like `echo __AUXV6_CMD_DONE__` does not trigger false command completion.
- `termdemo` smoke is intentionally completion-based (no fixed output `EXPECT` assertions) because interactive ANSI/TUI output timing and escape traffic are environment-sensitive under the minimal guest shell.
- Harness shutdown is spawn-safe: if the guest exits before explicit `halt`/`exit` is sent, teardown now ignores closed-spawn send/eof errors instead of failing the whole test run.

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
EXPECT last_fail_class=3
```

## Extending To New Areas

1. Add a new `tools/tests/<name>.cmds` file.
2. Reuse an existing QEMU make target or add one for required hardware.
3. Invoke `qemu-guesttest-template` with those two inputs.

This keeps automated tests consistent across storage, networking, tty, and userland utilities.

## Locking/Console Validation Policy

For any change that touches lock primitives, lock ordering, console locking,
console read/write paths, or framebuffer mirror behavior, validation must cover
both headless and graphical boots.

Required minimum matrix:

1. `sudo make qemu-nox`
2. `sudo make qemu`
3. In guest: `lockprobe`
4. In guest: `lockprobe -v`
5. In guest: `lockprobe -D -C`
6. In guest: `lockprobe -D -F`
7. In guest: `lockprobe -L`
8. In guest: `halt`

Rationale:

- `qemu-nox` alone can miss framebuffer/graphics-console bringup regressions.
- `qemu` alone can hide serial-path and non-graphics interactions.
- `-L` specifically covers sanctioned sleep/wakeup handoff transitions used by
  lockdep false-positive detection work.
