# LibreSSL Port Completion

Date: 2026-04-12
Repo: auxv6
Status: COMPLETE

## Goal
Make the LibreSSL port build reliably for auxv6, produce a valid ELF user binary, and ensure it is staged into rootfs for normal boot/test flows.

## Final Result
- `ports/libressl-4.2.1/_libressl` is produced successfully.
- `_libressl` is verified as `elf32-i386`.
- `targetfs/usr/bin/libressl` is installed by `make ports-progs PORTS=1`.
- Incremental ports behavior works: second run reports up-to-date and skips rebuild.
- Runtime config lookup issue is fixed: binary now uses `/etc/ssl` instead of `no/openssl.cnf`.

## Key Problems Encountered
1. Port build races and instability around shared dmenu/stest build paths.
2. Repeated sudo/permissions friction with root-owned artifacts.
3. Ports pipeline rebuilding every port every run.
4. LibreSSL libraries built, but app binary stage failed (`build-apps` failure).
5. Runtime error in guest:
   - `fopen('no/openssl.cnf', 'rb')`

## Decisions and Constraints Followed
- No host contamination allowed in port lane.
- No throwaway shims as final solution.
- Kept sudo-first workflow for host-side build operations on macOS.
- No guest boot automation used.

## Code Changes Made

### 1) Top-level Makefile: race fix for dmenu/stest
- Unified build dependency so both `_dmenu` and `_stest` depend on one shared dmenu port build target.
- Prevents concurrent duplicate sub-build behavior.

### 2) Top-level Makefile: build-aware `ports-progs`
- Added up-to-date detection:
  - Locate existing port artifact (`_name` or `binname`).
  - Verify artifact format is `elf32-i386`.
  - Compare source/build metadata timestamps against artifact.
  - Skip rebuild when current and still install into targetfs.

### 3) LibreSSL lane: principled build fixes
File: `ports/libressl-4.2.1/Makefile.auxv6`

- Removed shim-based direction.
- Added configure cache hints for available libc functions:
  - `ac_cv_func_strcasecmp=yes`
  - `ac_cv_func_strncasecmp=yes`
  - `ac_cv_func_arc4random=yes`
  - `ac_cv_func_arc4random_buf=yes`
  - `ac_cv_func_arc4random_uniform=yes`
- Ensured cross `libgcc` comes from the cross compiler.
- Preserved host-contamination checks in build log scanning.

### 4) Aux libc: added real `atof`
File: `user/stdlib.c`

- Implemented `atof(const char *)` in libc.
- This addressed unresolved `atof` usage in LibreSSL app-link path.

### 5) Runtime config path fix for LibreSSL
File: `ports/libressl-4.2.1/Makefile.auxv6`

- Replaced configure option:
  - from `--without-openssldir`
  - to `--with-openssldir=/etc/ssl`
- Added runtime config staging:
  - install `ports/libressl-4.2.1/openssl.cnf`
  - to `targetfs/etc/ssl/openssl.cnf`

Notes:
- A temporary cert bundle staging attempt was removed to avoid giant source-control churn.
- Current final staging includes `openssl.cnf` only.

## Validation Evidence
Host-side (sudo) validation completed:

1. Build lane success:
- `sudo make -C ports/libressl-4.2.1 -f Makefile.auxv6 all`
- Output includes copy to `ports/libressl-4.2.1/_libressl`.

2. ELF format checks:
- `_libressl`: `file format elf32-i386`
- staged `targetfs/usr/bin/libressl`: `file format elf32-i386`

3. Incremental behavior:
- First run: builds/installs.
- Second run: `ports: up-to-date libressl (skipping build)` and installs.

4. Runtime path validation:
- `strings ports/libressl-4.2.1/_libressl` shows:
  - `OPENSSLDIR: "/etc/ssl"`
  - `%s/openssl.cnf`
- `targetfs/etc/ssl/openssl.cnf` exists.
- Legacy `no/openssl.cnf` fallback is no longer used.

## Commands Used (Representative)
- `sudo make user/libc.a`
- `sudo make -C ports/libressl-4.2.1 -f Makefile.auxv6 clean all`
- `sudo make ports-progs PORTS=1`
- `i386-jos-elf-objdump -f ports/libressl-4.2.1/_libressl`
- `i386-jos-elf-objdump -f targetfs/usr/bin/libressl`
- `strings ports/libressl-4.2.1/_libressl | rg 'openssl\.cnf|/etc/ssl|no/openssl'`

## Outcome
LibreSSL port is now operational in auxv6 build/staging flow with:
- deterministic host-side sudo builds,
- valid ELF artifact generation,
- correct rootfs staging,
- incremental build skipping when up-to-date,
- and fixed runtime config path behavior.

This closes the port-completion effort for LibreSSL on 2026-04-12.
