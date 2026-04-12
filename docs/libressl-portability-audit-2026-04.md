# LibreSSL Portability Audit (auxv6) - 2026-04

## Purpose

This audit evaluates auxv6 libc, public headers, kernel prerequisites, and
targetfs policy against the practical needs of importing LibreSSL Portable,
with `libtls` preferred as the first application-facing consumer.

The goal is not to maximize how much compatibility glue LibreSSL Portable can
carry for us. The goal is to identify where auxv6 should implement correct
native behavior first, so LibreSSL integration is built on a truthful platform
rather than a pile of local exceptions.

## Audit Method

This audit used two inputs:

1. auxv6 local header/source inspection for libc, socket, resolver, time, RNG,
   and portability helper coverage.
2. LibreSSL Portable build and compatibility surface review, focused on the
   symbols and fallback layers the project probes for on non-OpenBSD systems.

## Executive Summary

auxv6 is not yet ready for a clean LibreSSL import.

The biggest issues are not inside cryptography. They are in the platform layer:

1. no secure entropy API or device surface.
2. `time_t` is currently 32-bit, which is incorrect for long-term certificate
   validation and conflicts with LibreSSL's own concern about RFC 5280 date
   handling on 32-bit time platforms.
3. the public socket ABI is non-POSIX in several important ways.
4. modern resolver APIs are absent.
5. security-sensitive OpenBSD-style libc helpers are missing.
6. trust-store policy does not yet align with LibreSSL's default expectations.

Some lower-level libc coverage is already decent and reduces churn. Notably,
auxv6 already has `strlcpy`, `strlcat`, `strsep`, `getline`, `getdelim`,
`reallocarray`, `mkstemp`, `mkdtemp`, `realpath`, `poll`, `select`, `fcntl`,
`clock_gettime`, `gettimeofday`, `setsockopt`, `getsockopt`, and `shutdown`.

That means the work should concentrate on correctness-critical gaps, not a
generic libc expansion spree.

## Findings

## 1. Hard blocker: no secure entropy substrate

Current state:

- No native `getrandom` API found.
- No native `getentropy` API found.
- No `arc4random`, `arc4random_buf`, or `arc4random_uniform` found.
- No `/dev/random` or `/dev/urandom` surface found.
- libc `random()` is currently an LCG-family non-cryptographic generator.

Why this matters:

- LibreSSL Portable can ship fallbacks on some platforms, but auxv6 should not
  treat that as acceptable native behavior.
- TLS requires secure randomness for private keys, ephemeral keys, nonces,
  session identifiers, and internal blinding.

Assessment:

- Severity: critical.
- Native implementation required before meaningful TLS work.

## 2. Hard blocker: `time_t` is 32-bit

Current state:

- `time_t` is defined as `int` in `include/sys/types.h`.
- LibreSSL Portable explicitly warns that 32-bit `time_t` causes incorrect
  handling for valid RFC 5280 certificate dates.

Why this matters:

- Certificate validity checking is not optional. If time representation is
  fundamentally wrong, TLS verification becomes incorrect even if the crypto is
  otherwise sound.
- This is a real platform-ABI issue, not just a library detail.

Assessment:

- Severity: critical.
- This must be elevated from a generic time hardening task to a top-tier ABI
  remediation item.

## 3. Hard blocker: public socket ABI is not POSIX-clean

Current state:

- No canonical public `include/sys/socket.h` exists.
- Public socket declarations are exposed through auxv6-specific headers.
- `bind` and `connect` take `struct sockaddr_in *` instead of generic
  `struct sockaddr *`.
- `accept` lacks the standard peer-address out-parameters.
- `send` and `recv` lack `flags` arguments in the public signature.
- `sockaddr_in` comments/documentation state host-byte-order semantics for
  `sin_port` and `sin_addr`, which is not standard socket ABI behavior.

Why this matters:

- LibreSSL itself can compile with portability shims, but `libtls`, `nc`,
  `ocspcheck`, and any future TLS client code assume a conventional socket API.
- The current ABI shape forces local wrappers and weakens the value of
  standards-based headers.

Assessment:

- Severity: critical.
- This is one of the main reasons to do libc/header cleanup before TLS import.

