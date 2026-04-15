# libc/Auth/Credential Portability Plan (2026-04)

## Scope
This document tracks libc and kernel portability/auth gaps for:
- setresuid/setresgid
- seteuid/setegid/setgroups/getgroups consistency
- closefrom
- getspnam/crypt/shadow authentication path
- utmp/utmpx/lastlog APIs
- socketpair/sendmsg/recvmsg and fd passing paths (SCM_RIGHTS)

## Completed

### Phase 1: Credential Core
- Expanded process credential model to include:
  - real/effective/saved uid
  - real/effective/saved gid
  - supplementary groups (up to 16)
- Added kernel credential helpers:
  - proc_geteuid/proc_getegid
  - proc_setresuid/proc_setresgid
  - proc_getgroups/proc_setgroups
  - proc_in_group
- Updated process lifecycle to initialize/inherit/reset new credential fields.
- Updated inode access checks to use supplementary group membership.
- Added syscall numbers and dispatch for:
  - geteuid/getegid
  - getgroups/setgroups
  - setresuid/setresgid
- Reworked setreuid/setregid through setres helpers in kernel.

### Phase 1.5: Semantics Hardening
- Added libc wrappers and errno mapping for:
  - setuid/setgid
  - setreuid/setregid
  - setresuid/setresgid
  - getgroups/setgroups
- Switched raw credential syscall stubs to internal __auxv6_sys_* names where wrappers now own behavior.
- Updated login/su privilege transition to call setgroups before setgid/setuid.

### Additional landed after Phase 1.5
- Added closefrom(int lowfd) in libc.
- Added shadow API surface:
  - include/shadow.h
  - libc getspnam implementation
- Added targetfs seed file: /etc/shadow
- Updated login and su to prefer shadow entries when present (fallback remained plain compare before crypt landing).
- Began removing OpenSSH configure workarounds for newly landed APIs:
  - seteuid/setegid/setgroups/setresuid/setresgid/getspnam/closefrom now advertised as present.

## In Progress (This changeset)
- Landing crypt(3) implementation in libc.
- Migrating passwd(1) from /etc/passwd plaintext updates to:
  - /etc/passwd password placeholder field (x)
  - /etc/shadow hashed password updates
- Switching login/su/passwd auth checks to crypt-based verification for hashed shadow entries while retaining plaintext fallback for legacy entries.

## Remaining Work

### Auth path
- Optional: add crypt_r if ports require re-entrant variant.
- Optional: provide additional hash format compatibility if needed beyond auxv6-native format.
- Migrate existing default targetfs credentials to hashed shadow format.
- Ensure useradd/usermod/userdel/group* tools maintain passwd+shadow coherently in all workflows.

### Session/accounting APIs
- Add headers and libc implementations for:
  - utmp
  - utmpx
  - lastlog
- Rewire login accounting to API-backed records instead of ad-hoc file appends.

### Socket portability path
- Add kernel/libc/header support for:
  - socketpair(AF_UNIX, SOCK_STREAM)
  - sendmsg/recvmsg
  - SCM_RIGHTS control messages (fd passing)
- Preserve existing AF_INET behavior and layer AF_UNIX support cleanly.

### Port cleanup
- Continue removing OpenSSH portability overrides once crypt + remaining socket/session APIs are in place.
- Re-run configure probes and trim no/disable flags that are no longer needed.

## Validation Checklist
- Build libc and core userland.
- Verify login/su/passwd flows with:
  - legacy plaintext entries
  - hashed shadow entries
- Verify credential transitions for root and non-root:
  - set*id permutations
  - getgroups/setgroups behavior and limits
- Re-check OpenSSH configure probe outcomes for newly provided APIs.
