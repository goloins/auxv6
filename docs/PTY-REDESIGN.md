# PTY Subsystem Redesign - April 2026

## Executive Summary

The current PTY implementation has fundamental architectural issues that prevent stable shell/terminal interaction. This document outlines a comprehensive rewrite that:
- Adopts proven design patterns from OpenBSD, FreeBSD, NetBSD, and Linux
- Implements a full line discipline layer (no shell-side processing needed)
- Uses single ref-counting via file->ref (no separate master_refs/slave_refs)
- Provides proper signal delivery (SIGWINCH, SIGHUP, etc)
- Is future-proof for at least 6 months of OS development

## Scaffolded Files (Now In Tree)

The following files were created as the first implementation slice of the rewrite:

- `include/tty.h`
   - Purpose: Shared kernel TTY constants and sizing knobs for the new line discipline.
   - Current contents: `TTY_LDISC_CANON_BUFSZ`, `TTY_LDISC_SCRATCH_BUFSZ`.

- `kernel/driver/tty_ldisc.h`
   - Purpose: Public interface for kernel line discipline processing.
   - Current contents: `struct tty_ldisc_state`, init/reset functions, input/output processing APIs.

- `kernel/driver/tty_ldisc.c`
   - Purpose: New line discipline engine inspired by BSD `tty.c` patterns.
   - Current contents:
      - Canonical input buffering (`ICANON`)
      - Echo handling (`ECHO`, `ECHOE`, `ECHOK`, `ECHONL`)
      - Input translation (`IGNCR`, `ICRNL`, `INLCR`)
      - Output post-processing (`OPOST`, `ONLCR`, `OCRNL`, `ONOCR`, `ONLRET`)
      - Basic software flow control state (`IXON` start/stop tracking)

Files updated for integration:

- `Makefile`
   - Added `kernel/driver/tty_ldisc.o` to `OBJS`.

- `include/defs.h`
   - Added prototypes for the new line discipline APIs.

- `kernel/driver/pty.c`
   - Added `tty_ldisc` state into each PTY pair.
   - Wired write-path processing through the new line discipline.
   - Removed noisy debug logging from poll/open paths to restore runtime responsiveness.

## OpenBSD/NetBSD Design Notes Applied

Patterns already adopted:

- Keep PTY transport and line discipline as separate responsibilities (BSD split model).
- Keep line discipline state per terminal endpoint (canonical queue + output column tracking).
- Treat termios flags as kernel-enforced behavior, not shell-enforced behavior.
- Keep processing hooks explicit and testable (`process_input`, `process_output`).

Patterns queued next:

- Job-control signal behavior parity with BSD tty semantics (`SIGTTIN`, `SIGTTOU`, `SIGHUP`).
- Better peer-closure semantics tied to actual file lifetime.
- Optional packet/remote modes (`TIOCPKT`/`TIOCREMOTE`-style) if needed by userland.

## Current Problems

1. **Double Ref-Counting**: `master_refs`/`slave_refs` desync from `file->ref` across fork
2. **No Line Discipline**: ECHO, ICANON, ICRNL, ONLCR not kernel-enforced
3. **Fragile EOF Logic**: Can't reliably detect when peer truly closes
4. **No Signal Infrastructure**: SIGWINCH ignored, job control broken
5. **Incomplete POSIX termios**: Missing flow control, input processing
6. **Architecture**: Monolithic driver, mixing low-level I/O with high-level tty state

## Design Goals

### Core PTY Semantics (POSIX-compliant)
- **Single unified data path**: Master ↔ [PTY] ↔ Slave  
- **Proper ref-counting**: File table owns PTY lifetime via `file->ref`
- **EOF semantics**: EOF only when slave closes AND no input pending
- **Atomic operations**: Prevent TOCTOU races on termios, winsize, etc
- **Proper wakeups**: Both input/output sides wake correctly after blocking

### Line Discipline Layer
Implement kernel-side processing of:
- **ICANON** (canonical mode): Buffer input by lines, deliver on newline or special chars
- **ECHO**: Kernel echoes input back to master (user sees typed chars)
- **C_cc processing**: Respect INTR, QUIT, ERASE, KILL, EOF, START, STOP, SUSP
- **ICRNL**: Convert CR→LF on input
- **ONLCR**: Convert NL→CRLF on output (and OCRNL, ONOCR variants)
- **IXON/IXOFF**: Software flow control (^S/^Q)
- **c_lflag flags**: ISIG (send signals), IEXTEN (extended input processing)

