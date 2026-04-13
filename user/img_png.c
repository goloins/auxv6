#include "types.h"
#include "auxv6/user.h"
#include "auxv6/img_png.h"
#include "checksum.h"
#include "string.h"
#include "stdint.h"

#define PNG_HUFF_MAXBITS 15
#define PNG_HUFF_MAX_SYMS 288
#define PNG_HUFF_MAX_NODES (PNG_HUFF_MAX_SYMS * PNG_HUFF_MAXBITS + 1)

struct png_inflate {
  const uchar *src;
  size_t len;
  size_t pos;
  uint bitbuf;
  int bitcnt;
};

struct png_huff_tree {
  short left[PNG_HUFF_MAX_NODES];
  short right[PNG_HUFF_MAX_NODES];
  short sym[PNG_HUFF_MAX_NODES];
  int nodes;
};

static const int png_len_base[29] = {
  3, 4, 5, 6, 7, 8, 9, 10,
  11, 13, 15, 17, 19, 23, 27, 31,
  35, 43, 51, 59, 67, 83, 99, 115,
  131, 163, 195, 227, 258
};

static const int png_len_extra[29] = {
  0, 0, 0, 0, 0, 0, 0, 0,
  1, 1, 1, 1, 2, 2, 2, 2,
  3, 3, 3, 3, 4, 4, 4, 4,
  5, 5, 5, 5, 0
};

static const int png_dist_base[30] = {
  1, 2, 3, 4, 5, 7, 9, 13, 17, 25,
  33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
  1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};

static const int png_dist_extra[30] = {
  0, 0, 0, 0, 1, 1, 2, 2, 3, 3,
  4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
  9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static uint32_t
png_read_be32(const uchar *p)
{
  return ((uint32_t)p[0] << 24) |
         ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) |
         ((uint32_t)p[3]);
}

static int
png_bits_read_bit(struct png_inflate *bs)
{
  if(bs->bitcnt == 0) {
    if(bs->pos >= bs->len)
      return -1;
    bs->bitbuf = (uint)bs->src[bs->pos++];
    bs->bitcnt = 8;
  }

  {
    int c = (int)(bs->bitbuf & 1U);
    bs->bitbuf >>= 1;
    bs->bitcnt--;
    return c;
  }
}

static int
png_bits_read(struct png_inflate *bs, int n, uint *out)
{
  int i;
  uint v;

  v = 0;
  for(i = 0; i < n; i++) {
    int b = png_bits_read_bit(bs);
    if(b < 0)
      return -1;
    v |= ((uint)b << i);
  }

  *out = v;
  return 0;
}

static void
png_bits_align_byte(struct png_inflate *bs)
{
  bs->bitbuf = 0;
  bs->bitcnt = 0;
}

static void
png_huff_reset(struct png_huff_tree *t)
{
  int i;

  for(i = 0; i < PNG_HUFF_MAX_NODES; i++) {
    t->left[i] = -1;
    t->right[i] = -1;
    t->sym[i] = -1;
  }
  t->nodes = 1;
}

static int
png_huff_insert(struct png_huff_tree *t, uint code, int bits, int sym)
{
  int node;
  int i;

  node = 0;
  for(i = bits - 1; i >= 0; i--) {
    int dir;
    short *next;

    dir = (int)((code >> i) & 1U);
    next = dir ? &t->right[node] : &t->left[node];

    if(*next < 0) {
      if(t->nodes >= PNG_HUFF_MAX_NODES)
        return -1;
      *next = (short)t->nodes;
      t->nodes++;
    }

    node = *next;
  }

  if(t->sym[node] >= 0)
    return -1;
  t->sym[node] = (short)sym;
  return 0;
}

