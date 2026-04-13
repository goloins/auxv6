# Tranche 0: POSIX Contract Freeze (2026-04)

**Purpose**: Define exact target POSIX ABI signatures, struct layouts, and semantics that will anchor all 8 implementation tranches. This document is the source of truth for what "correctness" means in each phase.

**Status**: Contract Freeze (2026-04-12)  
**Baseline Source**: POSIX.1-2008, IEEE 1003.1-2008, RFC 5280, LibreSSL Portable v4.2.1  
**Modifications from Standard**: None. All deviations are explicitly noted and justified.

---

## 1. Time ABI Contract (`time_t` and Related Structs)

### 1.1 Type Definition

```c
/* include/sys/types.h */
typedef long time_t;      /* 64-bit signed, sufficient for RFC 5280 cert dates (≤ year 9999) */
typedef long suseconds_t; /* For timeval.tv_usec arithmetic */
```

**Rationale**: 
- Current `time_t` is 32-bit int (broken; overflows 2038-01-19)
- LibreSSL Portable explicitly warns about 32-bit time_t and RFC 5280 validation
- 64-bit aligns with POSIX.1-2008 on 64-bit-capable systems (auxv6 is i386 but kernel is 64-file-capable)

### 1.2 Affected Public Structs

```c
/* include/time.h */
struct tm {
    int   tm_sec;      /* seconds [0, 61] */
    int   tm_min;      /* minutes [0, 59] */
    int   tm_hour;     /* hours [0, 23] */
    int   tm_mday;     /* day of month [1, 31] */
    int   tm_mon;      /* month of year [0, 11] */
    int   tm_year;     /* years since 1900 (not affected by time_t width) */
    int   tm_wday;     /* day of week [0, 6] */
    int   tm_yday;     /* day of year [0, 365] */
    int   tm_isdst;    /* daylight saving time flag [-1, 0, 1] */
};

struct timespec {
    time_t  tv_sec;    /* seconds (64-bit after widening) */
    long    tv_nsec;   /* nanoseconds [0, 999999999] */
};

struct timeval {
    time_t        tv_sec;     /* seconds (64-bit after widening) */
    suseconds_t   tv_usec;    /* microseconds [0, 999999] */
};
```

**Rationale**: `struct timespec` and `struct timeval` embed time_t; must widen along with type.

### 1.3 Kernel and Libc Syscall Signatures

#### Time Query Syscalls

```c
/* kernel/syscall.c, user/timecore.c */
int clock_gettime(clockid_t clock_id, struct timespec *tp);
/* 
   Current: likely uses int64_t storage internally, but libc wrapper may truncate
   Target: Full 64-bit time_t in tp->tv_sec, no truncation
*/

int clock_settime(clockid_t clock_id, const struct timespec *tp);
/*
   Current: as above
   Target: Accept full 64-bit values; validate > 1900 and < year 9999
*/

time_t time(time_t *tloc);
/*
   Current: likely int32_t return (broken)
   Target: int64_t return; if tloc non-NULL, write same value to *tloc
*/
```

#### File Stat Syscall (if stat.st_mtime uses time_t)

```c
struct stat {
    /* ... existing fields ... */
    time_t  st_atime;     /* 64-bit after widening */
    time_t  st_mtime;     /* 64-bit after widening */
    time_t  st_ctime;     /* 64-bit after widening */
    /* ... */
};
```

**Kernel Validation Rules**:
- All `clock_settime()` calls: validate `tp->tv_sec >= 0` and `tp->tv_sec < 253402300800` (Y9999-12-31 23:59:59 UTC)
- All `time()` return: refuse to return any value outside valid range above
- NTP daemon (ntpd): Must not set clock to invalid times (audit ntp server trust or ignore bad responses)

### 1.4 Libc Conversion Functions (Exact Signatures)

