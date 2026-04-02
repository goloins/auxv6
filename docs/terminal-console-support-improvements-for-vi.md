# Terminal/Console Support Improvements for vi/vim

## Objective

Enable reliable vi/vim-style full-screen editing on auxv6 with a Unix-compatible terminal stack.

This plan prioritizes POSIX/SUS-correct behavior in kernel and ulib over app-specific shortcuts. The target is stable vi/tinyvim operation first, then PTY-backed multi-session correctness.

## Compatibility Principles

1. Prefer standards-conformant tty/termios/ioctl semantics over editor-specific hacks.
2. Treat current console behavior as a bootstrap implementation, not the compatibility endpoint.
3. Make behavior measurable with repeatable tests at each phase.
4. Preserve existing functionality (shell job control, select/poll, telnet raw mode) while expanding compatibility.

## Current Status Snapshot (2026-04-01)

Already landed:
1. Non-canonical reads with `VMIN/VTIME` behavior.
2. `tcsetattr()` optional actions (`TCSANOW`, `TCSADRAIN`, `TCSAFLUSH`).
3. Background tty semantics (`SIGTTIN`/`SIGTTOU`) in read/write paths.
4. `TIOCGWINSZ`/`TIOCSWINSZ` and `SIGWINCH` propagation.
5. ANSI/CSI handling sufficient for many line-editor and shell redraw paths.
6. Linux-compatible ioctl termios/queue compatibility (`TCGETS`, `TCSETS*`, `FIONREAD`/`TIOCINQ`, `TIOCOUTQ`).
7. Dynamic PTY allocation via `/dev/ptmx` -> `/dev/pts/N`, with per-endpoint termios/winsize/ioctl routing.
8. Baseline `TERM=vt100` + `/etc/termcap` staging for capability discovery.
9. DECSET/DECRST parser tranche: multi-parameter `CSI ? ... h/l`, autowrap mode (`?7`), and insert mode (`CSI 4 h/l`).
10. PTY ioctl semantics tranche: `TIOCSCTTY` now binds PTY foreground process group to caller `pgid`, and termcheck covers `TIOCSPGRP/TIOCGPGRP` roundtrip on PTY slave fds.
11. ANSI parser extension tranche: added `CSI X` (erase chars), `CSI b` (repeat last glyph), `CSI d`/`CSI e`/`CSI a` cursor ops, and `CSI ! p` soft reset handling.
12. Terminal query compatibility tranche: `CSI 5n`/`CSI 6n` DSR replies are now synthesized into tty input (`ESC[0n`, `ESC[row;colR`) including DEC-private variants.
13. PTY job-control tranche: PTY slave read/write now enforce background signal semantics (`SIGTTIN`/`SIGTTOU` with `TOSTOP`) and termcheck validates isolation.
14. Device attribute query tranche: `ESC Z` and `CSI c` now return a primary DA response (`ESC[?1;0c`), and termcheck validates DSR/DA roundtrips.
15. Terminal query/parsing follow-up tranche: console now supports secondary DA (`CSI > c` -> `ESC[>0;0;0c`) and termcheck now validates DEC-private CPR (`CSI ?6n`) plus cursor/wrap/REP parser probes (`?7` wrap toggling, `CSI X`, `CSI b`, `CSI 4 h/l`).
16. Terminal mode-query tranche: console now supports mode query replies for `CSI Ps $ p` and `CSI ? Ps $ p` (RMQ/DECRQM), returning `CSI Ps ; Pm $ y` / `CSI ? Ps ; Pm $ y`; termcheck full mode now validates insert mode (`Ps=4`) and cursor-visibility private mode (`Ps=25`) set/reset query responses.
17. Terminal DEC/private matrix follow-up: termcheck full mode now validates additional mode query transitions (private `?1`, `?7`, `?6`, `?5`, `?12`, `?1049` and standard `20`), unknown-mode query fallback (`Pm=0`), origin/scroll-region cursor invariants (`DECSTBM` + DECOM interaction for `CUP`/`CPR`), and alt-screen cursor save/restore invariants across `?1049h/l`.
18. Terminal DSR/private-report follow-up: console now synthesizes DEC private status replies for `CSI ? 15 n` (printer status -> `CSI ? 13 n`) and `CSI ? 25 n` (UDK status -> `CSI ? 21 n`), and termcheck full mode validates both roundtrips.

