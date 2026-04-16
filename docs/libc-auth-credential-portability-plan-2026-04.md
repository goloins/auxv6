# libc/Auth/Credential Portability Plan (2026-04)

## Scope
This document tracks libc and kernel portability/auth gaps for OpenSSH-style
portability surfaces:
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

### Phase 2: Auth Surface
- Added closefrom(int lowfd) in libc.
- Added shadow API surface:
  - include/shadow.h
  - libc getspnam implementation
- Added crypt API surface:
  - include/crypt.h
  - libc crypt(3) implementation
- Added targetfs seed file: /etc/shadow
- Updated login and su to prefer shadow entries when present and verify hashed entries via crypt(3), while retaining legacy plaintext fallback.
- Updated passwd(1) to:
  - write hashed password entries to /etc/shadow
  - force /etc/passwd password field to x marker

### Phase 3: Session/Accounting APIs
- Added headers:
  - include/utmp.h
  - include/utmpx.h
  - include/lastlog.h
- Added libc utmp/utmpx/lastlog implementation (libc/utmpx.c), including:
  - setutxent/endutxent/getutxent/getutxid/getutxline/pututxline/updwtmpx
  - compatibility shims: setutent/endutent/getutent/getutid/getutline/pututline/updwtmp
  - logwtmp and write_lastlog helpers
- Integrated login accounting through these APIs in login(1).

### Phase 4: Socket/fd-passing Portability
- Added syscall numbers and syscall stubs for:
  - socketpair
  - sendmsg
  - recvmsg
- Added POSIX ancillary message API surface in headers:
  - SCM_RIGHTS
  - struct cmsghdr / struct msghdr
  - CMSG_SPACE/CMSG_LEN/CMSG_DATA helpers
- Added kernel socket support for fd passing paths (SCM_RIGHTS) and AF_UNIX socketpair plumbing.

## In Progress
- OpenSSH non-first-class lane iteration:
  - continue trimming conservative ac_cv overrides as probes become stable under auxv6 libc linkage
  - progressively re-enable disabled configure options only after clean compile/runtime validation

## Remaining Work

### Auth quality and compatibility
- Optional: add crypt_r if ports require re-entrant variant.
- Optional: provide broader hash format compatibility beyond the auxv6-native format.
- Replace current minimal hash scheme with a stronger password hashing design if security-hardening is desired.
- Migrate existing default targetfs credentials to hashed shadow format.
- Ensure useradd/usermod/userdel/group* tools maintain passwd+shadow coherently in all workflows.

### Session/accounting hardening
- Audit and harden record-locking/consistency behavior for concurrent utmp/wtmp/lastlog updates.
- Add tests for crash-consistency and malformed-record tolerance.

### Socket/fd-passing hardening
- Extend tests for recvmsg/sendmsg edge cases:
  - truncated control data
  - invalid cmsghdr layout
  - fd lifetime and bounds validation
- Validate OpenSSH use paths with fd passing enabled before dropping configure overrides.

### Port cleanup
- Continue removing OpenSSH portability overrides now that core libc surfaces are present.
- Re-run configure probes and trim conservative ac_cv overrides that are no longer required.
- Re-evaluate currently disabled OpenSSH options (shadow/utmp/wtmp/lastlog) as runtime validation coverage improves.

## Validation Checklist
- Build libc and core userland.
- Verify login/su/passwd flows with:
  - legacy plaintext entries
  - hashed shadow entries
- Verify credential transitions for root and non-root:
  - set*id permutations
  - getgroups/setgroups behavior and limits
- Verify session/accounting writes through utmp/wtmp/lastlog API paths.
- Verify socketpair + sendmsg/recvmsg + SCM_RIGHTS behavior with dedicated tests.
- Re-check OpenSSH configure probe outcomes for newly provided APIs and reduce overrides incrementally.
