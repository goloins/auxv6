# Serial Per-Minor Isolation (ttyS0..ttyS3)

## Why this tranche exists

The previous serial backend kept one shared runtime state for every `/dev/ttyS*` node. That made early bring-up fast, but created two practical problems:

1. Any ttyS minor could influence COM1-backed policy (termios, modem-control semantics, hangup behavior).
2. Future node availability (`ttyS1..ttyS3`) risked perturbing existing UART behavior while those lines are still placeholder-only.

This tranche isolates line state per minor while preserving current hardware reality:

- `ttyS0` (`minor=1`) is the only COM1-backed line.
- `ttyS1..ttyS3` (`minor=2..4`) are virtual placeholders with isolated state.

## Files changed

- `kernel/driver/serial.c`
- `kernel/core/proc.c`
- `kernel/core/sysproc.c`
- `include/defs.h`
- `docs/modem-driver.md`
- `docs/serial-per-minor-isolation.md` (this file)

## Procfs observability

New node:

- `/proc/serial_tty`

Purpose:

- Make line capability and runtime state observable without guessing from node names.
- Keep userspace tooling honest while `ttyS1..ttyS3` remain placeholder lines.

Current output columns:

- `minor`: inode minor (`1..4`)
- `name`: tty name (`ttyS0..ttyS3`)
- `hw_backed`: `1` for hardware-backed lines, `0` for placeholders
- `carrier`: current line carrier-present state
- `hungup`: current hangup state
- `open_refs`: active opens on that line
- `fg_pgid`: tracked foreground process group id
- `dtr`: current DTR output state
- `rts`: current RTS output state
- `icanon`: line in canonical input mode
- `clocal`: line ignores carrier for blocking/hangup policy

Example shape:

```
minor name hw_backed carrier hungup open_refs fg_pgid dtr rts icanon clocal
1 ttyS0 1 1 0 0 0 1 1 1 1
2 ttyS1 0 1 0 0 0 1 1 1 1
3 ttyS2 0 1 0 0 0 1 1 1 1
4 ttyS3 0 1 0 0 0 1 1 1 1
```

## API changes

Serial termios/ioctl entry points are now file-aware so dispatch can resolve the target minor line directly:

- `serial_get_termios_file(struct file *f, struct termios *tp)`
- `serial_set_termios_file(struct file *f, const struct termios *tp, int optional_actions)`
- `serial_ioctl_file(struct file *f, int request, uint arg)`

Callers updated:

- `proc_tcgetattr`/`proc_tcsetattr` in `kernel/core/proc.c`
- tty ioctl dispatch in `kernel/core/sysproc.c`

## Backend model

`kernel/driver/serial.c` now uses one isolated runtime structure per minor (`1..4`) containing:

- line identity and hardware binding flag
- open refs, foreground pgid
- carrier/hangup state
- deferred hangup signal state (`hup_pending`, `hup_pgid`)
- modem output mask (`DTR/RTS`)
- per-line termios and winsize
- per-line RX input ring buffer

Global structure layout:

- shared spinlock for consistency across line operations
- fixed line table indexed by minor

## Hardware binding policy

- `minor=1` (`ttyS0`) has `hw_backed=1` and is the only line that touches UART hardware:
  - TX path calls `uartputc`
  - termios c_cflag updates call `uart_apply_termios`
  - modem-control updates call `uart_set_modem_control`
  - modem status reads use `uart_get_modem_bits`
  - UART RX fanout (`serial_rx_char`) feeds only this line
- `minor=2..4` have `hw_backed=0` and never call UART control/data functions.

This guarantees placeholder nodes do not alter COM1 behavior.

## Open/close semantics by line

Each line now tracks open state independently.

Open (`serial_open`):

- Increments that line's refcount.
- Applies carrier-aware blocking only for that line.
- Honors nonblocking open (`O_NONBLOCK`) on that line.

Close (`serial_close`):

- Decrements that line's refcount.
- Applies `HUPCL` modem drop only for hardware-backed line (`ttyS0`).
- Leaves virtual minors free of hardware side effects.

## Carrier and hangup policy

Carrier state is per-line:

- Hardware line carrier is driven by `serial_modem_update()` from UART status.
- Virtual lines maintain local carrier flag through line ioctls.

Hangup delivery is per-line:

- On configured carrier drop, line sets hangup and queues signal delivery for its foreground pgid.
- Delivery emits `SIGHUP` and `SIGCONT` via `proc_signal_pgid`.

## Read/write behavior

Read path:

- Uses target inode minor to select line.
- Uses that line's termios, buffer, and hangup state.

Write path:

- Hardware line (`ttyS0`) transmits to UART.
- Virtual lines accept writes but do not transmit to COM1.
- Return value remains byte-count on success so userspace can stage against placeholder nodes without UART impact.

## Ioctl behavior

All tty serial ioctls now operate on the file's target line.

Notable per-line behavior:

- `TCGETS/TCSETS*`: isolated termios
- `TIOCGWINSZ/TIOCSWINSZ`: isolated winsize
- `FIONREAD/TIOCINQ`: isolated input queue depth
- `TIOCM*`: isolated modem outputs and carrier policy; hardware side effects only on `ttyS0`
- `TIOCGPGRP/TIOCSPGRP`: isolated foreground pgid tracking

## Virtual line defaults

For placeholder lines (`ttyS1..ttyS3`):

- default carrier is present (`carrier_present=1`)
- default hangup is clear (`hungup=0`)
- default termios matches ttyS0 defaults initially

Rationale:

- keep nodes operational for userspace scaffolding
- avoid immediate hangup behavior in placeholder-only state
- avoid implicit coupling to COM1 signal transitions

## Concurrency and wakeups

Wake channels are now per-line:

- readers sleep/wake on that line's input pointer channel
- carrier-blocked opens sleep/wake on that line's carrier channel

This avoids cross-line wake storms and cross-line blocking side effects.

## Known limits after this tranche

- Only one physical UART data source is present; ttyS1..ttyS3 are policy-isolated placeholders, not independent hardware ports.
- Virtual lines currently sink writes (no loopback or PTY-bridge behavior).
- Session-leader/controlling-tty validation for `TIOCSPGRP` remains minimal and can be tightened later.

## Suggested follow-ons

1. Add explicit virtual-line mode reporting (`procfs` or ioctl extension) so tools can distinguish hardware vs placeholder lines.
2. Add optional virtual-loopback mode for ttyS1..ttyS3 to support richer userspace smoke tests without hardware.
3. Tighten `TIOCSPGRP` checks against session/controlling-tty ownership rules.
4. If additional UART ports land, bind each hardware UART to its own ttyS minor and remove placeholder-only assumptions.
