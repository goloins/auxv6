# auxv6 Modernization Roadmap

## Project Overview

auxv6 is an xv6-derived Unix-like operating system with significant enhancements including:
- Multi-backend VFS layer (xv6fs, ext2, msdosfs, procfs)
- BSD-style networking with TCP/IP stack
- Signal handling infrastructure
- Job control with process groups, sessions, terminal control
- Multi-filesystem support with mount table

**Architecture:** x86 32-bit, single address space per process  
**Current State:** Educational OS with working core components, but missing modern device support and complete POSIX compliance

---

## Current Subsystem Status

### ✅ Mature Subsystems (70-90% complete)
| Subsystem | Status | Notes |
|-----------|--------|-------|
| VFS Layer | 90% | Multi-backend, mount table, longest-prefix matching |
| ext2 filesystem | 85% | ~2400 LOC, read/write, directories, inode management |
| FAT/msdosfs | 80% | ~1650 LOC, FAT12/16/32, short/long filenames |
| Process model | 85% | fork/exec/wait, process groups, sessions |
| Job control | 80% | setpgid, setsid, tcsetpgrp, terminal control |
| Memory management | 80% | Virtual memory, page tables, kalloc/kfree |

### ⚠️ Partially Implemented (30-60%)
| Subsystem | Status | Notes |
|-----------|--------|-------|
| Signal handling | 95% | Full delivery, alarm(), SIGPIPE, hardware faults |
| TCP/IP stack | 40% | UDP working, DNS works, TCP state machine but no actual packets |
| Networking interfaces | 30% | BSD ifnet abstraction, loopback only |
| procfs | 60% | Basic process info, missing many nodes |

### ❌ Missing/Stub Only
| Subsystem | Status | Notes |
|-----------|--------|-------|
| PCI subsystem | Stub | No device enumeration |
| DMA support | None | All I/O is PIO |
| Modern storage | None | Only IDE (PIO mode) |
| Real NICs | None | No hardware NIC drivers |
| Ethernet/ARP | Stub | No link layer |
| Symlinks | None | VFS supports but not implemented |

---

## Priority Tier 1: Foundation (Weeks 1-4)

These items are blocking everything else and must be done first.

### 1.1 Signal Delivery to Userspace [COMPLETE]
**Status:** Implemented 2026-03-30  
**Files:** `proc.c:proc_deliver_signal()`, `sysproc.c:sys_sigreturn()`, `trap.c`, `signal.h`, `vm.c:copyin()`  
**Implementation:**
1. Signal frame (`struct sigframe`) pushed onto user stack with saved registers
2. Trampoline code embedded in frame calls `sigreturn` syscall
3. `proc_deliver_signal()` called from `trap.c` before returning to userspace
4. `sigreturn` restores original context via `copyin()` from user stack
5. Signal mask saved/restored properly

**Signals Implemented (POSIX-compatible numbering):**
| Signal | # | Default | Notes |
|--------|---|---------|-------|
| SIGHUP | 1 | term | Terminal hangup |
| SIGINT | 2 | term | Interrupt (Ctrl+C) |
| SIGQUIT | 3 | term | Quit (Ctrl+\\) |
| SIGILL | 4 | term | Illegal instruction (from trap.c) |
| SIGTRAP | 5 | term | Breakpoint/debug (from trap.c) |
| SIGABRT | 6 | term | Abort |
| SIGBUS | 7 | term | Alignment fault (from trap.c) |
| SIGFPE | 8 | term | FPU/divide error (from trap.c) |
| SIGKILL | 9 | term | Kill (uncatchable) |
| SIGUSR1 | 10 | term | User-defined 1 |
| SIGSEGV | 11 | term | Segfault/GPF (from trap.c) |
| SIGUSR2 | 12 | term | User-defined 2 |
| SIGPIPE | 13 | term | Broken pipe (implemented in pipewrite) |
| SIGALRM | 14 | term | Alarm (alarm() syscall implemented) |
| SIGTERM | 15 | term | Termination |
| SIGCHLD | 17 | ignore | Child status change |
| SIGCONT | 18 | cont | Continue |
| SIGSTOP | 19 | stop | Stop (uncatchable) |
| SIGTSTP | 20 | stop | Terminal stop (Ctrl+Z) |
| SIGTTIN | 21 | stop | Background tty read |
| SIGTTOU | 22 | stop | Background tty write |
| SIGWINCH | 28 | ignore | Window resize |

**Estimate:** 3-4 days

### 1.2 lseek Syscall [CRITICAL]
**Current:** No file position manipulation  
**Impact:** Breaks ALL programs expecting to seek in files  
**Files:** `sysfile.c`, `file.c`, `syscall.h`

```c
// Add to syscall.h
#define SYS_lseek 64

// Implement in sysfile.c
int sys_lseek(void) {
    int fd, whence;
    off_t offset;
    if(argint(0, &fd) < 0 || argint(1, (int*)&offset) < 0 || argint(2, &whence) < 0)
        return -1;
    // Implementation using myproc()->ofile[fd]
}
```

