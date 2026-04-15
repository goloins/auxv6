#include "types.h"
#include "auxv6/user.h"
#include "auxv6/gzip.h"
#include "string.h"
#include "stdint.h"
#include "checksum.h"
#include "errno.h"

#define GZIP_INBUF_SZ 4096
#define GZIP_OUTBUF_SZ 4096
#define GZIP_STORE_CHUNK 4096
#define GZIP_WINSZ 32768
#define HUFF_MAXBITS 15
#define HUFF_MAX_SYMS 288
#define HUFF_MAX_NODES (HUFF_MAX_SYMS * HUFF_MAXBITS + 1)

#define GZIP_FTEXT    0x01
#define GZIP_FHCRC    0x02
#define GZIP_FEXTRA   0x04
#define GZIP_FNAME    0x08
#define GZIP_FCOMMENT 0x10

struct in_stream {
  int fd;
  uchar buf[GZIP_INBUF_SZ];
  int pos;
  int len;
};

struct out_stream {
  int fd;
  uchar buf[GZIP_OUTBUF_SZ];
  int len;
};

struct bit_stream {
  struct in_stream *in;
  uint bitbuf;
  int bitcnt;
};

struct huff_tree {
  short left[HUFF_MAX_NODES];
  short right[HUFF_MAX_NODES];
  short sym[HUFF_MAX_NODES];
  int nodes;
};

/*
 * Keep large inflate scratch buffers out of the user stack.
 * Dynamic-Huffman decode needs three trees (~78 KB total), and the
 * member window adds another 32 KB. Putting all of that on the stack
 * can corrupt caller locals in auxv6 userspace.
 */
static uchar gzip_member_window[GZIP_WINSZ];
static uchar gzip_dyn_cl_lens[19];
static uchar gzip_dyn_ll_lens[288 + 32];
static struct huff_tree gzip_dyn_cl_tree;
static struct huff_tree gzip_dyn_ll_tree;
static struct huff_tree gzip_dyn_dist_tree;

static const int len_base[29] = {
  3, 4, 5, 6, 7, 8, 9, 10,
  11, 13, 15, 17, 19, 23, 27, 31,
  35, 43, 51, 59, 67, 83, 99, 115,
  131, 163, 195, 227, 258
};

static const int len_extra[29] = {
  0, 0, 0, 0, 0, 0, 0, 0,
  1, 1, 1, 1, 2, 2, 2, 2,
  3, 3, 3, 3, 4, 4, 4, 4,
  5, 5, 5, 5, 0
};

static const int dist_base[30] = {
  1, 2, 3, 4, 5, 7, 9, 13, 17, 25,
  33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
  1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};

