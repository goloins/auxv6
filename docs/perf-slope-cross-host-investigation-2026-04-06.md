# Performance Slope Investigation and Cross-Host Build Analysis (2026-04-06)

## Scope

This note captures the full investigation thread around long-run performance decline, especially:

- declining throughput over uptime (notably in kallocstress pipe-page-churn)
- suspicion that VM corruption hardening/repair work introduced overhead
- possibility of cross-host build/toolchain contamination on amd64 host

This is intended as a durable handoff document for continued debugging on the other host.

## Problem Statement

Observed behavior:

- system gets slower the longer it runs
- in prior periods, this appeared after VM/COW/SMP corruption mitigation work
- current host (macOS with dedicated cross-compiler) appears stable/consistent
- other host (amd64, more powerful CPU) appears more affected by slope

Key user hypothesis:

- a VM-era mitigation may have become a persistent overhead "wallpaper"
- or an amd64 host build/toolchain difference may be introducing bugs/regressions

## Initial Suspect Classes

1. Tick wakeup path overhead
2. VM kernel-half PDE sync/repair path overhead
3. Ongoing PDE/PTE repair churn in steady state
4. Procfs monitor/diagnostic overhead masking root cause
5. Cross-host build contamination or toolchain drift

## Investigation Actions

### 1) History and hot-path review

Reviewed recent commit window and code paths across:

- kernel/core/vm.c
- kernel/core/proc.c
- kernel/core/trap.c
- kernel/core/pipe.c
- kernel/fs/procfs.c
- kernel/core/sysfile.c

Timeline findings:

- VM sync/repair path integration in switch paths predates later tick queue work.
- Tick-sleeper queue optimization landed later in performance-chasing commits.

### 2) New diagnostic binary added

Implemented vmprobe utility for controlled hypothesis testing:

- source: user/vmprobe.c
- build target: _vmprobe
- man page: targetfs/usr/share/man/vmprobe.md

Purpose:

- run deterministic micro-phases
- correlate throughput with /proc/vmstat and /proc/schedstat deltas
- distinguish steady VM sync tax vs active repair churn vs tick scaling effects

Phases per round:

1. fork/wait burst (switch-path pressure)
2. tick-sleeper low fanout
3. tick-sleeper high fanout (2x sleepers)

Reported signals:

- vm_sync_calls, vm_sync_entries, entries_per_fork
- vm_pde_repairs, vm_master_repairs, vm_bad_pte_drops
- wake_ticks_calls, wake_scanned
- tick_scale_ratio and first/last fork_ops slope

## User-Run Evidence

### vmprobe runs

Run set A:

- vmprobe (default)
- summary showed one short-window slope warning (first 960/s -> last 800/s, 83%)

Run set B:

- vmprobe -r 16 -f 160 -s 8 -i 24
- summary: first 888/s -> last 888/s, slope 100%
- no strong fork/switch decline over this longer controlled window

Run set C:

- vmprobe -r 8 -f 64 -s 12 -i 32
- summary: first 800/s -> last 800/s, slope 100%
- no strong fork/switch decline in this configuration

Common vmprobe pattern across runs:

- vm_pde_repairs = 0
- vm_master_repairs = 0
- vm_bad_pte_drops = 0
- vm_sync counters high but stable (steady activity, not runaway repair)
- tick_scale_ratio near ~1.03-1.12 under 2x sleepers (no obvious explosive scaling)

### kallocstress evidence

kallocstress -n 10 (profile r4) showed clear progressive decline in one subtest:

- pipe-page-churn performance decayed run-over-run (about 444 -> 229 round/s)
- fork-copyuvm-pressure remained strong
- allocator-reclaim remained strong
- reclaim memory remained flat (no obvious free-memory leak signal)

Diagnostic hints from kallocstress output:

- read-side timing in pipe path increased substantially over runs
- vm_sync deltas remained active but broadly stable
- vm repair counters remained zero

## Interpretation (Current Best)

1. Active page-table repair churn is not the present slope driver.

Reason:

- repairs and bad-pte-drop counters stayed at zero during vmprobe and kallocstress windows.

2. VM sync path likely contributes a steady overhead ceiling, not a growing leak.

Reason:

- vm_sync_* counters are consistently active due to switch-path usage,
  but behavior appears stable rather than escalating.

3. Current long-run slope is more consistent with pipe/read-path degradation under churn.

Reason:

- kallocstress decline is concentrated in pipe-page-churn while fork/reclaim remain stable.

4. Tick wake path is likely not dominant in the currently observed slope.

Reason:

- controlled sleeper doubling in vmprobe showed modest scaling, not dramatic blow-up.

## Cross-Host Build Contamination Assessment

Question investigated:

- could amd64 host leak host headers/structures into auxv6 builds and cause bugs?

Assessment:

- still possible in principle, but less likely to be classic header leakage now.
- stronger risk is toolchain/codegen drift between hosts.

Why:

- root Makefile enforces -nostdinc and has toolchain checks for i386 output and libgcc helpers.
- recent build hardening specifically addressed suspected host header contamination paths.
- TOOLPREFIX fallback can still select different compilers/binutils depending on host setup.
- different compiler/libgcc/binutils behavior can expose UB/race timing differences without header leaks.

Conclusion:

- prioritize proving/standardizing toolchain identity across hosts before attributing to architecture alone.

## Practical Conclusions

1. Keep corruption containment for now if needed for stability.
2. Do not assume repair path is current slope root cause when repair counters are zero.
3. Treat VM sync in switch path as likely steady tax and revisit when stable root cause path is isolated.
4. Focus immediate performance root-cause work on pipe read/churn behavior.
5. For cross-host mismatch, standardize/verify exact cross-toolchain and link/runtime selection.

## Suggested Next Work (Follow-up)

1. Add focused pipe-read attribution counters (lock-held time, copyout time, wake/sleep transitions).
2. Add vmprobe mode that runs a pipe-churn microbench directly and reports per-round read-path drift.
3. Add build fingerprint output (compiler path/version, TOOLPREFIX, LIBGCC path, key object hashes) to compare hosts deterministically.
4. Re-run same scripted sequence on both hosts and compare fingerprints plus vmprobe/kallocstress deltas.

## Files Added/Updated During This Investigation

- user/vmprobe.c
- targetfs/usr/share/man/vmprobe.md
- docs/man-pages.md (vmprobe entry)
- Makefile (_vmprobe target and UPROGS inclusion)
- .gitignore (user/vmprobe)

## Operator Note

The observed behavior is no longer best described as broad allocator collapse.
Current evidence supports a narrower sustained performance regression, with strongest signal in pipe-page-churn read-side cost growth while VM repair counters remain quiet.
