# halt(1)

## Name
halt - power off the auxv6 guest.

## Synopsis
- halt

## Duty
Request kernel poweroff using the guest shutdown path. Under QEMU this exits the emulator instead of requiring a host-side `killall`.

## Notes
- Intended for virtual-machine shutdown.
- If the emulator does not implement the standard poweroff ports, the kernel falls back to a halted CPU loop.

## Examples
- halt

## Source Audit
- Source file: user/halt.c
- Last updated: 2026-04-01