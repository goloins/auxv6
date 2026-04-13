# GCC Target Config File Map (auxv6)

Date: 2026-04-13
Status: Implementation companion to the GCC port plan

## Purpose

This document is the patch-planning map for adding i386-auxv6-elf support to binutils and GCC. It is designed to answer three practical questions quickly:

1. Which upstream files do we change?
2. Why each file matters?
3. What is the smallest viable initial patch for each file?

It intentionally starts with minimal, low-risk bring-up and leaves optimization/feature expansion for later.

## Ground Rules For Stage-1

1. Target triple is fixed: i386-auxv6-elf.
2. C language only.
3. Static runtime only (no shared, no plugins, no LTO plugin dependency).
4. Thread model is explicitly single-thread for stage-1.
5. Keep patches additive and narrowly scoped.

## A) Binutils File Map

## A.1 Triplet Recognition

Likely files:

1. config.sub (or generated equivalent in your chosen binutils snapshot)
2. config.bfd
3. gas/configure.tgt
4. ld/configure.tgt

Why:

- Build system must recognize i386-auxv6-elf as a valid target.
- BFD and linker must map it to an existing i386 ELF backend/emulation.

Minimal strategy:

- Alias i386-auxv6-elf to existing i386-*-elf handling initially.
- Do not invent new relocation ABI or linker emulation unless required by observed failures.

Example pattern (illustrative):

```sh
case ${target} in
  i[3-7]86-auxv6-elf)
    targ_defvec=i386_elf32_vec
    ;;
esac
```

Validation command examples:

```sh
i386-auxv6-elf-as --version
i386-auxv6-elf-ld --verbose | head -n 40
i386-auxv6-elf-objdump -i | grep elf32-i386
```

Expected result:

- Tools exist and report i386 ELF capability.

## A.2 Linker Emulation Mapping

Likely files:

