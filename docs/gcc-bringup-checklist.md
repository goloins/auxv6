# GCC Bring-Up Checklist (auxv6)

Date: 2026-04-13
Status: Execution checklist companion to the GCC port plan

## Purpose

This checklist turns strategy into an operator-friendly runbook. It is organized to minimize wasted cycles and make failures easy to localize.

Primary target:

- i386-auxv6-elf

Stage scope for this checklist:

- Cross binutils
- Cross GCC (C-only)
- Sysroot integration
- First native-compiler readiness checks

## 0) Quick Start (Operator Summary)

If you only need the shortest path:

1. Build/install binutils for i386-auxv6-elf.
2. Build canonical auxv6 sysroot.
3. Build/install GCC C-only with restrictive flags.
4. Compile/run smoke binaries under auxv6.
5. Gate on failure signatures before expanding features.

## 1) Preconditions

## 1.1 Host Tooling

Required on host:

1. POSIX shell (bash recommended for configure reliability)
2. GNU make
3. C/C++ bootstrap compiler
4. flex, bison (as required by GCC version/features)
5. GMP, MPFR, MPC (and isl if needed by selected build profile)

Precheck examples:

```sh
bash --version | head -n 1
make --version | head -n 1
gcc --version | head -n 1
```

## 1.2 Source Inputs

You should have:

1. binutils source tree (pinned revision)
2. GCC source tree (pinned revision)
3. auxv6 tree at known commit

Record all hashes before build.

## 1.3 Environment Sanity

Set explicit paths and avoid accidental host-tool leakage.

Example:

```sh
export PREFIX=/opt/cross-auxv6
export TARGET=i386-auxv6-elf
export SYSROOT=$PREFIX/$TARGET/sysroot
export PATH=$PREFIX/bin:$PATH
```

## 2) Build Binutils

## 2.1 Configure

```sh
mkdir -p build/binutils && cd build/binutils
../../sources/binutils/configure \
  --target=$TARGET \
  --prefix=$PREFIX \
  --disable-nls \
  --disable-werror
```

## 2.2 Build + Install

```sh
make -j$(nproc)
make install
```

## 2.3 Validate

```sh
$TARGET-as --version
$TARGET-ld --version
$TARGET-objdump -i | grep elf32-i386
```

Pass criteria:

1. All tools exist in $PREFIX/bin.
2. objdump reports elf32-i386 support.

## 3) Build Sysroot

Follow docs/gcc-sysroot-layout.md exactly.

Minimum checks:

```sh
test -f $SYSROOT/usr/include/unistd.h
test -f $SYSROOT/usr/lib/libc.a
```

Optional stronger checks:

1. Ensure crt objects exist if specs require them.
2. Write a sysroot manifest with auxv6 commit hash.

## 4) Build GCC (Stage-1 C-only)

## 4.1 Configure

```sh
mkdir -p build/gcc && cd build/gcc
../../sources/gcc/configure \
  --target=$TARGET \
  --prefix=$PREFIX \
  --with-sysroot=$SYSROOT \
  --enable-languages=c \
  --disable-bootstrap \
  --disable-nls \
  --disable-shared \
  --disable-threads \
  --disable-libssp \
  --disable-libquadmath \
  --disable-libgomp
```

## 4.2 Build + Install

```sh
make -j$(nproc) all-gcc all-target-libgcc
make install-gcc install-target-libgcc
```

## 4.3 Validate Compiler Identity

```sh
$TARGET-gcc -v
$TARGET-gcc --print-sysroot
$TARGET-gcc -print-libgcc-file-name
```

Pass criteria:

1. Sysroot path is expected.
2. libgcc path resolves under target install tree.

## 5) Smoke Tests (Cross Output)

## 5.1 Hello World

```c
#include <stdio.h>
int main(void) {
  puts("hello auxv6");
  return 0;
}
```

```sh
$TARGET-gcc -O2 -static hello.c -o hello
$TARGET-objdump -f hello | head -n 5
```

Expected:

- ELF 32-bit i386 executable.

## 5.2 Syscall Surface Test

Compile small test using open/read/write/getpid to ensure libc + syscall wrappers link cleanly.

## 5.3 Math/Helper Test

Compile a sample that triggers 64-bit division/mod arithmetic helpers.

Why:

- Validates libgcc helper coverage expected by i386 codegen.

## 6) auxv6 Runtime Validation

Run produced binaries under auxv6 and verify behavior.

Recommended first runtime tests:

1. hello output
2. simple file IO roundtrip
3. argv handling with moderate argument count

Failure focus:

- If runtime fails only on larger command lines, inspect EXEC_ARG_BYTES_MAX constraints.

## 7) Failure Triage Matrix

Use this matrix before random patching.

1. Configure says unknown target
Likely area:
- GCC/binutils target-recognition maps

2. Link cannot find crt objects
Likely area:
- sysroot layout or STARTFILE_SPEC/ENDFILE_SPEC

3. Host headers appear in include path
Likely area:
- sysroot plumbing/spec ordering

4. Missing libgcc helpers
Likely area:
- target-libgcc build not completed or wrong target mapping

5. Runtime instability under bigger builds
Likely area:
- kernel VM semantics and argument-space limits

## 8) Native Compiler Readiness Checks (Pre-Native)

Before attempting native GCC on auxv6, confirm:

1. Cross compiler builds substantial subset of auxv6 user programs.
2. Sysroot regeneration is deterministic and scripted.
3. No unresolved target-recognition hacks remain.

Optional pre-native stress test:

- compile many translation units in one build and track memory/exec failures.

## 9) Native Bring-Up Checklist (First Milestone)

1. Build native-targeted GCC binaries on host.
2. Install into auxv6 image/userspace.
3. Verify gcc -v and simple compile-run cycle inside auxv6.
4. Rebuild selected auxv6 user programs natively.
5. Capture failure logs and map to kernel/libc worklist.

Pass criteria:

- Native GCC can build multiple nontrivial C programs without recurrent crash loops.

## 10) Recommended Logging Artifacts

Keep these artifacts for each run:

1. configure command lines
2. configure logs (config.log)
3. build logs
4. sysroot manifest
5. compiler -v output for sample builds
6. auxv6 runtime test logs

Store under a timestamped directory.

Example:

```text
out/toolchain-runs/2026-04-13T23-55Z/
  binutils-configure.log
  gcc-configure.log
  gcc-build.log
  sysroot-manifest.txt
  smoke-hello.log
  smoke-runtime.log
```

## 11) Expansion Gates (Do Not Skip)

Only expand scope when current gate is green.

Gate A:
- C-only cross compiler stable

Gate B:
- sysroot reproducible and drift-free

Gate C:
- native C compiler stable for in-tree workloads

Gate D:
- kernel/libc upgrades for mmap/arg-space complete

Gate E:
- consider C++/libstdc++ experiments

## 12) Common Anti-Patterns

1. Enabling too many GCC runtime libraries at once.
2. Mixing sysroot artifacts from different auxv6 commits.
3. Debugging runtime crashes before confirming compiler path hygiene.
4. Treating target-recognition warnings as harmless.
5. Attempting full C++ runtime before thread and VM groundwork.

## 13) Done Definition For This Checklist

This checklist is considered complete when:

1. A new operator can follow it and reproduce a working cross compiler.
2. Smoke binaries run correctly on auxv6.
3. Failures map cleanly to documented triage categories.
4. The team can start native bring-up with low ambiguity.
