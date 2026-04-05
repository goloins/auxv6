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
| `DBG_MOUNT` | Mount operation details (ext2, isofs, ufs2) | `AUXV6_DEBUG` |
| `DBG_EXT2` | Ext2 filesystem details | `AUXV6_DEBUG` |
| `DBG_IDE` | IDE driver diagnostics | `AUXV6_DEBUG` |
| `DBG_EXEC` | exec/load diagnostics (including stack allocation policy) | `AUXV6_DEBUG` |
| `DBG_STACK` | On-demand stack growth: page-fault growth events and limit enforcement | `AUXV6_DEBUG` |
| `DBG_AHCI` | AHCI controller diagnostics | 0 (always off) |
| `DBG_NVME` | Deep NVMe bring-up tracing (reset/enable/identify/MSI disable) | 0 (always off) |
| `DBG_VIRTIO_NET` | Verbose virtio-net queue/IRQ/poll diagnostics | 0 (always off) |
| `KDEBUG_SPINLOCK_LOCKFAIL` | Print spinlock lock-name/owner diagnostics before panic on nested acquire, bad release, and timeout | 1 |
| `KDEBUG_LOCKDEP` | Enable lockdep-lite lock-order checks in spinlock acquire/release paths | 1 |

### `KDEBUG_SPINLOCK_LOCKFAIL` - Spinlock Failure Diagnostics

Controls whether spinlock failure paths emit lock-owner diagnostics right before
panic. This is useful while modernizing lock topology and tracking down lock
pairing bugs.

**Default:** 1 (enabled)  
**Type:** Compile-time flag (`include/param.h`)  
**File:** `kernel/core/spinlock.c`

**When enabled, these diagnostics are printed before panic:**
- nested acquire on same CPU:
   - `spinlock acquire nested: lock=<name> cpu=<apicid>`
- timeout in spin-wait path:
   - `spinlock timeout: lock=<name> cpu=<apicid> owner_cpu=<apicid> iter=<n>`
- bad release (not held by current CPU):
   - `spinlock bad release: lock=<name> cpu=<apicid> owner_cpu=<apicid> locked=<0|1>`

Set this to `0` for quieter production output once lock migration work is
stable.

### `KDEBUG_LOCKDEP` - Lock Order Validation (Lockdep-Lite)

Controls lock-order verification in `kernel/core/spinlock.c`.

**Default:** 1 (enabled)  
**Type:** Compile-time flag (`include/param.h`)  
**Files:** `kernel/core/spinlock.c`, lock init sites in core subsystems

**What it checks:**
- acquire-time order: panics if code acquires a lower-rank lock while holding
   a higher-rank lock.
- release-time order: panics if code releases a lock that is not the most
   recently acquired lock on that CPU.
- lockdep stack overflow/underflow in per-CPU lock tracking.

**Diagnostics include:**
- attempted lock name/class/rank
- top held lock/rank
- full held-lock chain dump for the current CPU

**Initial ranked classes in-tree:**
- `console_input` / `console_tty` / `console_gfx`
- `ftable_internal` (sleeplock internal spinlock)
- `ticks`
- `ptable`
- `log`

This is intentionally a lightweight lockdep pass: it gives fast failure for
order bugs without introducing a full witness graph.

### Block Device Table Snapshot (`/proc/bdev_table`)

`/proc/bdev_table` dumps the raw kernel block device table — one line per
registered device (slots with no ops pointer are omitted).

**Read-only.** No writable controls.

**Fields per line:**
- `dev=N` — device index (matches `devblocks(N)` argument)
- `nblocks=N` — capacity stored in the bdev table by `bdev_set_nblocks`
- `is_part=N` — 1 if this is a partition entry, 0 for a whole-disk device
- `parent=N` — parent device (for partitions; equals `dev` for disks)
- `start=N` — partition start offset in blocks (0 for disks)
- `has_nblocks_cb=N` — 1 if the driver supplies an `nblocks()` callback
- `query=N` — effective block count as returned by `bdev_nblocks()` (what
  `lsblk` and `sys_devblocks` see)

**Example:**
```
cat /proc/bdev_table
```

Cross-reference with `lsblk -v` to see the exact `devblocks()` return value
per slot from userspace.

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

### Runtime: NVMe Bring-Up Deep Trace
**Flag:** `DBG_NVME`  
**Files:** `kernel/driver/nvme.c`, `kernel/driver/pci.c`

