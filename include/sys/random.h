/*
 * <sys/random.h> - Cryptographically secure random number generation
 *
 * POSIX getentropy / getrandom interface for kernel entropy.
 * Tranche 2: Kernel RNG subsystem implementation.
 */

#ifndef _SYS_RANDOM_H
#define _SYS_RANDOM_H

#include "sys/types.h"

/* Flags for getrandom() */
#define GRND_NONBLOCK    0x0001    /* Don't block if no entropy available */
#define GRND_RANDOM      0x0002    /* Use /dev/random (blocking) instead of /dev/urandom */

/*
 * getrandom(buf, buflen, flags): Read random bytes from kernel entropy pool.
 *
 * Returns the number of bytes written (0 to buflen) on success, -1 on error.
 * If GRND_NONBLOCK is not set, may block until buflen bytes are available.
 * If GRND_NONBLOCK is set and no entropy available, returns -1 with errno=EAGAIN.
 *
 * On error, errno is set to:
 *   EINVAL - invalid flags or buflen > INT_MAX
 *   EAGAIN - GRND_NONBLOCK set and no entropy available
 *   EFAULT - buf points to invalid memory
 *   EFBIG  - buflen exceeds maximum (typically 256 or 32MB)
 */
ssize_t getrandom(void *buf, size_t buflen, unsigned int flags);

/*
 * getentropy(buf, buflen): Fill buffer with unpredictable entropy bytes.
 *
 * OpenBSD-compatible interface; simpler than getrandom.
 *
 * Returns 0 on success, -1 on error.
 * Blocks until buflen bytes of entropy are available.
 * Fails if buflen > 256 (with errno=EIO).
 *
 * Unlike getrandom, getentropy:
 *   - Never fails due to lack of entropy (always blocks)
 *   - Fails fatally if requested buflen > 256
 *   - No flags parameter
 *   - Returns 0 on success, not bytes written
 */
int getentropy(void *buf, size_t buflen);

#endif /* _SYS_RANDOM_H */
