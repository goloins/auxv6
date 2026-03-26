# Directory Organization

This xv6 OS project has been reorganized into a logical directory structure:

## Directory Structure

```
auxv6/
├── kernel/                 # Kernel source code
│   ├── boot/              # Bootloader and initialization
│   │   ├── bootasm.S
│   │   ├── bootmain.c
│   │   ├── entryother.S
│   │   └── initcode.S
│   ├── core/              # Core kernel functionality
│   │   ├── entry.S
│   │   ├── main.c
│   │   ├── proc.c         # Process management
│   │   ├── vm.c           # Virtual memory
│   │   ├── exec.c
│   │   ├── syscall.c      # System call handling
│   │   ├── sysproc.c
│   │   ├── sysfile.c
│   │   ├── trap.c         # Trap handling
│   │   ├── trapasm.S
│   │   ├── pipe.c
│   │   ├── kalloc.c       # Memory allocation
│   │   ├── spinlock.c     # Synchronization
│   │   ├── sleeplock.c
│   │   ├── string.c
│   │   ├── swtch.S
│   │   └── vectors.S      # (generated from vectors.pl)
│   ├── driver/            # Device drivers
│   │   ├── console.c
│   │   ├── kbd.c
│   │   ├── uart.c
│   │   ├── ide.c          # IDE disk driver
│   │   ├── memide.c
│   │   ├── mp.c           # Multi-processor
│   │   ├── lapic.c        # Local APIC
│   │   ├── ioapic.c       # I/O APIC
│   │   └── picirq.c       # PIC interrupt controller
│   └── fs/                # Filesystem
│       ├── fs.c
│       ├── bio.c          # Block I/O
│       ├── file.c         # File system
│       └── log.c
├── include/               # Header files
│   ├── types.h
│   ├── param.h
│   ├── memlayout.h
│   ├── defs.h
│   ├── x86.h
│   ├── asm.h
│   ├── mmu.h
│   ├── elf.h
│   ├── date.h
│   ├── proc.h
│   ├── fs.h
│   ├── file.h
│   ├── buf.h
│   ├── stat.h
│   ├── fcntl.h
│   ├── syscall.h
│   ├── traps.h
│   ├── spinlock.h
│   ├── sleeplock.h
│   ├── kbd.h
│   ├── mp.h
│   ├── user.h
│   └── (other headers)
├── user/                  # User programs and libraries
│   ├── init.c
│   ├── sh.c               # Shell
│   ├── cat.c
│   ├── ls.c
│   ├── grep.c
│   ├── echo.c
│   ├── wc.c
│   ├── mkdir.c
│   ├── ln.c
│   ├── rm.c
│   ├── kill.c
│   ├── stressfs.c
│   ├── usertests.c
│   ├── forktest.c
│   ├── zombie.c
│   ├── ulib.c             # User library
│   ├── umalloc.c          # User malloc
│   ├── printf.c
│   └── usys.S
├── tools/                 # Build tools and utilities
│   ├── vectors.pl         # Vector table generator
│   ├── sign.pl            # Bootblock signer
│   ├── pr.pl
│   ├── mkfs.c             # Filesystem creator
│   ├── runoff             # Documentation formatter
│   ├── runoff1            # Documentation formatter helper
│   ├── runoff.list        # Documentation file list
│   ├── runoff.spec
│   ├── show1
│   └── printpcs
├── config/                # Configuration files
│   ├── kernel.ld          # Kernel linker script
│   ├── dot-bochsrc        # Bochs emulator config
│   ├── .gdbinit.tmpl      # GDB configuration template
│   ├── toc.hdr            # Table of contents header
│   └── toc.ftr            # Table of contents footer
├── Makefile               # Build configuration
├── README
├── LICENSE
├── BUGS
├── TRICKS
├── Notes
└── ORGANIZATION.md        # This file

```

## Key Changes

### 1. **Compiler Include Paths**
   - The Makefile now includes `-Iinclude` for all compilations
   - Header files are searched in the `include/` directory
   - Source files include headers with `#include "filename.h"`

### 2. **Object File Locations**
   - Kernel object files are organized by subdirectory
   - E.g., `kernel/fs/bio.o`, `kernel/driver/console.o`, `kernel/core/proc.o`
   - User program object files are in the `user/` directory

### 3. **Build Artifact Locations**
   - Generated files (vectors.S, dependency files) are in their respective source directories
   - Build output remains in the root directory

### 4. **Documentation Generation**
   - The `runoff` script has been updated to reference files in their new locations
   - `runoff.list` now contains full paths relative to the tools directory
   - Documentation generation works with the new structure

## Building

To build the xv6 kernel:

```bash
make
```

To run in QEMU:

```bash
make qemu
```

To clean build artifacts:

```bash
make clean
```

## File References

All #include statements in the source code remain unchanged (e.g., `#include "types.h"`).
The compiler include path configuration in the Makefile resolves these references to `include/`.

The Makefile compilation commands have been updated to reference files in their new locations.
