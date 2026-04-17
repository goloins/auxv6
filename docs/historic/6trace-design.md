# 6trace — Syscall Tracer Design

**Status:** Design / pre-implementation  
**Goal:** `strace`-equivalent for auxv6 — launch a child under tracing and print every syscall with arguments and return value.  
**Out of scope (deferred):** single-step / instruction-level tracing (`PTRACE_SINGLESTEP` / x86 TF flag).

---

## Usage

```
/# 6trace /usr/bin/bash
/# 6trace /bin/ls /tmp
```

Expected output (strace-style):
```
execve("/usr/bin/bash", ["bash"], ...) = 0
open("/etc/passwd", O_RDONLY) = 3
read(3, "root:x:0:0:root\n...", 4096) = 64
close(3) = 0
...
+++ exited with 0 +++
```

---

## Kernel Changes

### 1. `include/syscall.h`

Add one new syscall number at the next available slot:

```c
#define SYS_ptrace  105
```

Update `NSYSCALLS` or the bounds check if one exists.

### 2. `include/proc.h` — `struct proc` additions

```c
/* ptrace support */
struct proc *ptrace_tracer;   /* non-NULL when this proc is being traced */
int          ptrace_stop_sig; /* signal that caused last ptrace stop (usually SIGTRAP) */
int          ptrace_flags;    /* PTRACE_F_* bitmask */
int          ptrace_in_syscall; /* 1 = stopped at syscall-enter, 0 = syscall-exit */
```

Flag bits (defined in a new `include/ptrace.h`):

```c
#define PTRACE_F_SYSCALL   0x01   /* stop on every syscall entry+exit */
#define PTRACE_F_EXECSTOP  0x02   /* one-shot: stop after first exec under trace */
```

### 3. `include/ptrace.h` — New header

Defines the public ABI shared between the kernel and `6trace`:

```c
#ifndef PTRACE_H
#define PTRACE_H

/* ptrace request codes */
#define PTRACE_TRACEME      0   /* child calls this before exec */
#define PTRACE_PEEKDATA     1   /* read word from tracee address space */
#define PTRACE_POKEDATA     2   /* write word to tracee address space */
#define PTRACE_GETREGS      3   /* copy tracee trapframe to tracer */
#define PTRACE_SETREGS      4   /* write tracer-supplied regs into tracee tf */
#define PTRACE_CONT         5   /* resume without syscall stops */
#define PTRACE_SYSCALL      6   /* resume, stop again at next syscall entry/exit */
#define PTRACE_DETACH       7   /* detach, resume tracee */
/* PTRACE_SINGLESTEP (8) — deferred; requires x86 TF eflags manipulation */

/* wait status encoding for ptrace stops (matches waitpid() convention) */
/* status = (SIGTRAP | (PTRACE_EVENT_xxx << 8)) when WIFSTOPPED */
#define PTRACE_EVENT_SYSCALL_ENTER  1
#define PTRACE_EVENT_SYSCALL_EXIT   2
#define PTRACE_EVENT_EXEC           3

/* register snapshot passed via PTRACE_GETREGS / PTRACE_SETREGS */
struct user_regs {
    uint edi, esi, ebp, esp_orig, ebx, edx, ecx, eax; /* pushal order */
    uint eip;
    uint eflags;
    uint esp;
    uint orig_eax;  /* syscall number at entry; -1 on exit stop */
};

#endif /* PTRACE_H */
```

### 4. `kernel/core/ptrace.c` — New file

Implements `sys_ptrace()`.  Roughly 200 lines.

```
sys_ptrace(request, pid, addr, data)

PTRACE_TRACEME:
  - Caller must be the child before its exec.
  - ptrace_tracer = myproc()->parent
  - ptrace_flags  = PTRACE_F_SYSCALL | PTRACE_F_EXECSTOP

PTRACE_PEEKDATA:
  - copyin(tracee->addrsp->pgdir, &word, addr, 4)
  - write word to *data in tracer's address space via copyout

PTRACE_POKEDATA:
  - copyout(tracee->addrsp->pgdir, addr, &data, 4)

PTRACE_GETREGS:
  - Copy tracee->tf fields into struct user_regs, copyout to tracer

PTRACE_SETREGS:
  - copyin struct user_regs from tracer, write into tracee->tf

PTRACE_CONT:
  - Clear PTRACE_F_SYSCALL from ptrace_flags
  - Set tracee->state = RUNNABLE, wakeup(tracee)

PTRACE_SYSCALL:
  - Ensure PTRACE_F_SYSCALL is set
  - Set tracee->state = RUNNABLE, wakeup(tracee)

PTRACE_DETACH:
  - tracee->ptrace_tracer = NULL
  - tracee->ptrace_flags  = 0
  - Set tracee->state = RUNNABLE, wakeup(tracee)
```

