# auxv6 GCC Porting And Cross-Compile Plan

Date: 2026-04-13
Status: Planning document (implementation-ready)

## 1) Goal And Scope

This document defines a concrete, staged plan to:

1. Build a working cross-GCC targeting auxv6.
2. Bring up a minimal native GCC running on auxv6.
3. Identify and track required kernel/libc/runtime work to move from minimal C-only compiler to broader hosted-toolchain support.

The plan intentionally separates:

- Stage-A viability (cross compiler + minimal native C compiler), from
- Stage-B completeness (C++, stronger POSIX/runtime, threads, richer VM semantics).

## 2) Current-State Audit (auxv6)

This section captures audited facts from tree state and their impact on GCC.

### 2.1 Toolchain/Build Model

- Root Makefile expects an i386-ELF style cross toolchain (i386-jos-elf, i386-elf, or i686-elf prefix detection).
- Build is static/ELF oriented with explicit libgcc checks.
- Userland is currently static-link focused.

Impact:
- This strongly aligns with an initial i386-auxv6-elf target model.

### 2.2 Syscall/ABI Surface Highlights

- Syscall table exists for process/file/signal/network/resource families.
- No mmap syscall family in syscall numbering/dispatch.
- Exec argument budget is small (EXEC_ARG_BYTES_MAX = 4096).

Impact:
- Lack of kernel mmap is a practical limitation for large compiler workloads.
- Small exec argument capacity is a likely early failure mode during configure/build/link command expansion.

### 2.3 libc/POSIX Surface Highlights

- C locale only (minimal locale implementation).
- pthread API exists as single-thread stubs; pthread types are placeholders.
- mman API exists, but user-side mmap is malloc-backed anonymous emulation; many memory-management calls are ENOSYS.
- Compatibility shims exist for times(), faccessat() limitations, and other partial semantics.

Impact:
- Minimal C compiler is feasible.
- Full hosted runtime expectations (especially C++ threading and broader VM semantics) are not yet met.

## 3) Feasibility Summary

### 3.1 Feasible Now (with disciplined scope)

- Cross binutils + cross GCC for C language.
- Static target runtime (no shared libs/plugins/LTO plugins initially).
- Minimal native compiler binaries (C only), after cross bootstrap path is in place.

### 3.2 Not Yet Feasible As "Complete" GCC Platform

Without additional kernel/libc work, the following are constrained:

- Full libstdc++ expectations with real thread model.
- mmap-heavy or shared-library-oriented workflows.
- Broad configure parity with mature Unix hosts.

## 4) Target Identity And Platform Contract

Define this contract first; all later work depends on it.

Recommended canonical target triple:

- i386-auxv6-elf

Rationale:

- Architecture matches current system build model.
- ELF/static baseline already aligns with current linker/runtime usage.
- Avoids overpromising glibc-like or linux-like semantics.

Initial platform contract:

- 32-bit i386 ELF user programs
- static linking only
- C locale only
- single-thread runtime model
- no dynamic loader requirement

## 5) Staged Execution Plan

## Phase 0: Contract Freeze (1-2 days)

Deliverables:

1. Written target contract (triple, ABI assumptions, runtime constraints).
2. Explicit non-goals for stage-1 compiler.
3. Baseline test list for cross-produced binaries in auxv6.

Exit criteria:

- Triple and scope accepted.
- Success criteria agreed for Phase 1.

## Phase 1: Cross Binutils Bring-Up (2-4 days)

Objective:
- Produce assembler/linker utilities for i386-auxv6-elf.

Expected upstream touchpoints (binutils):

1. config.sub / config.gcc-style triplet recognition paths (where applicable in binutils build machinery).
2. BFD target selection hooks for i386 ELF aliasing.
3. gas target config path for i386-elf compatibility.
4. ld emulation mapping (reuse existing i386 ELF emulation initially).

Implementation strategy:

- Start by aliasing i386-auxv6-elf to existing i386-elf machinery.
- Avoid custom emulation unless a concrete linker-script or relocation mismatch appears.

Validation:

- as/ld/objdump/ar/nm for i386-auxv6-elf exist and can assemble/link trivial test.

