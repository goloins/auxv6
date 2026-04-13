# COMPREHENSIVE i386 → amd64 MIGRATION AUDIT

**XV6-Based Operating System**  
**Analysis Date:** April 12, 2026

---

## EXECUTIVE SUMMARY

This audit identifies **all** i386-specific code, assumptions, and structures requiring changes for amd64 (64-bit x86-64) migration. The codebase is extensive, with over 100+ binaries, complex device drivers, advanced filesystem support (ext2, UFS2, btrfs, exFAT, ISO9660), and full networking stack.

**Key Scope:**
- **11 architecture-specific assembly files** (bootasm.S, entry.S, entryother.S, trapasm.S, swtch.S, initcode.S, crt0.S, setjmp.S, etc.)
- **32-bit type system** throughout (uint, pde_t, struct trapframe, struct context, etc.)
- **i386 paging model**: 2-level page tables (10+10+12 bits)
- **32-bit syscall mechanism**: `int $T_SYSCALL` (int 64)
- **Task State Segment (TSS) & GDT/IDT** structures fixed at 32-bit
- **32-bit instruction set** across kernel and userland
- **50+ kernel subsystems** with embedded i386 assumptions
- **ELF32 binary format** (elf32-i386)

**Complexity Level:** **VERY HIGH** — This is a top-to-bottom architectural migration affecting every layer.

---

## 1. KERNEL ASSEMBLY & CPU SPECIFICS

### 1.1 Assembly Files Requiring Rewrite

| File | Current Code | Purpose | i386-Specific Elements | Amd64 Equivalent | Complexity |
|------|-----------|---------|------------------------|--------------------|---|
| kernel/boot/bootasm.S | 16/32-bit real→protected mode | Primary bootloader | `.code16`, `.code32`, 32-bit protected mode setup, A20 line, GDT (.word, .long), `ljmp $(SEG_KCODE<<3), $start32`, 32-bit `lgdt`, `cr4`/`cr0` manipulation (4MB pages, CR0_PSE) | `.code16`, `.code64`, 64-bit protected mode + long mode setup, A20 handling (unchanged), **64-bit GDT** (new 16-byte format), `ljmpq`, **MSR_EFER enable**, **CR4_PAE** instead of PSE, PML4/PDPT/PD/PT tables | **HIGH** |
| kernel/core/entry.S | 32-bit paging setup, jump to main | Kernel entry after bootload | `movl %cr4, %eax` / `orl $CR4_PSE`, `movl $(V2P_WO(entrypgdir)), %eax` / `movl %eax, %cr3`, `orl $(CR0_PG\|CR0_WP)`, `movl $(stack + BOOT_STACK_SIZE), %esp`, `mov $main, %eax` / `jmp *%eax` | `movq %cr4, %rax` / `orq $CR4_PAE`, **PDPT setup** instead of PD, `movq`, `rax/rbx/rdx`, **RIP-relative addressing**, `lea` for address loading | **HIGH** |
| kernel/boot/entryother.S | AP (application processor) startup in real mode | Secondary CPU bringup | `.code16`, 16→32-bit mode switch, 32-bit registers (eax, esp, gs, fs), GDT load, `lgdt`, CR0 PE/PG setup, 4MB pages, `call *(start-8)` | `.code16`, 32→64-bit mode switch (long mode), **64-bit registers** (rax, rsp), **64-bit GDT**, `lgdt`, **MSR_EFER**, PAE mode, RIP-relative calls | **HIGH** |
| kernel/core/trapasm.S | Exception/interrupt entry & framing | Trap handler assembly | `pushl %ds`, `pushal` (8 32-bit GPRs), trap frame (all `uint`), `movw $(SEG_KDATA<<3), %ax`, `iret` (32-bit), segment frame layout | `pushq %rax/%rbx/...` (16 GPRs no longer callee-saved), **no segments** (kernel flat), **new trapframe format** (RIP, RSP, RFLAGS, RCX, R11 only per x86-64 ABI), `iretq` | **VERY HIGH** |
| kernel/core/swtch.S | Context switch save/restore | Process scheduling | `movl 4(%esp)` (arg access), `pushl %ebp/%ebx/%esi/%edi`, `movl %esp, (%eax)` (32-bit ptrs), `ret` | `movq 8(%rsp)` (arg access in x86-64), **no callee-saved GPR push** (rax/rcx/rdx preserved; rbx/rbp/rsi/rdi/r12-r15 must save manually or redefine struct context), **rip implicit in call stack** | **MEDIUM** |
| kernel/boot/initcode.S | Init userspace entry code | First user program (exec /init) | `pushl $argv` (32-bit stack args), `movl $SYS_exit, %eax`, `int $T_SYSCALL` (int 64) | `mov $SYS_exit, %rax`, **syscall** or **int 0x80** (if supported), **no stack args** (rdi/rsi args per ABI) | **MEDIUM** |
| user/crt0.S | User program startup | Program entry point before main() | `movl 4(%esp), %eax` (argc access), `movl 8(%esp), %edx` (argv access), `pushl %edx/%eax` (stack args to main), stack frame setup | `mov %rdi, %rax/%rcx` (rdi=argc per ABI), `mov %rsi, %rdx` (rsi=argv), **no push** (args already in registers), **red zone** (128B below RSP) | **MEDIUM** |
| user/setjmp.S | User setjmp/longjmp for signal handlers | Save/restore register state | Saves 6x uint: `ebx, esi, edi, ebp, esp, eip` (24 bytes) | Save 16x `uint64_t`: `rbx, rsp, rbp, r12-r15, rdx, rdi, rsi` + **rip** (128+ bytes), **rflags if needed** | **MEDIUM** |
| kernel/core/segreload.S | Segment reload (if exists) | GDT/LDT updates on context switch | Likely null/empty on i386; segment operations | **Removed entirely** (flat 64-bit model, no segment reloads needed) | **LOW** |

### 1.2 CPU Control Structures

#### Global Descriptor Table (GDT)

**Current (i386):**
```c
struct segdesc {
  uint lim_15_0 : 16;  // Low 16 bits of segment limit
  uint base_15_0 : 16; // Low 16 bits of base address
  uint base_23_16 : 8; // Bits 16-23 of base
  uint type : 4;       // Segment type
  uint s : 1;          // 0=system, 1=application
  uint dpl : 2;        // Descriptor Privilege Level
  uint p : 1;          // Present
  uint lim_19_16 : 4;  // High 4 bits of limit
  uint avl : 1;        // Available for software
  uint rsv1 : 1;       // Reserved
  uint db : 1;         // 0=16-bit, 1=32-bit segment
  uint g : 1;          // Granularity (limit scaled by 4K)
  uint base_31_24 : 8; // Bits 24-31 of base
};
```
- **Size**: 8 bytes, 32-bit base address, 32-bit limit
- **Used for**: CS, DS, SS, TSS, user code/data segments
- **Problem**: Flat model doesn't scale; segment limits unused in amd64

