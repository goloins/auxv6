# auxv6 MLFQ Scheduler Implementation Plan (High-Precision Rollout)

Date: 2026-04-18
Status: Design/implementation plan only (no code changes in this document)
Audience: kernel maintainers, scheduler contributors, validation owners

## 1. Executive Intent

This document defines a precise, low-regression path to evolve auxv6 from global table-scan round-robin behavior to a multi-level feedback queue (MLFQ) scheduler tuned for bursty desktop/power-user workloads.

Primary objective:
- Improve interactive latency under mixed load while preserving throughput and existing synchronization correctness.

Primary safety requirement:
- Maintain current process-state, lock-ordering, and trap-return invariants at every stage. Scheduler work has broad blast radius, so each phase is independently testable and revertible.

## 2. Verified Current-System Interface Baseline

This section is the explicit interface cross-check performed before making any decision.

### 2.1 Core scheduler interfaces (confirmed)

- `scheduler()` in `kernel/core/proc_sched.c`
  - Per-CPU loop, `sti()`, acquire `ptable.lock`, scan `ptable.proc[]` for `RUNNABLE`, set `RUNNING`, context-switch, idle `hlt` when no work.
- `sched()` in `kernel/core/proc_sched.c`
  - Requires only `ptable.lock` held, current process state already changed away from `RUNNING`, interrupts disabled semantics enforced.
- `yield()` in `kernel/core/proc_sched.c`
  - Sets current process to `RUNNABLE` and enters `sched()` under `ptable.lock`.

### 2.2 Process state transition entry points (confirmed)

- Sleep path:
  - `sleep(void *chan, struct spinlock *lk)` in `kernel/core/proc.c`
  - transitions current proc to `SLEEPING` under `ptable.lock` then `sched()`.
- Wake path:
  - `wakeup1()` / `wakeup()` in `kernel/core/proc.c`
  - transitions matching sleepers to `RUNNABLE` under `ptable.lock`.
- Fork admission:
  - child set to `RUNNABLE` in `kernel/core/proc_lifecycle.c`.
- Signal-driven resume:
  - `proc_note_signal_locked()` may move `STOPPED` or `SLEEPING` tasks to `RUNNABLE`.
- Alarm expiry:
  - `proc_check_alarms()` may wake sleeping tasks by setting `RUNNABLE`.

### 2.3 Trap/timer contract (confirmed)

- Timer interrupt path in `kernel/core/trap.c`:
  - CPU0 updates `ticks`, wakeup tick sleepers, runs clock-based periodic work.
  - All CPUs charge `myproc()->cticks` on timer tick if running a process.
  - If current process is `RUNNING` on timer interrupt, `yield()` is forced.

Implication:
- Time-slice enforcement can be policy-driven, but preemption trigger remains timer-driven and must stay compatible with existing trap flow.

### 2.4 Observable API surface (confirmed)

- Existing exported scheduler/load metrics:
  - `proc_get_sched_stats()` in `kernel/core/proc_stats.c`
  - `proc_get_sched_latency_stats()` in `kernel/core/proc_stats.c`
  - `/proc/schedstat` formatting in `kernel/fs/procfs.c`
- Process snapshot path used by user tooling:
  - `proc_snapshot()` in `kernel/core/proc.c`
  - `/proc/ps` output in `kernel/fs/procfs.c`

Compatibility implication:
- `/proc/schedstat` existing keys should not be removed/renamed in first MLFQ landing.

### 2.5 Struct-level ABI concerns (confirmed)

- Kernel/user shared process header exists (`include/proc.h` and mirrored targetfs header).
- `struct proc` is kernel-internal in practice, but changes may affect tooling assumptions via procfs outputs.

Decision:
- Preserve userspace-visible fields and procfs schema while adding new scheduler internals.

## 3. Scope and Non-Goals

In scope (initial MLFQ landing):
- Replace runnable selection policy with MLFQ.
- Introduce per-process scheduling metadata.
- Route all RUNNABLE transitions through scheduler-aware enqueue helpers.
- Add MLFQ observability counters and procfs exposure.
- Preserve existing trap/signal/sleep semantics.

Not in scope (initial landing):
- Full per-CPU runqueue architecture with work stealing.
- New user-facing scheduler syscalls or priority APIs.
- Real-time/deadline scheduling classes.
- Changing timer frequency.

## 4. MLFQ Policy Specification (initial tuned profile)

