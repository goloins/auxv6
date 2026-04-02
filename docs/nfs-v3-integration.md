# NFS v3 Integration (Kernel + VFS)

Last updated: 2026-04-02

## Scope

This document captures the current auxv6 NFS v3 integration state across:

- Kernel XDR/RPC transport
- MOUNT/NFS protocol clients
- VFS backend (`nfs` fstype)
- mount(8) userspace argument plumbing

This is an iterative, read-only milestone intended to be refined with live NFS server testing.

## Implemented Components

### 1) XDR/RPC transport foundation

- `kernel/net/xdr.c`: RFC 1014 XDR encode/decode primitives
- `kernel/net/rpc.c`: RFC 1057 call/reply framing and UDP exchange
- `kernel/net/socket.c`: kernel-internal UDP helper APIs used by RPC

Kernel socket helpers added for in-kernel clients:

- `ksock_open_udp(struct socket **out)`
- `ksock_sendto(struct socket *s, struct sockaddr_in *dst, char *buf, uint len)`
- `ksock_recvfrom_timeout(struct socket *s, char *buf, uint len, int timeout_ticks, struct sockaddr_in *src)`

RPC behavior:

- `rpc_call()` now performs real UDP send/receive when a result decoder is provided
- validates reply type (`REPLY`), xid match, and accepted status (`SUCCESS`)
- supports one-way calls via `rpc_udp_send()` when no result decoder is supplied

### 2) MOUNT protocol client path

- `kernel/net/mount.c`: portmapper + MOUNT/UMOUNT through `rpc_call()`
- `pmap_getport()` uses `PMAPPROC_GETPORT`
- `mount_nfs()` gets root file handle + auth flavor
- `umount_nfs()` issues UMOUNT RPC

### 3) NFS v3 protocol client path

- `kernel/net/nfs.c` uses shared RPC transport for:
  - `GETATTR`
  - `LOOKUP`
  - `READ`
  - `READDIR`

### 4) VFS backend: `nfs`

New file: `kernel/fs/vfs_nfs.c`

Registered via `vfs_nfs_init()` with read-only capability.

Mount-time behavior (`mount_init`):

1. Parses source string `server:/export` from mount data
2. Parses server as dotted IPv4 currently
3. Calls `mount_nfs()` to obtain root file handle
4. Discovers NFS service port (defaults to 2049 if portmapper returns 0)
5. Initializes per-mount NFS context + handle cache

Read-path behavior:

- `root_inode`, `namei`, `nameiparent`, `dirlookup`, `read`, `stat`, `access`
- per-mount file-handle cache maps inum -> NFS file handle
- inode fields are synthesized from NFS `fattr3`

### 5) Mount syscall + userspace plumbing

Kernel (`kernel/core/sysfile.c`):

- `sys_mount()` now recognizes fstype `"nfs"`
- allocates pseudo device numbers from:
  - `NFSDEV_BASE`
  - `NFSDEV_MAX`

Userspace (`user/mount.c`):

- supports NFS convention:
  - `mount server:/share nfs /mnt/nfs`
- passes source string through `mount(2)` data pointer/length for kernel `mount_init`
- fstab parsing also supports NFS source tokens containing `:`

## New/Updated Interfaces

### Headers

- `include/defs.h`: kernel UDP socket helper declarations
- `include/rpc.h`: UDP transport helper declarations
- `include/mount.h`: `mount_nfs()` / `umount_nfs()` declarations
- `include/vfs.h`: `vfs_nfs_init()` declaration
- `include/file.h`: NFS pseudo-device range constants

### Build

- `Makefile` includes `kernel/fs/vfs_nfs.o`

## Current Limitations

1. No live-server validation yet in this tree state
2. Source parser accepts dotted IPv4 only (no DNS hostnames yet)
3. NFS backend currently read-only (`VFS_CAP_READ`)
4. `READDIR` decode/path is still minimal and intended for iteration
5. Handle cache policy is simple replacement; no eviction tuning yet

## Intended Operator Usage

Once a server is available:

1. `mount server:/export nfs /mnt/nfs`
2. `ls /mnt/nfs`
3. `cat /mnt/nfs/file.txt`

## Next Iteration Targets

1. Live-server smoke and error-path validation
2. Harden pathname semantics (`..`, symlink handling, cross-mount expectations)
3. Improve READDIR result decoding and directory iteration behavior
4. Add hostname support for source parser (or resolver hook)
5. Expand diagnostics under existing debug gates for NFS/MOUNT/RPC failures
