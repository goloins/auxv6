# Tranche 1: time_t Widening Fallout Audit (2026-04)

**Date**: 2026-04-12  
**Scope**: Full audit of time_t usage in auxv6 codebase to identify all structs, syscalls, and functions that must be modified when widening `time_t` from 32-bit int to 64-bit long.

**Current Baseline**: `typedef int time_t;` (include/sys/types.h:40)  
**Target**: `typedef long time_t;` (64-bit signed, RFC 5280 compliant)

---

## 1. Public Type Definitions That Embed time_t

### 1.1 struct timespec (include/sys/time.h:19-22)

**Current**:
```c
struct timespec {
    time_t tv_sec;    /* Currently int (32-bit) */
    long   tv_nsec;
};
```

**After Widening**:
```c
struct timespec {
    time_t tv_sec;    /* Will be long (64-bit) — layout CHANGES */
    long   tv_nsec;
};
```

**Impact**: 
- Growth: 4 bytes → 8 bytes (on i386, padding may apply)
- Affected syscalls: clock_gettime(), clock_settime(), nanosleep(), clock_nanosleep()
- Affected libc: timespec_to_msec(), timespec_diff_msec()
- ABI breakage: All callers must recompile; kernel syscall interface changes

### 1.2 struct timeval (include/sys/time.h:10-12)

**Current**:
```c
struct timeval {
    time_t      tv_sec;    /* Currently int (32-bit) */
    suseconds_t tv_usec;
};
```

**After Widening**:
```c
struct timeval {
    time_t      tv_sec;    /* Will be long (64-bit) — layout CHANGES */
    suseconds_t tv_usec;
};
```

**Impact**:
- Growth: 8 bytes → 12 bytes (on i386; padding aligns)
- Affected syscalls: gettimeofday(), settimeofday()
- Affected libc: User code comparing struct timeval sizes
- ABI breakage: Full recompile required

### 1.3 struct stat (include/stat.h:28-48)

**Current**:
```c
struct stat {
    /* ... */
    int st_atime;   /* Seconds since epoch (32-bit int) */
    int st_mtime;   /* Seconds since epoch (32-bit int) */
    int st_ctime;   /* Seconds since epoch (32-bit int) */
};
```

**Status**: Currently uses plain `int`, NOT `time_t`.  
**Target**: Should use `time_t` for RFC 5280 correctness (file dates must be tested up to year 9999):

```c
struct stat {
    /* ... */
    time_t st_atime;   /* Will be long (64-bit) — MUST CHANGE */
    time_t st_mtime;   /* Will be long (64-bit) — MUST CHANGE */
    time_t st_ctime;   /* Will be long (64-bit) — MUST CHANGE */
};
```

**Impact**:
- Growth: 3 × 4 bytes → 3 × 8 bytes (24 bytes more)
- Affected: All stat() calls and struct stat users
- Affected syscalls: stat(), fstat(), lstat()
- Affected libc: get function - file modification time comparisons
- ABI breakage: Critical (file metadata interface change)

### 1.4 timezone struct (include/sys/time.h:14-16) — No Change

```c
struct timezone {
    int tz_minuteswest;  /* No time_t here; no change */
    int tz_dsttime;
};
```

**Status**: Uses int; not affected. (Struct timezone is legacy POSIX and should be deprecated anyway.)

---

## 2. Public Function Signatures That Use time_t

### 2.1 Time Query and Manipulation (include/time.h)

```c
/* Prototype signature, return type affected */
time_t      time(time_t *tloc);

/* Prototype signature, parameter types affected */
double      difftime(time_t time1, time_t time0);

struct tm  *gmtime(const time_t *timer);
struct tm  *gmtime_r(const time_t *timer, struct tm *result);
struct tm  *localtime(const time_t *timer);
struct tm  *localtime_r(const time_t *timer, struct tm *result);
time_t      mktime(struct tm *tm);

char       *ctime(const time_t *timer);
char       *ctime_r(const time_t *timer, char *buf);
```

**Implementation**: All implemented in user/timecore.c  
**Impact Class**: Source-compatible (function signatures in headers do NOT change); binary-incompatible (callers must recompile because parameter passing width changes)

### 2.2 Clock Syscalls (include/time.h)

```c
int         clock_gettime(clockid_t clock_id, struct timespec *tp);
int         clock_getres(clockid_t clock_id, struct timespec *res);
int         clock_settime(clockid_t clock_id, const struct timespec *tp);
int         clock_nanosleep(clockid_t clock_id, int flags,
                            const struct timespec *rqtp,
                            struct timespec *rmtp);
int         nanosleep(const struct timespec *rqtp, struct timespec *rmtp);
```