Still missing or incomplete for nano/curses-class reliability:
1. Session/controlling-tty integration for PTYs is still partial; job control still needs deeper session semantics beyond basic per-PTY foreground checks.
2. Terminal capability stack is termcap-only baseline; full terminfo database/tooling is still absent, but `/etc/termcap` now includes broader fullscreen capabilities (`ti/te`, `ks/ke`, `vi/ve/vs`, `im/ei`, `al/dl/dc/ic`, cursor movement aliases) with guest smoke checks to keep coverage stable.
3. ANSI parser still lacks portions of DEC/private behavior used by some curses implementations (for example full device-status reports, richer private mode matrix, and advanced terminal report sequences).
4. No configurable locale/wide-char width model akin full `wcwidth`/grapheme correctness for advanced TUI glyph handling.

## Staged Roadmap (Week-by-Week)

### Phase 0 (Week 1): Compatibility Contract and Baseline Harness

Deliverables:
1. Define must-pass Unix semantics for this milestone:
	- canonical/non-canonical read behavior,
	- termios control character behavior,
	- controlling terminal + process group semantics,
	- tty-generated signal behavior,
	- winsize ioctl behavior.
2. Create baseline tests and logs for current failures.
3. Add ANSI sequence capture harness from target editor redraw paths.

Exit criteria:
1. Baseline matrix is committed and reproducible.
2. Known failures are documented with concrete repro commands.

### Phase 1 (Week 2): tty API Surface Corrections (Kernel + ulib)

Deliverables:
1. Implement ioctl syscall path for terminal requests:
	- TIOCGWINSZ,
	- TIOCSWINSZ,
	- deterministic ENOTTY/EINVAL-style failures for unsupported requests.
2. Replace user-space ioctl stub behavior with syscall-backed behavior.
3. Fix ulib terminal identity helpers:
	- isatty(fd) based on tty-backed descriptor semantics,
	- ttyname(fd)/ttyname_r(fd, ...) minimally correct for console tty.
4. Preserve POSIX-shaped tcsetpgrp/tcgetpgrp wrapper behavior while keeping current ABI stable.

Exit criteria:
1. ioctl winsize round-trips from userspace.
2. isatty/ttyname tests pass for tty and non-tty fds.

### Phase 2 (Week 3): termios Correctness MVP

Deliverables:
1. Expand termios constants and behavior to POSIX-aligned subset required by vi/vim startup and interaction.
2. Implement optional actions beyond immediate apply:
	- TCSADRAIN,
	- TCSAFLUSH.
3. Implement critical c_cc semantics:
	- VMIN,
	- VTIME,
	- VINTR,
	- VSUSP,
	- VEOF.
4. Ensure tcgetattr/tcsetattr operate on tty-backed descriptors, not hardcoded assumptions.

Exit criteria:
1. Raw mode setup/teardown from editor startup path is stable.
2. Non-canonical reads obey VMIN/VTIME test matrix.

### Phase 3 (Week 4): Foreground/Background tty Semantics

Deliverables:
1. Enforce SIGTTIN on background tty read attempts.
2. Enforce SIGTTOU on background tty write attempts.
3. Confirm SIGINT/SIGTSTP target foreground process group of controlling tty.
4. Send SIGWINCH to foreground process group when winsize changes.

Exit criteria:
1. Shell fg/bg and Ctrl+Z flows behave like Unix job control expectations.
2. Background tty access tests match expected stop/signal behavior.

### Phase 4 (Week 5): ANSI/VT Parser MVP in Console Output Path

Deliverables:
1. Add strict parser for the sequence subset required by vi/vim redraw:
	- cursor movement/addressing,
	- erase line/screen,
	- CR/LF behavior,
	- tab behavior.
