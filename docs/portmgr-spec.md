# portmgr — auxv6 Port Manager Specification

## Overview

`portmgr` replaces the current `ports.list` + per-port `Makefile.auxv6` system with a
self-contained, dependency-aware port manager. It supports declarative per-port patch
series, license tracking, GPL source distribution obligations, and an installable package
database. It is a host tool but is written to be buildable and runnable on the auxv6
target.

---

## Design Principles

- Written in **C99/C11**, POSIX.1 interfaces only.
- No dynamic linking, no threads, no locale or wchar usage.
- No dependency on flex, bison, regex.h, Python, Perl, or awk.
- All string parsing is handwritten.
- File I/O through: `open`, `read`, `write`, `lseek`, `stat`, `mkdir`, `rename`, `unlink`.
- Statically linkable against auxv6's libc.
- `build.sh` scripts are **exec'd** as child processes, not sourced — this gives isolation
  and avoids any dependency on the invoking shell's feature set.
- The primary motivation for this tool is reliable embedding of complex software (e.g.,
  bash, libressl) into the target system, which will in turn remove shell constraints from
  future port build scripts.

---

## Port Definition Layout

Port definitions live under `ports/portdef/`. Each port gets its own subdirectory:

```
ports/
  portdef/
    dash/
      MANIFEST          ← key: value metadata
      patches/
        0001-auxv6-signal.patch
        0002-fix-mksyntax.patch
      LICENSE           ← license text (installed to targetfs)
      build.sh          ← POSIX sh build script exec'd by portmgr
    libressl/
      MANIFEST
      patches/
      LICENSE
      build.sh
    bash/
      ...
  dist/                 ← downloaded tarballs (one per port)
  src/                  ← extracted + patched source trees
  portdef/              ← port definitions (above)
```

The existing `makefiles/` directory and `ports.list` remain in place during migration and
are removed once all ports are converted.

---

## MANIFEST Format

A plain-text `key: value` file. Lines beginning with `#` are comments. Blank lines are
ignored. Order of keys is not significant.

```
name:          dash
version:       0.5.12
url:           https://ftp.gnu.org/gnu/ash/dash-0.5.12.tar.gz
checksum:      sha256:3c017e8e3f9476fdb9b6e7f28f2caf8ff0c6b8a09b3e9a78c44d28c05b0dcae4
depends:
license:       BSD-3-Clause
license_file:  COPYING
gpl:           no
install_class: system
binaries:      dash
```

### Key Definitions

| Key | Description |
|---|---|
| `name` | Port name; must match the directory name under `portdef/` |
| `version` | Upstream version string |
| `url` | Fetch URL. May be `https://`, `http://`, or a filesystem path (absolute or relative to the workspace root) |
| `checksum` | `sha256:<hex>` — verified after fetch, before extraction |
| `depends` | Space-separated list of port names this port requires to be installed first |
| `license` | SPDX license identifier |
| `license_file` | Filename inside the source tree to copy as the license |
| `gpl` | `yes` if this port is GPL-licensed and requires source distribution in targetfs |
| `install_class` | `system` → `/bin`, `user` → `/usr/bin`, `sbin` → `/usr/sbin` |
| `binaries` | Space-separated list of binary names produced by this port |

### Filesystem Path URLs

When `url` is not an HTTP(S) URL, `portmgr fetch` treats it as a local path. This allows
distributing `portmgr` itself as a source tree on disk without requiring a server:

```
url: /home/user/downloads/portmgr-1.0.tar.gz
url: ../local-tarballs/mybuild.tar.gz
```

Relative paths are resolved relative to the workspace root (`ROOT`). The checksum
verification step applies regardless of fetch method.

---

## Patch Management

- Patches live in `portdef/<name>/patches/` as numbered `.patch` files in unified diff
  format.
- Applied in **lexicographic order** after extraction via `patch -p1 < file`.
- No quilt dependency. The `extract` phase loops through the patches directory in order.
- Failed patch application is a hard error — the port is not built with partial patches.

---

## Build Script Interface

`build.sh` is **exec'd** as a child process (not sourced). `portmgr` `exec`s it via:

```c
execve("/bin/sh", (char *[]){ "sh", path_to_build_sh, hook_name, NULL }, envp);
```

where `hook_name` is one of `configure`, `build`, `install`.

The tool exports the following environment variables to the child:

```
PORT_NAME          port name
PORT_VERSION       upstream version
PORT_SRCDIR        path to extracted+patched source tree
PORT_BUILDDIR      path to out-of-tree build directory (inside PORT_SRCDIR/.portbuild)
ROOT               absolute path to the auxv6 workspace root
TOOLPREFIX         cross-compiler prefix (e.g. i386-jos-elf-)
CC                 cross C compiler
AR                 cross archiver
RANLIB             cross ranlib
STRIP              cross strip
OBJDUMP            cross objdump
AUXV6_LIBC_A       absolute path to libc.a
AUXV6_CRT0_OBJ     absolute path to crt0.o
AUXV6_AUXRT_A      absolute path to auxrt.a
TARGET_SYSROOT     absolute path to targetfs/
TARGETFS_BIN       targetfs/bin
TARGETFS_USR_BIN   targetfs/usr/bin
TARGETFS_USR_SBIN  targetfs/usr/sbin
```