**Status**: These functions do NOT change signatures, but struct timespec members DO change widths  
**Fallout**: Kernel syscall ABI changes (timespec layout in registers/memory changes)

### 2.3 gettimeofday (include/sys/time.h) — Current

```c
int gettimeofday(struct timeval *tv, struct timezone *tz);
```

**Status**: Signature unchanged, but struct timeval layout CHANGES  
**Fallout**: Kernel syscall ABI changes

---

## 3. Kernel Syscall Interface Changes

### Clock/Time Syscalls Requiring Kernel-Side Updates

#### sys_clock_gettime (kernel/syscall.c)

**Current Behavior** (inferred from user/timecore.c:531-545):
- Reads kernel monotonic time, fills struct timespec with 32-bit seconds
- **Bug**: Truncates 64-bit kernel time to 32-bit

**After Tranche 1**:
- Must write full 64-bit seconds to struct timespec (requires recompiling kernel and userspace)

#### sys_clock_settime (kernel/syscall.c)

**Current Behavior** (expected):
- Reads struct timespec from userspace
- Sets kernel's CLOCK_REALTIME

**After Tranche 1**:
- Must validate seconds >= 0 && <= 253402300800 (Y9999-12-31 23:59:59 UTC)
- Must read 64-bit seconds from struct timespec
- **Kernel change**: Validation logic differs (current 32-bit range check → 64-bit range check)

#### sys_gettimeofday (kernel/syscall.c or user/timecore.c)

**Current Behavior**:
- Returns current monotonic time in struct timeval
- **Bug**: Truncates to 32-bit

**After Tranche 1**:
- Must write full 64-bit seconds to struct timeval

#### sys_time (user/timecore.c:741-750)

**Current Implementation**:
```c
time_t
time(time_t *tloc)
{
    struct timeval tv;
    if(gettimeofday(&tv, 0) < 0)
        return (time_t)-1;
    if(tloc != 0)
        *tloc = tv.tv_sec;
    return tv.tv_sec;  /* Currently int (32-bit) */
}
```

**After Tranche 1**:
- Return type widens to 64-bit
- Callers must handle 64-bit return (mostly source-compatible, but recompile required)

---

## 4. Libc Conversion Functions Requiring Validation Updates

### 4.1 time_epoch_to_tm() (user/timecore.c:131-185)

**Current Code** (lines 131-185):
```c
static int
time_epoch_to_tm(time_t timer, struct tm *result)
{
    long long days = timer / 86400;    /* Input param is time_t (int) */
    /* ... rest of conversion ... */
}
```

**After Tranche 1**:
- Input parameter `time_t timer` becomes 64-bit
- Internal arithmetic (long long) already handles 64-bit; NO CODE CHANGE required
- **Benefit**: Now correctly handles dates beyond 2038

### 4.2 time_tm_to_epoch() (user/timecore.c:190-227)

**Current Code** (lines 217-221):
```c
if(seconds < -2147483648LL || seconds > 2147483647LL) {
    errno = EOVERFLOW;
    return -1;
}
*out = (time_t)seconds;  /* Casts long long to int */
```

**Critical Issue**: Overflow check hardcoded to 32-bit INT_MIN/INT_MAX  
**After Tranche 1**:
```c
if(seconds < -9223372036854775807LL || seconds > 9223372036854775807LL) {
    errno = EOVERFLOW;
    return -1;
}
*out = (time_t)seconds;  /* Now casts to 64-bit long */
```

**Action Required**: 
- [ ] Update overflow bounds to 64-bit range
- [ ] Consider bounds check using RFC 5280 limits: `seconds >= 0 && seconds <= 253402300800`

### 4.3 time_timespec_valid() (user/timecore.c:232-240)

**Current Code**:
```c
static int
time_timespec_valid(const struct timespec *ts)
{
    if(ts->tv_sec < 0) return 0;  /* Validates tv_sec >= 0 */
    /* ... rest of validation ... */
}
```

**After Tranche 1**:
- No code change needed; validation logic still applies to 64-bit tv_sec

### 4.4 gmtime_r, localtime_r, mktime (user/timecore.c:760-806)

**Status**: All use time_epoch_to_tm() and time_tm_to_epoch() internally  
**Fallout**: Automatic (inherits fixes from conversion functions)  
**Test Impact**: New test cases needed for post-2038 dates

---