```c
/* include/time.h */
struct tm *gmtime_r(const time_t *timep, struct tm *result);
struct tm *localtime_r(const time_t *timep, struct tm *result);
time_t mktime(struct tm *tm);
char *strftime(char *s, size_t max, const char *format, const struct tm *tm);
```

**Current Status**: gmtime_r, localtime_r, mktime exist → audit for 64-bit correctness  
**Breaking Change Risk**: mktime must handle dates beyond 2038; return type must be 64-bit

---

## 2. RNG ABI Contract

### 2.1 Cryptographically Secure Random APIs

```c
/* include/sys/random.h (new file) */

/*
   getentropy(buf, buflen): Fill buf with buflen bytes of system entropy.
   - Returns 0 on success, -1 on error
   - buflen <= 256; larger requests fail with errno=EIO
   - Cannot fail due to insufficient entropy (blocking OK until buffer filled)
   - Suitable for seeding CSPRNG and generating keys
*/
int getentropy(void *buf, size_t buflen);

/*
   getrandom(buf, buflen, flags): Fill buf with buflen random bytes.
   - Returns number of bytes written (0 to buflen), -1 on error
   - flags: GRND_RANDOM (use /dev/random, block if needed)
           GRND_NONBLOCK (fail immediately if no entropy)
   - Current state: getrandom() may not exist; kernel must implement
*/
#define GRND_NONBLOCK    0x0001
#define GRND_RANDOM      0x0002
ssize_t getrandom(void *buf, size_t buflen, unsigned int flags);
```

**Kernel Requirements**:
- Implement `/dev/urandom` device (seeded from chaotic sources: timer jitter, network timing, device I/O timing)
- Implement `/dev/random` device (optional: can alias to `/dev/urandom` if kernel entropy pool is maintained)
- Getrandom syscall dispatches to urandom / random device
- Getentropy: userspace wrapper around getrandom

### 2.2 Userspace Arc4-Family CSPRNG

```c
/* include/stdlib.h */

/*
   arc4random(): Return 32-bit cryptographically random value.
   - Seeded automatically from getentropy on first call
   - Reseeds periodically (every 1M bytes or per user request)
   - Thread-safe (per-thread state or global lock)
   - No failure mode; never returns error
*/
uint32_t arc4random(void);

/*
   arc4random_buf(buf, n): Fill n bytes with cryptographically random data.
   - Uses arc4random() internal state
   - No failure mode
*/
void arc4random_buf(void *buf, size_t n);

/*
   arc4random_uniform(upper_bound): Return random uint32_t in [0, upper_bound).
   - Uses rejection sampling to ensure uniform distribution
   - upper_bound error handling: if upper_bound == 0, return 0 (degenerate case)
*/
uint32_t arc4random_uniform(uint32_t upper_bound);

/*
   arc4random_stir(): Reseed the arc4random state from getentropy.
   - Called automatically on first use; can be called explicitly
   - Optional public API; private use acceptable
*/
void arc4random_stir(void);
```

**Implementation Guidance**:
- Use ChaCha20 or AES-CTR internally (LC libraries often prefer ChaCha20 for portability)
- Seed must derive from kernel entropy (via getentropy or /dev/urandom)
- Reseed on fork (to prevent child/parent state duplication)

### 2.3 Legacy Non-Cryptographic Random (Kept for Compatibility)

```c
/* include/stdlib.h - UNCHANGED */
long random(void);
void srandom(unsigned int seed);
/*
   Status: Deprecated; LCG implementation acceptable
   Use case: Tests, non-security-sensitive shuffles only
   Target: Keep as-is; do NOT upgrade to chaotic
*/
```

---

## 3. Socket ABI Contract

### 3.1 Core Data Structures (POSIX-Compliant)