static int
png_huff_build(struct png_huff_tree *t, const uchar *lens, int count)
{
  int i;
  int bits;
  int maxbits;
  int next_code[PNG_HUFF_MAXBITS + 1];
  int bl_count[PNG_HUFF_MAXBITS + 1];
  int code;

  png_huff_reset(t);
  memset(bl_count, 0, sizeof(bl_count));

  maxbits = 0;
  for(i = 0; i < count; i++) {
    bits = lens[i];
    if(bits < 0 || bits > PNG_HUFF_MAXBITS)
      return -1;
    if(bits == 0)
      continue;
    bl_count[bits]++;
    if(bits > maxbits)
      maxbits = bits;
  }

  code = 0;
  bl_count[0] = 0;
  for(bits = 1; bits <= PNG_HUFF_MAXBITS; bits++) {
    code = (code + bl_count[bits - 1]) << 1;
    next_code[bits] = code;
  }

  for(i = 0; i < count; i++) {
    uint c;
    bits = lens[i];
    if(bits == 0)
      continue;
    c = (uint)next_code[bits]++;
    if(png_huff_insert(t, c, bits, i) < 0)
      return -1;
  }

  return maxbits ? 0 : -1;
}

static int
png_huff_decode(struct png_inflate *bs, struct png_huff_tree *t)
{
  int node;

  node = 0;
  while(1) {
    int b;
    int next;

    if(t->sym[node] >= 0)
      return t->sym[node];

    b = png_bits_read_bit(bs);
    if(b < 0)
      return -1;

    next = b ? t->right[node] : t->left[node];
    if(next < 0)
      return -1;
    node = next;
  }
}

static int
png_out_append(uchar **dst, size_t *cap, size_t *len, uchar b)
{
  if(*len >= *cap) {
    size_t ncap;
    uchar *nbuf;

    ncap = (*cap == 0) ? 4096 : (*cap * 2);
    nbuf = (uchar*)malloc(ncap);
    if(!nbuf)
      return -1;
    if(*dst && *len)
      memmove(nbuf, *dst, *len);
    if(*dst)
      free(*dst);
    *dst = nbuf;
    *cap = ncap;
  }

  (*dst)[(*len)++] = b;
  return 0;
}

static int
png_inflate_stored(struct png_inflate *bs, uchar **out, size_t *out_cap, size_t *out_len)
{
  uint len;
  uint nlen;
  uint i;

  png_bits_align_byte(bs);

  if(bs->pos + 4 > bs->len)
    return -1;

  len = (uint)bs->src[bs->pos] | ((uint)bs->src[bs->pos + 1] << 8);
  nlen = (uint)bs->src[bs->pos + 2] | ((uint)bs->src[bs->pos + 3] << 8);
  bs->pos += 4;

  if((len ^ 0xffffU) != nlen)
    return -1;
  if(bs->pos + len > bs->len)
    return -1;

  for(i = 0; i < len; i++)
    if(png_out_append(out, out_cap, out_len, bs->src[bs->pos++]) < 0)
      return -1;

  return 0;
}

static int
png_inflate_codes(struct png_inflate *bs,
                  struct png_huff_tree *litlen,
                  struct png_huff_tree *dist,
                  uchar **out,
                  size_t *out_cap,
                  size_t *out_len)
{
  while(1) {
    int sym;

    sym = png_huff_decode(bs, litlen);
    if(sym < 0)
      return -1;

    if(sym < 256) {
      if(png_out_append(out, out_cap, out_len, (uchar)sym) < 0)
        return -1;
      continue;
    }

    if(sym == 256)
      return 0;

    if(sym > 285)
      return -1;

    {
      uint extra;
      int len;
      int dist_sym;
      int dist_len;
      uint dist_extra;
      int k;

      len = png_len_base[sym - 257];
      if(png_len_extra[sym - 257] > 0) {
        if(png_bits_read(bs, png_len_extra[sym - 257], &extra) < 0)
          return -1;
        len += (int)extra;
      }

      dist_sym = png_huff_decode(bs, dist);
      if(dist_sym < 0 || dist_sym > 29)
        return -1;

      dist_len = png_dist_base[dist_sym];
      if(png_dist_extra[dist_sym] > 0) {
        if(png_bits_read(bs, png_dist_extra[dist_sym], &dist_extra) < 0)
          return -1;
        dist_len += (int)dist_extra;
      }

      if(dist_len <= 0 || (size_t)dist_len > *out_len)
        return -1;

      for(k = 0; k < len; k++) {
        uchar b = (*out)[*out_len - (size_t)dist_len];
        if(png_out_append(out, out_cap, out_len, b) < 0)
          return -1;
      }
    }
  }
}

