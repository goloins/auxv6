# auxv6 Debug Flags and Logging Control

This document describes all compile-time debug flags available in auxv6 to control kernel logging output.

## Overview

The kernel implements several hierarchical debug flags to control verbosity of logging at different stages and for different subsystems. All flags default to **disabled** (0) for clean terminal output during normal operation.

In addition to compile-time kernel flags, auxv6 now has a userspace boot-time tuner for device-node manager verbosity in `/etc/devman.conf`.

### `devman` Runtime Debug Tuner

Controls `devman` verbosity during early runlevel `/dev` population.

**Default:** `debug=0` (concise output)  
**Type:** Runtime config (no rebuild required)  
**Config file:** `/etc/devman.conf`

**Accepted settings:**
- `debug=0` or `debug 0` -> concise mode (single-line progress + errors)
- `debug=1` or `debug 1` -> verbose mode (per-node creation diagnostics)

**Example:**
```conf
# /etc/devman.conf
debug=1
```

This is separate from `AUXV6_DEBUG`: kernel compile-time debug flags remain compile-time, while `devman` logging is tuned at runtime via config.

## Master Flags

### `AUXV6_DEBUG` - Global Debug Master Flag

Controls boot-time diagnostics and subsystem logging globally.

**Default:** 0 (disabled - quiet boot)  
**Type:** Compile-time flag  
**Controlled by:** `-DAUXV6_DEBUG=1`

**Controls:**
- All subsystem debug flags (`DBG_VFS`, `DBG_MOUNT`, `DBG_EXT2`, `DBG_IDE`)
- Boot information messages (`AUXV6_BOOTINFO`)

**Note:** Individual subsystem flags can still be overridden even when `AUXV6_DEBUG=0`

### `AUXV6_NET_DEBUG` - Runtime Network Logging

Controls verbose logging of socket operations, network state changes, and connection details during runtime.

**Default:** 0 (disabled - no socket spam)  
**Type:** Compile-time flag  
**Controlled by:** `-DAUXV6_NET_DEBUG=1`

**Gates:**
- Socket creation: `socket: created fd=... family=... type=...`
- Socket bind: `bind: fd=... port=...`
- (Error messages remain always visible)

---

## Boot-Time Logging (Controlled by `AUXV6_BOOTINFO`)

These flags gate diagnostic output during kernel initialization and driver probing.

### `AUXV6_BOOTINFO` - Boot Information Flag

Controls verbose device discovery, initialization messages, and hardware enumeration during boot.

**Default:** Same as `AUXV6_DEBUG` (0)  
**Type:** Compile-time flag  
**Override:** `-DAUXV6_BOOTINFO=1`

**Gated messages include:**
- PCI initialization and device enumeration
- Storage driver discovery (AHCI, NVMe, Loop devices)
- Network driver initialization
- IRQ registration details
- Per-CPU startup messages
- Detailed device configuration (MAC addresses, queue creation, etc.)

### Subsystem-Specific Flags (`DBG_*`)

Control filesystem and driver diagnostic output:

| Flag | Description | Default |
|------|-------------|---------|
| `DBG_VFS` | VFS layer diagnostics | `AUXV6_DEBUG` |
| `DBG_MOUNT` | Mount operation details (ext2, isofs) | `AUXV6_DEBUG` |
| `DBG_EXT2` | Ext2 filesystem details | `AUXV6_DEBUG` |
| `DBG_IDE` | IDE driver diagnostics | `AUXV6_DEBUG` |
| `DBG_EXEC` | exec/load diagnostics (including stack allocation policy) | `AUXV6_DEBUG` |
| `DBG_AHCI` | AHCI controller diagnostics | 0 (always off) |

### AHCI Runtime Tuning (`/proc/ahci_tune`)

AHCI also exposes runtime tuning and deterministic fault-injection controls via
`/proc/ahci_tune` for guest automation and recovery-path validation.

**Readable fields include:**
- `cmd_timeout_us`, `idle_timeout_us`, `rw_retries`
- `test_fail_mode`, `test_fail_remaining`, `last_fail_class`
- Per-port counters (`ok`, `err`, `timeout`, `tfes`, `retry`, `recover_fail`, `recover_ok`, `intr`, ...)
- Per-port type: `type=sata|atapi|other`
- Per-port last error snapshot (`last_is`, `last_tfd`, `last_serr`, `last_ci`, `last_sact`)

`last_fail_class` values:
- 1: timeout
- 2: taskfile error (TFES)
- 3: idle timeout
- 4: aborted by recovery reset

