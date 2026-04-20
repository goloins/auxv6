# auxv6 libc Math Gap Plan (2026-04-20)

## Scope

This note compares auxv6's current libc math surface against musl's public
`math.h` real-number API and turns the gap into an implementation plan.

The reference target is musl's exported real-math surface:

- C99 core math functions and classification support.
- POSIX/XSI Bessel and `signgam` surface.
- BSD/GNU compatibility entries that musl also exposes.

This is not a demand to make auxv6 bit-for-bit identical to musl internally.
It is a plan to reach roughly the same public API and semantics with musl as the
symbol checklist and preferred algorithm source.

## Current auxv6 State

Today auxv6 exposes only a tiny subset of libc math:

- `include/math.h` declares `pow(double, double)`.
- `include/math.h` provides inline `ceil`, `ceilf`, `floor`, and `floorf`.
- `libc/math.c` implements only `pow`, and only for integer exponents.

That means auxv6 is missing almost the entire standard libm-style surface.
Even the symbols that exist are not semantically complete:

- `pow` rejects non-integer exponents instead of implementing real `pow`.
- `ceil` and `floor` are truncation-based helpers and do not handle NaNs,
  infinities, signed zero, large magnitudes, or rounding-mode-sensitive corner
  cases correctly.
- `math.h` lacks classification macros and helper entry points expected by real
  callers.
- `float.h` currently defines only a small double-precision subset, which is not
  enough for a serious libm port.

## Missing Public Surface

### 1. C99 core families

For almost every family below, auxv6 is missing all `double`, `float`, and
`long double` entry points unless otherwise noted.

- Trig and inverse trig: `acos`, `asin`, `atan`, `atan2`, `cos`, `sin`, `tan`.
- Hyperbolic and inverse hyperbolic: `acosh`, `asinh`, `atanh`, `cosh`, `sinh`, `tanh`.
- Powers, exponentials, logs, roots: `cbrt`, `exp`, `exp2`, `expm1`, `hypot`, `log`, `log10`, `log1p`, `log2`, `logb`, `pow`, `sqrt`.
- Error and gamma functions: `erf`, `erfc`, `lgamma`, `tgamma`.
- Rounding and integral-conversion: `ceil`, `floor`, `trunc`, `round`, `rint`, `nearbyint`, `lrint`, `llrint`, `lround`, `llround`.
- Decomposition and scaling: `frexp`, `ldexp`, `scalbn`, `scalbln`, `ilogb`, `modf`.
- Neighbor and representation helpers: `copysign`, `nan`, `nextafter`, `nexttoward`.
- Remainder and fused/extrema ops: `fdim`, `fma`, `fmax`, `fmin`, `fmod`, `remainder`, `remquo`.
- Absolute value: `fabs`.

### 2. Classification and math-header support

auxv6 also lacks the support machinery callers expect from a modern `math.h`:

- `isfinite`, `isinf`, `isnan`, `isnormal`, `signbit`, `fpclassify`.
- Classification backend symbols along musl lines: `__fpclassify*`, `__signbit*`.
- `NAN`, `INFINITY`, `HUGE_VALF`, `HUGE_VAL`, `HUGE_VALL`, `math_errhandling`.
- `FP_*` classification constants and `FP_ILOGB0` / `FP_ILOGBNAN`.
- Full constant set such as `M_E`, `M_LN2`, `M_PI_2`, `M_SQRT2` where feature
  macros permit them.

### 3. POSIX/XSI and compatibility surface

The musl-visible non-core surface is also absent:

- XSI/POSIX: `j0`, `j1`, `jn`, `y0`, `y1`, `yn`, `signgam`.
- BSD/GNU compatibility: `drem`, `finite`, `lgamma_r`, `scalb`, `significand`.
- Float compatibility variants used by musl in BSD/GNU modes: `dremf`, `finitef`, `j0f`, `j1f`, `jnf`, `y0f`, `y1f`, `ynf`, `lgammaf_r`, `scalbf`, `significandf`.
- GNU-only: `exp10`, `pow10`, `sincos`, `lgammal_r` and corresponding float/long-double variants where musl provides them.

## Root Causes

The problem is broader than missing function bodies.