static int
png_inflate_fixed(struct png_inflate *bs, uchar **out, size_t *out_cap, size_t *out_len)
{
  uchar ll[288];
  uchar dl[32];
  int i;
  struct png_huff_tree litlen;
  struct png_huff_tree dist;

  for(i = 0; i <= 143; i++) ll[i] = 8;
  for(i = 144; i <= 255; i++) ll[i] = 9;
  for(i = 256; i <= 279; i++) ll[i] = 7;
  for(i = 280; i <= 287; i++) ll[i] = 8;
  for(i = 0; i < 32; i++) dl[i] = 5;

  if(png_huff_build(&litlen, ll, 288) < 0)
    return -1;
  if(png_huff_build(&dist, dl, 32) < 0)
    return -1;

  return png_inflate_codes(bs, &litlen, &dist, out, out_cap, out_len);
}

static int
png_inflate_dynamic(struct png_inflate *bs, uchar **out, size_t *out_cap, size_t *out_len)
{
  static const uchar order[19] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
  };

  uint hlit, hdist, hclen;
  uchar cl[19];
  uchar ll[320];
  int total;
  int i;
  struct png_huff_tree cl_tree;
  struct png_huff_tree ll_tree;
  struct png_huff_tree dist_tree;

  if(png_bits_read(bs, 5, &hlit) < 0)
    return -1;
  if(png_bits_read(bs, 5, &hdist) < 0)
    return -1;
  if(png_bits_read(bs, 4, &hclen) < 0)
    return -1;

  hlit += 257;
  hdist += 1;
  hclen += 4;

  memset(cl, 0, sizeof(cl));
  for(i = 0; i < (int)hclen; i++) {
    uint v;
    if(png_bits_read(bs, 3, &v) < 0)
      return -1;
    cl[order[i]] = (uchar)v;
  }

  if(png_huff_build(&cl_tree, cl, 19) < 0)
    return -1;

  total = (int)(hlit + hdist);
  for(i = 0; i < total; ) {
    int sym;

    sym = png_huff_decode(bs, &cl_tree);
    if(sym < 0)
      return -1;

    if(sym <= 15) {
      ll[i++] = (uchar)sym;
    } else if(sym == 16) {
      uint rep;
      int k;
      uchar prev;

      if(i == 0)
        return -1;
      if(png_bits_read(bs, 2, &rep) < 0)
        return -1;
      rep += 3;
      prev = ll[i - 1];
      for(k = 0; k < (int)rep && i < total; k++)
        ll[i++] = prev;
    } else if(sym == 17) {
      uint rep;
      int k;

      if(png_bits_read(bs, 3, &rep) < 0)
        return -1;
      rep += 3;
      for(k = 0; k < (int)rep && i < total; k++)
        ll[i++] = 0;
    } else if(sym == 18) {
      uint rep;
      int k;

      if(png_bits_read(bs, 7, &rep) < 0)
        return -1;
      rep += 11;
      for(k = 0; k < (int)rep && i < total; k++)
        ll[i++] = 0;
    } else {
      return -1;
    }
  }

  if(png_huff_build(&ll_tree, ll, (int)hlit) < 0)
    return -1;
  if(png_huff_build(&dist_tree, ll + hlit, (int)hdist) < 0)
    return -1;

  return png_inflate_codes(bs, &ll_tree, &dist_tree, out, out_cap, out_len);
}

static uint32_t
png_adler32(const uchar *buf, size_t len)
{
  uint32_t a = 1;
  uint32_t b = 0;
  size_t i;

  for(i = 0; i < len; i++) {
    a = (a + buf[i]) % 65521U;
    b = (b + a) % 65521U;
  }

  return (b << 16) | a;
}