### Signal Delivery
- **SIGWINCH**: On window size change (TIOCSWINSZ)
- **SIGHUP**: On final master close while slave endpoints still exist (job control)
- **SIGTTOU/SIGTTIN**: Job control signals (background process writes/reads)

### Termios/Winsize Storage
- Single canonical location (not replicated)
- Atomic reads/writes under min lock duration
- Proper notification of changes to both sides

## Architecture

### Kernel Structures

```c
// Line discipline data and operations
struct tty_ldisc {
  // Input side
  struct {
    char canon_buf[4096];   // Canonical line buffer
    int canon_len;          // Chars in current line
    char raw_buf[512];      // Raw input from master
    int raw_len;
  } input;
  
  // Output processing state
  struct {
    int oflag_state;        // Track CR/NL state for OCRNL, ONOCR
  } output;
  
  // Methods
  int (*input_process)(struct tty_ldisc *ld, const char *raw, int n, char *out);
  int (*output_process)(struct tty_ldisc *ld, const char *raw, int n, char *out);
};

struct pty_chan {
  char buf[4096];           // Larger buffer: 1 page
  uint r, w;                // Read/write pointers
};

struct pty_pair {
  // Unified data: use file->ref as ground truth for lifetime
  struct pty_chan master_to_slave;  // Master → Slave (user typing)
  struct pty_chan slave_to_master;  // Slave → Master (shell output)
  
  // Control state (protected by lock)
  struct termios termios;
  struct winsize winsize;
  pid_t fg_pgid;            // Foreground process group
  
  struct tty_ldisc ldisc;   // Line discipline state
  
  // Signaling
  int has_master;           // Any master fd open (read from file table)
  int has_slave;            // Any slave fd open  (read from file table)
  
  struct spinlock lock;     // Protects termios, winsize, ldisc state
};
```

### Key Changes from Current

1. **No master_refs/slave_refs**: Inspect open file table directly
2. **Larger buffers**: 4KB instead of 512B to reduce wakeup frequency
3. **Line discipline layer**: Separate module that processes I/O
4. **Atomic termios updates**: Single lock covers all control state
5. **Proper EOF**: Only return 0 when peer truly closed (check file table)

## Implementation Phases

### Phase 1: Core PTY Rewrite (Clean slate)
- [ ] Remove old pty.c code entirely
- [ ] Implement new pty_pair and pty_chan structures
- [ ] Implement basic open/close/read/write (no ldisc yet, raw passthrough)
- [ ] Replace ref-counting: use file open/close hooks
- [ ] Add proper EOF logic using file table inspection

### Phase 2: Line Discipline
- [ ] Implement tty_ldisc structure
- [ ] ICANON: Buffer input by lines
- [ ] ECHO: Echo input back to master
- [ ] ICRNL/ONLCR: Handle CR/LF conversion
- [ ] Test with shell interactive input

### Phase 3: Signal Delivery
- [ ] Implement fg_pgid tracking
- [ ] SIGWINCH on TIOCSWINSZ
- [ ] SIGTTIN/SIGTTOU job control
- [x] SIGHUP on master close

### Phase 4: Console/Supporting Infrastructure
- [ ] Audit console.c for PTY compatibility
- [ ] Check file.c ref-counting semantics
- [ ] Verify select/poll work with new PTY
- [ ] Check proc.c job control integration

## Reference Materials

### OpenBSD: `src/sys/kern/tty.c`, `src/sys/kern/tty_pty.c`
- **Good for**: Hardened line discipline, clean signal handling
- **Note**: Uses tty_getput/tty_putchar for atomic access