**Amd64 Long Mode GDT:**
- **Size**: 16 bytes per descriptor (extended for 64-bit base)
- **LDT/TSS** entries become `struct tss64` (128 bytes, flat)
- **Impact**: GDT size doubles; loader code, CPU init, `lgdt` instruction all change

**Files affected:**
- include/mmu.h: segdesc, gatedesc structures
- kernel/core/vm.c: seginit() — GDT initialization
- kernel/core/proc.c: CPU gdt array in struct cpu
- kernel/boot/bootasm.S: GDT bootstrap
- kernel/boot/entryother.S: AP GDT load

#### Interrupt Descriptor Table (IDT)

**Current (i386):**
```c
struct gatedesc {
  uint off_15_0 : 16;   // Low 16 bits of offset in segment
  uint cs : 16;         // Code segment selector
  uint args : 5;        // Argument count
  uint rsv1 : 3;        // Reserved
  uint type : 4;        // STS_IG32 or STS_TG32
  uint dpl : 2;         // Descriptor Privilege Level
  uint p : 1;           // Present
  uint off_31_16 : 16;  // High 16 bits of offset
};
```
- **Size**: 8 bytes
- **256 entries** (one per interrupt 0-255)

**Amd64 Long Mode IDT:**
- **Size**: 16 bytes per descriptor
- **RIP instead of segment:offset** (64-bit address)
- **IST (Interrupt Stack Table)** field points to 3-entry table per CPU

**Files affected:**
- include/mmu.h: gatedesc structure definition
- kernel/core/trap.c: IDT initialization, `lidt`
- kernel/core/vectors.S: interrupt stubs

#### Task State Segment (TSS)

**Current (i386):**
- **Size**: ~104 bytes
- Used for: ISP (Interrupt Stack Pointer) for ring transitions

