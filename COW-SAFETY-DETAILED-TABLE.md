# COW Safety Issues - Detailed Reference Table

## Quick Reference Index

| Issue ID | Severity | File | Line(s) | Category | Description | Exploit |
|----------|----------|------|---------|----------|-------------|---------|
| COW-001 | CRITICAL | kernel/core/syscall.c | 35-85 | String Fetch | `fetchstr()` direct user VA scan | Fork + COW protect string → kernel trap 14 |
| COW-002 | CRITICAL | kernel/core/sysfile.c | 2133-2146 | Exec Debug | Direct `argv[i]` dereference in debug block | Fork + COW argv page → panic on cprintf |
| COW-003 | CRITICAL | kernel/audio/audio_core.c | 950-961 | Ioctl Nested | Audio enum writes through user-embedded pointer | User modifies ioctl.entries ptr between copyin and write |
| COW-004 | HIGH | kernel/driver/console.c | 3433,3451,3500,3506,3513,3526 | TTY Ioctl | Console tty_ioctl casts `arg` and derefs | Malformed winsize ptr → kernel fault |
| COW-005 | HIGH | kernel/driver/serial.c | 653,676,685,694,704,715,721,730,765,785,815,824 | TTY Ioctl | Serial tty_ioctl casts `arg` and derefs | Malformed serial struct ptr → kernel fault |
| COW-006 | HIGH | kernel/driver/pty.c | 203,219,225,232,238,247,253,260,266,275,277,283,290,292,299,306,308 | TTY Ioctl | PTY tty_ioctl casts `arg` and derefs | Malformed pty struct ptr → kernel fault |
| COW-007 | HIGH | kernel/driver/virtio_gpu.c | TBD | Device Ioctl | VirtIO GPU raw pointer cast in ioctl | User-controlled GPU cmd ptr write |
| COW-008 | HIGH | kernel/fs/vfs_btrfs.c | TBD | Device Ioctl | Btrfs ioctl raw pointer cast | Btrfs-specific pointer dereference |
| COW-009 | HIGH | kernel/fs/vfs_exfat.c | TBD | Device Ioctl | ExFAT ioctl raw pointer cast | ExFAT-specific pointer dereference |
| COW-010 | HIGH | kernel/fs/vfs_msdosfs.c | TBD | Device Ioctl | MS-DOS FS ioctl raw pointer cast | MS-DOS FS pointer dereference |
| COW-011 | MEDIUM | kernel/net/socket.c | 1914,1929,1964,1978,1981,1992,1995 | Socket Option | Setsockopt/getsockopt nested pointer (TOCTOU variant) | User modify optval page during copyout + COW |
| COW-012 | MEDIUM | kernel/core/sysfile.c | 667-700 | Filesystem Dirent | inode_dir_read with conditional user buffer | Race on directory entry under concurrent access |
| COW-013 | MEDIUM | kernel/core/sysfile.c | 2288-2338 | Poll/Select | Poll fd_set staging (currently correct but TOCTOU) | User modify fds page between copyin/copyout + COW |
| COW-014 | LOW | kernel/core/syscall.c | 62-70 | Legacy Interface | `argptr()` returns raw user pointer (no callers found) | Dead code; if resurrected, enables direct dereference |
| COW-015 | LOW | kernel/fs/fs.c | 690-710 | Device Read | Device read/write (CURRENTLY SAFE - uses copyout) | Listed for completeness; correctly implements COW-safe pattern |

---

## Critical Issues - Detailed Breakdown

### COW-001: `fetchstr()` Direct User String Dereference