## 5. Key Code Points That Cast int ↔ time_t

### 5.1 Kernel Time Source → struct timespec (kernel/core/ktime.c:198, 232)

**Line 198** (clock_gettime):
```c
ts->tv_sec = (time_t)seconds;  /* seconds is uint64_t or uint; cast to time_t (currently int) */
```

**Line 232** (clock_monotonic):
```c
ts->tv_sec = (time_t)seconds;  /* Similar cast */
```

**After Tranche 1**:
- Casts remain; no code change (64-bit cast of already-wide value)
- Kernel side: Must ensure seconds is truly 64-bit safe

### 5.2 NTP Response → struct timespec (user/ntpd.c:201)

**Line 201**:
```c
ts.tv_sec = (time_t)(tx_sec - NTP_UNIX_EPOCH_DELTA);
/* tx_sec is uint32_t from NTP packet; Delta is 2208988800UL (1900-1970) */
```

**Current Issue**: tx_sec is 32-bit; adding it to delta or casting to 32-bit time_t truncates  
**After Tranche 1**:
```c
ts.tv_sec = (time_t)(tx_sec - NTP_UNIX_EPOCH_DELTA);  /* Now 64-bit result */
```

**Still Safe**: NTP timestamp (seconds since 1900) up to ~2036; well within 64-bit range  
**Benefit**: Correctly handles NTP time values without truncation

### 5.3 Conversion Function Intermediate Casts (user/timecore.c)

**Line 96**: time_rtc_to_epoch(const struct rtcdate *r, time_t *out)  
**Line 190**: time_tm_to_epoch(const struct tm *tm, time_t *out)  

**Pattern**: Output parameter `time_t *out` receives 64-bit value  
**After Tranche 1**: No code change (already outputs via pointer)

### 5.4 User-Space Casting (user/date.c:115-171)

**Lines 115-116**:
```c
time_t epoch;
time_t local_epoch;
```

**Line 171**:
```c
local_epoch = epoch + (time_t)offset_sec;
```

**After Tranche 1**: Automatic; time_t variables now 64-bit

---

## 6. Struct stat Modernization (Requires New Header)

### 6.1 Current struct stat (include/stat.h:28-48)

**Problem**: Uses plain int for timestamps (not time_t)

**Solution**: Define modern struct stat with time_t fields

### 6.2 Proposed New include/sys/stat.h

Create canonical POSIX stat structure:

```c
/* include/sys/stat.h (new) */
#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include "sys/types.h"

struct stat {
    dev_t    st_dev;      /* Device ID */
    ino_t    st_ino;      /* Inode number */
    mode_t   st_mode;     /* Mode and permissions */
    nlink_t  st_nlink;    /* Number of hard links */
    uid_t    st_uid;      /* User ID of owner */
    gid_t    st_gid;      /* Group ID of owner */
    dev_t    st_rdev;     /* Device ID (for devices) */
    off_t    st_size;     /* File size in bytes */
    time_t   st_atime;    /* Last access time (64-bit) */
    time_t   st_mtime;    /* Last modification time (64-bit) */
    time_t   st_ctime;    /* Last change time (64-bit) */
    blksize_t st_blksize; /* Block size for I/O */
    blkcnt_t  st_blocks;  /* Number of blocks allocated */
};

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
/* ... mode constants ... */

#endif /* _SYS_STAT_H */
```

**Compatibility Strategy**:
- Update include/stat.h (internal) to use time_t for st_atime, st_mtime, st_ctime
- Create include/sys/stat.h (public) with same struct
- Update kernel stat() syscall to fill 64-bit times

---

## 7. Summary of Required Changes by File

### 7.1 Header Files (No Source Changes, But ABI Breaks)

| File | Change | Reason |
|------|--------|--------|
| include/sys/types.h (line 40) | `typedef int time_t;` → `typedef long time_t;` | Widen from 32-bit to 64-bit |
| include/sys/time.h (lines 11, 21) | struct timeval, struct timespec: time_t fields | Inherit widening |
| include/stat.h (lines 46-48) | `int st_atime/st_mtime/st_ctime` → `time_t` | Enable RFC 5280 support |
| include/sys/stat.h (new) | Create canonical POSIX struct stat with time_t | Public interface |

### 7.2 Kernel Files (Logic Changes Required)