**Amd64 Long Mode TSS:**
```c
struct tss64 {
  uint32_t reserved0;
  uint64_t rsp0, rsp1, rsp2;       // Stack pointers for privilege levels
  uint64_t reserved1;
  uint64_t ist[7];                 // Interrupt Stack Table entries 1-7
  uint64_t reserved2;
  uint16_t reserved3;
  uint16_t iopb_offset;
};
```
- **Size**: ~104-128 bytes (simpler, IST replaces esp1/esp2)
- **No hardware context save** (x86-64 doesn't use task gates)
- **rsp0 only** (single kernel-mode stack)

**Files affected:**
- include/mmu.h: taskstate definition
- kernel/core/vm.c: CPU ts field initialization
- kernel/core/proc.c: struct cpu contains taskstate ts

#### Control Registers (CR0, CR3, CR4, CR8+)

**i386 paging setup:**
```
CR4[PSE] = 1  (Page Size Extension, enables 4MB pages)
CR0[PG] = 1   (Paging Enable)
CR0[WP] = 1   (Write Protect kernel memory)
CR0[PE] = 1   (Protected mode)
CR3 = page directory physical address
```

**Amd64 long mode setup:**
```
CR4[PAE] = 1    (Physical Address Extension)
CR4[PSE] = 0    (PSE no longer used)
CR0[PG] = 1     (Paging still enabled)
CR0[WP] = 1     (Write Protect)
CR0[PE] = 1     (Protected mode)
CR3 = PML4 physical address
EFER[LME] = 1   (Long Mode Enable, via MSR 0xC0000080)
EFER[SCE] = 1   (Syscall Enable, if using syscall instead of int)
CR8 = CPU priority mask (new in 64-bit)
```

**Files affected:**
- kernel/core/entry.S: CR0, CR4 setup
- kernel/boot/bootasm.S: CR0, CR3 setup
- include/mmu.h: CR0_*, CR4_* constants

### 1.3 Paging and Memory Management Model

#### Current (i386) 2-Level Paging

Virtual Address structure:
```
+--------10------+-----------10----------+-------12-------+
| PD Index (PDX) |  PT Index (PTX)       | Offset (OFFSET)|
+----------------+-----------------------+---------------+
     bits 22-31       bits 12-21              bits 0-11
```

**Constants in mmu.h:**
```c
#define NPDENTRIES 1024    // 2^10 page directory entries
#define NPTENTRIES 1024    // 2^10 page table entries
#define PGSIZE 4096        // 4K pages

#define PDXSHIFT 22
#define PTXSHIFT 12

#define PDX(va) (((uint)(va) >> PDXSHIFT) & 0x3FF)
#define PTX(va) (((uint)(va) >> PTXSHIFT) & 0x3FF)
```

#### Amd64 4-Level Paging (PAE → Long Mode)

Virtual Address (canonical 48-bit):
```
+-----9-----+-----9-----+-----9-----+-----9-----+-------12-------+
|  L4 (PML4)| L3 (PDPT) | L2 (PD)   | L1 (PT)   |  Offset         |
+------------+----------+-----------+-----------+---------------+
 bits 39-47  bits 30-38  bits 21-29  bits 12-20   bits 0-11
```

**Code changes:**
- include/mmu.h: Add NPDPENTRIES (512), NPTENTRIES (512), NPTLEVELS (4), PGSHIFT (12)
- include/memlayout.h: Update KERNBASE to 0xFFFF800000000000
- kernel/core/vm.c: Rewrite `walkpgdir()` to handle 4-level tables
- kernel/core/proc_lifecycle.c: Process address space setup for 4-level tables
- kernel/core/trap.c: Page fault handler walkpgdir calls

#### Memory Layout Changes

**Current (i386):**
```
0x00000000            User: text+data+stack+heap
0x00000000-0x80000000 User address space (2 GB)
0x80000000            KERNBASE
0x80000000-0x80100000 Identity map (low memory, I/O space)
0x80100000            Kernel code starts (KERNLINK)
0x80100000-0x??       Kernel ELF image
0xFE000000            DEVSPACE (devices)
0xFFFFFFF0-0xFFFFFFFF BIOS/ROM
```

**New (amd64):**
```
0x00000000-0x00007FFFFFFFFFFF User: full 47-bit space (140 TB)
0xFFFF800000000000     KERNBASE (typical)
0xFFFF800000000000-0xFFFF8001 Identity map (low physical)
0xFFFF800000100000    Kernel code starts
0xFFFFFFFF80000000    Alternative KERNBASE (sometimes used)
0x... (device mappings high)
```

---

## 2. KERNEL HEADERS & CORE TYPES

### 2.1 Type System Overhaul

**Current (include/types.h):**
```c
typedef unsigned int   uint;      // 32 bits
typedef signed int     sint;      // 32 bits
typedef unsigned long  ulong;     // 32 bits
typedef signed long    slong;
typedef unsigned short ushort;    // 16 bits
typedef signed char    schar;
typedef unsigned char  uchar;
typedef uint pde_t;               // Page directory entry = 32-bit uint
```

**Required changes:**
1. **New stdint.h definitions** (add if not present):
   ```c
   #include <stdint.h>
   typedef uint32_t uint32;
   typedef uint64_t uint64;
   typedef intptr_t intptr;
   typedef uintptr_t uintptr;
   typedef ptrdiff_t ptrdiff;
   ```

2. **Address-related types must become 64-bit:**
   ```c
   // OLD (i386):
   uint sz;          // User memory size
   uint *esp;        // Register value
   uint eip;         // Instruction pointer
   
   // NEW (amd64):
   uint64_t sz;
   uint64_t rsp;
   uint64_t rip;
   ```

3. **Type casts change:**
   ```c
   // OLD: (uint)ptr — truncates on amd64!
   // NEW: (uintptr_t)ptr or (uint64_t)ptr
   ```

4. **Page directory/table entries become 64-bit:**
   ```c
   typedef uint64_t pde_t;
   typedef uint64_t pte_t;
   ```

**Files affected:**
- include/types.h: Type aliases
- include/stdint.h or new: Standard integer types
- **All kernel files** that cast pointers or use `uint` for addresses
- include/fs.h: File sizes, inode fields
- include/proc.h: struct proc, struct trapframe

### 2.2 Struct Definitions Affected

#### struct trapframe (include/x86.h)

**Current (i386):**
```c
struct trapframe {
  uint edi, esi, ebp, oesp, ebx, edx, ecx, eax;
  ushort gs, padding1;
  ushort fs, padding2;
  ushort es, padding3;
  ushort ds, padding4;
  uint trapno;
  uint err;
  uint eip;
  ushort cs, padding5;
  uint eflags;
  uint esp;
  ushort ss, padding6;
};
```
- **Size**: ~68 bytes
- **All 32-bit fields**

**New (amd64):**
```c
struct trapframe {
  // Caller-saved registers
  uint64_t r15, r14, r13, r12;
  uint64_t r11, r10, r9, r8;
  uint64_t rsi, rdi, rcx, rdx;
  uint64_t rax;
  
  // Kernel-managed
  uint64_t gs_base;  // GS segment base
  uint64_t fs_base;  // FS segment base
  
  // Exception frame (pushed by CPU)
  uint64_t rip;
  uint64_t cs;
  uint64_t rflags;
  uint64_t rsp;
  uint64_t ss;
  
  // Error code & trap number
  uint64_t err;
  uint64_t trapno;
};
```
- **Size**: ~150+ bytes (more registers to save)
- **No segment registers** (GS/FS base stored separately)

**Impact**: Trapframe layout changes break all trap handling code.

**Files affected:**
- include/x86.h: struct trapframe definition
- kernel/core/trapasm.S: Trap frame construction
- kernel/core/trap.c: Trap dispatch, access to trapframe fields
- kernel/vm/fault.c: Page fault handler
- kernel/core/trap_diag.c: Diagnostic printing

#### struct context (include/proc.h)

**Current (i386):**
```c
struct context {
  uint edi, esi, ebx, ebp, eip;
};
```
- **Size**: 20 bytes (5 x uint32)

**New (amd64):**
```c
struct context {
  uint64_t rbx, rsp, rbp;
  uint64_t r12, r13, r14, r15;
  uint64_t rip;
};
```
- **Size**: 64 bytes (8 x uint64)

**Impact**: swtch() assembly changes, context allocation/layout changes.

**Files affected:**
- include/proc.h: struct context
- kernel/core/swtch.S: Save/restore logic

#### struct proc (include/proc.h)

**Key fields that depend on architecture:**
```c
struct proc {
  uint sz;              // → uint64_t: User memory size
  uint *esp;            // → uint64_t rsp
  uint eip;             // → uint64_t rip
  uint sig_handler[NSIG];  // → uint64_t sig_handler[]
};
```

#### struct cpu (include/proc.h)

```c
struct cpu {
  struct taskstate ts;      // → tss64
  struct segdesc gdt[NSEGS]; // → 16-byte descriptors
};
```

### 2.3 Address Sizing and KERNBASE Changes

**Current constants (include/memlayout.h):**
```c
#define KERNBASE 0x80000000
#define KERNLINK (KERNBASE+EXTMEM)
#define PHYSTOP  0x20000000

#define V2P(a) (((uint) (a)) - KERNBASE)
#define P2V(a) ((void *)(((char *) (a)) + KERNBASE))
```

**New (amd64 typical):**
```c
#define KERNBASE 0xFFFF800000000000
#define KERNLINK (KERNBASE + 0x100000)
#define PHYSTOP  0x20000000

#define V2P(a) (((uint64_t)(a)) - KERNBASE)
#define P2V(a) ((void *)((uint64_t)(a) + KERNBASE))
```

**Implications:**
- All kernel virtual addresses now > 0xFFFF800000000000
- Linker script changes: kernel linked at new KERNBASE
- RIP-relative addressing required in bootloader and early code
- Early boot uses identity or temporary mapping before paging enabled

**Files affected:**
- include/memlayout.h: KERNBASE, V2P, P2V macros
- config/kernel.ld: Link address (OUTPUT_ARCH, entry point)
- All code using `(uint)addr` must change to `(uint64_t)addr`

---

## 3. BUILD SYSTEM

### 3.1 Makefile Changes

**Current (Makefile):**
```makefile
TOOLPREFIX := $(shell if i386-jos-elf-objdump -i 2>&1 | grep '^elf32-i386$$' >/dev/null 2>&1; \
  then echo 'i386-jos-elf-'; \
  elif test -x '$(CROSS_BINDIR)/i386-jos-elf-objdump' && ...; \
    then echo '$(CROSS_BINDIR)/i386-jos-elf-'; \
  ...
  else echo "*** Error: Couldn't find an i386-*-elf version of GCC/binutils." 1>&2; ...
```

**Changes needed:**
1. **Add amd64 toolchain detection:**
   ```makefile
   ifdef AMD64
   TOOLPREFIX := $(shell if x86_64-elf-objdump -i 2>&1 | grep '^elf64-x86-64$$' >/dev/null 2>&1; \
     then echo 'x86_64-elf-'; \
     elif test -x '$(CROSS_BINDIR)/x86_64-elf-objdump' ...; \
       then echo '$(CROSS_BINDIR)/x86_64-elf-'; \
     else error "x86_64-elf cross-compiler not found"
   else
     # Current i386 detection
   endif
   ```

2. **Update QEMU selection:**
   ```makefile
   ifdef AMD64
   QEMU ?= qemu-system-x86_64
   else
   QEMU ?= qemu-system-i386
   endif
   ```

3. **Compiler flags for amd64:**
   ```makefile
   ifdef AMD64
   CFLAGS += -m64 -mcmodel=kernel -fno-red-zone
   ASFLAGS += --64
   LDFLAGS += -m elf_x86_64
   else
   CFLAGS += -m32
   ASFLAGS += --32
   LDFLAGS += -m elf_i386
   endif
   ```

4. **Linker script selection:**
   ```makefile
   ifdef AMD64
   LD_SCRIPT = config/kernel-amd64.ld
   else
   LD_SCRIPT = config/kernel.ld
   endif
   ```

**Files affected:**
- Makefile: Lines 160-190 (toolprefix), add architecture switch
- config/kernel.ld: Create new `kernel-amd64.ld`

### 3.2 Linker Script (config/kernel.ld) → config/kernel-amd64.ld

**Current (config/kernel.ld):**
```ld
OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
  . = 0x80100000;
  .text : AT(0x100000) { ... }
  ...
}
```

**New (kernel-amd64.ld):**
```ld
OUTPUT_FORMAT("elf64-x86-64", "elf64-x86-64", "elf64-x86-64")
OUTPUT_ARCH(x86-64)
ENTRY(_start)

SECTIONS
{
  . = 0xFFFF800000100000;  // Link address (high half)
  
  .text : AT(0x100000) { ... }  // Load address still low (physical)
  
  /* Align sections, assertions on size as before */
  ...
  
  ASSERT((end - 0xFFFF800000000000) <= 0x??, "kernel too large for early-boot mapping")
}
```

**Key differences:**
- `OUTPUT_FORMAT` → elf64
- `OUTPUT_ARCH` → x86-64
- Link address → 0xFFFF800000100000
- Load address stays 0x100000 (physical)
- Size assertions must account for 64-bit math

---

## 4. C LIBRARY (libc) — SYSCALL ABI

### 4.1 Syscall Invocation Mechanism

**Current (user/usys.S):**
```asm
#define SYSCALL(name) \
  .globl name; \
  name: \
    movl $SYS_ ## name, %eax; \
    int $T_SYSCALL; \
    ret
```

- **Mechanism**: `int 64` (interrupt gate)
- **Calling convention**: `eax` = syscall number, args in custom registers
- **Return**: `%eax` = result (or -1 for error)

**Amd64 Option A: Keep `int` (backward compat):**
```asm
#define SYSCALL(name) \
  .globl name; \
  name: \
    movq $SYS_ ## name, %rax; \
    int $0x80; \
    ret
```
- Simple, minimal change
- `rax` = syscall number
- Args in `%rdi, %rsi, %rdx, %r10, %r8, %r9` (x86-64 ABI)
- Interrupt overhead: ~100 cycles

**Amd64 Option B: Use `syscall` instruction (modern, faster - RECOMMENDED):**
```asm
#define SYSCALL(name) \
  .globl name; \
  name: \
    movq $SYS_ ## name, %rax; \
    movq %rcx, %r10;  /* rcx clobbered by syscall */ \
    syscall; \
    cmp $-4095, %rax; \
    jae 1f; \
    ret; \
  1: negl %eax; \
    movl %eax, %edi; \
    call __set_errno; \
    movq $-1, %rax; \
    ret
```
- Faster: ~20-30 cycles
- Requires MSR setup: `LSTAR` (syscall entry RIP), `STAR` (segments)
- More complex kernel-side handler

**Recommendation**: Option B (syscall), but requires:
- Kernel must set MSR_LSTAR, MSR_STAR before entering userspace
- Separate kernel entry point for syscall
- User library must reload %rcx from %r10 on return

**Files affected:**
- user/usys.S: Macro definition, all SYSCALL invocations
- kernel/core/syscall.c: Syscall dispatcher, error handling
- kernel/core/trap.c: Syscall entry (if using int), MSR setup (if using syscall)

### 4.2 Calling Convention Changes

**i386 cdecl (custom variant for syscalls):**
- Arguments: pushed on stack (right to left), or in registers (custom)
- Return: `%eax` (32-bit), `%edx:%eax` (64-bit if needed)
- Caller cleanup
- Caller-saved: eax, ecx, edx
- Callee-saved: ebx, esi, edi, ebp, esp

**amd64 System V AMD64 ABI (standard for x86-64):**
- Arguments (first 6): `%rdi, %rsi, %rdx, %rcx, %r8, %r9` (remaining on stack)
- Return: `%rax` (64-bit), `%rdx:%rax` (128-bit if needed)
- Caller cleanup
- Caller-saved: rax, rcx, rdx, rsi, rdi, r8-r11
- Callee-saved: rbx, rbp, r12-r15, rsp
- **Red zone**: 128 bytes below RSP (not touched by signal handlers, interrupts)

**Implications:**
- All function calls change argument passing
- **Integer**: Now passes first 6 via registers (faster!)
- **Pointers**: Become 64-bit (just wider registers)
- **Stack alignment**: Must be 16-byte aligned at function entry

**Files affected:**
- ALL C code uses function calls; no changes needed if compiled with amd64 compiler
- BUT: inline asm, syscall wrappers, signal handlers must be rewritten

### 4.3 Type Changes in libc Headers

**include/setjmp.h** (if exists):
```c
// OLD: jmp_buf holds 6 x uint = 24 bytes
uint jmp_buf[6];  // ebx, esi, edi, ebp, esp, eip

// NEW: 8 x uint64 = 64 bytes
uint64_t jmp_buf[8];  // rbx, rsp, rbp, r12-r15, rip
```

**include/stdio.h**, **include/stdlib.h**, etc.:
- Off_t changes if 32-bit (add lseek64 support)
- Printf format specifiers (%p for pointers widens to 16 hex digits)

---

## 5. KERNEL SUBSYSTEMS

### 5.1 Virtual Memory / Paging (kernel/core/vm.c, kernel/vm/)

**Complexity: VERY HIGH**

#### Current Code Patterns

1. **Page directory/table allocation** (vm.c):
   ```c
   pde_t *pgdir;
   pgdir = (pde_t*)kalloc();
   memset(pgdir, 0, PGSIZE);
   ```
   - Works the same, but pgdir is now 64-bit entries

2. **Walking page tables** (walkpgdir function):
   ```c
   pde_t *pde = &pgdir[PDX(va)];
   if(!(*pde & PTE_P)) return 0;
   pte_t *pte = (pte_t*)P2V(PTE_ADDR(*pde));
   return &pte[PTX(va)];
   ```
   - **Must change**: PDX/PTX macros handle 2 levels; need 4 levels
   - New function: walkaddr(pgdir, va, level) for 4-level walk

3. **Setting page table entries** (setupkvm, mappages):
   ```c
   *pte = pa | pte_flags;
   ```
   - Changes from 32-bit assignment to 64-bit
   - Paging bit layout different (bits [51:12] vs [31:12])

#### Changes Required

1. **Rewrite walkpgdir → walkpgdir64:**
   - Input: pgdir (PML4), va (virtual address)
   - Return: pointer to PTE in level-1 page table
   - Allocate intermediate tables as needed (PDPT, PD, PT)

2. **Update mappages() for 4-level walk:**
   - For each virtual page va to va+size
   - Call walkpgdir64 to find/create PTE
   - Set PTE |= pa

3. **Update page table entry structure:**
   ```c
   #define PTE_ADDR(pte) ((pte) & 0xFFFFFFFFF000ULL)  // Bits 51:12
   #define PTE_FLAGS(pte) ((pte) & 0xFFFUL)           // Bits [11:0]
   #define PTE_P       0x001
   #define PTE_W       0x002
   #define PTE_U       0x004
   #define PTE_PWT     0x008  // Page Write-Through
   #define PTE_PCD     0x010  // Page Cache Disable
   #define PTE_A       0x020  // Accessed
   #define PTE_D       0x040  // Dirty
   #define PTE_PS      0x080  // Page Size
   #define PTE_G       0x100  // Global
   #define PTE_AVAIL1  0x200  // Available for software
   #define PTE_AVAIL2  0x400
   #define PTE_AVAIL3  0x800
   #define PTE_NX   0x8000000000000000ULL  // Execute-Disable (NEW, bit 63)
   ```

4. **Update seginit() to set MSR_EFER[NXE]** for Execute-Disable support

5. **Page fault handler** (kernel/vm/fault.c):
   - Reads CR2 (same in amd64)
   - But walkpgdir logic changed
   - Error code interpretation identical

**Files affected:**
- kernel/core/vm.c: ~1000 lines, seginit(), kvmalloc(), mappages(), walkpgdir(), deallocuvm(), copyuvm()
- include/mmu.h: NPDENTRIES, NPTENTRIES, PDX, PTX, PTE_* constants
- kernel/vm/fault.c: Page fault handling
- kernel/core/proc_lifecycle.c: allocuvm, deallocuvm for process address spaces
- kernel/core/entry.S: Early paging setup uses entrypgdir

### 5.2 Process Management (kernel/core/proc*.c)

**Complexity: HIGH**

#### Changes

1. **struct proc fields** (include/proc.h):
   ```c
   uint sz;  // → uint64_t
   uint *esp, eip;  // → uint64_t rsp, rip
   uint sig_handler[NSIG];  // → uint64_t sig_handler[]
   ```

2. **Process creation** (allocproc):
   - Push context onto kernel stack
   - Save entry point address (rip vs eip)

3. **fork/exec** (kernel/core/proc_lifecycle.c):
   - Copy page directory: walkpgdir → walkpgdir64
   - User stack setup changes

4. **Signal handling** (kernel/core/proc_signal.c, user/setjmp.S):
   - Signal handler entry: set up register arguments
   - Stack unwinding differs (RSP vs ESP)
   - Sigreturn restores from signal frame

**Files affected:**
- include/proc.h: struct proc, struct context
- kernel/core/proc_lifecycle.c: allocproc, fork, exit, wait
- kernel/core/proc.c: Process state management
- kernel/core/proc_signal.c: Signal delivery, mask, handlers
- kernel/core/sysproc.c: Syscalls affecting process state

### 5.3 File System (kernel/fs/)

**Complexity: MEDIUM** (mostly type changes, not logic)

#### Changes

1. **File structure sizes** (include/fs.h):
   - Most FS code uses relative block/inode addressing (uint32, which is fine)
   - File offsets become 64-bit naturally

2. **Ext2 filesystem** (kernel/fs/vfs_ext2.c):
   - Inode block pointers: still 32-bit, no change
   - File size field: no amd64-specific issue

3. **Other filesystems** (btrfs, ufs2, exfat, tmpfs):
   - Check for hardcoded 32-bit offsets
   - Most safe with 64-bit changes

4. **Memory copy for user buffers** (kernel/fs/file.c, kernel/fs/fs.c):
   ```c
   if((uint)addr < KERNBASE) { /* User pointer */ }
   // Changes: uint → uint64_t
   ```

**Files affected:**
- include/fs.h: struct dirent, struct dinode (mostly unchanged)
- kernel/fs/vfs_ext2.c: Inode handling, block addressing
- kernel/fs/file.c: User pointer checks
- kernel/fs/fs.c: User space I/O checks
- kernel/fs/procfs.c: User pointer checks
- Other fs: vfs_btrfs.c, vfs_ufs2.c

### 5.4 Interrupt & Exception Handling (kernel/core/trap*, vectors, irq)

**Complexity: VERY HIGH**

#### Changes

1. **IDT structure** (include/mmu.h):
   - 8 bytes → 16 bytes per descriptor
   - Gate descriptor format changes

2. **Interrupt entry stubs** (kernel/core/vectors.S):
   - Generate 256 entry points (one per interrupt)
   - Each stub pushes trapno/errcode and jumps to alltraps
   - **amd64 version must push 64-bit registers, handle IST**

3. **alltraps in trapasm.S** (kernel/core/trapasm.S):
   - Push all 16 GPRs, different trapframe layout

4. **Trap dispatcher** (kernel/core/trap.c):
   - Receives trapframe *tf
   - Dispatches on tf->trapno
   - Almost no changes, but field accesses change (tf->eip → tf->rip)

5. **Per-CPU interrupt stack (IST)**:
   - amd64 IDT entries can specify IST index (1-7)
   - Each CPU has IST[7] entries (64-bit addresses) in TSS64
   - Used for double-faults, NMI, stack overflow isolation
   - Setup in seginit() when initializing tss

**Files affected:**
- kernel/core/vectors.S: 256 interrupt stubs (regenerate)
- kernel/core/trapasm.S: alltraps, trapret (rewrite)
- kernel/core/trap.c: trap() dispatcher
- include/mmu.h: gatedesc → gatedesc64, IDT layout
- kernel/core/vm.c: seginit() IDT setup

### 5.5 Device Drivers (kernel/driver/)

**Complexity: LOW to MEDIUM** (I/O mostly unchanged)

#### Changes

1. **Architecture-independent I/O**:
   - inb, outb, inl, outl (include/x86.h): asm syntax may need updating, semantics same
   - Memory-mapped I/O: still via pointers, just wider

2. **Driver data structures**:
   - Most drivers use hardware-defined structs
   - Some may have embedded addresses (DMA descriptors)
   - These pointers must be 64-bit if needed

3. **Specific drivers**:
   - **IDE**: Using port I/O, no changes
   - **AHCI**: MMIO, HBA structs likely already 64-bit safe
   - **NVMe**: Commands likely spec-compliant, safe
   - **UART/Serial**: Port I/O only, no changes
   - **Keyboard, mouse**: Port I/O, no changes
   - **PCI**: BARs handling unchanged
   - **Audio**: MMIO/DMA likely already safe

**Files affected:**
- include/x86.h: inb, outb, inl, outl syntax
- kernel/driver/ahci.c: DMA address fields
- kernel/driver/nvme.c: Command structures
- **Other drivers**: Minimal changes expected

### 5.6 Multiprocessor Support (kernel/core/mp.c, entryother.S)

**Complexity: HIGH**

#### Changes

1. **AP startup** (entryother.S):
   - Already discussed: Bootloader code rewritten for long mode

2. **APIC addressing**:
   - Currently: MMIO APIC mapped at DEVSPACE (0xFE000000)
   - amd64: Can use MSR-based xAPIC or keep MMIO

3. **cpu_apicid_cpuid()** (include/x86.h):
   ```c
   // No change: cpuid $1 returns apic id in ebx[31:24]
   ```

4. **Per-CPU storage**:
   - GS segment (in i386, points to per-CPU struct)
   - amd64: Can use GS.base MSR (IA32_GS_BASE, IA32_KERNEL_GS_BASE)
   - Or keep loading GS selector

**Files affected:**
- kernel/boot/entryother.S: AP bootloader
- kernel/core/mp.c: Per-CPU setup, APIC config
- include/x86.h: cpu_apicid_cpuid

### 5.7 Locking & Synchronization (kernel/core/spinlock.c, sleeplock.c)

**Complexity: LOW**

#### Changes

1. **xchg atomic operation** (include/x86.h):
   ```c
   // OLD: uint xchg(volatile uint *addr, uint newval)
   // NEW: uint64_t xchg(volatile uint64_t *addr, uint64_t newval)
   ```
   - Inline asm: `lock; xchgq %0, %1` (q suffix for 64-bit)

2. **Spinlock structure** (include/spinlock.h):
   - No changes (still uint locked field)

**Files affected:**
- include/x86.h: xchg() inline asm
- kernel/core/spinlock.c: No changes

---

## 6. USER SPACE / APPLICATION BINARY INTERFACE (ABI)

**Complexity: HIGH**

### 6.1 ELF Executable Format

**Current (ELF32):**
- Magic: 0x7F "ELF"
- Class: ELFCLASS32 (1)
- Data: ELFDATA2LSB (little-endian)
- Machine: EM_386 (3)
- Entry point: 32-bit address
- Program header addresses: 32-bit

**New (ELF64):**
- Magic: Same 0x7F "ELF"
- Class: ELFCLASS64 (2)
- Data: ELFDATA2LSB (same)
- Machine: EM_X86_64 (62)
- Entry point: 64-bit address
- Program header addresses: 64-bit

**Code changes:**
- include/elf.h: struct elfhdr, struct proghdr to use 64-bit fields
- kernel/core/exec.c: ELF loader parsing changes

### 6.2 User Stack Layout

**i386 argument passing (stack-based):**
```
[RSP]     return address
[RSP+4]   argc
[RSP+8]   argv (pointer to array of strings)
[RSP+12]  envp
```

**amd64 argument passing (register-based, then stack):**
```
[RDI]     argc
[RSI]     argv
[RDX]     envp (if third arg)

[RSP]     return address (pushed by call)
[RSP+8]   first stack arg (if more than 6 args)
[RSP+16]  second stack arg
...

Red zone: [RSP-128] to [RSP-1]
```

**Impact on exec.c:**
- Argument construction (user/initcode.S must change)
- Main() call: main(argc, argv) still valid (rdi, rsi)

### 6.3 User Program Headers & Startup

**crt0.S changes:**
```c
// OLD: Extract argc/argv from stack
movl 4(%esp), %eax  // argc
movl 8(%esp), %edx  // argv

// NEW: Extract from registers
mov %rdi, %rax  // argc comes in rdi
mov %rsi, %rdx  // argv comes in rsi
```

**Changes:**
- user/crt0.S: Extract argc/argv from rdi/rsi

### 6.4 Shared Libraries & Dynamic Linking

**Not currently in scope** (XV6-like systems are typically statically linked), but if supported:
- ELF dynamic section changes (ELF64)
- Relocation entries become 64-bit
- PLT/GOT entries become 64-bit

---

## 7. CROSS-COMPILATION & TOOLS

### 7.1 Toolchain Requirements

**Current:**
- **Compiler**: i386-jos-elf-gcc
- **Assembler**: i386-jos-elf-as
- **Linker**: i386-jos-elf-ld
- **Binutils**: objdump, objcopy, nm for i386

**Required for amd64:**
- **Compiler**: x86_64-elf-gcc (or x86_64-unknown-elf-gcc)
- **Assembler**: x86_64-elf-as
- **Linker**: x86_64-elf-ld
- **Binutils**: x86_64-elf-{objdump, objcopy, nm, addr2line}

**Installation** (macOS typical):
```bash
# Using homebrew
brew install x86_64-elf-binutils x86_64-elf-gcc

# Or cross-compile from source
# https://github.com/lordmilko/i686-elf-tools
```

### 7.2 mkfs_asan or Filesystem Creation Tools

**Current:**
- mkfs_asan builds initial filesystem image
- No architecture-specific code (generates data, not machine code)

**Changes needed:**
- Recompile for host (likely no changes)
- Verify block size, inode layout unchanged

**Files affected:**
- tools/mkfs_asan: If source, recompile (unchanged logic)

### 7.3 Utility Programs in user/

**Binaries like _file, _lsof, _which, etc.:**
- Recompile with x86_64-elf-gcc
- Most are architecture-independent
- Some may have hardcoded sizeof(int*) comparisons

**Files affected:**
- user/*.c: All user programs — recompile with x86_64-elf-gcc

---

## 8. MIGRATION DEPENDENCY GRAPH

**This ordering matters — some changes are prerequisites:**

```
PHASE 0: Preparation
├─ Set up amd64 cross-compiler toolchain
├─ Create Makefile AMD64 build option
└─ Create config/kernel-amd64.ld linker script

PHASE 1: Assembly & Boot (Blocker for everything else)
├─ Rewrite kernel/boot/bootasm.S for 64-bit
├─ Rewrite kernel/core/entry.S for 64-bit paging
├─ Rewrite kernel/boot/entryother.S for AP startup
└─ Verify bootloader loads, prints "hello"

PHASE 2: Core MMU & VM (Blocker for processes)
├─ Update include/mmu.h: segdesc64, gatedesc64, PTE/PDE definitions
├─ Update include/memlayout.h: KERNBASE, V2P, P2V macros
├─ Rewrite kernel/core/vm.c: walkpgdir64, mappages, seginit
├─ Update kernel/core/entry.S early paging for 4-level tables
└─ Test with hardcoded kernel-only setup (no processes yet)

PHASE 3: Interrupt & Trap Handling
├─ Regenerate kernel/core/vectors.S for amd64 entry stubs
├─ Rewrite kernel/core/trapasm.S: trap frame, alltraps, trapret
├─ Update include/x86.h: struct trapframe (64-bit)
├─ Update kernel/core/trap.c: vector access, trap dispatch
├─ Verify interrupts fire, return correctly
└─ Test timer ticks, keyboard input

PHASE 4: Process Management
├─ Update include/proc.h: struct context, struct proc
├─ Rewrite kernel/core/swtch.S for context switching
├─ Update kernel/core/proc_lifecycle.c: allocproc, fork, exec
├─ Verify process creation, context switches, exit
└─ Test single-process operation (init alone)

PHASE 5: Syscall ABI
├─ Rewrite user/usys.S: syscall macro (int $0x80 or syscall)
├─ Update kernel/core/syscall.c: Dispatch with amd64 ABI
├─ Update user/crt0.S: Argument passing (rdi/rsi)
└─ Test simple calls: fork, exit, write

PHASE 6: Type System & User I/O
├─ Update include/types.h: uint → uint32, add uint64_t
├─ Update all user pointer checks: (uint)addr → (uint64_t)addr
├─ Update kernel/fs/file.c, fs.c: KERNBASE checks
├─ Verify user programs can read/write files
└─ Test: exec init, run shell

PHASE 7: Userland Compilation
├─ Recompile all user/*.c programs with x86_64-elf-gcc
├─ Update user/setjmp.S for 64-bit setjmp/longjmp
├─ Test: Run multiple utilities, test signal handling
└─ Full userland operational test

PHASE 8: Device Drivers & Features
├─ Audit & test each driver:
│  ├─ IDE/AHCI disk I/O
│  ├─ Networking drivers
│  ├─ Audio drivers
│  ├─ Graphics/framebuffer
│  └─ Keyboard/mouse/serial
├─ Fix any issues in amd64 semantics
└─ Full feature test

PHASE 9: Performance & Cleanup
├─ Optimize if needed
├─ Remove old i386-specific code
├─ Documentation update
└─ Regression testing
```

---

## 9. DETAILED FILE LIST WITH CHANGE SEVERITY

| File | Purpose | Current | **Required Changes** | **Complexity** | **Risk** |
|------|---------|---------|----------------------|---|---|
| Makefile | Build driver | i386-jos-elf toolchain | Add amd64 detection, conditional TOOLPREFIX, add kernel-amd64.ld target | **MEDIUM** | **MEDIUM** |
| config/kernel.ld | Kernel linker script | elf32-i386, 0x80100000 | Create kernel-amd64.ld: elf64-x86-64, 0xFFFF800000100000 | **MEDIUM** | **LOW** |
| include/mmu.h | MMU definitions | 32-bit descriptors, PTE | Add gatedesc64, update PTE macros, NPDPENTRIES, NPDLEVELS | **HIGH** | **HIGH** |
| include/x86.h | x86 intrinsics | inb/outb, struct trapframe | Update syntax, 16-reg trapframe, MSR read/write | **HIGH** | **HIGH** |
| include/memlayout.h | Address constants | KERNBASE=0x80000000 | Change to 0xFFFF800000000000, update V2P/P2V | **LOW** | **MEDIUM** |
| include/types.h | Type aliases | uint32 as uint | Add uint64_t, verify pointer casts | **LOW** | **LOW** |
| include/proc.h | Process structure | 32-bit registers | Update context, proc (rsp/rip, uint64_t handlers) | **MEDIUM** | **MEDIUM** |
| include/elf.h | ELF format | 32-bit elfhdr | Support elf64 (64-bit vaddr/offset) | **LOW** | **LOW** |
| include/syscall.h | Syscall numbers | SYS_* constants | No changes (numbers same) | **LOW** | **LOW** |
| include/fs.h | Filesystem layout | struct dinode, dirent | No changes (block addressing stable) | **LOW** | **LOW** |
| kernel/boot/bootasm.S | Primary bootloader | .code16/.code32, GDT | Rewrite: .code16/.code64, long mode, MSR_EFER, 64-bit GDT | **VERY HIGH** | **VERY HIGH** |
| kernel/boot/entryother.S | AP startup | 32-bit mode, 4MB pages | Rewrite: 64-bit mode, PAE, 4-level PT, MSR setup | **VERY HIGH** | **HIGH** |
| kernel/core/entry.S | Kernel paging setup | CR4_PSE, PD setup | Rewrite: CR4_PAE, PML4/PDPT/PT, RIP-relative | **HIGH** | **HIGH** |
| kernel/core/trapasm.S | Trap frame construction | pushl %ds, pusha | Rewrite: pushq regs, 16-reg layout, no segments | **VERY HIGH** | **VERY HIGH** |
| kernel/core/vectors.S | Interrupt stubs | 256 entries, ljmpl | Regenerate: 256 entries, pushq trapno, jmp | **MEDIUM** | **HIGH** |
| kernel/core/swtch.S | Context switch | pushl ebp/ebx/esi/edi | Rewrite: movq callee-saved (rbx/r12-r15) | **MEDIUM** | **HIGH** |
| kernel/core/segreload.S | Segment reload | (if used) | Remove entirely (flat 64-bit) | **LOW** | **LOW** |
| kernel/core/vm.c | Paging engine | walkpgdir, mappages, seginit | Rewrite walkpgdir for 4-level, 64-bit GDT/IDT/TSS | **VERY HIGH** | **VERY HIGH** |
| kernel/core/trap.c | Trap dispatcher | trapframe access, dispatch | Update field accesses (eip→rip) | **MEDIUM** | **MEDIUM** |
| kernel/core/syscall.c | Syscall handler | arg extraction, dispatch | Update arg extraction (ABI-dependent) | **MEDIUM** | **MEDIUM** |
| kernel/core/proc_lifecycle.c | Process lifecycle | allocproc, fork, exec | Update context init, page walk, struct layout | **HIGH** | **HIGH** |
| kernel/core/proc.c | Process state | Process table, scheduling | Update proc struct layout | **LOW** | **MEDIUM** |
| kernel/vm/fault.c | Page fault handler | walkpgdir lookup | Update to walkpgdir64 | **MEDIUM** | **MEDIUM** |
| kernel/fs/file.c | File I/O | User pointer checks | Change (uint)addr to (uint64_t)addr | **LOW** | **MEDIUM** |
| kernel/fs/fs.c | Generic I/O | User pointer checks | Same as file.c | **LOW** | **MEDIUM** |
| kernel/fs/vfs_ext2.c | Ext2 FS | Block addressing, inode ops | User pointer checks updated | **LOW** | **LOW** |
| kernel/fs/vfs_*.c | Other FS | Block/inode addressing | User pointer checks, address updates | **LOW** | **LOW** |
| kernel/driver/ide.c | IDE driver | Port I/O | Update I/O syntax if needed | **LOW** | **LOW** |
| kernel/driver/ahci.c | AHCI driver | MMIO, DMA | DMA address fields audit | **LOW** | **MEDIUM** |
| kernel/driver/*.c | Other drivers | Port I/O, MMIO | Audit for 32-bit assumptions | **LOW** | **LOW** |
| user/usys.S | User syscall stubs | movl, int 64 | Rewrite movq, int 0x80 or syscall | **MEDIUM** | **HIGH** |
| user/crt0.S | User startup | Stack arg access | Change to rdi/rsi register args | **MEDIUM** | **MEDIUM** |
| user/setjmp.S | User setjmp/longjmp | 6-int jmp_buf | Change to 8-uint64 jmp_buf | **MEDIUM** | **MEDIUM** |
| user/initcode.S | Init process | pushl argv | Change arg passing to registers | **LOW** | **LOW** |
| user/*.c | User utilities | i386 cross-compiler | Recompile with x86_64 cross-compiler | **LOW** | **LOW** |

---

## 10. SUMMARY: CRITICAL PATH & EFFORT ESTIMATE

### Longest Dependency Chain (Critical Path)

```
Toolchain setup (1 week)
  ↓
bootasm.S rewrite (1 week)
  ↓
entry.S + kernel.ld rewrite (1 week)
  ↓
vm.c paging rewrite (2-3 weeks)
  ↓
trapasm.S + vectors.S rewrite (2 weeks)
  ↓
swtch.S + proc rewrite (1 week)
  ↓
usys.S syscall rewrite (1 week)
  ↓
User compilation + testing (1 week)
  ↓
Driver/feature testing (2-3 weeks)
  ↓
Regression testing (1-2 weeks)

TOTAL: ~16-21 weeks serial (can parallelize some late phases)
```

### Estimated Complexity Breakdown

| **Phase** | **Effort (weeks)** | **Risk** | **Blocker if Failed** |
|-----------|---|---|---|
| Phase 0 (Toolchain) | 0.5 | LOW | Everything |
| Phase 1 (Bootasm/Entry) | 3 | **VERY HIGH** | All further development |
| Phase 2 (VM/Paging) | 3 | **VERY HIGH** | Process management |
| Phase 3 (Traps/Interrupts) | 2.5 | **VERY HIGH** | Process execution |
| Phase 4 (Processes) | 1.5 | **HIGH** | Shell operation |
| Phase 5 (Syscalls) | 1 | **HIGH** | Shell operation |
| Phase 6 (Type fixes & I/O) | 1 | **MEDIUM** | File I/O |
| Phase 7 (Userland) | 1 | **LOW** | Feature demos |
| Phase 8 (Drivers) | 2-3 | **MEDIUM** | Full feature set |
| Phase 9 (Performance/Cleanup) | 1-2 | **LOW** | Production readiness |
| **TOTAL** | **16-20 weeks** | — | — |

---

## 11. ARCHITECTURE-SPECIFIC ASSUMPTIONS INDEX

**Quick reference of all i386-isms found:**

1. **32-bit registers** (eax, ebx, etc.) → 64-bit (rax, rbx, r8-r15)
2. **2-level paging** (PD, PT) → 4-level (PML4, PDPT, PD, PT)
3. **32-bit pointers/addresses** → 64-bit
4. **.code32 assembly** → .code16/.code64
5. **CR4_PSE (4MB pages)** → CR4_PAE (2MB pages, 36-bit physical)
6. **Segment-based addressing** → Flat 64-bit model
7. **8-byte GDT descriptors** → 16-byte (64-bit variants)
8. **8-byte IDT gates** → 16-byte with IST support
9. **Task State Segment** (esp0/ss0) → 64-bit (rsp0, IST array)
10. **struct trapframe** (68 bytes, 8 regs) → ~150+ bytes (16 regs)
11. **struct context** (20 bytes, 5 regs) → 64 bytes (8 regs)
12. **Stack-based syscall args** → Register-based (rdi, rsi, rdx, rcx, r8, r9)
13. **int $T_SYSCALL (int 64)** → int $0x80 or syscall (MSR-based)
14. **KERNBASE at 0x80000000** → 0xFFFF800000000000 (high half)
15. **ELF32 binaries** → ELF64 binaries
16. **uint for addressing** → uint64_t (or uintptr_t)

---

## 12. TESTING STRATEGY

After each phase, verify:

**Phase 1**: Bootloader executes, prints text, reaches entry point (serial output)

**Phase 2**: Paging enabled, kernel runs at high addresses, memory accessible

**Phase 3**: Interrupts fire (timer, keyboard), return correctly

**Phase 4**: Processes fork/exec, context switches, exit

**Phase 5**: Syscalls work (fork, write, exit), arguments pass correctly

**Phase 6**: File I/O works, user programs read/write filesystem

**Phase 7**: Shell runs, commands execute

**Phase 8**: Disk I/O, networking, graphics (if applicable)

**Phase 9**: Full system stability, regression test suite

---

## CONCLUSION

This migration is **non-trivial but achievable**. The critical path involves:

1. **Bootloader rewrite** (most error-prone, needs careful asm)
2. **Paging engine rewrite** (complex, foundational)
3. **Trap handling rewrite** (intricate, pervasive)
4. **Syscall ABI change** (moderate, well-defined)

**Estimated effort: 16-20 weeks** with one engineer.  
**Estimated risk: HIGH** — any mistake in phases 1-3 blocks all further progress.

Key success factors:
- Robust testing at each phase
- Early serial port debugging output
- Gradual feature enablement (kernel-only → processes → userland → drivers)
- Extensive OS testing after migration completion

The remaining areas (filesystems, drivers, userland compilation) are largely mechanical and low-risk once the core architecture is stable.
