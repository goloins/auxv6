# libc + Kernel Correctness Remediation Plan (TLS Prereq Scope)

## Purpose

This document captures deficiencies found in the current libc/header/kernel surface within TLS/SSL prerequisite scope, and defines a correctness-first implementation plan.

Project policy for this work:

1. Correctness over convenience.
2. POSIX-compatible public ABI where practical.
3. Truthful behavior over compatibility theater.
4. Kernel enforces security invariants (do not rely on libc-only checks).

## Scope

In scope:

- libc public API and header correctness relevant to networking, resolver, RNG, and time.
- kernel support required by secure TLS consumers (entropy, time, socket behavior).
- targetfs trust/resolver baseline needed by TLS clients.

Out of scope (for this plan):

- guest boot/runtime validation procedures.
- full IPv6 feature parity.
- kernel TLS implementation.

## Findings Summary (Current Deficiencies)

## A. Public socket ABI is non-POSIX and blocks upstream portability

Observed in public user ABI and headers:

- `bind`/`connect` accept `struct sockaddr_in *` directly instead of `const struct sockaddr *`.
- `accept` does not expose peer address out-params (`accept(int, struct sockaddr *, socklen_t *)`).
- `send`/`recv` lack `flags` argument in the public signature.
- No canonical `<sys/socket.h>` public header; socket API is exposed through project-specific headers.
- `sockaddr_in` internals are host-byte-order by convention, not standard network-order semantics.

Why this matters:

- Breaks direct portability of TLS stacks and many third-party network clients.
- Encourages app-side ifdef forks and local shims.

## B. Resolver API is incomplete

Observed:

- `<netdb.h>` intentionally omits `getaddrinfo`/`freeaddrinfo`/`getnameinfo`.
- Current resolver path is IPv4-only helper flow (`resolve_ipv4`, DNS helper functions, `gethostbyname`).

Why this matters:

- Modern TLS clients and HTTP stacks generally assume `getaddrinfo` path.
- `gethostbyname` is legacy and non-reentrant.

## C. Entropy/CSPRNG substrate is missing for security use

Observed:

- No `getrandom`/`getentropy` API.
- No `/dev/random` or `/dev/urandom` surface.
- libc `random()` is an LCG-family PRNG and is not cryptographically secure.

Why this matters:

- Hard blocker for key generation, nonce generation, and secure TLS sessions.

## D. Time correctness needs explicit hardening for cert validation

Observed:

- `CLOCK_REALTIME` and `clock_settime` exist, with `ntpd` integration.
- Root check is performed in libc before `clock_settime` syscall path.

Risk to address:

- Kernel must independently enforce privilege and parameter validation regardless of libc behavior.
- Cert validation correctness depends on reliable realtime and boot ordering.

## E. Trust-store filesystem policy is missing

Observed:

- Resolver config exists (`/etc/resolv.conf`, `/etc/hosts`).
- No standardized CA trust path/bundle policy present in `targetfs`.

Why this matters:

- TLS verification cannot be safely enabled by default without trusted roots.

## F. Header layering and standards naming need cleanup

Observed:

- POSIX/BSD-style include paths are partial/inconsistent.
- Some behavior relies on auxv6-specific conventions rather than standard declarations.

Why this matters:

- Increases long-term maintenance load and complicates external code import.

## Remediation Strategy

Implement in strict order:

1. Security substrate correctness (RNG/time enforcement).
2. Public ABI correctness (socket + resolver).
3. Trust-store and policy.
4. TLS library integration only after 1-3 are landed.

## Implementation Plan

## Tranche 0: Contract Freeze and Audit Baseline

Goals:

- Freeze target POSIX-facing contract for networking/resolver/time/RNG surfaces.
- Avoid parallel drift while implementation begins.

Tasks:

1. Create an API contract doc with exact required signatures and struct layouts.
2. Mark deprecated auxv6-specific prototypes in headers as transitional.
3. Add CI/grep checks that reject new non-standard socket prototype additions.

Exit criteria:

- Contract doc approved.
- No new ABI drift introduced during subsequent tranches.

## Tranche 1: Kernel Entropy and Secure Random API

Goals:

- Provide cryptographically secure bytes from kernel.

Tasks:

1. Add kernel entropy pool and CSPRNG (seeded from available hardware/runtime sources).
2. Add `getrandom` syscall with blocking/non-blocking semantics defined.
3. Add `/dev/urandom` character device as a stable userland source.
4. Add libc wrappers: `getrandom`, `getentropy`.
5. Add explicit man/doc guidance that `rand/random` are non-crypto.

Exit criteria:

- Kernel returns secure random bytes under load.
- Userland can obtain entropy without ad-hoc hacks.
- Security-focused tests cover short reads, EINTR, and early-boot behavior.