| File | Function | Change |
|------|----------|--------|
| kernel/core/ktime.c (line 198) | clock_gettime logic | Ensure 64-bit seconds written to timespec |
| kernel/core/ktime.c (line 232) | CLOCK_MONOTONIC logic | Ensure 64-bit seconds written to timespec |
| kernel/syscall.c | sys_clock_settime | Add 64-bit range validation (> Y1900, < Y9999) |
| kernel/syscall.c | sys_stat / sys_fstat | Fill stat struct with 64-bit time values |

### 7.3 Libc Files (Logic Updates Required)

| File | Function | Change | Severity |
|------|----------|--------|----------|
| user/timecore.c (lines 217-221) | time_tm_to_epoch | Update overflow bounds from 2^31 range to 2^63 range | HIGH |
| user/timecore.c (line 96) | time_rtc_to_epoch | No change needed (already handles wide values) | LOW |
| user/timecore.c (lines 760-806) | gmtime, localtime, mktime | Inherit from conversion functions | LOW |
| user/ntpd.c (line 201) | NTP time conversion | No code change (cast now produces 64-bit) | LOW |
| user/date.c (lines 115-171) | date formatting | No code change (arithmetic on 64-bit values) | LOW |

---

## 8. Testing Requirements

### 8.1 Pre-Year-2038 Test Cases (Regression)

- [ ] Current time query and conversion (within 32-bit range)
- [ ] File stat() on recent files (modification times in 2020-2026 range)
- [ ] clock_gettime/settime for near-current epoch
- [ ] gmtime/localtime for dates in 1970-2037 range
- [ ] mktime roundtrip (tm → epoch → tm)

### 8.2 Post-Year-2038 Test Cases (New Requirement)

- [ ] time() query in far future (simulated via clock_settime)
- [ ] gmtime/localtime for dates beyond 2038 (e.g., Y2050, Y2100, Y9999)
- [ ] mktime for dates beyond 2038
- [ ] struct timespec with tv_sec > 32-bit range
- [ ] File stat() with synthetic mtime beyond 2038
- [ ] Calendar boundary at 2038-01-19 00:00:00 (transition from valid 32-bit to overflow)

### 8.3 RFC 5280 Compliance Test

- [ ] Load certificate with notAfter = Y9999-12-31
- [ ] Verify time_t can represent certificate expiry without overflow
- [ ] Test date_cmp() or similar validation with wide time values

---

## 9. Kernel Validation Range (RFC 5280)

**Valid time_t range after widening**:
```
Minimum: 0 (1970-01-01 00:00:00 UTC)
Maximum: 253402300799 (9999-12-31 23:59:59 UTC)
```

**Kernel must enforce**:
- `clock_settime()`: Only accept times in [0, 253402300799]
- File timestamp writes: Clamp or reject times outside range
- NTP sync: Warn/reject NTP responses that produce out-of-range times

---

## 10. Dependencies and Sequencing

**Before Tranche 1 Implementation Can Begin**:
- [x] Tranche 0 (POSIX Contract) frozen
- [x] This audit document identifies all affected code points

**Tranche 1 Implementation Sequence**:
1. Update include/sys/types.h (time_t typedef)
2. Update struct definitions (timespec, timeval, stat)
3. Create include/sys/stat.h (new canonical header)
4. Update user/timecore.c overflow bounds
5. Update kernel time syscalls for 64-bit handling
6. Rebuild kernel + userspace
7. Run regression + new post-2038 tests
8. Proceed to Tranche 2 (RNG)

---

## 11. Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|-----------|
| **ABI Breakage** | All binaries must recompile | Forced full rebuild; expected |
| **Kernel Syscall Interface Change** | Timespec/timeval offsets change | Tested before release |
| **Overflow Validation Gaps** | Out-of-range times not rejected | Update bounds in _tm_to_epoch, clock_settime |
| **Truncation Bugs** | 64-bit → 32-bit cast breaks post-2038 dates | Remove int casts; use time_t consistently |
| **File Metadata Loss** | Old struct stat size differs | Plan migration path (new stat64-like call) |

---

## 12. Completion Checklist

- [ ] All public headers using time_t identified (§1)
- [ ] All function signatures with time_t parameters identified (§2)
- [ ] All kernel syscall interface changes planned (§3)
- [ ] All conversion functions audited (§4)
- [ ] All type casts inventoried (§5)
- [ ] struct stat modernization plan approved (§6)
- [ ] File-by-file change list populated (§7)
- [ ] Test cases drafted (§8)
- [ ] Validation ranges calculated (§9)
- [ ] Sequencing approved (§10)
- [ ] Risk mitigation strategies in place (§11)

**This audit document is FINALIZED and ready for implementation phase.**