static const int dist_extra[30] = {
  0, 0, 0, 0, 1, 1, 2, 2, 3, 3,
  4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
  9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static int
in_next_byte(struct in_stream *in)
{
  int n;

  if(in->pos >= in->len) {
    n = read(in->fd, in->buf, sizeof(in->buf));
    if(n <= 0)
      return -1;
    in->pos = 0;
    in->len = n;
  }

  return in->buf[in->pos++];
}

static int
in_read_bytes(struct in_stream *in, uchar *dst, int n)
{
  int i;

  for(i = 0; i < n; i++) {
    int c = in_next_byte(in);
    if(c < 0)
      return -1;
    dst[i] = (uchar)c;
  }
  return 0;
}

static int
out_flush(struct out_stream *out)
{
  int off;
  int n;

  off = 0;
  while(off < out->len) {
    n = write(out->fd, out->buf + off, out->len - off);
    if(n <= 0)
      return -1;
    off += n;
  }

  out->len = 0;
  return 0;
}

static int
out_put_byte(struct out_stream *out, uchar b)
{
  if(out->len >= (int)sizeof(out->buf)) {
    if(out_flush(out) < 0)
      return -1;
  }

  out->buf[out->len++] = b;
  return 0;
}

static int
gzip_write_all(int fd, const void *buf, int n)
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

static int
bits_read_bit(struct bit_stream *bs)
{
  int c;

  if(bs->bitcnt == 0) {
    c = in_next_byte(bs->in);
    if(c < 0)
      return -1;
    bs->bitbuf = (uint)c;
    bs->bitcnt = 8;
  }

  c = (int)(bs->bitbuf & 1U);
  bs->bitbuf >>= 1;
  bs->bitcnt--;
  return c;
}

static int
bits_read(struct bit_stream *bs, int n, uint *out)
{
  int i;
  uint v;

  v = 0;
  for(i = 0; i < n; i++) {
    int b = bits_read_bit(bs);
    if(b < 0)
      return -1;
    v |= ((uint)b << i);
  }

  *out = v;
  return 0;
}

static void
bits_align_byte(struct bit_stream *bs)
{
  bs->bitbuf = 0;
  bs->bitcnt = 0;
}

static void
huff_reset(struct huff_tree *t)
{
  int i;

  for(i = 0; i < HUFF_MAX_NODES; i++) {
    t->left[i] = -1;
    t->right[i] = -1;
    t->sym[i] = -1;
  }

  t->nodes = 1;
}

static int
huff_new_node(struct huff_tree *t)
{
  int id;

  if(t->nodes >= HUFF_MAX_NODES)
    return -1;

  id = t->nodes++;
  t->left[id] = -1;
  t->right[id] = -1;
  t->sym[id] = -1;
  return id;
}

static int
huff_build(struct huff_tree *t, const uchar *lens, int nsym)
{
  int count[HUFF_MAXBITS + 1];
  int next_code[HUFF_MAXBITS + 1];
  int code;
  int bits;
  int s;

  huff_reset(t);
  for(bits = 0; bits <= HUFF_MAXBITS; bits++)
    count[bits] = 0;

  for(s = 0; s < nsym; s++) {
    int len = lens[s];
    if(len < 0 || len > HUFF_MAXBITS)
      return -1;
    if(len)
      count[len]++;
  }

  code = 0;
  count[0] = 0;
  for(bits = 1; bits <= HUFF_MAXBITS; bits++) {
    code = (code + count[bits - 1]) << 1;
    next_code[bits] = code;
  }

  for(s = 0; s < nsym; s++) {
    int len = lens[s];
    int cur;
    int node;
    int i;

    if(len == 0)
      continue;

    cur = next_code[len]++;
    node = 0;

    for(i = len - 1; i >= 0; i--) {
      int bit = (cur >> i) & 1;
      int child;

      if(bit == 0) {
        child = t->left[node];
        if(child < 0) {
          child = huff_new_node(t);
          if(child < 0)
            return -1;
          t->left[node] = child;
        }
      } else {
        child = t->right[node];
        if(child < 0) {
          child = huff_new_node(t);
          if(child < 0)
            return -1;
          t->right[node] = child;
        }
      }

      node = child;
    }

    if(t->sym[node] >= 0)
      return -1;
    t->sym[node] = (short)s;
  }

  return 0;
}

static int
huff_decode(struct bit_stream *bs, const struct huff_tree *t, int *sym)
{
  int node;

  node = 0;
  while(1) {
    int s = t->sym[node];
    int b;

    if(s >= 0) {
      *sym = s;
      return 0;
    }

    b = bits_read_bit(bs);
    if(b < 0)
      return -1;

    if(b == 0)
      node = t->left[node];
    else
      node = t->right[node];

    if(node < 0)
      return -1;
  }
}

static int
gzip_skip_cstring(struct in_stream *in)
{
  int c;

  while(1) {
    c = in_next_byte(in);
    if(c < 0)
      return -1;
    if(c == 0)
      return 0;
  }
}

static int
gzip_skip_header(struct in_stream *in)
{
  uchar fixed[10];
  uchar tmp[2];
  uint xlen;
  int i;

  if(in_read_bytes(in, fixed, sizeof(fixed)) < 0)
    return -1;

  if(fixed[0] != 0x1f || fixed[1] != 0x8b)
    return -1;
  if(fixed[2] != 8)
    return -1;

  if(fixed[3] & GZIP_FEXTRA) {
    if(in_read_bytes(in, tmp, 2) < 0)
      return -1;
    xlen = (uint)tmp[0] | ((uint)tmp[1] << 8);
    for(i = 0; i < (int)xlen; i++) {
      if(in_next_byte(in) < 0)
        return -1;
    }
  }

  if(fixed[3] & GZIP_FNAME) {
    if(gzip_skip_cstring(in) < 0)
      return -1;
  }

  if(fixed[3] & GZIP_FCOMMENT) {
    if(gzip_skip_cstring(in) < 0)
      return -1;
  }

  if(fixed[3] & GZIP_FHCRC) {
    if(in_read_bytes(in, tmp, 2) < 0)
      return -1;
  }

  return 0;
}

static int
inflate_stored(struct bit_stream *bs,
               struct out_stream *out,
               uchar *win,
               uint *wpos,
               uint *crc,
               uint *usize)
{
  uchar hdr[4];
  uint len;
  uint nlen;
  uint i;

  bits_align_byte(bs);

  if(in_read_bytes(bs->in, hdr, 4) < 0)
    return -1;

  len = (uint)hdr[0] | ((uint)hdr[1] << 8);
  nlen = (uint)hdr[2] | ((uint)hdr[3] << 8);
  if((len ^ 0xffffU) != nlen)
    return -1;

  for(i = 0; i < len; i++) {
    int c = in_next_byte(bs->in);
    if(c < 0)
      return -1;

    if(out_put_byte(out, (uchar)c) < 0)
      return -1;
    win[*wpos & (GZIP_WINSZ - 1)] = (uchar)c;
    *wpos = (*wpos + 1) & (GZIP_WINSZ - 1);
    *crc = aux_crc32_update_byte(*crc, (uchar)c);
    *usize = *usize + 1;
  }

  return 0;
}

static int
inflate_codes(struct bit_stream *bs,
              const struct huff_tree *litlen,
              const struct huff_tree *dist,
              struct out_stream *out,
              uchar *win,
              uint *wpos,
              uint *crc,
              uint *usize)
{
  while(1) {
    int sym;

    if(huff_decode(bs, litlen, &sym) < 0)
      return -1;

    if(sym < 256) {
      uchar b;
      b = (uchar)sym;
      if(out_put_byte(out, b) < 0)
        return -1;
      win[*wpos & (GZIP_WINSZ - 1)] = b;
      *wpos = (*wpos + 1) & (GZIP_WINSZ - 1);
      *crc = aux_crc32_update_byte(*crc, b);
      *usize = *usize + 1;
      continue;
    }

    if(sym == 256)
      return 0;

    if(sym > 285)
      return -1;

    {
      int len_idx = sym - 257;
      int length;
      int dist_sym;
      int dist_val;
      uint extra;
      int i;

      length = len_base[len_idx];
      if(len_extra[len_idx]) {
        if(bits_read(bs, len_extra[len_idx], &extra) < 0)
          return -1;
        length += (int)extra;
      }

      if(huff_decode(bs, dist, &dist_sym) < 0)
        return -1;
      if(dist_sym < 0 || dist_sym >= 30)
        return -1;

      dist_val = dist_base[dist_sym];
      if(dist_extra[dist_sym]) {
        if(bits_read(bs, dist_extra[dist_sym], &extra) < 0)
          return -1;
        dist_val += (int)extra;
      }

      if(dist_val <= 0 || dist_val > GZIP_WINSZ)
        return -1;

      for(i = 0; i < length; i++) {
        uchar b = win[(*wpos - dist_val) & (GZIP_WINSZ - 1)];
        if(out_put_byte(out, b) < 0)
          return -1;
        win[*wpos & (GZIP_WINSZ - 1)] = b;
        *wpos = (*wpos + 1) & (GZIP_WINSZ - 1);
        *crc = aux_crc32_update_byte(*crc, b);
        *usize = *usize + 1;
      }
    }
  }
}

static int
inflate_fixed(struct bit_stream *bs,
              struct out_stream *out,
              uchar *win,
              uint *wpos,
              uint *crc,
              uint *usize)
{
  static int ready;
  static struct huff_tree litlen_tree;
  static struct huff_tree dist_tree;
  uchar litlen_lens[288];
  uchar dist_lens[32];
  int i;

  if(!ready) {
    for(i = 0; i <= 143; i++) litlen_lens[i] = 8;
    for(i = 144; i <= 255; i++) litlen_lens[i] = 9;
    for(i = 256; i <= 279; i++) litlen_lens[i] = 7;
    for(i = 280; i <= 287; i++) litlen_lens[i] = 8;
    for(i = 0; i < 32; i++) dist_lens[i] = 5;

    if(huff_build(&litlen_tree, litlen_lens, 288) < 0)
      return -1;
    if(huff_build(&dist_tree, dist_lens, 32) < 0)
      return -1;
    ready = 1;
  }

  return inflate_codes(bs, &litlen_tree, &dist_tree, out, win, wpos, crc, usize);
}

static int
inflate_dynamic(struct bit_stream *bs,
                struct out_stream *out,
                uchar *win,
                uint *wpos,
                uint *crc,
                uint *usize)
{
  static const uchar cl_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
  };
  uchar *cl_lens;
  uchar *ll_lens;
  struct huff_tree *cl_tree;
  struct huff_tree *ll_tree;
  struct huff_tree *dist_tree;
  uint hlit_raw;
  uint hdist_raw;
  uint hclen_raw;
  int hlit;
  int hdist;
  int hclen;
  int total;
  int i;

  cl_lens = gzip_dyn_cl_lens;
  ll_lens = gzip_dyn_ll_lens;
  cl_tree = &gzip_dyn_cl_tree;
  ll_tree = &gzip_dyn_ll_tree;
  dist_tree = &gzip_dyn_dist_tree;

  if(bits_read(bs, 5, &hlit_raw) < 0) return -1;
  if(bits_read(bs, 5, &hdist_raw) < 0) return -1;
  if(bits_read(bs, 4, &hclen_raw) < 0) return -1;

  hlit = (int)hlit_raw + 257;
  hdist = (int)hdist_raw + 1;
  hclen = (int)hclen_raw + 4;
  total = hlit + hdist;

  if(hlit > 286 || hdist > 32)
    return -1;

  for(i = 0; i < 19; i++)
    cl_lens[i] = 0;

  for(i = 0; i < hclen; i++) {
    uint v;
    if(bits_read(bs, 3, &v) < 0)
      return -1;
    cl_lens[cl_order[i]] = (uchar)v;
  }

  if(huff_build(cl_tree, cl_lens, 19) < 0)
    return -1;

  for(i = 0; i < total; ) {
    int sym;

    if(huff_decode(bs, cl_tree, &sym) < 0)
      return -1;

    if(sym <= 15) {
      ll_lens[i++] = (uchar)sym;
    } else if(sym == 16) {
      uint rep;
      uchar prev;
      int k;

      if(i == 0)
        return -1;
      if(bits_read(bs, 2, &rep) < 0)
        return -1;
      rep += 3;
      prev = ll_lens[i - 1];
      for(k = 0; k < (int)rep && i < total; k++)
        ll_lens[i++] = prev;
    } else if(sym == 17) {
      uint rep;
      int k;

      if(bits_read(bs, 3, &rep) < 0)
        return -1;
      rep += 3;
      for(k = 0; k < (int)rep && i < total; k++)
        ll_lens[i++] = 0;
    } else if(sym == 18) {
      uint rep;
      int k;

      if(bits_read(bs, 7, &rep) < 0)
        return -1;
      rep += 11;
      for(k = 0; k < (int)rep && i < total; k++)
        ll_lens[i++] = 0;
    } else {
      return -1;
    }
  }

  if(huff_build(ll_tree, ll_lens, hlit) < 0)
    return -1;
  if(huff_build(dist_tree, ll_lens + hlit, hdist) < 0)
    return -1;

  return inflate_codes(bs, ll_tree, dist_tree, out, win, wpos, crc, usize);
}