1. ld/configure.tgt
2. ld/emulparams/* (only if custom behavior required)

Why:

- ld must choose emulation script and defaults for the target.

Minimal strategy:

- Reuse elf_i386 emulation at first.
- Add custom emulparams only if default script conflicts with auxv6 user ABI/startfile flow.

Example trigger to add custom emulation later:

- Wrong default entry point/startfile ordering repeatedly requiring fragile linker flags.

## A.3 Optional: Assembler Target Alias Only

If gas does not need auxv6-specific syntax/ABI extensions, keep it mapped to generic i386 ELF.

Rule:

- No assembler behavior forks unless a concrete incompatibility is proven.

## B) GCC File Map

## B.1 Target Dispatch

Primary file:

1. gcc/config.gcc

Why:

- config.gcc is the target router: it selects backend files, tm_file chain, and target defaults.

Minimal patch intent:

- Add i[3-7]86-auxv6-elf case.
- Reuse i386 ELF base plus auxv6-specific header.

Illustrative shape:

```sh
i[3-7]86-*-auxv6-elf*)
  tm_file="i386/unix.h i386/att.h dbxelf.h elfos.h i386/i386elf.h auxv6.h"
  tmake_file="i386/t-i386elf t-svr4"
  ;;
```

Notes:

- Final tm_file list depends on GCC version and existing i386 target structure.
- Keep first patch minimal; avoid introducing speculative knobs.

## B.2 New OS/Target Header

Primary new file:

1. gcc/config/auxv6.h

Why:

- This is where auxv6 target personality lives: predefined macros, default spec strings, runtime assumptions.

Minimum contents for stage-1:

1. Builtin preprocessor defines:
- __auxv6__
- __unix__ (if desired for compatibility)
- __ELF__

2. Link/runtime defaults:
- static by default
- no shared-library assumptions

3. Startfile/lib specs:
- explicit crt begin/end ordering for auxv6 sysroot
- explicit libc/libgcc order

4. Feature policy defaults:
- no pthread runtime assumptions

Illustrative snippet (conceptual, not copy-paste ready):

```c
#define TARGET_OS_CPP_BUILTINS()      \
  do {                                 \
    builtin_define("__auxv6__");      \
    builtin_define("__ELF__");        \
  } while (0)

#define LIB_SPEC "-lc"
#define STARTFILE_SPEC "crt0.o crti.o crtbegin.o"
#define ENDFILE_SPEC "crtend.o crtn.o"
```

Important:

- Exact spec strings depend on your crt object naming in auxv6 sysroot.
- Keep this synchronized with docs/gcc-sysroot-layout.md.

## B.3 i386 Backend Integration

Likely files (existing):

1. gcc/config/i386/*

Why:

- You generally should not need i386 backend logic changes for initial bring-up.

Policy:

- Do not fork backend unless a concrete codegen or ABI mismatch appears.
- Prefer changing target specs in auxv6.h and build config first.

## B.4 libgcc Build Plumbing

Likely files:

1. libgcc/config.host
2. potentially libgcc/config/* mapping files depending on GCC version

Why:

- libgcc helper routines must be produced for i386-auxv6-elf.

Minimal strategy:

- Map i386-auxv6-elf to existing i386 ELF libgcc behavior.
- Keep unwind/eh complexity low at stage-1.

Validation:

```sh
i386-auxv6-elf-gcc -print-libgcc-file-name
i386-auxv6-elf-nm $(i386-auxv6-elf-gcc -print-libgcc-file-name) | grep -E "__divdi3|__udivdi3|__moddi3|__umoddi3"
```

## B.5 libstdc++ / gthr Policy (Stage-1)

Files to inspect but not fully enable yet:

1. libstdc++-v3/configure* and gthr selection paths

Policy:

- Stage-1 should avoid promising full C++ runtime.
- If you experiment early, force single-thread gthr path explicitly and document it.

## C) Host/Build Integration Map

## C.1 Top-Level Configure Guardrails

Files:

1. top-level configure.ac/configure fragments (only if needed)

Use case:

- If configure rejects target unexpectedly or selects wrong defaults.

Policy:

- Keep top-level changes minimal and target-scoped.

## C.2 Sysroot Plumbing Validation

No single upstream file; this is command/spec hygiene.

Critical checks:

```sh
i386-auxv6-elf-gcc -v hello.c -o hello
i386-auxv6-elf-gcc --print-sysroot
i386-auxv6-elf-gcc -print-search-dirs
```

Expected:

- Header/library lookup paths point to auxv6 sysroot, not host system directories.

## D) Patch Sequencing (Recommended)

1. Binutils triplet + BFD + ld mapping.
2. GCC config.gcc target stanza + new auxv6.h.
3. libgcc target mapping.
4. Sysroot integration/spec tuning.
5. Optional early libstdc++ experiment only after C bring-up is stable.

Why this order:

- Produces fast feedback at each layer with minimal blast radius.

## E) Example Minimal Bring-Up Command Flow

Assume:

- sources/binutils
- sources/gcc
- build/binutils
- build/gcc
- prefix=/opt/cross-auxv6
- sysroot=/opt/cross-auxv6/i386-auxv6-elf/sysroot

### E.1 Build binutils

```sh
mkdir -p build/binutils && cd build/binutils
../../sources/binutils/configure \
  --target=i386-auxv6-elf \
  --prefix=/opt/cross-auxv6 \
  --disable-nls --disable-werror
make -j$(nproc)
make install
```

### E.2 Build GCC stage-1

```sh
mkdir -p build/gcc && cd build/gcc
../../sources/gcc/configure \
  --target=i386-auxv6-elf \
  --prefix=/opt/cross-auxv6 \
  --with-sysroot=/opt/cross-auxv6/i386-auxv6-elf/sysroot \
  --enable-languages=c \
  --disable-bootstrap \
  --disable-nls \
  --disable-shared \
  --disable-threads \
  --disable-libssp \
  --disable-libquadmath \
  --disable-libgomp
make -j$(nproc) all-gcc all-target-libgcc
make install-gcc install-target-libgcc
```

## F) Failure Signatures And Likely File To Revisit

1. "target not recognized"
- Revisit config.sub/config.gcc/configure.tgt mapping.

2. "cannot find crt0.o" or wrong startfile order
- Revisit auxv6.h STARTFILE_SPEC/ENDFILE_SPEC and sysroot layout.

3. Host headers leaking into build
- Revisit --with-sysroot usage and GCC spec path order.

4. Missing libgcc arithmetic helpers
- Revisit libgcc/config.host mapping and target-libgcc build status.

5. Unexpected pthread or shared-runtime assumptions
- Revisit configure flags and any accidentally enabled runtime components.

## G) Upstream Hygiene Recommendations

1. Keep patches in logical series:
- 0001-binutils-target-recognition.patch
- 0002-gcc-target-stanza-and-auxv6h.patch
- 0003-libgcc-target-mapping.patch
- 0004-specs-sysroot-tuning.patch

2. Each patch should compile independently where possible.
3. Add a short commit message section titled "Why not more?" to prevent scope creep.

## H) Definition Of Done For This File Map

This file map is complete when:

1. Every touched upstream file has a tracked patch and rationale.
2. C cross compiler produces runnable auxv6 binaries.
3. No unresolved ambiguity remains about where a given bring-up failure should be debugged first.