### 5. `kernel/core/syscall.c` — Hook syscall entry and exit

Inside `syscall()`, bracket the actual dispatch with ptrace stops:

```c
void syscall(void) {
    struct proc *p = myproc();
    int num = p->tf->eax;

    /* --- ptrace syscall-enter stop --- */
    if (p->ptrace_tracer && (p->ptrace_flags & PTRACE_F_SYSCALL)) {
        p->ptrace_in_syscall = 1;
        ptrace_stop(p, SIGTRAP | (PTRACE_EVENT_SYSCALL_ENTER << 8));
        /* tracer may have modified tf->eax (syscall number) via SETREGS */
        num = p->tf->eax;
    }

    /* dispatch */
    if (num > 0 && num < NELEM(syscalls) && syscalls[num])
        p->tf->eax = syscalls[num]();
    else
        p->tf->eax = -1;

    /* --- ptrace syscall-exit stop --- */
    if (p->ptrace_tracer && (p->ptrace_flags & PTRACE_F_SYSCALL)) {
        p->ptrace_in_syscall = 0;
        ptrace_stop(p, SIGTRAP | (PTRACE_EVENT_SYSCALL_EXIT << 8));
    }
}
```

`ptrace_stop(p, status)` is a small helper (in `ptrace.c`) that:
1. Encodes `status` into `p->wait_status`
2. Sets `p->state = STOPPED`
3. Sets `p->wait_event = WAIT_EVENT_STOPPED`
4. Calls `wakeup(p->ptrace_tracer)` so the tracer's `waitpid()` returns
5. Calls `sched()` — parks the tracee until the tracer issues PTRACE_CONT/SYSCALL

Lock discipline: acquire `ptable.lock` before touching state/wait_event, release before `sched()`, same pattern as the existing `sleep()`.

### 6. `kernel/core/exec.c` — exec SIGTRAP stop

After the new image is loaded, but before returning to user space, check the one-shot flag:

```c
if (myproc()->ptrace_flags & PTRACE_F_EXECSTOP) {
    myproc()->ptrace_flags &= ~PTRACE_F_EXECSTOP;
    ptrace_stop(myproc(), SIGTRAP | (PTRACE_EVENT_EXEC << 3));
}
```

This gives the tracer a chance to observe the process before its first instruction runs.

### 7. `kernel/core/proc.c` — `waitpid()` surfaces ptrace stops

The existing `wait_event` / `wait_status` path already handles `WAIT_EVENT_STOPPED`.
Verify that `waitpid(pid, &status, WUNTRACED)` returns when the tracee hits a `STOPPED` state (it should already, since job-control SIGSTOP uses the same path).

One addition: clear `wait_event` after the tracer consumes it, so repeated `waitpid` calls don't re-fire on the same stop.

### 8. `kernel/core/proc.c` — cleanup on tracee exit

In `exit()`, if `myproc()->ptrace_tracer != NULL`, null out the pointer to prevent a dangling reference. The tracer will observe `WIFEXITED` on its next `waitpid()`.

---

## User-Space: `user/6trace.c`

### Algorithm

```
main(argc, argv):
  pid = fork()
  if pid == 0:
    ptrace(PTRACE_TRACEME, 0, 0, 0)
    exec(argv[1], argv+1)
    die("exec failed")
  else:
    trace_loop(pid)

trace_loop(pid):
  syscall_entry = true          // alternates entry/exit
  loop:
    waitpid(pid, &status, WUNTRACED)
    if WIFEXITED(status):
      printf("+++ exited with %d +++\n", WEXITSTATUS(status))
      break
    if WIFSIGNALED(status):
      printf("+++ killed by signal %d +++\n", WTERMSIG(status))
      break
    if WIFSTOPPED(status):
      event = (status >> 8) & 0xff
      ptrace(PTRACE_GETREGS, pid, 0, &regs)
      if syscall_entry:
        print_syscall_entry(&regs)
        syscall_entry = false
      else:
        print_syscall_exit(&regs)
        syscall_entry = true
    ptrace(PTRACE_SYSCALL, pid, 0, 0)
```