## Tranche 2: Socket ABI and Header Normalization

Goals:

- Land POSIX-compatible public networking surface while preserving compatibility.

Tasks:

1. Introduce canonical `<sys/socket.h>` and standard `struct sockaddr` usage.
2. Add standard signatures:
   - `bind(int, const struct sockaddr *, socklen_t)`
   - `connect(int, const struct sockaddr *, socklen_t)`
   - `accept(int, struct sockaddr *, socklen_t *)`
   - `send(int, const void *, size_t, int)`
   - `recv(int, void *, size_t, int)`
3. Keep compatibility wrappers for old auxv6 signatures during migration window.
4. Correct `sockaddr_in` semantics and byte-order expectations to standards behavior.
5. Add compile tests for common portable network snippets.

Exit criteria:

- Existing auxv6 apps continue to build via compatibility path.
- Portable POSIX network code builds without local patches.

## Tranche 3: Resolver Modernization

Goals:

- Provide modern, reentrant resolver surface used by TLS consumers.

Tasks:

1. Implement `getaddrinfo`, `freeaddrinfo`, `getnameinfo` with IPv4-first support.
2. Map to existing `/etc/hosts` and DNS query path.
3. Keep legacy `gethostbyname` available but documented as legacy.
4. Add deterministic error mapping (`EAI_*`, `h_errno` compatibility behavior).

Exit criteria:

- TLS-capable clients can resolve hosts using standard APIs.
- Resolver tests cover hosts-only, DNS-only, NXDOMAIN, timeout, and malformed responses.

## Tranche 4: Time and Privilege Hardening

Goals:

- Make cert-time validation reliable and security checks kernel-authoritative.

Tasks:

1. Ensure kernel enforces privilege checks for `clock_settime` regardless of libc.
2. Verify kernel-side validation for timespec ranges and clock ids.
3. Add boot policy: network up, then time sync, then services requiring TLS verification.
4. Add diagnostics for unsynced/invalid wall-clock in userland tools.

Exit criteria:

- Privilege bypass via custom syscall caller is not possible.
- Time-dependent verification has predictable behavior at boot.

## Tranche 5: Trust Store and Certificate Policy

Goals:

- Establish a secure default trust anchor policy.

Tasks:

1. Define canonical CA bundle path in targetfs (for example `/etc/ssl/certs/ca-certificates.crt`).
2. Ship initial CA bundle and document update workflow.
3. Add hostname verification policy requirements in docs.
4. Add failure-mode diagnostics for expired/untrusted/hostname-mismatch certs.

Exit criteria:

- TLS client code can verify peers by default with documented trust roots.

## Tranche 6: Deletion of Transitional Cruft

Goals:

- Remove obsolete non-standard interfaces after migration period.

Tasks:

1. Remove deprecated auxv6-only socket prototypes once all in-tree users are migrated.
2. Remove duplicate/incorrect struct definitions and stale header aliases.
3. Tighten lint/build checks to prevent regression.

Exit criteria:

- Public libc/network surface is coherent, documented, and standards-aligned.

## Work Breakdown by Subsystem

Kernel:

1. entropy pool + CSPRNG + syscall/device exposure.
2. strict kernel-side permission and validation for time setters.
3. socket syscall argument translation compatible with `sockaddr` ABI.

libc + headers:

1. POSIX-correct networking declarations in canonical headers.
2. resolver API expansion (`getaddrinfo` family).
3. random/time wrappers and error semantics consistency.

targetfs/docs:

1. trust-store path and initial bundle policy.
2. resolver/time operational docs.
3. migration guide for deprecated signatures.

## Validation Matrix

Build-level:

1. `make aux.kern`
2. userland utilities build with no new warnings in touched areas.

API-level tests to add:

1. socket prototype conformance compile tests.
2. resolver behavior tests (`getaddrinfo` + legacy API).
3. entropy tests (availability, blocking semantics, error behavior).
4. time privilege tests (`clock_settime` root vs non-root).
5. trust-store path existence and cert validation smoke tests.

## Risk Register

1. ABI migration breakage for existing auxv6 user binaries.
Mitigation: compatibility wrappers + staged deprecation.

2. Entropy source quality uncertainty early in boot.
Mitigation: conservative blocking behavior before pool is seeded.

3. Resolver modernization introduces regressions in existing tools.
Mitigation: keep legacy path and add side-by-side tests.

4. Time sync race at boot causes false cert failures.
Mitigation: startup ordering and clear diagnostics.

## Recommended Execution Order (No Skips)

1. Tranche 0
2. Tranche 1
3. Tranche 2
4. Tranche 3
5. Tranche 4
6. Tranche 5
7. Tranche 6

This order minimizes security risk and avoids building TLS on an unstable ABI foundation.