## 4. Hard blocker: resolver API is legacy-only

Current state:

- `include/netdb.h` explicitly omits `getaddrinfo`, `freeaddrinfo`, and
  `getnameinfo`.
- Current resolver surface is a truthful but minimal IPv4-only path based on
  `resolve_ipv4`, `gethostbyname`, `/etc/hosts`, and `/etc/resolv.conf`.

Why this matters:

- Modern network consumers and `libtls`-style client code generally assume
  `getaddrinfo` rather than `gethostbyname`.
- Reentrant, standard name resolution is more important than breadth of address
  family support at this stage.

Assessment:

- Severity: critical.
- IPv4-first `getaddrinfo` is enough for first tranche, but it must exist.

## 5. High priority: missing security-sensitive OpenBSD libc helpers

Current state:

- Present: `strlcpy`, `strlcat`, `strsep`, `reallocarray`.
- Not found: `explicit_bzero`, `timingsafe_bcmp`, `timingsafe_memcmp`,
  `arc4random` family, `freezero`, `recallocarray`, `strtonum`, `getprogname`.

Why this matters:

- LibreSSL Portable includes compatibility code for many of these.
- That does not mean auxv6 should defer the security-sensitive ones.
- `explicit_bzero` and constant-time comparison helpers are worth having
  natively because their semantics are security-relevant.

Assessment:

- Severity: high.
- Split into two groups:
  - Must implement natively early: `explicit_bzero`, `timingsafe_bcmp`,
    `timingsafe_memcmp`, `arc4random` family.
  - Can be deferred or allowed via compat first: `freezero`, `recallocarray`,
    `strtonum`, `getprogname`.

## 6. High priority: LibreSSL default trust-store path should drive policy

Current state:

- Resolver config exists in targetfs.
- No CA bundle policy exists yet.
- LibreSSL Portable defaults `OPENSSLDIR`/`TLS_DEFAULT_CA_FILE` to
  `.../ssl/cert.pem` rather than Debian-style `ca-certificates.crt` paths.

Why this matters:

- auxv6 should align trust-store layout with the stack it intends to ship.
- Choosing a different path is possible, but it creates needless divergence
  from LibreSSL defaults.

Assessment:

- Severity: high.
- Preferred policy target: `/etc/ssl/cert.pem` plus supporting directory policy
  if needed later.

## 7. Medium priority: some portable syscall/helper coverage is still absent

Current state:

- Not found in the current public surface: `pipe2`, `socketpair`, `accept4`,
  `pread`, `pwrite`, `getpagesize`, `readpassphrase`.
- `poll` and `select` are present.
- `mkstemp`, `mkdtemp`, `getline`, `getdelim`, `realpath`, and `ftruncate`
  are present.

Why this matters:

- LibreSSL core can often ship compatibility shims for some of these.
- Some LibreSSL tools/tests and future auxv6-facing TLS utilities will want
  these interfaces.

Assessment:

- Severity: medium.
- These are not all blockers for first `libtls` bring-up, but they should be
  tracked as a post-core portability tranche.

## 8. Medium priority: public header layering is still incomplete

Current state:

- `arpa/inet.h`, `netinet/in.h`, `netdb.h`, `poll.h`, `unistd.h`, `time.h`,
  `sys/time.h` exist.
- No public `sys/socket.h` found.
- No public `sys/random.h` found.
- No public `sys/uio.h` found.

Why this matters:

- Header availability strongly affects how much patching imported code needs.
- Lack of canonical include paths is often a bigger integration tax than the
  implementation of the underlying function itself.

Assessment:

- Severity: medium-high.
- `sys/socket.h` and `sys/random.h` should be treated as early work.

## 9. Positive findings: existing auxv6 coverage that lowers integration risk

Already present and useful:

- `clock_gettime`, `clock_settime`, `gettimeofday`, `gmtime_r`, `localtime_r`,
  `mktime`, `strftime`.
