# dmesg(1)

## Name
dmesg - Print the kernel message buffer.

## Synopsis
```
dmesg
```

## Duty
Read and print the kernel ring buffer (boot messages, driver output, kernel
errors) by invoking the `kmsgread` system call.

## Options
None.

## Notes
- Reads up to 4096 bytes of kernel messages.
- Output is printed directly to stdout without filtering or paging.
- Useful for diagnosing hardware detection and driver initialization at boot.

## Examples
```
dmesg
dmesg | grep ahci
```

## Source Audit
- Source file: user/dmesg.c
- Last updated: 2026-04-02