```c
/* include/sys/socket.h (new file) */

typedef unsigned int socklen_t;  /* For socket option/address length */

/* Address family constants */
#define AF_UNSPEC       0
#define AF_UNIX         1
#define AF_INET         2
#define AF_INET6       10

/* Socket type constants */
#define SOCK_STREAM    1  /* TCP */
#define SOCK_DGRAM     2  /* UDP */
#define SOCK_RAW       3  /* Raw IP */

/* Generic socket address (canonical POSIX) */
struct sockaddr {
    sa_family_t   sa_family;    /* Address family (AF_INET, etc.) */
    char          sa_data[14];  /* Protocol-specific address data */
};

/* IPv4-specific address structure */
struct in_addr {
    in_addr_t  s_addr;        /* 32-bit IPv4 address (network byte order) */
};

struct sockaddr_in {
    sa_family_t   sin_family;   /* AF_INET */
    in_port_t     sin_port;     /* Port number (network byte order) */
    struct in_addr sin_addr;    /* IPv4 address */
    uint8_t       sin_zero[8];  /* Padding to match sockaddr size */
};

/* Socket address length constants */
#define SOCKLEN_MAX    128

typedef uint16_t in_port_t;     /* Port: network byte order */
typedef uint32_t in_addr_t;     /* IPv4: network byte order (big-endian) */
typedef unsigned char sa_family_t;

/* Protocol constants */
#define IPPROTO_TCP    6
#define IPPROTO_UDP   17
```

**Network Byte Order Rules** (CRITICAL):
- `sin_port`: MUST be network byte order (big-endian) in-memory
- `sin_addr.s_addr`: MUST be network byte order (big-endian) in-memory
- Conversion helpers: `htons()`, `ntohs()`, `htonl()`, `ntohl()` in `<arpa/inet.h>` (existing)

### 3.2 Core Socket Syscalls (POSIX Signatures)

