#ifndef AUXV6_GZIP_H
#define AUXV6_GZIP_H

#include "stdint.h"

/*
 * Inflate a gzip stream from in_fd to out_fd.
 * Returns 0 on success, -1 on parse/decompression/checksum failure.
 */
int aux_gzip_inflate_fd(int in_fd, int out_fd);

/*
 * Write a gzip stream to out_fd from raw input bytes read on in_fd.
 * Uses valid deflate stored blocks (no compression) for broad compatibility.
 * Returns 0 on success, -1 on I/O failure.
 */
int aux_gzip_deflate_store_fd(int in_fd, int out_fd);

/*
 * Infer gunzip output name from a gzip input path.
 * .gz/.tgz suffixes are stripped; otherwise ".out" is appended.
 * Returns 0 on success, -1 if out is too small.
 */
int aux_gzip_output_name(const char *in_path, char *out, int out_sz);

/* Return non-zero when path ends with .gz or .tgz (case-sensitive). */
int aux_gzip_has_suffix(const char *path);

#endif