static int
png_zlib_inflate(const uchar *src, size_t len, uchar **out, size_t *out_len)
{
  struct png_inflate bs;
  uchar *dst;
  size_t cap;
  size_t used;
  uint bfinal;
  uint btype;

  if(len < 6)
    return -1;

  if((src[0] & 0x0fU) != 8)
    return -1;
  if((((uint)src[0] << 8) | src[1]) % 31U != 0)
    return -1;
  if(src[1] & 0x20U)
    return -1;

  bs.src = src + 2;
  bs.len = len - 6;
  bs.pos = 0;
  bs.bitbuf = 0;
  bs.bitcnt = 0;

  dst = 0;
  cap = 0;
  used = 0;

  do {
    if(png_bits_read(&bs, 1, &bfinal) < 0)
      goto fail;
    if(png_bits_read(&bs, 2, &btype) < 0)
      goto fail;

    if(btype == 0) {
      if(png_inflate_stored(&bs, &dst, &cap, &used) < 0)
        goto fail;
    } else if(btype == 1) {
      if(png_inflate_fixed(&bs, &dst, &cap, &used) < 0)
        goto fail;
    } else if(btype == 2) {
      if(png_inflate_dynamic(&bs, &dst, &cap, &used) < 0)
        goto fail;
    } else {
      goto fail;
    }
  } while(!bfinal);

  {
    uint32_t want;
    uint32_t have;

    want = png_read_be32(src + len - 4);
    have = png_adler32(dst, used);
    if(want != have)
      goto fail;
  }

  *out = dst;
  *out_len = used;
  return 0;

fail:
  if(dst)
    free(dst);
  return -1;
}

static int
png_paeth(int a, int b, int c)
{
  int p;
  int pa;
  int pb;
  int pc;

  p = a + b - c;
  pa = p - a;
  if(pa < 0) pa = -pa;
  pb = p - b;
  if(pb < 0) pb = -pb;
  pc = p - c;
  if(pc < 0) pc = -pc;

  if(pa <= pb && pa <= pc)
    return a;
  if(pb <= pc)
    return b;
  return c;
}

