# tmpfs Support

## Overview
auxv6 now includes an in-memory tmpfs backend integrated with the VFS layer. tmpfs provides a writable filesystem backed by RAM with a strict size limit.

## Mounting
- tmpfs requires a size option. Mounts without a size are rejected.
- The default mount path is controlled by the caller; use the `mktmpfs` utility for convenience.

Example:
- mktmpfs /tmp 32M

## Permissions And Ownership
- tmpfs root is owned by root:root and defaults to mode 0777.
- New nodes are created with permissive mode bits unless explicitly set by syscalls.

## Size Enforcement
- Writes that would exceed the configured size return an error.
- Unlinked-but-open files retain their space until the last reference is closed.

## Notes
- tmpfs is a memory filesystem; data does not persist across reboots.
- tmpfs is fully managed by the VFS layer and does not require a block device.