```c
/* include/sys/socket.h */

int socket(int domain, int type, int protocol);
/*
   Current: exists but may have non-POSIX signature
   Target:
     - domain: AF_INET (IPv4 required); AF_INET6, AF_UNIX optional for phase 2
     - type: SOCK_STREAM (TCP), SOCK_DGRAM (UDP), SOCK_RAW (root only)
     - protocol: 0 (auto select IPPROTO_TCP for SOCK_STREAM, etc.)
     - Returns: fd on success, -1 on error
   
   BREAKING CHANGE: Must accept network-order sin_port/sin_addr in sockaddr_in
*/

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
/*
   Current: bind(sockfd, sockaddr_in *addr) — non-POSIX
   Target:
     - sockfd: valid socket created by socket()
     - addr: pointer to struct sockaddr (cast from struct sockaddr_in)
     - addrlen: sizeof(struct sockaddr_in) for IPv4
     - Returns: 0 on success, -1 on error
     - Binds socket to local address; port 0 means auto-assign
   
   BREAKING CHANGE: Accept generic sockaddr*, not sockaddr_in*
*/

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
/*
   Current: connect(sockfd, sockaddr_in *addr) — non-POSIX
   Target:
     - sockfd: valid unconnected socket
     - addr: pointer to struct sockaddr (cast from struct sockaddr_in)
     - addrlen: sizeof(struct sockaddr_in) for IPv4
     - Returns: 0 on success, -1 on error (EINPROGRESS for async sockets)
     - Blocks until connection established (or timeout/error)
   
   BREAKING CHANGE: Accept generic sockaddr*, not sockaddr_in*
*/

int listen(int sockfd, int backlog);
/*
   Current: may exist
   Target:
     - sockfd: bound socket (TCP)
     - backlog: max queued connections (typical: 5-128)
     - Returns: 0 on success, -1 on error
     - Marks socket as accepting connections
*/

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
/*
   Current: accept(sockfd) — no peer-address out-params
   Target:
     - sockfd: listening socket
     - addr: output pointer to struct sockaddr (can be NULL)
     - addrlen: input/output length (in: max buffer, out: actual)
     - Returns: new fd on success, -1 on error
     - When addr non-NULL: fills sockaddr_in with peer (sin_family, sin_port, sin_addr)
   
   BREAKING CHANGE: Must support optional peer-address retrieval
*/

ssize_t send(int sockfd, const void *buf, size_t len, int flags);
/*
   Current: may lack flags parameter
   Target:
     - sockfd: connected socket
     - buf: data to send
     - len: byte count
     - flags: MSG_DONTWAIT (0x40), MSG_NOSIGNAL (0x4000), or 0
     - Returns: bytes sent, -1 on error
   
   BREAKING CHANGE: flags parameter required (can be 0)
*/

ssize_t recv(int sockfd, void *buf, size_t len, int flags);
/*
   Current: may lack flags parameter
   Target:
     - sockfd: connected socket
     - buf: receive buffer
     - len: max bytes to read
     - flags: MSG_DONTWAIT (0x40), MSG_PEEK (0x02), or 0
     - Returns: bytes read (0=EOF), -1 on error
   
   BREAKING CHANGE: flags parameter required (can be 0)
*/

int sendto(int sockfd, const void *buf, size_t len, int flags,
           const struct sockaddr *dest_addr, socklen_t addrlen);
/*
   Target (UDP support):
     - Like send but specifies destination address per-datagram
     - flags: same as send
     - Returns: bytes sent, -1 on error
*/

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);
/*
   Target (UDP support):
     - Returns: bytes read, -1 on error
     - src_addr: filled with sender address (can be NULL)
     - addrlen: input/output length
*/

int shutdown(int sockfd, int how);
/*
   Target (TCP teardown):
     - how: SHUT_RD (further receives disallowed)
           SHUT_WR (further sends disallowed)
           SHUT_RDWR (both)
     - Returns: 0 on success, -1 on error
*/

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
/*
   Target (socket options):
     - level: SOL_SOCKET, IPPROTO_TCP, IPPROTO_IP (TCP_NODELAY, etc.)
     - Standard options required: SO_REUSEADDR, SO_RCVTIMEO, SO_SNDTIMEO, TCP_NODELAY
     - Returns: 0 on success, -1 on error
*/

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
/*
   Target (address queries):
     - getpeername: fill addr with remote peer address
     - getsockname: fill addr with local bound address
     - Returns: 0 on success, -1 on error
*/
```

**Constants for Socket Options**:
```c
/* include/sys/socket.h */
#define SOL_SOCKET      1  /* Socket-level options */
#define SO_REUSEADDR    2  /* Allow reusing local addresses */
#define SO_RCVTIMEO    20  /* Receive timeout */
#define SO_SNDTIMEO    21  /* Send timeout */

/* include/netinet/tcp.h */
#define IPPROTO_TCP     6
#define TCP_NODELAY     1  /* Disable Nagle's algorithm */

/* include/netinet/in.h */
#define IPPROTO_IP      0
#define INADDR_ANY      0  /* Bind to all interfaces */
```

### 3.3 Signal Handling for Sockets

```c
/*
   All socket write operations must handle SIGPIPE gracefully:
   - recv/send/sendto must return EPIPE if socket closed by peer
   - Applications may ignore SIGPIPE and check for EPIPE in return value
   - Library code (TLS) must NOT crash on SIGPIPE; must use MSG_NOSIGNAL or handle signal
*/
#define MSG_NOSIGNAL    0x4000  /* Don't send SIGPIPE on closed socket */
```

---

## 4. Resolver ABI Contract

### 4.1 Data Structures