int
aux_img_png_decode(const uchar *src, size_t len, struct aux_img *out)
{
  size_t pos;
  uint32_t width;
  uint32_t height;
  int bit_depth;
  int color_type;
  int interlace;
  uchar *idat;
  size_t idat_len;
  size_t idat_cap;
  uchar *raw;
  size_t raw_len;
  int bpp;
  size_t row_bytes;
  uchar *rgba;
  size_t y;

  if(!src || !out)
    return -1;

  if(len < 8 || memcmp(src, "\x89PNG\r\n\x1a\n", 8) != 0)
    return -1;

  width = 0;
  height = 0;
  bit_depth = 0;
  color_type = -1;
  interlace = 1;
  idat = 0;
  idat_len = 0;
  idat_cap = 0;
  raw = 0;
  rgba = 0;

  pos = 8;
  while(pos + 12 <= len) {
    uint32_t clen;
    uint32_t crc_want;
    uint32_t crc_have;
    const uchar *ctype;
    const uchar *cdata;
    uint32_t crc;

    clen = png_read_be32(src + pos);
    ctype = src + pos + 4;
    cdata = src + pos + 8;

    if(pos + 12 + (size_t)clen > len)
      goto fail;

    crc_want = png_read_be32(src + pos + 8 + clen);

    aux_crc32_init();
    crc = 0xffffffffU;
    crc = aux_crc32_update(crc, ctype, 4);
    crc = aux_crc32_update(crc, cdata, clen);
    crc_have = aux_crc32_finish(crc);
    if(crc_want != crc_have)
      goto fail;

    if(memcmp(ctype, "IHDR", 4) == 0) {
      if(clen != 13)
        goto fail;
      width = png_read_be32(cdata);
      height = png_read_be32(cdata + 4);
      bit_depth = cdata[8];
      color_type = cdata[9];
      interlace = cdata[12];

      if(width == 0 || height == 0)
        goto fail;
      if(bit_depth != 8)
        goto fail;
      if(!(color_type == 2 || color_type == 6))
        goto fail;
      if(cdata[10] != 0 || cdata[11] != 0)
        goto fail;
      if(interlace != 0)
        goto fail;
    } else if(memcmp(ctype, "IDAT", 4) == 0) {
      if(idat_len + clen < idat_len)
        goto fail;
      if(idat_len + clen > idat_cap) {
        size_t ncap;
        uchar *nbuf;

        ncap = idat_cap ? idat_cap : 4096;
        while(ncap < idat_len + clen)
          ncap *= 2;

        nbuf = (uchar*)malloc(ncap);
        if(!nbuf)
          goto fail;
        if(idat && idat_len)
          memmove(nbuf, idat, idat_len);
        if(idat)
          free(idat);
        idat = nbuf;
        idat_cap = ncap;
      }
      memmove(idat + idat_len, cdata, clen);
      idat_len += clen;
    } else if(memcmp(ctype, "IEND", 4) == 0) {
      break;
    }

    pos += 12 + clen;
  }

  if(width == 0 || height == 0 || idat_len == 0)
    goto fail;

  if(png_zlib_inflate(idat, idat_len, &raw, &raw_len) < 0)
    goto fail;

  bpp = (color_type == 6) ? 4 : 3;
  row_bytes = (size_t)width * (size_t)bpp;
  if(raw_len != (row_bytes + 1) * (size_t)height)
    goto fail;

  rgba = (uchar*)malloc((size_t)width * (size_t)height * 4);
  if(!rgba)
    goto fail;

  {
    uchar *prev = 0;
    uchar *cur = (uchar*)malloc(row_bytes);
    if(!cur)
      goto fail;

    for(y = 0; y < (size_t)height; y++) {
      const uchar *inrow = raw + y * (row_bytes + 1);
      int filter = inrow[0];
      size_t x;

      memmove(cur, inrow + 1, row_bytes);

      if(filter == 1) {
        for(x = bpp; x < row_bytes; x++)
          cur[x] = (uchar)(cur[x] + cur[x - bpp]);
      } else if(filter == 2) {
        if(prev)
          for(x = 0; x < row_bytes; x++)
            cur[x] = (uchar)(cur[x] + prev[x]);
      } else if(filter == 3) {
        for(x = 0; x < row_bytes; x++) {
          int a = (x >= (size_t)bpp) ? cur[x - bpp] : 0;
          int b = prev ? prev[x] : 0;
          cur[x] = (uchar)(cur[x] + ((a + b) >> 1));
        }
      } else if(filter == 4) {
        for(x = 0; x < row_bytes; x++) {
          int a = (x >= (size_t)bpp) ? cur[x - bpp] : 0;
          int b = prev ? prev[x] : 0;
          int c = (prev && x >= (size_t)bpp) ? prev[x - bpp] : 0;
          cur[x] = (uchar)(cur[x] + png_paeth(a, b, c));
        }
      } else if(filter != 0) {
        free(cur);
        goto fail;
      }

      for(x = 0; x < (size_t)width; x++) {
        size_t si = x * (size_t)bpp;
        size_t di = (y * (size_t)width + x) * 4;
        rgba[di + 0] = cur[si + 0];
        rgba[di + 1] = cur[si + 1];
        rgba[di + 2] = cur[si + 2];
        rgba[di + 3] = (bpp == 4) ? cur[si + 3] : 255;
      }

      if(prev)
        memmove(prev, cur, row_bytes);
      else {
        prev = (uchar*)malloc(row_bytes);
        if(!prev) {
          free(cur);
          goto fail;
        }
        memmove(prev, cur, row_bytes);
      }
    }

    if(prev)
      free(prev);
    free(cur);
  }

  out->width = (int)width;
  out->height = (int)height;
  out->stride = (int)width * 4;
  out->format = AUX_IMG_FMT_RGBA8;
  out->pixels = rgba;

  free(idat);
  free(raw);
  return 0;

fail:
  if(idat)
    free(idat);
  if(raw)
    free(raw);
  if(rgba)
    free(rgba);
  return -1;
}