### FreeBSD: `sys/kern/tty_pty.c`, `sys/kern/tty.c`
- **Good for**: Well-documented termios implementation, output processing
- **Note**: Uses clist (character list) for buffering (we'll use simpler ring buffer)

### NetBSD: `sys/kern/tty.c`, `sys/kern/pty.c`
- **Good for**: Flexible line discipline architecture
- **Note**: Supports multiple ld implementations

### Linux: `drivers/tty/pty.c`, `drivers/tty/n_tty.c`
- **Good for**: Large-scale testing, performance patterns
- **Avoid**: Complexity not needed for our scale

## Known Missing Infrastructure

### Will Implement As Needed

1. **Signal delivery to process groups**
   - Current: `proc_signal_pgid()` exists but untested with ldisc
   - Needed: Verify reaches all members, proper pending signal queuing

2. **Job control signals (SIGTTIN, SIGTTOU)**
   - Current: Stub implementation in pty.c
   - Needed: Full suspend/resume on background access

3. **Console I/O buffering**
   - Current: console.c has direct character output
   - Needed: Ensure safe concurrent access with PTY writes

4. **File table query interface**
   - Current: file->ref is private to fs/file.c  
   - Needed: Public fn to check "is fd open" for EOF logic

## Success Criteria

1. **Shell interaction**: Full interactive dash shell in st
   - Type characters, see echo, press Enter, get prompt back
   - Commands execute and produce output to terminal
   - ^C, ^Z, job control works

2. **POSIX compliance**: ICANON, ECHO, ICRNL, flow control
   - stty commands work
   - Editing (backspace, ^U) works
   - Cooked mode vs raw mode

3. **Stability**: No spontaneous EOF, hangs, or lost output
   - Run for 30 min with interactive use
   - 100 shell open/close cycles without deadlock

4. **Performance**: 
   - Interactive latency <50ms (type→echo)
   - No 100% CPU spin on select

## File Structure

```
kernel/driver/pty.c          ← Rewrite: PTY pair management, open/close/ioctl
kernel/driver/tty_ldisc.c    ← New: Line discipline processing
kernel/driver/tty_ldisc.h    ← New: Line discipline interface
include/tty.h                ← New: Public TTY constants
user/tty.c                   ← May need updates for new APIs
```

## Next Steps

1. Read reference implementations (OpenBSD tty.c)
2. Prototype line discipline layer separately
3. Rewrite pty.c from scratch (don't patch old code)
4. Test incrementally: raw passthrough → ICANON → full ldisc
5. Validate against POSIX test suite

## Implementation Status Update (2026-04-10, Pass 2)

This section records concrete implementation deltas completed after scaffolding.

- PTY peer-liveness decisions now use live fdtable scans (`pty_side_is_open`) in read/write blocking, poll readiness, open validation, and final pair recycle decisions.
- Blocking channel helpers were hardened to re-check peer openness while sleeping, so blocked readers/writers do not wait forever after peer shutdown.
- Input line-discipline path now emits signal intents from control characters when `ISIG` is enabled (`VINTR`, `VQUIT`, `VSUSP`), and PTY write path forwards those to the foreground process group.
- `NOFLSH` behavior is now honored for signal-generating control characters during ldisc input processing.
- Legacy side counters (`master_refs`, `slave_refs`) were removed from PTY runtime behavior and struct storage; side-open truth is now derived from live open files.
- Final master-close now triggers hangup signaling (`SIGHUP`, then `SIGCONT`) for the foreground process group when slaves are still present.
- Canonical edit behavior was extended with `VWERASE` (word erase) and `VREPRINT` (reprint current cooked input line) under `IEXTEN`, plus PTY defaults for those control characters.
- Added Linux-compatible slave lock ioctls (`TIOCSPTLCK`, `TIOCGPTLCK`) with default locked slave state after `ptmx` allocation; slave opens are denied until unlocked from master side.
- Updated libc `unlockpt` to actively clear kernel slave lock and updated `openpty` to perform `grantpt`/`unlockpt` before opening the slave.
- Fixed syscall dispatch regression for PTY lock ioctls by routing `TIOCSPTLCK`/`TIOCGPTLCK` through `sys_ioctl` tty dispatch, restoring `openpty` success in `st` + `dash` startup.
- Fixed `dup2` close ordering to clear fdtable slots before `fileclose`, matching `sys_close` semantics and preventing PTY side-open accounting skew during stdio reassignment (`dup2` + `close`) in terminal child setup.

Open items for the next pass:

- Expand canonical edit behavior toward BSD parity (`VWERASE`, `VREPRINT`, and richer echo-control semantics).
- Complete hangup/read-write edge semantics under partial-buffer and close races.
- Add focused PTY regression tests for EOF/HUP/SIG behavior and document expected outcomes.
