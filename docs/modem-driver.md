# Modem Driver Status (Initial Scaffold)

## Scope

This document tracks modem-driver work for auxv6.

Current tranche is intentionally probe-only and focuses on common legacy PCI modem families, including well-known Winmodem-era chipsets where public data exists.

## Landed in this tranche

- PCI modem class constants and communications-subclass helpers in `include/pci.h`
- Boot-time modem subsystem init (`modem_init`) in `kernel/driver/modem.c`
- Per-family probe-only stubs:
  - `kernel/driver/conexant_hsf.c`
  - `kernel/driver/agere_lt.c`
  - `kernel/driver/smartlink.c`
  - `kernel/driver/pctel.c`
  - `kernel/driver/intel_softmodem.c`
  - `kernel/driver/motorola_sm56.c`
- Build integration in `Makefile`
- Boot wiring in `kernel/core/main.c`
- Shared modem probe registry in `kernel/driver/modem.c`
- Procfs observability node `/proc/modems` for discovered modem-family devices
- Serial chardev endpoint groundwork:
  - `kernel/driver/serial.c` registers `SERIALDEV` with read/write/ioctl paths
  - UART RX now fans out to serial input buffering via `serial_rx_char()`
  - `devman` now creates `/dev/ttyS0..3`
  - Non-canonical serial reads now honor `VMIN/VTIME` timer semantics
  - `c_cflag` updates now program UART baud/line settings (`CSIZE`, `PARENB/PARODD`, `CSTOPB`, speed)
  - Modem-control ioctls are hardware-backed on COM1 (`TIOCMGET/TIOCMSET/TIOCMBIS/TIOCMBIC`) via UART MCR/MSR line state
  - Serial open/close lifecycle is now wired from VFS device open/close to the serial backend
  - Carrier transitions now update serial hangup state (`DCD` changes feed `serial_modem_update()` from UART init/interrupt paths)
  - `CLOCAL` and `HUPCL` are now enforced in serial line behavior: dropped carrier can hang up active sessions and `HUPCL` drops DTR/RTS on last close
  - Foreground process-group ioctls are now supported for serial (`TIOCSPGRP`/`TIOCGPGRP`), and carrier-loss can emit `SIGHUP` to the tracked group
  - Non-`CLOCAL` serial opens are now carrier-aware: blocking `open()` waits for carrier, while `O_NONBLOCK` open fails fast when no carrier is present
  - Hangup signaling now follows job-control expectations by pairing `SIGHUP` with `SIGCONT` to the tracked foreground process group
  - Serial line runtime is now isolated per minor (`ttyS0..ttyS3`): termios, buffers, fg pgid, carrier/hangup state, and modem-control state no longer share one global serial instance
  - Only `ttyS0` is hardware-backed to COM1; `ttyS1..ttyS3` are isolated virtual placeholders and do not mutate UART hardware state
  - New procfs visibility node `/proc/serial_tty` reports per-minor capability/runtime state so tooling can identify hardware-backed vs placeholder lines

Detailed implementation notes are documented in `docs/serial-per-minor-isolation.md`.

## Coverage intent

The stub tranche identifies representative families commonly encountered in legacy Wintel systems:

- Conexant HSF/HCF
- Agere/Lucent LT
- Smart Link SL-family
- PCTel families
- Intel 536EP/537EP-class softmodems
- Motorola SM56

Behavior today:

- Probe/match at boot
- Emit explicit `(stub)` detection logs for matched devices
- Track detected modem-family devices in a shared probe table (family, ven:dev, class/subclass, bus:slot.func, irq)
- Expose that table through `/proc/modems` with a summary line (`total`, `datapath`, `probe_only`)
- Attach a basic runtime serial tty endpoint (`/dev/ttyS*`) for chardev read/write and minimal tty ioctls
- Basic terminal-mode behavior for serial now includes `ICANON` and `VMIN/VTIME` read-path handling
- No modem network data path

## Still intentionally limited

- No AT command channel
- Only `ttyS0` is currently hardware-backed; `ttyS1..ttyS3` are isolated placeholders rather than independent UART ports
- No PPP/SLIP integration

## Public-reference implementation policy

Implementation and structure are informed by public interfaces and openly available kernel design patterns (Linux/BSD style probe-and-attach flows).

No leaked source material is used.

## Next tranche candidates (terminal support focus)

1. Tighten session-leader validation around foreground-pgrp assignment for serial hangup policy.
2. Add explicit line-capability reporting so tools can distinguish hardware-backed vs placeholder ttyS minors.
3. Add richer flush/drain and output-queue semantics for serial write path.
4. Add userland smoke tools for ttyS + AT-session bring-up.
5. Defer PPP/SLIP integration to a later networking tranche.