**Location:** [kernel/core/syscall.c](kernel/core/syscall.c#L33-L45)

**Unsafe Code:**
```c
int
fetchstr(uint addr, char **pp)
{
  char *s, *ep;
  struct proc *curproc = myproc();

  if(addr >= curproc->sz)
    return -1;
  *pp = (char*)addr;  // Returns raw user pointer
  ep = (char*)curproc->sz;
  for(s = *pp; s < ep; s++){  // ← Scans user memory without copyin
    if(*s == 0)              // ← DIRECT USER VA DEREFERENCE
      return s - *pp;
  }
  return -1;
}
```

**Why It's Unsafe:**
- Kernel reads *every byte* of user string directly
- No bounds checking per byte (only initial addr check)
- Under fork+COW, user's string page may be read-only
- Page fault trap 14 in kernel context → panic

**Propagation:**
- Called from `argstr()`, which is called by 17+ syscalls
- Each syscall inherits the vulnerability

**All Affected Syscalls:**
1. Line 1065: `sys_chdir()` → path
2. Line 1102: `sys_open()` → path
3. Line 1179: `sys_link()` → old, new
4. Line 1354: `sys_unlink()` → path
5. Line 1364: `sys_chown()` → path
6. Line 1473: `sys_mkdir()` → path
7. Line 1617: `sys_mount()` → path, fstype, data
8. Line 1635: `sys_unmount()` → path
9. Line 1657: `sys_symlink()` → old, new
10. Line 1713: `sys_readlink()` → path
11. Line 1752: `sys_chmod()` → path
12. Line 2114: `sys_mkfifo()` → path
13. Line 2536: `sys_symlinkat()` → path
14. Line 2836, 2879, 2950, 2991: Exec-related paths

**Trigger Scenario:**
```
1. Process A forks Process B
2. Page tables marked COW; user strings now read-only
3. syscall() → fetchstr() → for loop dereference
4. Access to read-only COW page triggers page fault
5. Fault handler sees kernel mode (cs & 3 == DPL_KERN)
6. Kernel panic: trap 14, no user fault handling
```

**Fix:**
```c
static int
copyinstr_user(uint uaddr, char *dst, int dstsz)
{
  struct proc *p;
  int i;
  char c;

  if(dst == 0 || dstsz <= 0)
    return -1;

  p = myproc();
  if(p == 0 || p->pgdir == 0)
    return -1;

  for(i = 0; i < dstsz; i++){
    if(copyin(p->pgdir, &c, uaddr + (uint)i, 1) < 0)  // ← Byte-wise copyin
      return -1;
    dst[i] = c;
    if(c == 0)
      return 0;
  }

  dst[dstsz - 1] = 0;
  return -1;
}
```

---

### COW-002: `sys_exec()` Debug Block argv Dereference

**Location:** [kernel/core/sysfile.c](kernel/core/sysfile.c#L2127-L2146)

**Unsafe Code:**
```c
// Line 2127: fetchstr returns raw pointer to user string
fetchstr(uarg, &argv[i]);

// ... later ...

// Debug block dereferences raw user pointer
if(DEBUG_EXEC) {
    cprintf("exec: argc = %d\n", i);
    if(i > 0)
      // Lines 2135-2136: Direct dereference of argv[1]
      cprintf("exec: argv[1] = %s\n", argv[1]);  // ← USER VA DEREF
}
```

**Why It's Unsafe:**
- `argv[1]` is a user VA (from fetchstr)
- cprintf attempts to format string from user memory
- Under COW, triggers kernel page fault

**Severity:** CRITICAL
- Even though DEBUG_EXEC is usually 0, the code pattern is dangerous
- Conditional compilation doesn't remove vulnerability

**Fix:**
- Wrap debug output in safe copyinstr:
```c
if(DEBUG_EXEC && i > 0) {
    char debug_buf[256];
    if(copyinstr_user((uint)argv[1], debug_buf, sizeof(debug_buf)) == 0) {
        cprintf("exec: argv[1] = %s\n", debug_buf);
    }
}
```

---

### COW-003: Audio Ioctl Nested Pointer Write

**Location:** [kernel/audio/audio_core.c](kernel/audio/audio_core.c#L950-L961)

**Unsafe Code:**
```c
case AUDIO_IOC_ENUM_DEVICES: {
    struct audio_device_enum *ade = (struct audio_device_enum *)arg;
    // ade was staged into kernel buffer via copyin (safe so far)
    // BUT: ade->entries is a user-supplied pointer
    struct audio_device_info *entries_ptr = ade->entries;  // User VA
    
    // Kernel now writes directly through it:
    for(i = 0; i < count && i < MAX_DEVICE_ENUM; i++){
        entries_ptr[i] = kdevs[i];  // ← Direct write to user VA
    }
}
```

**Why It's Unsafe - TOCTOU Pattern:**
1. User calls ioctl(fd, AUDIO_IOC_ENUM_DEVICES, &ade)
2. Kernel copies ade into kernel buffer (safe)
3. Kernel extracts `ade->entries` pointer (still valid at this moment)
4. **Timing window:** User thread could:
   - Remap underlying page read-only
   - Trigger COW protection
   - User VA now has different backing page
5. Kernel writes to new page expecting user buffer
6. PageFault: kernel mode write to COW page

**Exploit Scenario:**
```c
// Malicious user code:
struct audio_device_enum ade;
ade.entries = (struct audio_device_info*)mmap_area;

// Fork to COW-protect mmap_area
if(fork() == 0) {
    // Child: page is COW-protected (read-only)
    // Parent calls ioctl in other thread
    // Kernel tries to write → BOOM
}
```

**Fix:**
```c
case AUDIO_IOC_ENUM_DEVICES: {
    struct audio_device_enum *ade = (struct audio_device_enum *)arg;
    // Stage entries array into kernel buffer
    struct audio_device_info kdevices[MAX_DEVICE_ENUM];
    int i;
    
    for(i = 0; i < count && i < MAX_DEVICE_ENUM; i++){
        kdevices[i] = kdevs[i];
    }
    
    // Now use copyout to write to user buffer
    if(copyout(p->pgdir, (uint)ade->entries, kdevices, 
               count * sizeof(struct audio_device_info)) < 0) {
        return -1;
    }
}
```

---

## High-Severity Issues - TTY Ioctl Backend Pattern

### COW-004/005/006: Console/Serial/PTY tty_ioctl Raw Pointer Dereference

**Pattern (affects 3 files with 35+ instances):**

**Console Example:** [kernel/driver/console.c](kernel/driver/console.c#L3433)
```c
int console_ioctl(int iocmd, uint arg) {
    struct winsize *ws;
    
    switch(iocmd) {
        case TIOCGWINSZ:
            ws = (struct winsize *)arg;  // arg is user VA, cast directly
            ws->ws_row = 24;             // ← Direct write to user VA
            ws->ws_col = 80;
            break;
    }
}
```

**Why It's Unsafe:**
- Backend ioctl handler receives raw user pointer in `arg`
- No bounds check, no copyin/copyout
- Direct struct member assignment writes to user VA
- Under COW, page fault in kernel mode

**Files Affected:**
- [kernel/driver/console.c](kernel/driver/console.c): 6 instances (lines 3433, 3451, 3500, 3506, 3513, 3526)
- [kernel/driver/serial.c](kernel/driver/serial.c): 12 instances (lines 653, 676, 685, 694, 704, 715, 721, 730, 765, 785, 815, 824)
- [kernel/driver/pty.c](kernel/driver/pty.c): 17 instances (lines 203, 219, 225, 232, 238, 247, 253, 260, 266, 275, 277, 283, 290, 292, 299, 306, 308)

**Total:** 35 instances

**Fix Architecture:**
```c
// In sys_ioctl dispatcher (kernel/core/sysfile.c):
int sys_ioctl(void) {
    int fd, iocmd;
    int arg_raw;
    uint arg_u;
    struct proc *p;
    char kbuf[256];  // Kernel staging buffer
    
    if(argint(0, &fd) < 0 || argint(1, &iocmd) < 0 || argint(2, &arg_raw) < 0)
        return -1;
    
    p = myproc();
    if(p == 0 || p->pgdir == 0)
        return -1;
    arg_u = (uint)arg_raw;
    
    // Stage pointer payloads based on iocmd
    if(iocmd == TIOCGWINSZ || iocmd == TIOCSWINSZ) {
        // Copyin before passing to backend
        if(copyin(p->pgdir, kbuf, arg_u, sizeof(struct winsize)) < 0)
            return -1;
        // Now pass kernel address to backend
        int result = devsw[major].ioctl(dev, iocmd, (uint)kbuf);
        // Copyout results
        if(result == 0 && iocmd == TIOCGWINSZ) {
            if(copyout(p->pgdir, arg_u, kbuf, sizeof(struct winsize)) < 0)
                return -1;
        }
        return result;
    }
    // ... handle other ioctls
}
```

---

## Medium-Severity Issues - TOCTOU Patterns

### COW-011: Socket Option Nested Pointer (TOCTOU)

**Location:** [kernel/net/socket.c](kernel/net/socket.c#L1914-L1995)

**Code Pattern:**
```c
// getsockopt syscall
if(copyin(p->pgdir, &v, optval_u, sizeof(int)) < 0)  // Read option value from user
    return -1;

// Process option value...

// Write result back
if(copyout(p->pgdir, optval_u, &outv, sizeof(int)) < 0)  // Write to same user address
    return -1;
```

**TOCTOU Window:**
1. First `copyin` reads option value at time T1
2. Kernel processes under locks
3. Second `copyout` writes result at time T2
4. Between T1 and T2, user could:
   - Remap page COW-protected (e.g., via madvise, fork)
   - Underlying physical page changes
   - `copyout` at T2 writes to different page than intended

**Severity:** MEDIUM (requires specific race scenario)

**Note:** Current socket.c code uses copyout correctly; this is listed as a TOCTOU variant to document the general pattern risk.

---

## Summary Table: By File

| File | Critical | High | Medium | Low | Total |
|------|----------|------|--------|-----|-------|
| kernel/core/syscall.c | 1 | 0 | 0 | 2 | 3 |
| kernel/core/sysfile.c | 2 | 0 | 2 | 0 | 4 |
| kernel/audio/audio_core.c | 1 | 0 | 0 | 0 | 1 |
| kernel/driver/console.c | 0 | 6 | 0 | 0 | 6 |
| kernel/driver/serial.c | 0 | 12 | 0 | 0 | 12 |
| kernel/driver/pty.c | 0 | 17 | 0 | 0 | 17 |
| kernel/driver/virtio_gpu.c | 0 | 1 | 0 | 0 | 1 |
| kernel/fs/vfs_btrfs.c | 0 | 1 | 0 | 0 | 1 |
| kernel/fs/vfs_exfat.c | 0 | 1 | 0 | 0 | 1 |
| kernel/fs/vfs_msdosfs.c | 0 | 1 | 0 | 0 | 1 |
| kernel/net/socket.c | 0 | 0 | 1 | 0 | 1 |
| **TOTAL** | **4** | **39** | **3** | **2** | **48** |

*Note: Numbers reflect distinct issue instances; some files have multiple instances of the same pattern.*