**Estimate:** 1 day

### 1.3 dup2 Syscall [CRITICAL]
**Current:** Only dup() exists (dupes to lowest available fd)  
**Impact:** Shell redirection, standard idioms broken  
**Files:** `sysfile.c`, `syscall.h`

**Estimate:** 0.5 days

### 1.4 fcntl Syscall [HIGH]
**Current:** Missing  
**Impact:** File descriptor flags, locking, advisory locks  
**Files:** `sysfile.c`, `fcntl.h`, `syscall.h`

**Estimate:** 2 days

---

## Priority Tier 2: Device Infrastructure (Weeks 5-8)

### 2.1 PCI Subsystem [HIGH]
**Status:** Stub created at `kernel/driver/pci.c`  
**Blocking:** All modern device drivers  
**Tasks:**
- [ ] PCI bus enumeration
- [ ] Config space read/write (I/O ports 0xCF8/0xCFC)
- [ ] BAR mapping to virtual memory
- [ ] MSI/MSI-X interrupt setup
- [ ] Device driver registration framework

**Dependencies:** None  
**Estimate:** 1 week

### 2.2 Interrupt Routing Modernization [HIGH]
**Current:** trap.c has hardcoded switch statement  
**Target:** Dynamic interrupt table with handler registration
```c
typedef void (*irq_handler_t)(int irq);
void irq_register(int irq, irq_handler_t handler);
```

**Estimate:** 2-3 days

### 2.3 DMA Abstraction [MEDIUM]
**Current:** No DMA support  
**Target:** Simple bounce buffer and contiguous allocation
```c
void *dma_alloc(size_t size, uint32_t *phys_addr);
void dma_free(void *vaddr, size_t size);
```

**Dependencies:** Virtual memory changes  
**Estimate:** 3-4 days

---

## Priority Tier 3: Storage Drivers (Weeks 9-12)

### 3.1 Virtio-blk Driver [HIGH]
**Status:** Stub at `kernel/driver/virtio_blk.c`  
**Value:** QEMU testing, cloud deployment  
**Tasks:**
- [ ] PCI detection (vendor=0x1AF4, device=0x1001)
- [ ] Virtqueue setup
- [ ] Feature negotiation
- [ ] Block read/write commands
- [ ] Integration with bdevsw

**Dependencies:** PCI, Virtio core  
**Estimate:** 1 week

### 3.2 AHCI/SATA Driver [MEDIUM]
**Status:** Stub at `kernel/driver/ahci.c`  
**Value:** Real hardware support  
**Tasks:**
- [ ] AHCI HBA detection
- [ ] Port initialization
- [ ] FIS construction
- [ ] Command submission/completion
- [ ] DMA buffer management

**Dependencies:** PCI, DMA  
**Estimate:** 2 weeks

### 3.3 NVMe Driver [MEDIUM]
**Status:** Stub at `kernel/driver/nvme.c`  
**Value:** Modern SSD support  
**Dependencies:** PCI, DMA  
**Estimate:** 2 weeks

---

## Priority Tier 4: Network Stack (Weeks 13-18)

### 4.1 Ethernet Layer [HIGH]
**Status:** Stub at `kernel/net/ethernet.c`  
**Tasks:**
- [ ] Frame encapsulation/decapsulation
- [ ] MTU handling
- [ ] Protocol demux (ETHERTYPE_IP, ETHERTYPE_ARP)

**Estimate:** 3-4 days

### 4.2 ARP Implementation [HIGH]
**Status:** Stub at `kernel/net/arp.c`  
**Tasks:**
- [ ] ARP cache with timeout
- [ ] ARP request/reply handling
- [ ] Packet queuing pending resolution

**Estimate:** 3-4 days

### 4.3 Virtio-net Driver [HIGH]
**Status:** Stub at `kernel/driver/virtio_net.c`  
**Value:** Easiest NIC to test with QEMU  
**Dependencies:** PCI, Virtio core, Ethernet layer  
**Estimate:** 1 week

### 4.4 TCP Implementation [MEDIUM]
**Current:** State machine exists, no actual packet exchange  
**Tasks:**
- [ ] SYN/SYN-ACK/ACK handshake
- [ ] Sequence number management
- [ ] Retransmission
- [ ] Flow control
- [ ] Connection teardown

**Estimate:** 3-4 weeks

---

## Priority Tier 5: Filesystem Enhancements (Weeks 19-22)

### 5.1 Symbolic Links [HIGH]
**Current:** VFS has type but not implemented  
**Tasks:**
- [ ] Add S_IFLNK handling
- [ ] symlink syscall
- [ ] readlink syscall
- [ ] Path resolution symlink following
- [ ] Symlink loop detection

**File:** `vfs.c`, `sysfile.c`  
**Estimate:** 4-5 days