int
aux_gzip_inflate_fd(int in_fd, int out_fd)
{
  struct in_stream in;
  struct out_stream out;
  struct bit_stream bs;
  uchar trailer[8];
  uchar *win;
  uint crc;
  uint usize;
  uint got_crc;
  uint got_size;
  uint wpos;
  uint last;
  int first_member;

  errno = 0;

  aux_crc32_init();
  win = gzip_member_window;

  in.fd = in_fd;
  in.pos = 0;
  in.len = 0;

  out.fd = out_fd;
  out.len = 0;

  bs.in = &in;
  bs.bitbuf = 0;
  bs.bitcnt = 0;

  first_member = 1;
  while(1) {
    int c;

    while(1) {
      c = in_next_byte(&in);
      if(c < 0)
        break;
      if(c != 0)
        break;
    }
    if(c < 0) {
      if(first_member) {
        errno = ENODATA;
        return -1;
      }
      break;
    }
    if(c != 0x1f) {
      errno = EBADMSG;
      return -1;
    }
    in.pos--;

    bs.bitbuf = 0;
    bs.bitcnt = 0;
    crc = 0xffffffffU;
    usize = 0;
    wpos = 0;

    if(gzip_skip_header(&in) < 0) {
      if(errno == 0)
        errno = EBADMSG;
      return -1;
    }

    do {
      uint btype;

      if(bits_read(&bs, 1, &last) < 0) {
        if(errno == 0)
          errno = EILSEQ;
        return -1;
      }
      if(bits_read(&bs, 2, &btype) < 0) {
        if(errno == 0)
          errno = EILSEQ;
        return -1;
      }

      if(btype == 0) {
        if(inflate_stored(&bs, &out, win, &wpos, &crc, &usize) < 0) {
          if(errno == 0)
            errno = EILSEQ;
          return -1;
        }
      } else if(btype == 1) {
        if(inflate_fixed(&bs, &out, win, &wpos, &crc, &usize) < 0) {
          if(errno == 0)
            errno = EILSEQ;
          return -1;
        }
      } else if(btype == 2) {
        if(inflate_dynamic(&bs, &out, win, &wpos, &crc, &usize) < 0) {
          if(errno == 0)
            errno = EILSEQ;
          return -1;
        }
      } else {
        errno = EILSEQ;
        return -1;
      }
    } while(!last);

    bits_align_byte(&bs);
    if(in_read_bytes(&in, trailer, sizeof(trailer)) < 0) {
      if(errno == 0)
        errno = EILSEQ;
      return -1;
    }

    got_crc = (uint)trailer[0] |
              ((uint)trailer[1] << 8) |
              ((uint)trailer[2] << 16) |
              ((uint)trailer[3] << 24);
    got_size = (uint)trailer[4] |
               ((uint)trailer[5] << 8) |
               ((uint)trailer[6] << 16) |
               ((uint)trailer[7] << 24);

    crc = aux_crc32_finish(crc);

    if(got_crc != crc) {
      errno = EBADMSG;
      return -1;
    }
    if(got_size != (usize & 0xffffffffU)) {
      errno = EBADMSG;
      return -1;
    }

    first_member = 0;
  }

  if(out_flush(&out) < 0) {
    if(errno == 0)
      errno = EIO;
    return -1;
  }

  return 0;
}

