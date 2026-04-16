#ifndef AUXV6_BZIP2_H
#define AUXV6_BZIP2_H

#include "stdint.h"

/*
 * Decompress a bzip2 stream from in_fd to out_fd.
 * Returns 0 on success, -1 on parse/decompression/CRC failure.
 */
int aux_bzip2_inflate_fd(int in_fd, int out_fd);

/*
 * Compress raw input from in_fd to out_fd using bzip2.
 * Uses block size 900KB (standard) and single block compression.
 * Returns 0 on success, -1 on I/O failure.
 */
int aux_bzip2_deflate_fd(int in_fd, int out_fd);

/*
 * Infer bunzip2 output name from a bzip2 input path.
 * .bz2/.tar.bz2 suffixes are stripped; otherwise ".out" is appended.
 * Returns 0 on success, -1 if out is too small.
 */
int aux_bzip2_output_name(const char *in_path, char *out, int out_sz);

/* Return non-zero when path ends with .bz2 or .tar.bz2 (case-sensitive). */
int aux_bzip2_has_suffix(const char *path);

#endif