1. `math.h` is currently a compatibility stub, not a standards-shaped header.
2. `float.h` does not describe float and long-double properties well enough for
   portable libm code.
3. There is no shared internal libm helper layer for bit access, classification,
   argument reduction, overflow/underflow signaling, or excess-precision control.
4. auxv6 is i386, which means x87 excess precision and 80-bit `long double`
   matter if we want musl-style completeness instead of a double-only shim.
5. There is no math-specific validation suite yet.

## Recommended Strategy

Do not implement this as one giant `libc/math.c` rewrite.

Instead:

1. Adopt musl's public symbol list as the contract.
2. Build a small internal libm substrate first.
3. Port musl algorithms family by family, preserving musl's helper structure
   where that lowers risk.
4. Keep `double` correctness first, but do not design the ABI in a way that
   blocks later `float` and `long double` completion.
5. Treat i386 `long double` as a real target, not as a typedef alias for
   `double`, unless the project explicitly chooses a reduced-ABI policy.

## Phased Plan

### Phase 0: Lock the ABI and header contract

Goal: define the public surface before importing algorithms.

Work:

- Replace `include/math.h` with a standards-shaped header modeled on musl's
  public declarations and feature-test gating.
- Expand `include/float.h` so it includes real `float`, `double`, and
  `long double` limits needed by libm code.
- Decide whether auxv6 will support the full musl long-double ABI on i386.
  Recommendation: yes, because punting on ld80 will leak into header design,
  symbol behavior, and future port compatibility.
- Add feature macros and constants: `NAN`, `INFINITY`, `HUGE_VAL*`, `FP_*`,
  `math_errhandling`, and the common `M_*` constants under the same policy musl
  uses.

Exit criteria:

- Code can compile against a mostly complete `math.h` without link success yet.
- The public declaration set is intentionally aligned with musl's symbol list.

### Phase 1: Build the internal libm substrate

Goal: create the machinery most functions depend on.

Work:

- Add an internal header similar to musl's `libm.h`.
- Add bit-casting and word-extraction helpers for `float` and `double`.
- Add i386 ld80 shape helpers for `long double`.
- Add classification/sign helpers: `__fpclassify*`, `__signbit*`.
- Add overflow/underflow/invalid/divide-by-zero helpers similar to musl's
  `__math_*` family.
- Add barriers and forced-evaluation helpers needed to control x87 excess
  precision and exception behavior.

Exit criteria:

- Header macros like `isfinite` and `signbit` can be implemented correctly.
- New math code no longer needs ad hoc bit hacks inside each function.

### Phase 2: Land the low-risk, high-utility primitives

Goal: replace trivial stubs and unlock common ports quickly.

Implement first:

- `fabs`, `copysign`, `fmax`, `fmin`, `fdim`.
- Correct `ceil`, `floor`, `trunc`, `round`.
- `fmod`, `modf`, `frexp`, `ldexp`, `scalbn`, `scalbln`.
- `nextafter`, `nexttoward`, `ilogb`, `logb`, `nan`.

Why first:

- These have contained implementations.
- They unlock a large amount of caller code.
- They establish correct special-value handling before harder transcendental work.

Exit criteria:

- Existing `pow`/rounding hacks are removed.
- A basic standards-conforming scalar helper layer exists for both `double` and
  `float`.

### Phase 3: Fix the rounding and integer-conversion family

Goal: close the conversion and rounding API set that ports and stdio-style code
often reach for.

Implement:

- `rint`, `nearbyint`.
- `lrint`, `llrint`, `lround`, `llround`.
- Float and long-double variants.

Key constraint:

- These functions are where x87 rounding-mode and excess-precision mistakes show
  up quickly, so they should be built after the Phase 1 substrate exists.

### Phase 4: Port the root, exponential, logarithmic, and power core

Goal: establish the numerical backbone most higher-level math depends on.

Implement:

- `sqrt`, `cbrt`, `hypot`.
- `exp`, `exp2`, `expm1`.
- `log`, `log10`, `log1p`, `log2`.
- Replace auxv6's current integer-only `pow` with real `pow` and add `powf` /
  `powl`.
- `fma` once the base floating-point semantics are trustworthy.

