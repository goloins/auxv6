# rarp(1)

## Name
rarp - Display reverse ARP (RARP) table.

## Synopsis
```
rarp
```

## Duty
Display the kernel's reverse ARP table, which maps MAC (hardware) addresses
to IP addresses. This is the inverse of the ARP table shown by `arp -a`.

## Options
None.

## Notes
- The RARP table is populated as responses to RARP requests.
- Useful for diagnosing RARP-based address assignment.

## Examples
```
rarp
```

## Source Audit
- Source file: user/rarp.c
- Last updated: 2026-04-02