2. Add minimal attribute/color support required by target editor traces.
3. Preserve fast path for plain output and safe fallback for unknown sequences.

Exit criteria:
1. Editor redraw no longer produces visibly broken cursor/screen placement.
2. High-frequency redraw traces are handled without regressions.

### Phase 5 (Week 6): Input Translation Completeness

Deliverables:
1. Translate special keyboard keys into terminal sequences expected by editor keypaths:
	- arrows,
	- home/end,
	- page keys,
	- function keys where feasible.
2. Validate behavior across canonical/raw transitions and interrupted reads.

Exit criteria:
1. Navigation and command-mode key behavior in vi/vim-like apps is stable.

### Phase 6 (Week 7): PTY Design Finalization

Deliverables:
1. Define PTY master/slave model and buffering semantics.
2. Define controlling-tty assignment and session/pgrp transitions.
3. Define /dev/ptmx and /dev/pts/N device model and lifecycle.
4. Define select/poll readiness semantics for PTY endpoints.
5. Document migration path preserving console tty0 fallback.

Exit criteria:
1. PTY design spec is implementation-ready and testable.

### Phase 7 (Weeks 8-9): PTY Implementation

Deliverables:
1. Implement PTY driver and allocation/open plumbing.
2. Implement /dev/ptmx broker and slave endpoint lifecycle.
3. Move tty state to per-tty model where needed:
	- foreground process group,
	- termios state,
	- winsize,
	- tty signal routing.
4. Enable shell/editor sessions on PTY-backed terminals for validation.

Exit criteria:
1. Multiple PTY sessions operate independently.
2. Foreground/background semantics are enforced per controlling tty.

### Phase 8 (Week 10): Hardening and Compatibility Closure

Deliverables:
1. Execute full compatibility suite:
	- launch/edit/save/quit in vi-like target,
	- redraw stress,
	- suspend/resume + fg/bg,
	- winsize/SIGWINCH,
	- PTY multi-session isolation,
	- select/poll behavior on tty/pty endpoints.
2. Fix semantic regressions before adding nonessential terminal features.

Exit criteria:
1. Milestone matrix passes with no critical tty/job-control regressions.

## Kernel/ulib Work Breakdown

Kernel focus areas:
1. tty/ioctl syscall integration and request dispatch.
2. termios state and c_cc behavior in read/write paths.
3. foreground/background enforcement and tty-generated signals.
4. console output parser for ANSI/VT subset.
5. per-tty state model and PTY implementation.

ulib/userspace focus areas:
1. isatty/ttyname/ttyname_r correctness.
2. ioctl wrapper behavior and error propagation.
3. helper test utilities for winsize, termios, and tty semantics.

## Suggested Verification Matrix

1. API correctness:
	- tcgetattr/tcsetattr,
	- ioctl(TIOCGWINSZ/TIOCSWINSZ),
	- isatty/ttyname behavior on tty vs pipe/file/socket.
2. Job control:
	- Ctrl+C/Ctrl+Z delivery,
	- background read/write stop behavior,
	- fg/bg restoration.
3. Editor behavior:
	- command/insert transitions,
	- cursor motion,
	- screen redraw,
	- suspend/resume.
4. PTY behavior:
	- multi-session isolation,
	- controlling tty correctness,
	- signal/winsize isolation.
5. Regression coverage:
	- shell interaction,
	- telnet raw mode,
	- select/poll users.

## Milestone Acceptance Criteria

1. A vi-like editor can run interactively with correct redraw and key handling.
2. tty semantics for signals/process groups align with Unix expectations for tested cases.
3. winsize and SIGWINCH flows are functional.
4. PTY-backed interactive sessions work with per-tty semantics.
5. Existing terminal-aware tools remain functional.

## Implementation Notes

1. Implement standards-compatible behavior incrementally; avoid broad speculative feature dumps.
2. Prioritize high-frequency editor traces when expanding ANSI/VT support.
3. Keep behavior deterministic and test-driven at each phase boundary.