- `poll`, `select`, `fcntl`, `ioctl`, `setsockopt`, `getsockopt`, `shutdown`.
- `inet_pton`, `inet_ntop`, `inet_aton`, `inet_ntoa`.
- `getline`, `getdelim`.
- `strlcpy`, `strlcat`, `strsep`, `strndup`, `strnlen`, `strcasecmp`.
- `reallocarray`, `mkstemp`, `mkdtemp`, `realpath`, `ftruncate`.
- `/etc/hosts` and `/etc/resolv.conf` based resolver backend already exists.
- working NTP/realtime path already exists conceptually.

These are the reasons the work can stay focused rather than sprawling.

## Native vs Compat Guidance

The right rule for auxv6 is:

1. native implementation for security-critical semantics.
2. native implementation for public POSIX/OpenBSD ABI shape.
3. temporary compat allowance for app/test convenience helpers.

## Should be native before LibreSSL import

- `time_t` correctness.
- kernel RNG + `getrandom`/`getentropy` + `/dev/urandom`.
- `arc4random`, `arc4random_buf`, `arc4random_uniform`.
- `explicit_bzero`.
- `timingsafe_bcmp` and `timingsafe_memcmp`.
- `sys/socket.h` and corrected socket ABI signatures.
- `getaddrinfo`, `freeaddrinfo`, `getnameinfo`, `gai_strerror`.
- trust-store path policy aligned to LibreSSL.

## Can use compat initially if needed

- `freezero`.
- `recallocarray`.
- `strtonum`.
- `getprogname`.
- `getpagesize`.
- `readpassphrase`.
- `accept4`, `pipe2`, `socketpair`, `pread`, `pwrite`.

That said, some of the deferred items are still worth adding soon because they
improve general libc quality and reduce friction for future ports.

## Tranche Impact

This audit changes the recommended implementation focus.

The earlier remediation plan should be interpreted with the following priority
adjustments:

1. Split time work into two pieces:
   - ABI correctness: widen `time_t` and audit dependent structs/APIs.
   - operational hardening: kernel privilege enforcement and boot ordering.
2. Treat `sys/socket.h` plus POSIX socket signature cleanup as equally urgent
   with resolver modernization.
3. Add a dedicated native security-helper tranche for OpenBSD-style libc
   functions rather than burying them under generic cleanup.
4. Align trust-store policy to LibreSSL defaults (`/etc/ssl/cert.pem`) instead
   of picking a distro-specific convention unrelated to the target stack.

## Recommended Revised Order

1. ABI contract freeze, including `time_t` migration plan.
2. `time_t` widening and time ABI audit.
3. kernel RNG + `getrandom`/`getentropy` + `/dev/urandom`.
4. native `arc4random` family.
5. `explicit_bzero` and constant-time compare helpers.
6. public socket ABI normalization + `sys/socket.h`.
7. resolver modernization (`getaddrinfo` family).
8. trust-store policy at `/etc/ssl/cert.pem`.
9. secondary portability helpers (`pipe2`, `socketpair`, `pread`, `pwrite`,
   `accept4`, `getpagesize`, `strtonum`, `freezero`, `recallocarray`,
   `getprogname`, `readpassphrase`).
10. LibreSSL import and first `libtls` consumer.

## Concrete Recommendations

## Must-fix before touching LibreSSL source import

1. Widen `time_t` to 64-bit and audit the fallout.
2. Land kernel-backed secure randomness and userspace APIs.
3. Add `sys/socket.h` and repair the public socket function signatures.
4. Add `getaddrinfo` family.
5. Add `explicit_bzero`, `timingsafe_bcmp`, `timingsafe_memcmp`, and
   `arc4random` family.
6. Define `/etc/ssl/cert.pem` trust-store policy.

## Safe to defer until after first `libtls` bring-up

1. `freezero`.
2. `recallocarray`.
3. `strtonum`.
4. `getprogname`.
5. `getpagesize`.
6. `readpassphrase`.
7. `pipe2`, `socketpair`, `accept4`, `pread`, `pwrite`.

## Bottom Line

LibreSSL does not primarily expose a crypto deficiency in auxv6. It exposes a
platform correctness deficiency.

The work should therefore begin with ABI and kernel substrate correction,
especially `time_t`, RNG, socket ABI, resolver ABI, and OpenBSD-style
security helpers. Once those are fixed, LibreSSL integration becomes a normal
porting task rather than a moving-target rescue project.
