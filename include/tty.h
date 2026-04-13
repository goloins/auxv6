#ifndef AUXV6_TTY_H
#define AUXV6_TTY_H

/*
 * Kernel-internal TTY/PTY constants for the staged BSD-style rewrite.
 * These sizes are intentionally conservative and can be tuned after profiling.
 */
#define TTY_LDISC_CANON_BUFSZ 4096
#define TTY_LDISC_SCRATCH_BUFSZ 512

#endif