int
aux_gzip_has_suffix(const char *path)
{
  int n;

  if(path == 0)
    return 0;

  n = strlen(path);
  if(n >= 3 && strcmp(path + n - 3, ".gz") == 0)
    return 1;
  if(n >= 4 && strcmp(path + n - 4, ".tgz") == 0)
    return 1;
  return 0;
}

int
aux_gzip_output_name(const char *in_path, char *out, int out_sz)
{
  int n;

  if(in_path == 0 || out == 0 || out_sz <= 0)
    return -1;

  n = strlen(in_path);
  if(n >= 3 && strcmp(in_path + n - 3, ".gz") == 0) {
    if(n - 3 + 1 > out_sz)
      return -1;
    memmove(out, in_path, n - 3);
    out[n - 3] = 0;
    return 0;
  }

  if(n >= 4 && strcmp(in_path + n - 4, ".tgz") == 0) {
    if(n + 1 > out_sz)
      return -1;
    memmove(out, in_path, n - 4);
    memmove(out + (n - 4), ".tar", 5);
    return 0;
  }

  if(n + 4 + 1 > out_sz)
    return -1;
  memmove(out, in_path, n);
  memmove(out + n, ".out", 5);
  return 0;
}

int
aux_gzip_deflate_store_fd(int in_fd, int out_fd)
{
  static const uchar gzip_hdr[10] = {
    0x1f, 0x8b, 0x08, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x03
  };
  uchar buf[GZIP_STORE_CHUNK];
  uchar next_buf[GZIP_STORE_CHUNK];
  uint crc;
  uint usize;
  int n;
  int i;

  aux_crc32_init();

  if(gzip_write_all(out_fd, gzip_hdr, sizeof(gzip_hdr)) < 0)
    return -1;

  crc = 0xffffffffU;
  usize = 0;

  n = read(in_fd, buf, sizeof(buf));
  if(n < 0)
    return -1;

  if(n == 0) {
    uchar empty_blk[5];

    empty_blk[0] = 0x01;
    empty_blk[1] = 0x00;
    empty_blk[2] = 0x00;
    empty_blk[3] = 0xff;
    empty_blk[4] = 0xff;
    if(gzip_write_all(out_fd, empty_blk, sizeof(empty_blk)) < 0)
      return -1;
  }

  while(n > 0) {
    uchar blk_hdr[5];
    uint nlen;
    int nn;
    int final;

    nn = read(in_fd, next_buf, sizeof(next_buf));
    if(nn < 0)
      return -1;
    final = (nn == 0);

    blk_hdr[0] = final ? 0x01 : 0x00;
    blk_hdr[1] = (uchar)(n & 0xff);
    blk_hdr[2] = (uchar)((n >> 8) & 0xff);
    nlen = ((uint)n) ^ 0xffffU;
    blk_hdr[3] = (uchar)(nlen & 0xff);
    blk_hdr[4] = (uchar)((nlen >> 8) & 0xff);

    if(gzip_write_all(out_fd, blk_hdr, sizeof(blk_hdr)) < 0)
      return -1;
    if(gzip_write_all(out_fd, buf, n) < 0)
      return -1;

    for(i = 0; i < n; i++)
      crc = aux_crc32_update_byte(crc, buf[i]);
    usize += (uint)n;

    if(final)
      break;
    memmove(buf, next_buf, (uint)nn);
    n = nn;
  }

  crc = aux_crc32_finish(crc);

  {
    uchar tr[8];
    tr[0] = (uchar)(crc & 0xff);
    tr[1] = (uchar)((crc >> 8) & 0xff);
    tr[2] = (uchar)((crc >> 16) & 0xff);
    tr[3] = (uchar)((crc >> 24) & 0xff);
    tr[4] = (uchar)(usize & 0xff);
    tr[5] = (uchar)((usize >> 8) & 0xff);
    tr[6] = (uchar)((usize >> 16) & 0xff);
    tr[7] = (uchar)((usize >> 24) & 0xff);
    if(gzip_write_all(out_fd, tr, sizeof(tr)) < 0)
      return -1;
  }

  return 0;
}
