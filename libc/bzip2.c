#include "types.h"
#include "auxv6/user.h"
#include "auxv6/bzip2.h"
#include "bzlib.h"
#include "string.h"
#include "stdlib.h"
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "errno.h"

static int
bz_write_all(int fd, const void *buf, int n)
{
  const uchar *p;
  int off;

  p = (const uchar*)buf;
  off = 0;
  while(off < n) {
    int m = write(fd, p + off, n - off);
    if(m <= 0)
      return -1;
    off += m;
  }

  return 0;
}

int
aux_bzip2_inflate_fd(int in_fd, int out_fd)
{
  uchar inbuf[4096];
  uchar outbuf[4096];
  bz_stream strm;
  int in_pos;
  int in_len;
  int eof;
  int rc;
  int members;

  memset(&strm, 0, sizeof(strm));
  rc = BZ2_bzDecompressInit(&strm, 0, 0);
  if(rc != BZ_OK) {
    errno = EINVAL;
    return -1;
  }

  in_pos = 0;
  in_len = 0;
  eof = 0;
  members = 0;

  while(1) {
    int out_n;

    if(in_pos == in_len && !eof) {
      in_len = read(in_fd, inbuf, sizeof(inbuf));
      if(in_len < 0) {
        BZ2_bzDecompressEnd(&strm);
        return -1;
      }
      in_pos = 0;
      if(in_len == 0)
        eof = 1;
    }

    strm.next_in = (char*)inbuf + in_pos;
    strm.avail_in = (uint)(in_len - in_pos);
    strm.next_out = (char*)outbuf;
    strm.avail_out = sizeof(outbuf);

    rc = BZ2_bzDecompress(&strm);

    in_pos = in_len - (int)strm.avail_in;
    out_n = sizeof(outbuf) - (int)strm.avail_out;
    if(out_n > 0 && bz_write_all(out_fd, outbuf, out_n) < 0) {
      BZ2_bzDecompressEnd(&strm);
      return -1;
    }

    if(rc == BZ_OK) {
      if(eof && in_pos == in_len && out_n == 0) {
        BZ2_bzDecompressEnd(&strm);
        errno = EINVAL;
        return -1;
      }
      continue;
    }

    if(rc == BZ_STREAM_END) {
      members++;
      BZ2_bzDecompressEnd(&strm);

      if(in_pos == in_len && !eof) {
        in_len = read(in_fd, inbuf, sizeof(inbuf));
        if(in_len < 0)
          return -1;
        in_pos = 0;
        if(in_len == 0)
          eof = 1;
      }

      while((in_len - in_pos) < 3 && !eof) {
        int n;
        int keep = in_len - in_pos;

        if(keep > 0 && in_pos > 0)
          memmove(inbuf, inbuf + in_pos, keep);
        in_pos = 0;
        in_len = keep;

        n = read(in_fd, inbuf + in_len, sizeof(inbuf) - in_len);
        if(n < 0)
          return -1;
        if(n == 0)
          eof = 1;
        in_len += n;
      }

      if(in_pos == in_len && eof)
        return 0;

      if((in_len - in_pos) < 3 ||
         inbuf[in_pos] != 'B' ||
         inbuf[in_pos + 1] != 'Z' ||
         inbuf[in_pos + 2] != 'h') {
        errno = EINVAL;
        return -1;
      }

      memset(&strm, 0, sizeof(strm));
      rc = BZ2_bzDecompressInit(&strm, 0, 0);
      if(rc != BZ_OK) {
        errno = EINVAL;
        return -1;
      }
      continue;
    }

    BZ2_bzDecompressEnd(&strm);
    errno = EINVAL;
    return -1;
  }

  (void)members;
  return 0;
}

int
aux_bzip2_deflate_fd(int in_fd, int out_fd)
{
  uchar inbuf[4096];
  uchar outbuf[4096];
  bz_stream strm;
  int rc;

  memset(&strm, 0, sizeof(strm));
  rc = BZ2_bzCompressInit(&strm, 9, 0, 30);
  if(rc != BZ_OK) {
    errno = EINVAL;
    return -1;
  }

  while(1) {
    int n;

    n = read(in_fd, inbuf, sizeof(inbuf));
    if(n < 0) {
      BZ2_bzCompressEnd(&strm);
      return -1;
    }

    if(n == 0)
      break;

    strm.next_in = (char*)inbuf;
    strm.avail_in = (uint)n;
    while(strm.avail_in > 0) {
      int out_n;

      strm.next_out = (char*)outbuf;
      strm.avail_out = sizeof(outbuf);
      rc = BZ2_bzCompress(&strm, BZ_RUN);
      if(rc != BZ_RUN_OK) {
        BZ2_bzCompressEnd(&strm);
        errno = EINVAL;
        return -1;
      }

      out_n = sizeof(outbuf) - (int)strm.avail_out;
      if(out_n > 0 && bz_write_all(out_fd, outbuf, out_n) < 0) {
        BZ2_bzCompressEnd(&strm);
        return -1;
      }
    }
  }

  while(1) {
    int out_n;

    strm.next_out = (char*)outbuf;
    strm.avail_out = sizeof(outbuf);
    rc = BZ2_bzCompress(&strm, BZ_FINISH);
    if(rc != BZ_FINISH_OK && rc != BZ_STREAM_END) {
      BZ2_bzCompressEnd(&strm);
      errno = EINVAL;
      return -1;
    }

    out_n = sizeof(outbuf) - (int)strm.avail_out;
    if(out_n > 0 && bz_write_all(out_fd, outbuf, out_n) < 0) {
      BZ2_bzCompressEnd(&strm);
      return -1;
    }

    if(rc == BZ_STREAM_END)
      break;
  }

  BZ2_bzCompressEnd(&strm);
  return 0;
}

int
aux_bzip2_output_name(const char *in_path, char *out, int out_sz)
{
  int len;

  len = strlen(in_path);

  /* Strip .bz2 suffix if present */
  if(len >= 4 && strcmp(in_path + len - 4, ".bz2") == 0) {
    if(len - 4 + 1 > out_sz)
      return -1;
    memmove(out, in_path, len - 4);
    out[len - 4] = 0;
    return 0;
  }

  /* Strip .tar.bz2 suffix */
  if(len >= 8 && strcmp(in_path + len - 8, ".tar.bz2") == 0) {
    if(len - 4 + 1 > out_sz)
      return -1;
    memmove(out, in_path, len - 4);
    out[len - 4] = 0;
    return 0;
  }

  /* Append .out */
  if(len + 5 > out_sz)
    return -1;
  memmove(out, in_path, len);
  strcpy(out + len, ".out");
  return 0;
}

int
aux_bzip2_has_suffix(const char *path)
{
  int len = strlen(path);
  if(len >= 4 && strcmp(path + len - 4, ".bz2") == 0)
    return 1;
  if(len >= 8 && strcmp(path + len - 8, ".tar.bz2") == 0)
    return 1;
  return 0;
}