`build.sh` responds to the hook passed as `$1`:

```sh
#!/bin/sh
case "$1" in
  configure) do_configure ;;
  build)     do_build ;;
  install)   do_install ;;
esac

do_configure() {
    cd "$PORT_BUILDDIR"
    ...
}
```

Any non-zero exit from the child is a hard error.

---

## Dependency Resolution

Implemented in C using **Kahn's algorithm** (iterative topological sort):

1. Parse all enabled port MANIFESTs and build an adjacency list.
2. Detect cycles; report the cycle members and abort.
3. Emit a build order with all transitive dependencies resolved — a port is built at most
   once regardless of how many ports depend on it.
4. The enabled port list (`ports/ports-enabled.list`) remains a human-editable flat text
   file (one name per line, `#` comments allowed).

---

## Checksum Verification

`portmgr` implements SHA-256 in C (no OpenSSL dependency at tool build time). After fetch
and before extraction the computed digest is compared against the `checksum` field in
MANIFEST. Mismatch is a hard error; the tarball is removed to prevent a corrupt cached
copy from being reused.

---

## License Tracking

On install, for every port:

1. Copy `license_file` from the source tree to:
   ```
   targetfs/usr/share/licenses/<name>/LICENSE
   ```

2. Append a record to `targetfs/var/db/ports/licenses.txt`:
   ```
   dash  BSD-3-Clause  /usr/share/licenses/dash/LICENSE
   ```

### GPL Source Distribution

When `gpl: yes` is set in MANIFEST, `portmgr install` additionally copies the fetched
tarball to:

```
targetfs/usr/src/ports/<name>/<tarball-filename>
```

This satisfies the GPL written-offer obligation for ports embedded into the base system.
The tarball is the unmodified upstream archive; patches are tracked separately in the port
definition and are included under `portdef/` which is part of the auxv6 source tree.

---

## Package Database

A flat-file database at `targetfs/var/db/ports/`:

```
targetfs/var/db/ports/
  installed/
    dash          ← one file per installed port
    libressl
    bash
  licenses.txt
```

Each installed-port file records:

```
name: dash
version: 0.5.12
date: 2026-04-20
license: BSD-3-Clause
files:
  /bin/dash
  /usr/share/licenses/dash/LICENSE
```

One file per line under `files:`. This supports a future `portmgr remove` without
requiring a relational database engine.

---

## Install Destinations

Ports are installed **directly into targetfs** (no DESTDIR staging):

| `install_class` | Binary destination |
|---|---|
| `system` | `targetfs/bin/` |
| `user` | `targetfs/usr/bin/` |
| `sbin` | `targetfs/usr/sbin/` |

Lib and header installs are handled by `build.sh`'s `do_install` hook using the exported
`TARGET_SYSROOT` variable.

---

## Tool Subcommands

| Command | Action |
|---|---|
| `portmgr fetch <name>` | Download (or copy from local path) and verify checksum |
| `portmgr extract <name>` | Extract tarball into `ports/src/`, apply patches in order |
| `portmgr build <name>` | Exec `build.sh configure` then `build.sh build` |
| `portmgr install <name>` | Exec `build.sh install`, copy license, handle GPL src, update DB |
| `portmgr sync [<name>...]` | fetch→extract→build→install in dependency order for all enabled ports or named subset |
| `portmgr list` | Show enabled ports, installed versions, and status |
| `portmgr deps <name>` | Print resolved build order for a port and its dependencies |
| `portmgr verify <name>` | Re-verify checksum of cached tarball without re-fetching |

---

## Tool Source Layout

```
tools/portmgr/
  portmgr.c       ← main + subcommand dispatch
  manifest.c/h    ← MANIFEST parser
  graph.c/h       ← dependency graph + topological sort
  fetch.c/h       ← HTTP and local-path fetch
  sha256.c/h      ← SHA-256 implementation
  patch.c/h       ← patch application driver
  db.c/h          ← package database read/write
  Makefile        ← builds portmgr as a host tool under tools/
```

Built by the host Makefile in the same lane as existing tools (mkfs, etc.). The resulting
binary is placed at `tools/portmgr/portmgr`.

---

## Migration Path

1. Build `portmgr` as a new host tool — no existing ports are touched.
2. Convert one in-tree port (suggested: `dash`) to the new layout as a proof-of-concept.
3. Validate that `portmgr sync dash` produces the same result as `make _dash`.
4. Convert remaining ports one at a time.
5. After all ports are converted, remove `ports/makefiles/`, `ports/ports.list`, and all
   `Makefile.auxv6` files from extracted source trees.
6. The root `Makefile` targets `ports-sync` and `ports-progs` become thin wrappers around
   `portmgr sync`.

---

## Open / Deferred Items

- **portmgr as a self-hosted port**: deferred. The `url: <filesystem-path>` mechanism in
  MANIFEST is the bridge — once the target system is capable of running portmgr, a port
  definition can be written that installs it from a local path without requiring a server.
- **Signature verification**: deferred. SHA-256 checksum is sufficient for current threat
  model (known-good upstream sources on a private build machine).
- **Parallel builds**: deferred. Dependency ordering is serial for now; parallelism across
  independent ports can be added later without changing the port definition format.