## Phase 2: GCC Target Skeleton (3-6 days)

Objective:
- Build cross GCC (C only) that targets auxv6 userland.

Expected upstream touchpoints (GCC):

1. gcc/config.gcc
- Add i[3-7]86-auxv6-elf target stanza.
- Select i386 backend defaults and auxv6 OS header.

2. gcc/config/auxv6.h (new)
- Predefined macros (__auxv6__, etc.).
- Target default specs for startfiles/libs.
- Force static/no-shared defaults for stage-1.

3. gcc/config/i386/* integration
- Reuse existing i386 ELF paths unless auxv6-specific ABI constraints require overrides.

4. libgcc config glue
- Ensure libgcc builds for target with expected helper symbols.

5. fixincludes behavior
- Confirm it does not mangle target sysroot headers unexpectedly.

Recommended stage-1 configure profile:

- --target=i386-auxv6-elf
- --prefix=<toolchain-prefix>
- --enable-languages=c
- --disable-nls
- --disable-shared
- --disable-threads
- --disable-libssp
- --disable-libquadmath
- --disable-libgomp
- --disable-libatomic (if needed initially)
- --without-headers (only for earliest bootstrap step) OR use a prepared sysroot immediately

Note:
- Prefer moving quickly to a proper sysroot-based build rather than staying in without-headers mode.

Validation:

- Cross gcc compiles and links auxv6 user binaries against auxv6 libc/sysroot.
- Resulting binaries execute under auxv6.

## Phase 3: Sysroot And Runtime Packaging (3-5 days)

Objective:
- Make cross-compiler reproducible and portable via canonical auxv6 sysroot.

Sysroot contents:

1. Canonical headers from include/.
2. libc archive and start files from libc/ build artifacts (for example: `libc/libc.a`, `libc/crt0.o`).
3. Any auxv6-specific crt objects and linker scripts used for user binaries.

Checklist:

- Header provenance documented (what is copied from where).
- crt*.o ownership documented.
- Link specs point only into sysroot (no host leakage).

Validation:

- Build simple external C projects with only the cross toolchain + sysroot.

## Phase 4: Minimal Native GCC On auxv6 (1-2 weeks)

Objective:
- Run native compiler binaries on auxv6 for C-only workloads.

Approach:

1. Use host build machine to perform Canadian-cross style build for host=target=i386-auxv6-elf.
2. Install resulting binaries into auxv6 target filesystem.
3. Compile representative C workloads natively on auxv6.

Recommended limitations for first native milestone:

- C frontend only
- static linking only
- no plugins/LTO
- no thread-dependent runtime expectations

Validation:

- Native compiler can rebuild several auxv6 user programs.
- No host tooling required at runtime beyond native binutils equivalents.

## Phase 5: Practicality Hardening (parallel stream, high value)

Objective:
- Remove predictable pain points affecting real builds.

Priority changes in auxv6:

1. Increase exec argument capacity substantially.
2. Add real kernel mmap syscall family (at minimum: mmap/munmap with anonymous + basic file-backed read mapping).
3. Tighten remaining libc truthfulness gaps that impact configure/build logic.

Validation:

- Build larger C codebases without command-line overflow failures.
- Reduced memory pressure/fragmentation issues during compilation.

## Phase 6: Extended Hosted Toolchain (long-term)

Objective:
- Move toward richer GCC feature set and C++ viability.

Preconditions:

1. Real thread model (kernel + libc) beyond stub pthread layer.
2. Stronger VM semantics and mmap behavior.
3. Decision on shared-library runtime story.

Then evaluate:

- C++ frontend and libstdc++
- additional runtime libraries
- optional plugin/LTO enablement

## 6) Kernel/Libc Worklist Linked To GCC Risk

## 6.1 Critical (do early)

1. Exec argument budget increase
- Current risk: command vectors exceed 4096-byte aggregate arg budget.
- GCC impact: configure scripts, link lines, generated command wrappers.

2. Real mmap syscall surface
- Current risk: malloc-backed emulation is insufficient for compiler-scale behaviors.
- GCC impact: memory behavior, large translation units, future toolchain pieces.

## 6.2 Important

1. Clarify and enforce single-thread compiler runtime policy
- If single-thread remains for stage-1, force matching GCC/gthr config.

2. Tighten partial POSIX shims affecting feature detection
- times()/faccessat()/signal waiting semantics should be documented with explicit configure cache guidance.

## 6.3 Later

1. Thread-capable runtime and synchronization primitives.
2. Optional dynamic loader and shared library policy.
3. Wider locale and character-type support only if target software requires it.

## 7) Configure Tuples And Command Profiles

These are recommended starting profiles; tune after first successful build.

## 7.1 Cross Binutils

- --target=i386-auxv6-elf
- --prefix=<toolchain-prefix>
- --disable-nls
- --disable-werror

## 7.2 Cross GCC Stage-1 (C only)

- --target=i386-auxv6-elf
- --prefix=<toolchain-prefix>
- --with-sysroot=<auxv6-sysroot>
- --enable-languages=c
- --disable-nls
- --disable-shared
- --disable-threads
- --disable-libssp
- --disable-libquadmath
- --disable-libgomp
- --disable-libatomic (if needed)
- --disable-bootstrap

## 7.3 Native GCC (first milestone)

- host=i386-auxv6-elf
- target=i386-auxv6-elf
- --enable-languages=c
- keep same disable set as stage-1 unless each feature is proven

## 8) Test Strategy

## 8.1 Compiler Bring-Up Tests

1. Compile/link hello-world and syscall-heavy samples.
2. Build a subset of auxv6 user tools with cross GCC.
3. Run outputs under auxv6 and compare behavior to baseline binaries.

## 8.2 Regression Gates

1. No host-header contamination in target builds.
2. libgcc helper symbols satisfy expected runtime use.
3. Reproducible builds with pinned binutils/GCC revisions.

## 8.3 Native Compiler Gates

1. Native GCC can compile at least a medium subset of user/*.c.
2. No frequent exec-argument or memory-failure crashes in ordinary builds.

## 9) Risks And Mitigations

1. Risk: Target triplet churn and partial forked patches.
Mitigation:
- Freeze triple early, upstream patch stack discipline, maintain patch manifests.

2. Risk: Configure false-positives from compatibility stubs.
Mitigation:
- Maintain a small, explicit configure-cache profile for known auxv6 semantics.

3. Risk: Hidden dependence on real threading in later toolchain pieces.
Mitigation:
- Keep stage goals explicit; do not enable C++ runtime before thread model lands.

4. Risk: Native compile instability from VM limitations.
Mitigation:
- Prioritize mmap syscall work and argument-space increase before scaling workloads.

## 10) Work Breakdown Checklist

## 10.1 Week 1 (Cross foundation)

1. Finalize target contract doc.
2. Build binutils for i386-auxv6-elf.
3. Add GCC target stanza and minimal auxv6 config header.
4. Produce first cross C compiler.

## 10.2 Week 2 (Sysroot and reliability)

1. Build canonical auxv6 sysroot package.
2. Validate external project compile/link/run loop.
3. Add reproducible build scripts and version pinning notes.

## 10.3 Week 3+ (Native and hardening)

1. Produce first native GCC binaries.
2. Increase exec argument limit.
3. Start kernel mmap syscall implementation tranche.

## 11) Exit Criteria Per Milestone

M1: Cross compiler usable
- i386-auxv6-elf-gcc builds and links runnable auxv6 C binaries.

M2: Reproducible toolchain package
- Cross compiler + sysroot can be used on fresh host without local hacks.

M3: Native C compiler usable
- GCC runs in auxv6 and compiles meaningful in-tree user programs.

M4: Practical build baseline
- Argument-space and mmap limitations no longer routinely block compilation.

## 12) Optional Follow-On Document Set

If desired, split this plan into implementation docs:

1. docs/gcc-target-config-file-map.md
- Per-upstream-file patch notes for binutils/GCC.

2. docs/gcc-sysroot-layout.md
- Exact sysroot tree and ownership rules.

3. docs/gcc-bringup-checklist.md
- One-command build/validate workflow.