### 4.1 Queue topology

Use 5 queues, highest priority first:
- `Q0` (highest)
- `Q1`
- `Q2`
- `Q3`
- `Q4` (lowest)

Round-robin within each queue.

### 4.2 Quantum map (at 100 Hz tick)

- `Q0`: 1 tick
- `Q1`: 2 ticks
- `Q2`: 4 ticks
- `Q3`: 8 ticks
- `Q4`: 16 ticks

Rationale:
- Short quanta at top improve interactivity.
- Exponential widening toward bottom preserves throughput and lowers context switch overhead for CPU-bound jobs.

### 4.3 Admission and movement rules

- New child (`fork`) starts in `Q1` (not `Q0`) with full `Q1` budget.
- If process uses full budget while still running, demote by one queue (clamped at `Q4`).
- If process blocks before budget exhaustion and has meaningful sleep interval, promote by one queue (clamped at `Q0`).
- `yield()` does not automatically promote.
- Global anti-starvation boost every 200 ticks: move runnable/sleeping tasks to `Q1` and reset budget.

### 4.4 Anti-gaming rule

Do not reward very short sleep/yield loops as strongly as true I/O-interactive behavior:
- promotion only if block duration >= 2 ticks OR process had substantial unused budget and is not repeatedly micro-sleeping.

## 5. Data Model Additions

## 5.1 `struct proc` additions (kernel-internal scheduling metadata)

Add fields near existing CPU accounting (`cticks`) in `include/proc.h`:
- `uchar sched_q;`                     // current MLFQ level [0..4]
- `uchar sched_flags;`                 // bit flags (queued, boost-applied, etc.)
- `ushort sched_budget_left;`          // ticks left in current quantum
- `uint sched_enq_tick;`               // tick timestamp when enqueued runnable
- `uint sched_last_start_tick;`        // timestamp when dispatched
- `uint sched_last_block_tick;`        // timestamp when transitioned to sleep
- `uint sched_last_wake_tick;`         // timestamp when woken runnable
- `uint sched_cpu_burst_ticks;`        // recent CPU burst accumulator
- `uint sched_sleep_avg_x8;`           // EWMA sleep estimate (fixed-point)

Initialization requirements:
- Zero/initialize in alloc path (`allocproc`) and reset-on-reap path (`wait` zombie cleanup).
- Seed defaults for `userinit`, `fork` child.

### 5.2 `struct cpu` additions (observability only for phase 1)

Add optional per-CPU MLFQ counters in `include/proc.h`:
- `uint sched_q_dispatch[5];`
- `uint sched_q_empty_passes;`

Keep legacy counters intact:
- `sched_passes`, `sched_idle_halts`, `sched_picks` remain unchanged.

## 6. Queue Structure and Locking Model

Phase-1 model (selected for lowest risk):
- Single global MLFQ queue set protected by existing `ptable.lock`.

Proposed queue representation:
- Intrusive doubly-linked list per queue using `struct proc *sched_next`, `*sched_prev`.
- Head/tail per queue in scheduler state object.

Alternative considered:
- Bitmap + ring indices over proc slots.

Why intrusive list now:
- Simpler O(1) enqueue/dequeue under current lock discipline.
- Minimal coupling to `NPROC` table layout.

Locking invariant:
- Any queue mutation requires `ptable.lock` held.
- No additional lock introduced in phase 1.

## 7. New Internal Scheduler Helper API

Add these internal helpers (declared in core-local headers or static where possible):

- `static void schedq_init_locked(void);`
- `static void schedq_enqueue_locked(struct proc *p, int reason);`
- `static void schedq_dequeue_locked(struct proc *p);`
- `static struct proc* schedq_pick_next_locked(void);`
- `static void sched_on_run_start_locked(struct proc *p, uint now_ticks);`
- `static int sched_on_tick_preempt_locked(struct proc *p, uint now_ticks);`
- `static void sched_on_block_locked(struct proc *p, uint now_ticks);`
- `static void sched_on_wake_locked(struct proc *p, uint now_ticks, int wake_reason);`
- `static void sched_apply_global_boost_locked(uint now_ticks);`

Reason enums (internal):
- `SCHED_ENQ_FORK`, `SCHED_ENQ_WAKE`, `SCHED_ENQ_YIELD`, `SCHED_ENQ_SIGNAL`, `SCHED_ENQ_ALARM`, `SCHED_ENQ_BOOST`.