```c
/* include/netdb.h (modified) */

struct addrinfo {
    int             ai_flags;       /* Input/output flags (AI_PASSIVE, AI_NUMERICHOST, ...) */
    int             ai_family;      /* AF_UNSPEC, AF_INET, AF_INET6 */
    int             ai_socktype;    /* SOCK_STREAM, SOCK_DGRAM, SOCK_RAW */
    int             ai_protocol;    /* IPPROTO_TCP, IPPROTO_UDP, 0 */
    socklen_t       ai_addrlen;     /* Length of ai_addr */
    struct sockaddr *ai_addr;       /* Socket address */
    char            *ai_canonname;  /* Canonical name (optional) */
    struct addrinfo *ai_next;       /* Next result in linked list */
};

/*
   Flags for getaddrinfo input (hints.ai_flags)
*/
#define AI_PASSIVE      0x0001  /* Socket address for bind (localhost if hostname NULL) */
#define AI_CANONNAME    0x0002  /* Fill ai_canonname with canonical hostname */
#define AI_NUMERICHOST  0x0004  /* Hostname is numeric IP; don't query resolver */
#define AI_NUMERICSERV  0x0008  /* Service is numeric port; don't query /etc/services */
#define AI_ADDRCONFIG   0x0020  /* Return only addresses for configured families */

/*
   Return codes (gai_strerror strings)
*/
#define EAI_NONAME      1   /* Name or service not known */
#define EAI_NODATA      2   /* No address associated with hostname */
#define EAI_AGAIN       2   /* Temporary failure (e.g., DNS timeout) */
#define EAI_FAIL        3   /* Non-recoverable failure */
#define EAI_FAMILY      5   /* Address family not supported */
#define EAI_SOCKTYPE    7   /* Socket type not supported */
#define EAI_SERVICE     8   /* Service not supported for ai_socktype */
#define EAI_MEMORY     10   /* Memory allocation failure */
#define EAI_SYSTEM     11   /* System error (check errno) */
```

### 4.2 Resolver Functions

```c
/* include/netdb.h */

/*
   getaddrinfo(hostname, service, hints, res):
   Perform DNS/host lookup and return list of socket addresses.
   
   - hostname: domain name (or IP address string, or NULL with AI_PASSIVE)
   - service: port number (as string: "80", "http") or NULL
   - hints: optional template addrinfo struct (set ai_family, ai_socktype, ai_protocol, ai_flags)
   - res: output pointer to addrinfo linked list (must be freed with freeaddrinfo)
   
   Returns: 0 on success, EAI_* on error
   
   Semantics:
   - hostname=NULL, AI_PASSIVE=set → return address_any with port
   - hostname=NULL, AI_PASSIVE=unset → return loopback with port
   - hostname="::1" or "127.0.0.1" → numeric lookup only
   - service=NULL → use port 0 (ephemeral)
   - Typical loop: for (p = res; p != NULL; p = p->ai_next) { try_connect(p); }
*/
int getaddrinfo(const char *hostname, const char *service,
                const struct addrinfo *hints, struct addrinfo **res);

/*
   freeaddrinfo(res): Free addrinfo linked list and all allocated memory.
   
   Must be called on all non-NULL results from getaddrinfo.
*/
void freeaddrinfo(struct addrinfo *res);

/*
   gai_strerror(errcode): Return human-readable string for EAI_* error code.
   
   - errcode: EAI_NONAME, EAI_FAIL, EAI_MEMORY, etc.
   - Returns: pointer to static string (do not free)
*/
const char *gai_strerror(int errcode);

/*
   getnameinfo(sa, salen, host, hostlen, serv, servlen, flags):
   Reverse-resolve socket address to hostname and service (optional).
   
   - sa: socket address to reverse-resolve
   - salen: length of sa
   - host: output buffer for hostname (can be NULL if hostlen==0)
   - hostlen: size of host buffer (if < 256, may truncate)
   - serv: output buffer for service name (can be NULL if servlen==0)
   - servlen: size of serv buffer
   - flags: NI_NUMERICHOST (return numeric IP), NI_NUMERICSERV (return numeric port), etc.
   
   Returns: 0 on success, EAI_* on error
*/
#define NI_NUMERICHOST  0x0001
#define NI_NUMERICSERV  0x0002
#define NI_NOFQDN       0x0004
#define NI_NAMEREQD     0x0008
#define NI_DGRAM        0x0010

int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags);

/*
   Legacy (deprecated but keep for compatibility): gethostbyname
*/
struct hostent {
    char  *h_name;          /* Official hostname */
    char **h_aliases;       /* Alternative hostnames */
    int    h_addrtype;      /* AF_INET */
    int    h_length;        /* 4 for IPv4, 16 for IPv6 */
    char **h_addr_list;     /* List of addresses */
};

struct hostent *gethostbyname(const char *name);
/*
   NOT THREAD-SAFE. Target: keep as wrapper around getaddrinfo;
   return first result only.
*/

struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type);
/*
   NOT THREAD-SAFE. Target: wrapper around getnameinfo.
*/
```