**Writable controls:**
- `cmd_timeout_us=<N>`
- `idle_timeout_us=<N>`
- `rw_retries=<N>`
- `reset_stats=1`
- `test_fail_mode=none|timeout|tfes|idle`
- `test_fail_count=<N>`

**Examples:**
```bash
cat /proc/ahci_tune
echo rw_retries=2 > /proc/ahci_tune
echo test_fail_mode=timeout > /proc/ahci_tune
echo test_fail_count=2 > /proc/ahci_tune
```

---

## Gated Messages by Category

### Boot: PCI Driver Discovery
**Flag:** `AUXV6_BOOTINFO`  
**File:** `kernel/driver/pci.c`

| Message | When |
|---------|------|
| `pci: initializing PCI bus driver` | PCI subsystem startup |
| `pci B:S.F: VID:DID class irq` | Each device enumerated |
| `pci: found N devices` | Discovery complete |

### Boot: Storage Drivers
**Flag:** `AUXV6_BOOTINFO`  
**Files:** `kernel/driver/{ahci,nvme,virtio_blk,loop}.c`

| Message | When |
|---------|------|
| `ahci: initializing driver` | AHCI controller startup |
| `nvme: initializing driver` | NVMe controller startup |
| `virtio_blk: initializing driver` | Virtio block device startup |
| `loop: initialized N loop devices` | Loop device subsystem init |

### Boot: Network Device Discovery
**Flag:** `AUXV6_BOOTINFO`  
**Files:** `kernel/driver/{i219,i226,ax88179,rtl8111,e1000,pcnet,vmxnet3,virtio_net,netvsc}.c`

| Message | When |
|---------|------|
| `<driver>: initializing driver` | Driver probing starts |
| `<driver>: found at B:S.F ...` | Device hardware discovered |
| `<driver>: MAC XX:XX:XX:XX:XX:XX` | MAC address retrieved |
| `<driver>: attached <ifname> irq=N` | **ALWAYS VISIBLE** - device ready |

### Boot: Virtio Core
**Flag:** `AUXV6_BOOTINFO`  
**File:** `kernel/driver/virtio.c`

| Message | When |
|---------|------|
| `virtio: found device type X at B:S.F` | Virtio device discovered |
| `virtio: created queue N with M entries` | Each virtqueue initialized |

### Boot: System Infrastructure
**Flag:** `AUXV6_BOOTINFO`  
**Files:** `kernel/core/{trap.c,main.c}` and `kernel/net/device.c`

| Message | When |
|---------|------|
| `irq: registered IRQ N for <name>` | IRQ handler registration |
| `cpuN: starting N` | AP processor startup |
| `net: attached <ifname> (ifN)` | Network interface registered |

### Runtime: Socket Operations
**Flag:** `AUXV6_NET_DEBUG`  
**File:** `kernel/net/socket.c`

| Message | When |
|---------|------|
| `socket: created fd=... family=... type=...` | Socket syscall completes |
| `bind: fd=... port=...` | Socket bind syscall completes |

**Error messages (ALWAYS VISIBLE):**
- `socket: unsupported family N`
- `socket: invalid type N`
- `socket: out of sockets`
- `bind: invalid address length`
- `bind: invalid socket fd N`

### Runtime: Exec Stack Allocation
**Flag:** `DBG_EXEC`  
**File:** `kernel/core/exec.c`

| Message | When |
|---------|------|
| `exec: <path> stack guard=G pages stack=S pages total=B bytes` | On successful user stack region setup during `exec()` |

---

## Usage Examples

### Enable Verbose Boot Diagnostics
```bash
make EXTRA_CFLAGS="-DAUXV6_DEBUG=1" clean aux.kern
```

### Enable Network Socket Logging
```bash
make EXTRA_CFLAGS="-DAUXV6_NET_DEBUG=1" clean aux.kern
```

### Enable Both Boot and Network Debugging
```bash
make EXTRA_CFLAGS="-DAUXV6_DEBUG=1 -DAUXV6_NET_DEBUG=1" clean aux.kern
```

### Disable One Subsystem While Keeping Others Enabled
```bash
make EXTRA_CFLAGS="-DAUXV6_DEBUG=1 -DDBG_VFS=0" clean aux.kern
```

### Add Extra Flags Without Replacing Base CFLAGS
```bash
make EXTRA_CFLAGS="-DAUXV6_DEBUG=0 -DDBG_VFS=1 -DDBG_EXT2=1" qemu
```

### In Code (for header edits)
Edit `include/defs.h` directly:

```c
#ifndef AUXV6_DEBUG
#define AUXV6_DEBUG 1  // Change to 1
#endif

#ifndef AUXV6_NET_DEBUG
#define AUXV6_NET_DEBUG 1  // Change to 1
#endif
```