| Message | When |
|---------|------|
| `nvme: DBG_NVME=1 verbose bring-up tracing enabled` | NVMe driver init banner |
| `pci: B:S.F disable_msi start ...` | Before walking PCI capabilities |
| `pci: B:S.F cap@.. id=.. next=..` | Per-capability traversal during MSI/MSI-X disable |
| `nvme: reset begin ...` / `reset complete ...` | Around CC=0 reset flow |
| `nvme: enable begin ...` / `enable write cc=...` / `enable ready ...` / `enable intms=...` | Around AQA/ASQ/ACQ/CC programming, RDY polling, and post-reset interrupt re-mask |
| `nvme: init before id_ctrl alloc` / `init before identify controller` | Narrowing crash window after controller ready |
| `nvme: identify nsid=... cns=... cid=... prp1=...` | Admin IDENTIFY submission details |

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

### Runtime: virtio-net Driver Flow
**Flag:** `DBG_VIRTIO_NET`  
**File:** `kernel/driver/virtio_net.c`

| Message | When |
|---------|------|
| `virtio_net: probe bdf=...` | Per-device probe start |
| `virtio_net: negotiated features=...` | Feature negotiation results |
| `virtio_net: irq=... dispatch` | IRQ path entry |
| `virtio_net: tx enq ...` | Packet queued to TX virtqueue |
| `virtio_net: tx complete ...` | TX completion reclaim |
| `virtio_net: refill rx ...` | RX descriptor replenishment |
| `virtio_net: rx done ...` | RX delivery/drop summary |
| `virtio_net: rx frame/ip/udp/dhcp ...` | Decoded receive packet metadata (including DHCP op/msg type/xid when present) |
| `virtio_net[poll|intr|attach]: ...` | Queue state snapshots |

`virtio_net` poll logging is state-change gated: repeated timer polls with
unchanged queue indices are suppressed to reduce log noise.

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
| `exec: <path> stack guard=G pages stack=S pages max=M pages total=B bytes` | On successful user stack region setup during `exec()` |

### Runtime: On-Demand Stack Growth and Overflow Fallback
**Flag:** `DBG_STACK`  
**Files:** `kernel/core/proc.c`

| Message | When |
|---------|------|
| `stack: pid P grew stack to 0xADDR (U/MAX pages used)` | A guard-page fault successfully grows stack by one page |
| `stack: pid P tried to grow beyond max (MAX pages)` | Growth attempt hits `USER_STACK_MAX_PAGES` ceiling |
| `stack: pid P signal delivery failed, bad stack 0xADDR` | Signal-frame setup failed bounds check at exhausted stack |
| `stack: pid P signal delivery copyout failed` | Signal-frame copyout failed; kernel falls back to fatal signaled terminate |

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

### Enable Only virtio-net Deep Driver Logs
```bash
make EXTRA_CFLAGS="-DDBG_VIRTIO_NET=1" clean aux.kern
```

### Enable Only NVMe Bring-Up Logs
```bash
make EXTRA_CFLAGS="-DDBG_NVME=1" qemu-nvme
# or: make qemu-nvme-dbg
```

Important: do not split this into `make ... aux.kern` followed by a separate
plain `make qemu-nvme`. The second invocation resets `EXTRA_CFLAGS`, updates
`.extra_cflags.stamp`, and rebuilds the kernel with `DBG_NVME=0`.

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

Note: the build now tracks `EXTRA_CFLAGS` via a generated stamp, so changing
`EXTRA_CFLAGS` triggers recompilation of affected objects automatically without
requiring `make clean`.

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
i219: attached wm0 (polling TX/RX)
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
i219: initializing driver
i219: found at 0:5.0 devid=15fb rev=2 irq=20
i219: MAC XX:XX:XX:XX:XX:XX
i219: attached wm0 (polling TX/RX)
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
#define NVMEDBG(...)  do { if(DBG_NVME) cprintf(__VA_ARGS__); } while(0)
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

## Userland Runtime Debug Gates

### `LS_DEBUG` - `ls(1)` Internal Trace

**Type:** Compile-time macro (userland)  
**Default:** off

Build `ls` with `-DLS_DEBUG=1` to emit detailed diagnostics to stderr, including:
- option parsing
- path classification
- `getdents` batch/read counts
- hidden-entry filtering decisions
- stat failures
- sorting decisions
- recursion traversal

Build without the macro (or with `-DLS_DEBUG=0`) for quiet mode.

Example:

```sh
make EXTRA_CFLAGS="-DLS_DEBUG=1" _ls
```

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
