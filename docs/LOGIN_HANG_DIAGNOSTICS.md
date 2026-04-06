# Login Path Silent Hang - Diagnostic Hardening

## Problem
When attempting login with invalid username "oot" (typo), system hangs with zero UART output, then resets. This is a critical maturity issue because:
- Kernel must fail loudly, never silently disappear
- User cannot see any diagnostic information
- Unknown whether hang is in kernel or userspace

## Root Cause Hypothesis
Silent hang with zero UART output suggests:
1. **Spinlock deadlock with interrupts disabled** - `acquire()` spins forever, preventing timer IRQ from firing, preventing any UART ISR execution, and emergency reporter cannot run
2. **Panic before console.lock acquisition** - kernel tries to output panic but deadlocks acquiring console.lock first
3. **Deadlock in console lock path** - login tries to open/read/write /dev/console and deadlocks in file system or console device driver

## Changes Implemented

### 1. Spinlock Acquire Timeout Watchdog
**File:** `kernel/core/spinlock.c`

Added iteration counter in `acquire()` loop:
- Tracks loop iterations while waiting for lock
- After 100M+ iterations, forces "SPIN: " + lock name to UART every 50M iterations
- Uses `uartputc()` directly (bypasses all locks)
- Ensures even infinite spinlock hangs produce diagnostic output

```c
// Spinlock timeout watchdog: if we've been spinning for way too long,
// force diagnostic output to UART even though interrupts are disabled.
if(iter > 100000000 && (iter - prev_iter) > 50000000) {
  prev_iter = iter;
  uartputc('S'); uartputc('P'); uartputc('I'); uartputc('N'); uartputc(':');
  // Output lock name...
}
```

### 2. Console Lock Acquisition Diagnostics
**File:** `kernel/driver/console.c`

Added pre-lock UART markers:
- **consoleread()**: outputs `[R]` before `acquire(&cons.lock)`
- **consolewrite()**: outputs `[W]` before `acquire(&cons.lock)`

These markers indicate:
- If you see `[R]` or `[W]`, console operations are being attempted
- If followed by `SPIN:`, deadlock is in console lock
- If no `[R]` or `[W]` at all, hang is before console device access

### 3. Kernel Boot Stage Markers
**Files:** `kernel/core/main.c`, `kernel/core/proc.c`

Added UART output at critical initialization points:
- **KLOCK** (in main.c after consoleinit): Console lock initialized successfully
- **INIT** (in proc.c at userinit): Kernel is creating init process

These markers help pinpoint where hang occurs relative to boot sequence.

### 4. Login Binary Startup Marker
**File:** `user/login.c`

Added diagnostic output at login binary entry:
- **LOGIN_START**: Confirms login binary has started execution
- Output goes to stderr (fd 2) so you see it even if stdin/stdout are redirected

If you don't see LOGIN_START, hang is before/during fork/exec of login, or in kernel path handling the binary load.

## Testing Procedure

### To reproduce the hang with diagnostics:

```bash
# 1. Build kernel with diagnostics
sudo make aux.kern

# 2. Build rootfs with updated login binary
sudo make ext2root

# 3. Boot QEMU with serial output to console
make qemu-nox-ext2root

# 4. At login prompt, try invalid username "oot"
# login: oot

# 5. Observe UART output for diagnostic markers:
#    - KLOCK: Console initialized
#    - INIT: Init process created
#    - [W] or [R]: Console device operation
#    - SPIN: Spinlock timeout detected (shows lock name)
#    - LOGIN_START: Login binary running
```

### Expected Output Sequence (normal boot):
1. Early boot messages (bootloader, kernel)
2. `KLOCK` - console lock initialized
3. `INIT` - init process created
4. Init messages (opening /dev/console, etc.)
5. `LOGIN_START` - login binary runs
6. `login: ` prompt appears

### What Diagnostics Tell You:

| Observed | Interpretation |
|----------|-----------------|
| No KLOCK | Hang before console lock initialization |
| KLOCK but no INIT | Hang in userinit or early process setup |
| INIT but no LOGIN_START | Hang in exec of login binary or VFS path |
| LOGIN_START but no `[W]` | Hang before login tries to write prompt |
| `[W]` then hang (stays in acquire loop) | DEADLOCK: login waiting for console lock |
| `SPIN: CONS` | Spinlock timeout on console lock - deadlock detected |

## Implementation Details

### Why UART-only forece output?
- Console subsystem uses spinlock so deadlock in console code blocks normal output
- UART is hardware serial port, direct chip access via `uartputc()`
- Can work even with interrupts disabled and all spinlocks held
- Not buffered or processed through console infrastructure

### Why iteration counter instead of time-based?
- Timer interrupt won't fire if interrupts are disabled (spinlock problem we're debugging)
- Iteration counter works even under full spinlock + interrupt disable
- Every 50M iterations at ~1GHz CPU = ~50ms between UART outputs (visible in serial log)

### How deadlock is detected:
Classic spinlock deadlock pattern:
```
CPU0: acquire(cons.lock) waits for CPU1 to release
CPU1: already has cons.lock, tries to acquire another lock held by CPU0
→ both CPUs spin forever, interrupts disabled, watchdog fires
```

## Next Steps After Testing

1. **If you see SPIN output**: Note which lock (e.g., "CONS") is spinning. This tells us which kernel subsystem has the deadlock.

2. **If you see [W]/[R] followed by hang**: Console operations are triggering the deadlock. Likely candidate: signal delivery trying to print something while console.lock is held.

3. **If you see no UART at all**: Hang is in CPU code before UART is even initialized (unlikely with KLOCK marker).

4. **If deadlock confirmed**: May need to:
   - Add timeout to acquire() that panics after timeout expires
   - Add deadlock detection for circular lock dependencies
   - Review signal delivery paths during I/O operations

## Files Modified
- `kernel/core/spinlock.c` - acquire() timeout watchdog
- `kernel/driver/console.c` - consoleread/consolewrite diagnostics
- `kernel/core/main.c` - boot stage markers
- `kernel/core/proc.c` - init process start marker
- `user/login.c` - login startup marker

## Build Instructions
```bash
cd /Users/bird/auxv6
sudo make clean
sudo make aux.kern
sudo make ext2root
make qemu-nox-ext2root   # Test with diagnostics
```

The kernel will not crash immediately even with spinlock hangs thanks to the watchdog output to UART—you'll see diagnostic progress output rather than silent hangs.