### 4.3 Resolver Lookups (Semantics)

**Priority Order for Name Resolution**:
1. `/etc/hosts` lookup (exact match, prefix match disabled)
2. `/etc/resolv.conf` DNS servers (in order listed)
3. Fall back to localhost if no result

**DNS Query Behavior**:
- Query A records (IPv4) for AF_INET requests
- Query AAAA records (IPv6) for AF_INET6 (optional in phase 1)
- On NXDOMAIN (no such domain): return EAI_NONAME
- On SERVFAIL (server failure): return EAI_AGAIN
- On timeout (>5s): return EAI_AGAIN

**Trust Model**:
- No DNSSEC validation in phase 1
- Accept any response from configured /etc/resolv.conf nameserver
- No loop detection; nameserver must not point to self

---

## 5. Security Helper ABI Contract

### 5.1 Memory Wiping (Constant-Time)

```c
/* include/string.h */

/*
   explicit_bzero(buf, len): Securely clear len bytes of memory.
   
   - Unlike bzero, explicit_bzero is NOT optimized away by compiler
   - Clears keys, passwords, secrets before freeing
   - Equivalent to OPENSSL_cleanse or BoringSSL's OPENSSL_cleanse
   - Must use volatile or asm barrier to prevent dead-store elimination
*/
void explicit_bzero(void *buf, size_t len);

/*
   timingsafe_bcmp(a, b, len): Compare len bytes in constant time.
   
   - Returns 0 if equal, non-zero if different
   - Timing is independent of input values (no early exit on mismatch)
   - Used for comparing MACs, tokens, hashes where timing leaks are dangerous
   - Slower than memcmp but safe for cryptographic comparisons
*/
int timingsafe_bcmp(const void *a, const void *b, size_t len);

/*
   timingsafe_memcmp(a, b, len): Alias for timingsafe_bcmp.
   
   - Some libraries expect this name instead
   - Must have identical semantics to timingsafe_bcmp
*/
int timingsafe_memcmp(const void *a, const void *b, size_t len);
```

### 5.2 Memory Allocation Helpers (Security-Aware)

```c
/* include/stdlib.h */

/*
   freezero(ptr, len): Free memory and wipe first.
   
   - Calls explicit_bzero(ptr, len) then free(ptr)
   - Safe for zero-length buffers (freezero(ptr, 0) is valid)
   - Handles NULL pointer (no-op, like free)
*/
void freezero(void *ptr, size_t len);

/*
   recallocarray(ptr, oldcount, newcount, size):
   Reallocate array and wipe old size on shrink.
   
   - Similar to reallocarray but wipes old data if shrinking
   - ptr: existing allocation (or NULL)
   - oldcount: previous count (ignored if ptr==NULL)
   - newcount: desired count (0 is valid; frees and wipes)
   - size: per-element size
   - Returns: new allocation, or NULL if newcount==0
   - On error: returns NULL, leaves old pointer intact
*/
void *recallocarray(void *ptr, size_t oldcount, size_t newcount, size_t size);
```

### 5.3 Misc Libc Security Helpers