---

## Boot Output Comparison

### Default (Quiet Boot) - `AUXV6_DEBUG=0`
```
rtl8111: attached rtl0 irq=27
i219: attached wm0 (stub, TX/RX not implemented yet)
virtio_net: attached vtnet0 irq=11
net: attached lo0 (if1)
mount: skip /mnt (dev /dev/hdb not present)
init: entering main login loop
```

### Verbose Boot - `AUXV6_DEBUG=1`
```
pci: initializing PCI bus driver
pci 0:0.0: 8086:5904 class 6:0 irq 0
pci 0:2.0: 1022:1450 class ff:0 irq 32
pci: found 8 devices
ahci: initializing driver
virtio_blk: initializing driver
nvme: initializing driver
loop: initialized 8 loop devices (minor 7-14)
rtl8111: initializing driver
rtl8111: found 8169 at 0:3.0 irq=27 regs=0xffffd000a3e00000
rtl8111: attached rtl0 irq=27
i219: initializing driver stub
i219: found at 0:5.0 devid=15fb rev=2 irq=20
i219: MAC XX:XX:XX:XX:XX:XX
i219: attached wm0 (stub, TX/RX not implemented yet)
virtio_net: initializing driver
virtio_net: MAC 52:54:00:12:34:56
virtio: created queue 0 with 256 entries
virtio: created queue 1 with 256 entries
irq: registered IRQ 11 for virtio_net
virtio_net: attached vtnet0 irq=11
cpu1: starting 1
net: attached lo0 (if1)
net: attached vtnet0 (if2)
mount: skip /mnt (dev /dev/hdb not present)
init: entering main login loop
```

### Runtime with Network Debug - `AUXV6_NET_DEBUG=1`
```
$ nc -l -p 8000 &
socket: created fd=3 family=2 type=1
bind: fd=3 port=8000
$ telnet localhost 8000
socket: created fd=4 family=2 type=1
connect: <internal handling>
```

---

## Implementation Details

### Macro Definitions

**Boot diagnostics:**
```c
#define BOOTDBG(...)  do { if(AUXV6_BOOTINFO) cprintf(__VA_ARGS__); } while(0)
```

**Network diagnostics:**
```c
#define NETDBG(...)   do { if(AUXV6_NET_DEBUG) cprintf(__VA_ARGS__); } while(0)
```

**Subsystem diagnostics:**
```c
#define VFSDBG(...)   do { if(DBG_VFS) cprintf(__VA_ARGS__); } while(0)
#define MOUNTDBG(...) do { if(DBG_MOUNT) cprintf(__VA_ARGS__); } while(0)
#define EXT2DBG(...)  do { if(DBG_EXT2) cprintf(__VA_ARGS__); } while(0)
#define IDEDBG(...)   do { if(DBG_IDE) cprintf(__VA_ARGS__); } while(0)
#define AHCIDBG(...)  do { if(DBG_AHCI) cprintf(__VA_ARGS__); } while(0)
```

### Zero Overhead When Disabled

When debug flags are 0, the preprocessor compiles out the logging statements entirely:
- No runtime overhead (messages don't execute)
- No code size impact (statements removed)
- Zero performance cost

---

## Best Practices

1. **Default should be quiet:** Production kernels use `AUXV6_DEBUG=0` and `AUXV6_NET_DEBUG=0`

2. **Debugging boot issues:** Use `AUXV6_DEBUG=1` to see driver discovery and initialization

3. **Debugging network issues:** Use `AUXV6_NET_DEBUG=1` to trace socket operations

4. **Selective debugging:** Override individual flags with `-D` flag:
   ```bash
   make CFLAGS="-DAUXV6_DEBUG=0 -DDBG_VFS=1" clean aux.kern
   ```

5. **Persistent debugging:** Edit `include/defs.h` if you want repeated debug builds

---

## Adding New Debug Logging

To add new gated debug output:

1. Define the flag in `include/defs.h`:
   ```c
   #ifndef AUXV6_NEW_DEBUG
   #define AUXV6_NEW_DEBUG 0  // or AUXV6_DEBUG for boot time
   #endif
   ```

2. Create a macro:
   ```c
   #define NEWDBG(...) do { if(AUXV6_NEW_DEBUG) cprintf(__VA_ARGS__); } while(0)
   ```

3. Use in code:
   ```c
   NEWDBG("message: details\n");
   ```

4. Document in this file

---

**Last Updated:** March 31, 2026  
**Related Files:** [include/defs.h](include/defs.h)