Porting note:

- This is the first phase where reusing musl's internal table/data layout and
  helper organization likely saves more time than writing new code.

### Phase 5: Port trig and hyperbolic functions

Goal: complete the mainstream C99 transcendental surface.

Implement:

- `sin`, `cos`, `tan`, `atan`, `atan2`, `asin`, `acos`.
- `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh`.

Dependencies:

- Argument reduction helpers analogous to `__rem_pio2*`.
- Kernel helpers analogous to musl's `__sin*`, `__cos*`, `__tan*`.

Reason to split from Phase 4:

- Trig correctness and large-argument reduction are their own risk cluster.

### Phase 6: Port special functions

Goal: finish the full C99 numerical surface.

Implement:

- `erf`, `erfc`.
- `tgamma`, `lgamma`, `signgam`.
- Reentrant variants `lgamma_r`, `lgammaf_r`, `lgammal_r` if the extension set
  remains aligned with musl.

Why later:

- These are less commonly required for early ports.
- They depend on the quality of the core exp/log/trig substrate.

### Phase 7: Add XSI/BSD/GNU compatibility layer

Goal: finish musl-level compatibility rather than only strict C99.

Implement:

- Bessel family: `j0`, `j1`, `jn`, `y0`, `y1`, `yn` and float variants where
  musl exposes them.
- Compatibility aliases/functions: `drem`, `finite`, `scalb`, `significand`.
- GNU helpers: `sincos`, `exp10`, `pow10`.

Policy note:

- `pow10*` should be treated as a compatibility alias layer, not as a separate
  algorithmic investment.

## Suggested Delivery Order Inside Each Phase

For each family, use this order:

1. Implement the `double` version.
2. Implement `float` either as a real port or a thin musl-style wrapper where
   musl does that safely.
3. Implement `long double` with real i386 ld80 handling.
4. Add any compatibility aliases only after the main function is validated.

This avoids a common failure mode where wrappers are added early but the base
semantics are still wrong.

## Validation Plan

This work needs a dedicated math test layer; otherwise regressions will hide in
special cases.

Minimum coverage:

- Header-compile tests for declaration visibility under feature macros.
- Unit-style tests for zeros, signed zeros, infinities, NaNs, subnormals,
  overflow, underflow, and domain/pole cases.
- Cross-check tests against known identities and high-value reference vectors.
- Integer-boundary tests for `ceil`/`floor`/`round`/`lrint` families.
- Large-argument tests for trig reduction.
- Accuracy sampling against musl or host reference results for representative
  ranges.

Given the current project workflow, the simplest path is probably a small set of
native auxv6 test binaries rather than a large external harness.

## Practical Implementation Notes

- Preserve `libc.a` integration; there is no need to split out a separate
  `libm.a` unless the broader build strategy changes.
- Stop using inline header implementations for functions that need correct IEEE
  semantics.
- Keep public headers clean and move implementation details into internal libc
  headers under `libc/` or `include/auxv6/` if needed.
- Prefer importing musl's algorithm structure over inventing new approximations.
  That reduces numerical risk and keeps future diffs against upstream source
  understandable.

## Recommended Milestones

### Milestone A: standards-shaped headers and helper substrate

- Complete Phases 0 and 1.
- No major port should fail to compile just because `math.h` is missing symbols.

### Milestone B: practical port-enabling math

- Complete Phases 2 through 4.
- This should cover the majority of ordinary userland and many ports.

### Milestone C: full C99 libm parity target

- Complete Phases 5 and 6.
- auxv6 now has a serious C99 math implementation rather than compatibility stubs.

### Milestone D: musl-style compatibility surface

- Complete Phase 7.
- auxv6 reaches the musl-shaped extended surface used by stricter ports and
  compatibility-heavy software.

## Bottom Line

The missing work is not "add a few functions to `math.c`".

The right plan is:

1. Define the public `math.h` and floating-point ABI correctly.
2. Add the internal libm substrate.
3. Port musl-compatible implementations in dependency order.
4. Validate special cases aggressively.

If done in that order, auxv6 can move from a stub math layer to a credible
musl-shaped libc math surface without accumulating a second round of cleanup
debt.