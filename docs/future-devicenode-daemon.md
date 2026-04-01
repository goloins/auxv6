# Future Device-Node Daemon Notes

## Context

This document captures practical notes from dynamic PTY work in auxv6.
A full mdev/devfs-style userspace daemon remains a separate project.

## What Exists Today

- Kernel PTY allocator now supports multiple PTY pairs.
- `/dev/ptmx` allocates the next free PTY master endpoint.
- Slave PTYs are addressed as `/dev/pts/N`.
- Current bootstrap path is static node creation in `init` for `/dev/pts/0..15`.

## Why Static Node Creation Is Temporary

- Fixed ranges are brittle and can drift from kernel limits.
- Node ownership/mode policy is hard-coded in init.
- There is no dynamic cleanup or policy-based re-creation.

## Recommendations For A Future Daemon

- Add a small userspace tool (`mdev -s` style) that scans kernel device inventory and populates `/dev`.
- Keep PTY policy centralized in config, not in `init` code.
- Allow mode/owner/group rules per device pattern (for example `pts/[0-9]+`).
- Reconcile desired state at boot and optionally on events.

## Kernel Interfaces That Help

- `TIOCGPTN` is now available on a PTY master fd to resolve the slave number.
- `ptsname()/ptsname_r()` can be used by userspace software to open the correct slave path.

## Future Kernel Hooks Worth Adding

- A lightweight procfs node for live PTY slot state (`allocated`, `master_refs`, `slave_refs`).
- Optional kernel-exported device inventory endpoint for daemon discovery.

## Suggested Rollout

1. Keep static `/dev/pts/0..15` bootstrap in `init` as a compatibility fallback.
2. Implement daemon static-scan mode (`mdev -s`) and run it during init.
3. Move node policy from code to `/etc/mdev.conf`.
4. Remove hardcoded PTY node loop from init once daemon coverage is stable.
