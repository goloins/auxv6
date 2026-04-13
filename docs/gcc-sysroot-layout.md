# GCC Sysroot Layout For auxv6

Date: 2026-04-13
Status: Implementation companion to the GCC port plan

## Purpose

This document defines the canonical sysroot structure and ownership rules for i386-auxv6-elf builds. The goal is to avoid host contamination, hidden path assumptions, and toolchain drift.

A correct sysroot is the difference between:

- a reproducible cross compiler that always targets auxv6, and
- a fragile compiler that silently links against host headers/libs.

## 1) Canonical Paths

Recommended installation root:

- /opt/cross-auxv6

Canonical target path:

- /opt/cross-auxv6/i386-auxv6-elf

Canonical sysroot path:

- /opt/cross-auxv6/i386-auxv6-elf/sysroot

## 2) Required Directory Tree

Minimal expected tree:

```text
/opt/cross-auxv6/
  bin/
    i386-auxv6-elf-gcc
    i386-auxv6-elf-ld
    i386-auxv6-elf-as
    ...
  i386-auxv6-elf/
    bin/
    lib/
      gcc/
        i386-auxv6-elf/
          <gcc-version>/
            libgcc.a
            ...
    sysroot/
      usr/
        include/
          *.h
          sys/
          posix/
          auxv6/
        lib/
          libc.a
          crt0.o
          crti.o
          crtbegin.o
          crtend.o
          crtn.o
```

Notes:

1. Keep target headers under sysroot/usr/include.
2. Keep target libraries/startfiles under sysroot/usr/lib unless you intentionally define a different convention in specs.
3. Avoid splitting startfiles across multiple ad hoc locations.

## 3) Ownership Map (Source Of Truth)

This table defines where each sysroot artifact comes from in auxv6 source tree.

1. Public headers
- Source: include/
- Destination: sysroot/usr/include/

2. libc archive
- Source: user build output (libc.a)
- Destination: sysroot/usr/lib/libc.a

3. Startfiles (crt objects)
- Source: auxv6 user runtime build artifacts
- Destination: sysroot/usr/lib/

4. Optional compatibility headers
- Source: include/posix/
- Destination: sysroot/usr/include/posix/

Rule:

- Every file in sysroot must have an explicit provenance entry in tooling scripts.

## 4) Staging Strategy

## 4.1 Immutable Build Inputs

Create a deterministic staging directory first, then copy into final sysroot.

Example:

```sh
SYSROOT=/opt/cross-auxv6/i386-auxv6-elf/sysroot
STAGE=$PWD/out/sysroot-stage

rm -rf "$STAGE"
mkdir -p "$STAGE/usr/include" "$STAGE/usr/lib"
```

Then populate:

```sh
# Headers
rsync -a include/ "$STAGE/usr/include/"

# Libraries/startfiles (adjust paths to actual build outputs)
cp path/to/libc.a "$STAGE/usr/lib/libc.a"
cp path/to/crt0.o "$STAGE/usr/lib/"
cp path/to/crti.o "$STAGE/usr/lib/"
cp path/to/crtbegin.o "$STAGE/usr/lib/"
cp path/to/crtend.o "$STAGE/usr/lib/"
cp path/to/crtn.o "$STAGE/usr/lib/"
```

Finally install atomically:

```sh
rm -rf "$SYSROOT"
mkdir -p "$(dirname "$SYSROOT")"
cp -a "$STAGE" "$SYSROOT"
```

## 4.2 Why Atomic Staging Matters

Without staging, interrupted copies can leave partial sysroots that produce misleading compiler/linker failures.

## 5) GCC Spec Alignment

Your target specs (in auxv6 target header) must match this sysroot layout.

Typical alignment points:

1. Header search defaults should resolve to:
- <sysroot>/usr/include

2. Startfile search defaults should resolve to:
- <sysroot>/usr/lib

3. Library search defaults should include:
- <sysroot>/usr/lib

Validation commands:

```sh
i386-auxv6-elf-gcc --print-sysroot
i386-auxv6-elf-gcc -print-search-dirs
i386-auxv6-elf-gcc -v hello.c -o hello
```

What to inspect in verbose output:

1. Include search list contains sysroot paths only for target headers.
2. Link line uses target crt objects from sysroot.
3. No accidental host /usr/include or host /lib paths in target link resolution.

## 6) Reproducibility Metadata

Keep a manifest next to sysroot with:

1. auxv6 commit hash used to generate headers/libs.
2. gcc/binutils commit hashes or release versions.
3. script version/checksum that assembled sysroot.
4. timestamp in UTC.

Example manifest:

```text
target=i386-auxv6-elf
auxv6_commit=<hash>
gcc_version=<version-or-hash>
binutils_version=<version-or-hash>
sysroot_layout_version=1
generated_utc=2026-04-13T23:40:00Z
```

## 7) Common Sysroot Failure Modes

1. Missing crt objects
Symptoms:
- ld: cannot find crt0.o/crti.o
Fix:
- Ensure startfiles are copied and specs point to sysroot/usr/lib.

2. Host header contamination
Symptoms:
- build unexpectedly compiles with host-only types/features
Fix:
- Validate include search list and remove accidental host include injections.

3. Wrong libc selected
Symptoms:
- unresolved symbols that should exist in auxv6 libc, or ABI mismatch
Fix:
- Check search-dirs order and library path priority.

4. Drift between headers and libc archive
Symptoms:
- compile passes but runtime behavior mismatches or link surprises
Fix:
- Regenerate sysroot from one auxv6 commit, never mix artifacts from different revisions.

## 8) Recommended Sysroot Build Script Structure

Create a dedicated script (example naming):

- tools/build-sysroot-auxv6.sh

Recommended script phases:

1. Preconditions
- verify required artifacts exist
- verify target triple expected

2. Stage headers
3. Stage libraries and startfiles
4. Write manifest
5. Atomic install
6. Post-install sanity checks

Sample sanity checks:

```sh
test -f "$SYSROOT/usr/include/unistd.h"
test -f "$SYSROOT/usr/lib/libc.a"
i386-auxv6-elf-gcc --print-sysroot | grep -q "$SYSROOT"
```

## 9) Example End-To-End Smoke Build

```c
/* hello.c */
#include <stdio.h>

int main(void) {
  puts("hello from auxv6 cross build");
  return 0;
}
```

Compile:

```sh
i386-auxv6-elf-gcc -O2 -static hello.c -o hello
file hello
```

Expected:

- ELF 32-bit i386 executable for auxv6 target model.

## 10) Versioning The Layout

If sysroot structure changes (for example moving crt objects), increment layout version and document migration.

Guideline:

1. layout version 1: usr/include + usr/lib baseline
2. layout version 2+: only when structure changes, not when file contents change

## 11) Definition Of Done

Sysroot layout is complete when:

1. A fresh machine can build and use i386-auxv6-elf cross compiler with no manual path hacks.
2. Verbose compiler output shows clean, deterministic sysroot-based include and link resolution.
3. Manifest allows exact reconstruction of toolchain + sysroot provenance.