### Syscall argument decoding

A static table maps syscall number → name and an argument-type list:

```c
struct syscall_info {
    const char *name;
    int nargs;
    const char argtypes[6];   // 'i'=int, 'p'=ptr, 's'=string, 'f'=fd, 'x'=hex
};

static const struct syscall_info sctab[] = {
    [SYS_fork]   = { "fork",   0, {} },
    [SYS_exit]   = { "exit",   1, {'i'} },
    [SYS_read]   = { "read",   3, {'f','p','i'} },
    [SYS_write]  = { "write",  3, {'f','p','i'} },
    [SYS_open]   = { "open",   2, {'s','x'} },
    [SYS_close]  = { "close",  1, {'f'} },
    [SYS_exec]   = { "exec",   2, {'s','p'} },
    /* ... all 104 syscalls ... */
};
```

String arguments are read from the tracee's address space using `PTRACE_PEEKDATA` (word at a time) and printed with length cap (e.g. 32 chars + `...` if longer).

### Flags / options (stretch goals)

| Flag | Meaning |
|------|---------|
| `-e trace=open,read,write` | Filter to specific syscalls |
| `-o file` | Write output to file |
| `-s N` | Max string length (default 32) |
| `-c` | Summary table of call counts at exit |

---

## File Inventory

| File | Change |
|------|--------|
| `include/syscall.h` | Add `SYS_ptrace 105` |
| `include/ptrace.h` | **New** — request codes, `struct user_regs`, event codes |
| `include/proc.h` | Add 4 fields to `struct proc` |
| `kernel/core/ptrace.c` | **New** — `sys_ptrace()` + `ptrace_stop()` helper |
| `kernel/core/syscall.c` | Hook entry/exit in `syscall()` |
| `kernel/core/exec.c` | One-shot exec-stop for `PTRACE_F_EXECSTOP` |
| `kernel/core/proc.c` | Cleanup on exit; verify `waitpid` surfaces STOPPED |
| `libc/usys.S` | Add `SYSCALL(ptrace)` stub |
| `libc/posix.c` | Add `ptrace()` wrapper (or expose raw stub) |
| `user/6trace.c` | **New** — tracer tool |
| `Makefile` | Add `6trace` to user targets + `_6trace` build target |

---

## Implementation Order (recommended)

1. `include/ptrace.h` + `include/syscall.h` + `include/proc.h` fields
2. `kernel/core/ptrace.c` skeleton — `ptrace_stop()` + `sys_ptrace()` with just `PTRACE_TRACEME` and `PTRACE_CONT`
3. Hook in `syscall()` (entry stop only first — skip exit stop until entry is working)
4. `exec.c` exec-stop hook
5. `libc/usys.S` + libc stub
6. `user/6trace.c` minimal version (entry only, no arg decoding, just syscall name)
7. Add syscall-exit stop + return value printing
8. Build out the full syscall argument decoding table
9. `PTRACE_PEEKDATA` and string reading
10. Optional: filter flags, `-c` summary, `-o` output

---

## Deferred / Out of Scope

- **`PTRACE_SINGLESTEP`** — instruction-level tracing via x86 `eflags.TF`. Requires setting the TF bit in the tracee's `tf->eflags` before `iret` and catching the resulting `#DB` (debug exception, vector 1) in `trap.c`. Non-trivial because it fires once per instruction and needs an IDT entry for vector 1. Deferred indefinitely.
- **`PTRACE_ATTACH`** to a running process — requires sending SIGSTOP and waiting for the target to enter STOPPED from outside its own syscall path. Doable but adds complexity to signal delivery; deferred until core tracing is stable.
- **`/proc/PID/syscall`** virtual file — expose current tracee state via procfs if procfs is ever added.