Wake reasons (internal):
- `SCHED_WAKE_TICK`, `SCHED_WAKE_IO`, `SCHED_WAKE_SIGNAL`, `SCHED_WAKE_TIMEOUT`, `SCHED_WAKE_OTHER`.

## 8. Integration Touchpoints (Exact Landing Areas)

All listed locations are current interface points requiring adaptation to avoid bypassing policy.

### 8.1 `kernel/core/proc_sched.c`

Changes:
- Replace table-scan runnable selection in `scheduler()` with `schedq_pick_next_locked()`.
- Keep idle `hlt` behavior unchanged when no runnable tasks in queues.
- In `yield()`, route to enqueue helper instead of raw `state = RUNNABLE` only.

Must preserve:
- `sched()` invariant checks.
- `switchuvm/swtch/switchkvm` ordering.

### 8.2 `kernel/core/proc.c` sleep/wakeup

Changes:
- In `sleep()`, call `sched_on_block_locked()` when transitioning to `SLEEPING`.
- In `wakeup1()`, replace direct `p->state = RUNNABLE` with helper that sets state and enqueues.

Must preserve:
- tick sleeper queue fast path behavior.
- channel hash optimization behavior.

### 8.3 `kernel/core/proc_lifecycle.c`

Changes:
- On `fork()` child admission: enqueue child into selected initial queue via helper.
- On reap/reset path: clear queue linkage/flags safely.

Must preserve:
- PID/state lifecycle semantics.

### 8.4 `kernel/core/proc_signal.c` and signal paths in `proc.c`

Changes:
- Any transition from `STOPPED/SLEEPING` to `RUNNABLE` must enqueue through helper.

Must preserve:
- signal semantics and existing killed/continue behavior.

### 8.5 `kernel/core/trap.c`

Changes:
- Timer tick path: after charging `cticks`, call scheduler tick accounting helper for currently running process.
- Keep final existing `yield()` preemption gate in place for first landing, driven by helper decision.

Must preserve:
- syscall/trap signal handling ordering.
- CPU0-only periodic tasks and current timer-source semantics.

### 8.6 `kernel/core/proc_stats.c` and `kernel/fs/procfs.c`

Changes:
- Add queue-level counters and movement counters.
- Extend `/proc/schedstat` with new keys, but do not remove legacy keys.

Suggested new `/proc/schedstat` keys:
- `mlfq_q0_len`..`mlfq_q4_len`
- `mlfq_dispatch_q0`..`mlfq_dispatch_q4`
- `mlfq_promotions`
- `mlfq_demotions`
- `mlfq_boosts`
- `mlfq_preempt_budget_expired`

Compatibility rule:
- Existing parsers expecting `passes`, `idle_halts`, `picks`, wake metrics must continue to function.

## 9. State Machine Contract (Process-State + Queue-State)

Define explicit dual-state model:

- Process-state (`UNUSED/EMBRYO/SLEEPING/RUNNABLE/RUNNING/STOPPED/ZOMBIE`) remains authoritative for lifecycle.
- Queue-state (`queued` flag + links) is valid only when process-state is `RUNNABLE`.

Required invariants:
- `state == RUNNABLE` implies queued exactly once.
- `state != RUNNABLE` implies not queued.
- `state == RUNNING` implies not queued and owned by one CPU (`cpu->proc == p`).

Debug assertions (KDEBUG path):
- assert queue membership consistency during enqueue/dequeue transitions.

## 10. Phase-by-Phase Implementation Plan

## Phase 0: Preparation and instrumentation hardening

Deliverables:
- Introduce scheduler constants and reason enums.
- Add placeholder fields in `struct proc` / `struct cpu` with init/reset wiring only.
- Add compile-time gate, for example `CONFIG_SCHED_MLFQ`.

Validation:
- Build passes.
- Boot and run baseline tests with flag disabled.

## Phase 1: Queue infrastructure under existing lock

Deliverables:
- Implement queue primitives and consistency assertions.
- Keep scheduler using old pick path while queue code is present but unused.

Validation:
- Unit-style kernel checks for enqueue/dequeue invariants in debug builds.

## Phase 2: Route all RUNNABLE transitions through enqueue helpers

Deliverables:
- Update fork/wakeup/signal/alarm/yield paths.
- Ensure no direct `state = RUNNABLE` remains without helper route.

Validation:
- grep audit for direct RUNNABLE writes.
- Boot + `schedperf`, `usertests`, signal/job-control paths.