```c
/* include/stdlib.h (optional phase 2) */

/*
   strtonum(str, min, max, errstr):
   Parse string to long with bounds checking.
   
   - Safer than strtol for user input parsing
   - errstr: output pointer to error string (or empty string on success)
   - Returns: parsed value if in [min, max], else 0 or clamped value
   - Phase 2: optional (config parsing, argument validation)
*/
long strtonum(const char *str, long min, long max, const char **errstr);

/*
   getprogname(void): Return program name (argv[0] basename).
   
   - Phase 2: optional (for error messages)
*/
const char *getprogname(void);
```

---

## 6. Error Codes & errno Contract

### 6.1 Critical POSIX errno Values

```c
/* include/errno.h */
#define EPERM       1   /* Operation not permitted */
#define ENOENT      2   /* No such file or directory */
#define ESRCH       3   /* No such process */
#define EINTR       4   /* Interrupted system call */
#define EIO         5   /* I/O error */
#define ENXIO       6   /* No such device or address */
#define E2BIG       7   /* Argument list too long */
#define ENOEXEC     8   /* Exec format error */
#define EBADF       9   /* Bad file descriptor */
#define ECHILD     10   /* No child processes */
#define EAGAIN     11   /* Resource temporarily unavailable */
#define ENOMEM     12   /* Cannot allocate memory */
#define EACCES     13   /* Permission denied */
#define EFAULT     14   /* Bad address */
#define ENOTBLK    15   /* Block device required */
#define EBUSY      16   /* Device or resource busy */
#define EEXIST     17   /* File exists */
#define EXDEV      18   /* Invalid cross-device link */
#define ENODEV     19   /* No such device */
#define ENOTDIR    20   /* Not a directory */
#define EISDIR     21   /* Is a directory */
#define EINVAL     22   /* Invalid argument */
#define ENFILE     23   /* Too many open files in system */
#define EMFILE     24   /* Too many open files */
#define ENOTTY     25   /* Inappropriate ioctl for device */
#define ETXTBSY    26   /* Text file busy */
#define EFBIG      27   /* File too large */
#define ENOSPC     28   /* No space left on device */
#define ESPIPE     29   /* Illegal seek */
#define EROFS      30   /* Read-only file system */
#define EMLINK     31   /* Too many links */
#define EPIPE      32   /* Broken pipe */
#define EDOM       33   /* Numerical argument out of domain */
#define ERANGE     34   /* Numerical result out of range */
#define ETIMEDOUT 110   /* Connection timed out (used by socket ops) */
#define ECONNREFUSED 111 /* Connection refused */
#define ECONNRESET   104 /* Connection reset by peer */
#define EHOSTUNREACH 113 /* No route to host */
```

**Socket-Specific Syscall Errors**:
- `socket()`: EMFILE (too many open files), ENOMEM
- `bind()`: EADDRINUSE (address already in use), EACCES (permission denied), EINVAL (invalid address)
- `connect()`: EINPROGRESS (non-blocking, try again), ETIMEDOUT, ECONNREFUSED, EHOSTUNREACH
- `accept()`: EAGAIN/EWOULDBLOCK (no connections pending), EBADF (bad fd)
- `send()`: EPIPE (closed by peer), EWOULDBLOCK (non-blocking, no space), EINTR (interrupted)
- `recv()`: EAGAIN/EWOULDBLOCK (non-blocking, no data), EINTR, 0 (EOF on TCP shutdown)

---

## 7. Floating-Point and Misc Libc Additions

### 7.1 Already Present (Verify)

These must already exist and be correct; verify during Tranche 0 audit:
- `strlcpy`, `strlcat`, `strsep`, `strndup`, `strnlen`, `strcasecmp`
- `reallocarray`, `mkstemp`, `mkdtemp`, `realpath`
- `bzero`, `bcopy`, `memcpy`, `memmove`, `memset`

### 7.2 Optional / Phase 2