### 5.2 ISO 9660 (CD-ROM) [MEDIUM]
**Status:** Stub at `kernel/fs/vfs_isofs.c`  
**Value:** Read ISO images, distribution media  
**Tasks:**
- [ ] Volume descriptor parsing
- [ ] Directory traversal
- [ ] File reading
- [ ] Rock Ridge extensions (optional)

**Estimate:** 1 week

### 5.3 devfs [LOW]
**Current:** Device nodes created manually  
**Target:** Dynamic /dev population  
**Estimate:** 1 week

---

## Priority Tier 6: POSIX Compliance (Weeks 23-28)

### 6.1 Missing Syscalls [HIGH]
| Syscall | Priority | Complexity | Notes |
|---------|----------|------------|-------|
| lseek | Critical | Low | File seeking |
| dup2 | Critical | Low | FD manipulation |
| fcntl | High | Medium | File control |
| select/poll | High | Medium | I/O multiplexing |
| mmap | High | High | Memory mapping |
| ioctl | High | Medium | Device control |
| stat/lstat | Medium | Low | Complete stat info |
| time/gettimeofday | Medium | Low | Time support |
| getrlimit/setrlimit | Low | Medium | Resource limits |

### 6.2 Header Compliance [MEDIUM]
**Created Stubs:**
- `stddef.h` - size_t, NULL, offsetof
- `stdint.h` - uintXX_t, intXX_t
- `sys/types.h` - pid_t, uid_t, off_t, etc.
- `unistd.h` - POSIX constants
- `stdlib.h` - Standard library
- `string.h` - String operations

**Still Needed:**
- `stdio.h` - FILE operations
- `dirent.h` - Directory entries
- `sys/stat.h` - stat structure
- `sys/socket.h` - Socket interface
- `netinet/in.h` - Internet addresses
- `arpa/inet.h` - Address conversion

### 6.3 Library Functions [LOW]
Most library functions should be in userspace, not kernel:
- printf/sprintf family
- Memory functions (memcpy, memset, etc.)
- String functions
- stdlib functions

---

## File Stubs Created

### Drivers
| File | Description |
|------|-------------|
| `kernel/driver/pci.c` | PCI bus enumeration and config access |
| `kernel/driver/virtio.c` | Virtio framework core |
| `kernel/driver/virtio_net.c` | Virtio network driver |
| `kernel/driver/virtio_blk.c` | Virtio block driver |
| `kernel/driver/e1000.c` | Intel E1000 Gigabit Ethernet |
| `kernel/driver/pcnet.c` | AMD PCNET-PCI Ethernet |
| `kernel/driver/ahci.c` | AHCI/SATA controller |
| `kernel/driver/nvme.c` | NVMe SSD controller |

### Headers
| File | Description |
|------|-------------|
| `include/pci.h` | PCI definitions |
| `include/virtio.h` | Virtio definitions |
| `include/stddef.h` | Standard definitions |
| `include/stdint.h` | Integer types |
| `include/stdlib.h` | Standard library |
| `include/string.h` | String operations |
| `include/unistd.h` | POSIX constants |
| `include/sys/types.h` | POSIX types |

### Network
| File | Description |
|------|-------------|
| `kernel/net/ethernet.c` | Ethernet frame handling |
| `kernel/net/arp.c` | ARP implementation |

### Filesystem
| File | Description |
|------|-------------|
| `kernel/fs/vfs_isofs.c` | ISO 9660 filesystem |

---

## Estimated Timeline

| Phase | Duration | Focus |
|-------|----------|-------|
| Foundation | 4 weeks | Signal delivery, critical syscalls |
| Device Infra | 4 weeks | PCI, interrupts, DMA |
| Storage | 4 weeks | Virtio-blk, AHCI |
| Networking | 6 weeks | Ethernet, ARP, virtio-net, TCP |
| Filesystems | 4 weeks | Symlinks, ISO9660 |
| POSIX | 6 weeks | Syscalls, headers, compliance |
| **Total** | **~28 weeks** | |

---

## Quick Wins (Can be done anytime)

1. **Add more errno values to errno.h** - 30 minutes
2. **Expand procfs** - Add /proc/uptime, /proc/meminfo - 2 hours each
3. **Implement getrlimit/setrlimit** (return sane defaults) - 1 hour
4. **Add gettimeofday syscall** - 2 hours
5. **Add hostname syscall** - 30 minutes

---

## Testing Infrastructure Needed

1. **Unit test framework** for kernel components
2. **QEMU scripting** for automated boot tests
3. **POSIX conformance test suite** (subset)
4. **Network test environment** with virtual bridge

---

## Next Steps (Recommended Order)

1. **Immediately:** Fix signal delivery - this unblocks many programs
2. **Week 1:** Implement lseek, dup2, basic fcntl
3. **Week 2-3:** Get PCI enumeration working
4. **Week 4:** Interrupt routing modernization
5. **Week 5-6:** Virtio-blk driver (enables faster QEMU testing)
6. **Week 7-8:** Ethernet + ARP layer
7. **Week 9:** Virtio-net driver
8. **Week 10+:** TCP fixes, then rest of roadmap