## Phase 3: Switch `scheduler()` pick policy to MLFQ queues

Deliverables:
- Replace ptable scan dispatch with queue pick.
- Preserve idle `hlt` behavior.

Validation:
- No deadlocks/panics in prolonged idle and mixed-load tests.

## Phase 4: Tick-based budget accounting and demotion

Deliverables:
- Enforce queue quanta.
- Demote on budget exhaustion.

Validation:
- synthetic CPU hog tests show stable demotion to lower queues.

## Phase 5: Wake/block heuristics, promotions, and global boost

Deliverables:
- Promotion on meaningful block/wake patterns.
- periodic anti-starvation boost.

Validation:
- interactive workload latency improves while batch throughput remains acceptable.

## Phase 6: Observability completion

Deliverables:
- Add new `/proc/schedstat` keys and stats plumbing.
- optional `/proc/ps` scheduler column extension (defer unless needed).

Validation:
- tools consuming old keys remain functional.

## 11. Validation Matrix (Blast-Radius Coverage)

### 11.1 Core scheduler correctness

- Repeated context switch stress: `user/schedperf.c`
- Multi-process yield storms.
- Idle system hlt behavior preserved.

### 11.2 Sleep/wakeup correctness

- tick sleep path (`sys_sleep`) regressions.
- pipe read/write wakeup behavior.
- wait/waitpid parent-child channel wakeups.

### 11.3 Signals/job-control correctness

- stop/continue transitions.
- signal delivery around preemption boundaries.
- kill during sleep/runnable/running states.

### 11.4 Subsystem integration regression scan

- Network periodic/timer path remains stable (`netdev_poll`, TCP slow timer).
- Audio daemon behavior under mixed load (`audiod`, poll/select wakeups).
- Terminal/UI interactive responsiveness under concurrent compile/load.

### 11.5 Procfs/tooling compatibility

- `/proc/schedstat`, `/proc/ps`, `/proc/loadavg` formatting sanity.
- `top` and `schedperf` remain operational.

## 12. Performance and Latency Targets

Initial measurable targets relative to current baseline:
- >= 30% reduction in p95 wake-to-run latency for interactive bursts under concurrent CPU stress.
- <= 5% regression in aggregate throughput for CPU-bound parallel compile-style workloads.
- No statistically significant increase in wakeup scan overhead counters.

## 13. Failure Modes and Guardrails

Potential failure modes:
- Lost runnable tasks due to missed enqueue on transition.
- Duplicate queue entries causing corruption or starvation.
- lock-order regressions under trap/wakeup race windows.
- starvation if boost logic broken.

Guardrails:
- Centralized enqueue helper with assertions.
- debug-only queue integrity scan (`O(NPROC)`) toggled by config.
- phased rollout with feature flag and immediate fallback path.

## 14. Rollback and Recovery Strategy

- Keep old RR pick path guarded by compile/runtime switch until Phase 5 is validated.
- If instability appears:
  1) disable MLFQ gate,
  2) retain instrumentation,
  3) collect `/proc/schedstat` and panic traces,
  4) bisect within phase boundary commits.

## 15. Open Decisions to Resolve Before Coding Phase 5

- exact promotion heuristic constants for micro-sleep filtering.
- whether interactive boost should cap at `Q0` or `Q1` for specific wake reasons.
- whether STOPPED tasks should have queue metadata reset or preserved on SIGCONT.

Default decision for first landing:
- conservative promotion (single-level), conservative boost target (`Q1`), full metadata reset on STOPPED->RUNNABLE transitions.

## 16. Proposed Commit Topology

1. sched: add mlfq data fields + config gate (no behavior change)
2. sched: add queue primitives + debug invariants (no behavior change)
3. sched: route runnable transitions through helpers
4. sched: switch dispatch path to mlfq selection
5. sched: add quantum accounting + demotion
6. sched: add wake heuristics + global boost
7. sched: add procfs telemetry and stats counters
8. docs/tests: update scheduler docs + extend schedperf scenarios

Each commit must boot and pass core smoke tests.

## 17. Definition of Done

MLFQ is considered successfully landed when all are true:
- No lock/race regressions in stress/soak runs.
- All baseline functional tests pass.
- Interactive workloads show materially better tail latency.
- Throughput regression remains within target.
- Procfs and tooling compatibility preserved.
- Fallback switch retained until at least one release cycle of validation completes.