These are nice-to-have; defer until after TLS build:
- `sys/uio.h` (readv, writev, preadv, pwritev)
- `poll.h` proper struct poll_fd definition
- `fcntl.h` F_SETFL (for O_NONBLOCK)
- `netdb.h` service lookup (/etc/services)

---

## 8. Signing Off on This Contract

**This document is FROZEN as of 2026-04-12.**

All implementation tranches (1-8) must conform to this contract. No deviations from these signatures, semantics, or struct layouts without explicit amendment to this document and sign-off from project leadership.

**Next Steps**:
1. Audit current auxv6 code for deviations (Tranche 0 execution)
2. File fallout list (which structs/syscalls need changes) → Tranche 1 fallout audit
3. Begin implementation in order: Tranche 1, then 2, etc.

---

## Appendix: Verification Checklist (For Tranche 0 Audit)

**Header Files to Create/Modify**:
- [ ] `include/sys/socket.h` (new) — socket ABI
- [ ] `include/sys/random.h` (new) — RNG APIs
- [ ] `include/sys/types.h` (modify) — time_t, sa_family_t, socklen_t, in_addr_t, in_port_t
- [ ] `include/time.h` (verify) — struct timespec, struct timeval
- [ ] `include/netdb.h` (modify) — add struct addrinfo, getaddrinfo, freeaddrinfo, getnameinfo, gai_strerror
- [ ] `include/string.h` (add) — explicit_bzero, timingsafe_bcmp, timingsafe_memcmp
- [ ] `include/stdlib.h` (add) — arc4random, arc4random_buf, arc4random_uniform, freezero, recallocarray
- [ ] `include/arpa/inet.h` (verify) — htons, ntohs, htonl, ntohl, inet_aton, inet_ntoa
- [ ] `include/netinet/in.h` (verify) — INADDR_ANY, INADDR_LOOPBACK, sockaddr_in
- [ ] `include/netinet/tcp.h` (verify) — TCP_NODELAY

**Kernel Syscalls to Audit/Create**:
- [ ] `clock_gettime(clockid_t, struct timespec *)` supports 64-bit time_t
- [ ] `clock_settime(clockid_t, const struct timespec *)` validates time_t range
- [ ] `time(time_t *)` returns 64-bit value, no truncation
- [ ] `getrandom(void *, size_t, unsigned int)` implemented
- [ ] `socket(int domain, int type, int protocol)` accepts AF_INET, SOCK_STREAM, SOCK_DGRAM
- [ ] `bind(int, const struct sockaddr *, socklen_t)` handles network-byte-order addresses
- [ ] `connect(int, const struct sockaddr *, socklen_t)` same
- [ ] `accept(int, struct sockaddr *, socklen_t *)` fills peer address
- [ ] `send(int, const void *, size_t, int flags)` with flags parameter
- [ ] `recv(int, void *, size_t, int flags)` with flags parameter
- [ ] `getpeername(int, struct sockaddr *, socklen_t *)`
- [ ] `getsockname(int, struct sockaddr *, socklen_t *)`
- [ ] `getsockopt(int, int, int, void *, socklen_t *)`
- [ ] `setsockopt(int, int, int, const void *, socklen_t)`

**Libc Functions to Audit/Create**:
- [ ] `gmtime_r`, `localtime_r`, `mktime` handle 64-bit time_t
- [ ] `getentropy(void *, size_t)` implemented
- [ ] `arc4random()`, `arc4random_buf(void *, size_t)` implemented
- [ ] `explicit_bzero(void *, size_t)` implemented with volatile barrier
- [ ] `timingsafe_bcmp(const void *, const void *, size_t)` constant-time
- [ ] `getaddrinfo(const char *, const char *, const struct addrinfo *, struct addrinfo **)` implemented
- [ ] `freeaddrinfo(struct addrinfo *)` implemented
- [ ] `getnameinfo(const struct sockaddr *, socklen_t, char *, socklen_t, char *, socklen_t, int)` implemented

