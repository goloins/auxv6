# lspci(1)

## Name
lspci - List PCI devices.

## Synopsis
```
lspci
```

## Duty
Enumerate all PCI devices detected by the kernel by reading `/proc/pci`.

## Options
None.

## Output Format
Each line shows:
```
BUS:SLOT.FUNC  VENDOR:DEVICE  CLASS:SUBCLASS  IRQ irq
```
- `BUS:SLOT.FUNC` — PCI address in standard notation
- `VENDOR:DEVICE` — PCI vendor and device IDs (hex)
- `CLASS:SUBCLASS` — PCI device class and subclass codes
- `IRQ` — Interrupt number assigned to the device

## Examples
```
lspci
```

## Source Audit
- Source file: user/lspci.c
- Last updated: 2026-04-02
