# halt(1)

## Name
halt - Power off the system.

## Synopsis
```
halt
```

## Duty
Request kernel poweroff using the guest shutdown path. Calls the `halt(2)`
system call. Under QEMU this exits the emulator via the standard poweroff
port sequence.

## Options
None.

## Notes
- Intended for virtual-machine shutdown.
- If the emulator does not implement the standard poweroff ports, the kernel
  falls back to a halted CPU loop.
- For a graceful multi-user shutdown, use `telinit 0` instead, which lets
  `init` run shutdown scripts first.

## Examples
```
halt
telinit 0    # graceful shutdown via init
```

## Source Audit
- Source file: user/halt.c
- Last updated: 2026-04-02