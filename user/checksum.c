#include "types.h"
#include "auxv6/user.h"
#include "stdint.h"
#include "string.h"
#include "checksum.h"

#define AUX_HASH_READ_CHUNK 1024

static uint32_t
rol32(uint32_t v, int n)
{
  return (v << n) | (v >> (32 - n));
}

static uint32_t
ror32(uint32_t v, int n)
{
  return (v >> n) | (v << (32 - n));
}

static uint64_t
ror64(uint64_t v, int n)
{
  return (v >> n) | (v << (64 - n));
}

/* ---------------- MD5 ---------------- */

typedef struct {
  uint32_t a, b, c, d;
  uint64_t total;
  uint8_t buf[64];
  int blen;
} md5_ctx;

static const uint32_t md5_k[64] = {
  0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU,
  0xf57c0fafU, 0x4787c62aU, 0xa8304613U, 0xfd469501U,
  0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU,
  0x6b901122U, 0xfd987193U, 0xa679438eU, 0x49b40821U,
  0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU,
  0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U,
  0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU,
  0xa9e3e905U, 0xfcefa3f8U, 0x676f02d9U, 0x8d2a4c8aU,
  0xfffa3942U, 0x8771f681U, 0x6d9d6122U, 0xfde5380cU,
  0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U,
  0x289b7ec6U, 0xeaa127faU, 0xd4ef3085U, 0x04881d05U,
  0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U,
  0xf4292244U, 0x432aff97U, 0xab9423a7U, 0xfc93a039U,
  0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU, 0x85845dd1U,
  0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U,
  0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U,
};

static const uint8_t md5_r[64] = {
  7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
  5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
  4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
  6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21,
};

static void
md5_block(md5_ctx *c, const uint8_t *blk)
{
  uint32_t a = c->a, b = c->b, cc = c->c, d = c->d;
  uint32_t m[16];
  int i;

  for(i = 0; i < 16; i++) {
    m[i] = (uint32_t)blk[i*4] |
           ((uint32_t)blk[i*4 + 1] << 8) |
           ((uint32_t)blk[i*4 + 2] << 16) |
           ((uint32_t)blk[i*4 + 3] << 24);
  }

  for(i = 0; i < 64; i++) {
    uint32_t f, g, t;

    if(i < 16) {
      f = (b & cc) | ((~b) & d);
      g = (uint32_t)i;
    } else if(i < 32) {
      f = (d & b) | ((~d) & cc);
      g = (uint32_t)((5*i + 1) & 15);
    } else if(i < 48) {
      f = b ^ cc ^ d;
      g = (uint32_t)((3*i + 5) & 15);
    } else {
      f = cc ^ (b | (~d));
      g = (uint32_t)((7*i) & 15);
    }

    t = d;
    d = cc;
    cc = b;
    b = b + rol32(a + f + md5_k[i] + m[g], md5_r[i]);
    a = t;
  }

  c->a += a;
  c->b += b;
  c->c += cc;
  c->d += d;
}

static void
md5_init(md5_ctx *c)
{
  c->a = 0x67452301U;
  c->b = 0xefcdab89U;
  c->c = 0x98badcfeU;
  c->d = 0x10325476U;
  c->total = 0;
  c->blen = 0;
}

static void
md5_update(md5_ctx *c, const uint8_t *p, int n)
{
  int i = 0;

  c->total += (uint64_t)n;

  if(c->blen) {
    while(i < n && c->blen < 64)
      c->buf[c->blen++] = p[i++];
    if(c->blen == 64) {
      md5_block(c, c->buf);
      c->blen = 0;
    }
  }

  while(i + 64 <= n) {
    md5_block(c, p + i);
    i += 64;
  }

  while(i < n)
    c->buf[c->blen++] = p[i++];
}

static void
md5_final(md5_ctx *c, uint8_t out[16])
{
  uint64_t bits = c->total * 8;
  int i;

  c->buf[c->blen++] = 0x80;
  if(c->blen > 56) {
    while(c->blen < 64)
      c->buf[c->blen++] = 0;
    md5_block(c, c->buf);
    c->blen = 0;
  }
  while(c->blen < 56)
    c->buf[c->blen++] = 0;

  for(i = 0; i < 8; i++)
    c->buf[56 + i] = (uint8_t)(bits >> (8 * i));
  md5_block(c, c->buf);

  out[0] = (uint8_t)(c->a);
  out[1] = (uint8_t)(c->a >> 8);
  out[2] = (uint8_t)(c->a >> 16);
  out[3] = (uint8_t)(c->a >> 24);
  out[4] = (uint8_t)(c->b);
  out[5] = (uint8_t)(c->b >> 8);
  out[6] = (uint8_t)(c->b >> 16);
  out[7] = (uint8_t)(c->b >> 24);
  out[8] = (uint8_t)(c->c);
  out[9] = (uint8_t)(c->c >> 8);
  out[10] = (uint8_t)(c->c >> 16);
  out[11] = (uint8_t)(c->c >> 24);
  out[12] = (uint8_t)(c->d);
  out[13] = (uint8_t)(c->d >> 8);
  out[14] = (uint8_t)(c->d >> 16);
  out[15] = (uint8_t)(c->d >> 24);
}

/* ---------------- SHA-1 ---------------- */

typedef struct {
  uint32_t h[5];
  uint64_t total;
  uint8_t buf[64];
  int blen;
} sha1_ctx;

static void
sha1_block(sha1_ctx *c, const uint8_t *blk)
{
  uint32_t w[80];
  uint32_t a, b, cc, d, e;
  int t;

  for(t = 0; t < 16; t++) {
    w[t] = ((uint32_t)blk[t*4] << 24) |
           ((uint32_t)blk[t*4 + 1] << 16) |
           ((uint32_t)blk[t*4 + 2] << 8) |
           (uint32_t)blk[t*4 + 3];
  }
  for(t = 16; t < 80; t++)
    w[t] = rol32(w[t-3] ^ w[t-8] ^ w[t-14] ^ w[t-16], 1);

  a = c->h[0];
  b = c->h[1];
  cc = c->h[2];
  d = c->h[3];
  e = c->h[4];

  for(t = 0; t < 80; t++) {
    uint32_t f, k, tmp;

    if(t < 20) {
      f = (b & cc) | ((~b) & d);
      k = 0x5a827999U;
    } else if(t < 40) {
      f = b ^ cc ^ d;
      k = 0x6ed9eba1U;
    } else if(t < 60) {
      f = (b & cc) | (b & d) | (cc & d);
      k = 0x8f1bbcdcU;
    } else {
      f = b ^ cc ^ d;
      k = 0xca62c1d6U;
    }

    tmp = rol32(a, 5) + f + e + k + w[t];
    e = d;
    d = cc;
    cc = rol32(b, 30);
    b = a;
    a = tmp;
  }

  c->h[0] += a;
  c->h[1] += b;
  c->h[2] += cc;
  c->h[3] += d;
  c->h[4] += e;
}

static void
sha1_init(sha1_ctx *c)
{
  c->h[0] = 0x67452301U;
  c->h[1] = 0xefcdab89U;
  c->h[2] = 0x98badcfeU;
  c->h[3] = 0x10325476U;
  c->h[4] = 0xc3d2e1f0U;
  c->total = 0;
  c->blen = 0;
}

static void
sha1_update(sha1_ctx *c, const uint8_t *p, int n)
{
  int i = 0;

  c->total += (uint64_t)n;

  if(c->blen) {
    while(i < n && c->blen < 64)
      c->buf[c->blen++] = p[i++];
    if(c->blen == 64) {
      sha1_block(c, c->buf);
      c->blen = 0;
    }
  }

  while(i + 64 <= n) {
    sha1_block(c, p + i);
    i += 64;
  }

  while(i < n)
    c->buf[c->blen++] = p[i++];
}

static void
sha1_final(sha1_ctx *c, uint8_t out[20])
{
  uint64_t bits = c->total * 8;
  int i;

  c->buf[c->blen++] = 0x80;
  if(c->blen > 56) {
    while(c->blen < 64)
      c->buf[c->blen++] = 0;
    sha1_block(c, c->buf);
    c->blen = 0;
  }
  while(c->blen < 56)
    c->buf[c->blen++] = 0;

  for(i = 0; i < 8; i++)
    c->buf[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
  sha1_block(c, c->buf);

  for(i = 0; i < 5; i++) {
    out[i*4] = (uint8_t)(c->h[i] >> 24);
    out[i*4 + 1] = (uint8_t)(c->h[i] >> 16);
    out[i*4 + 2] = (uint8_t)(c->h[i] >> 8);
    out[i*4 + 3] = (uint8_t)(c->h[i]);
  }
}

/* ---------------- SHA-224 / SHA-256 ---------------- */

typedef struct {
  uint32_t h[8];
  uint64_t total;
  uint8_t buf[64];
  int blen;
} sha256_ctx;

static const uint32_t sha256_k[64] = {
  0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
  0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
  0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
  0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
  0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
  0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
  0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
  0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
  0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
  0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
  0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
  0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
  0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
  0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
  0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
  0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

static void
sha256_init(sha256_ctx *c, int is224)
{
  if(is224) {
    c->h[0] = 0xc1059ed8U;
    c->h[1] = 0x367cd507U;
    c->h[2] = 0x3070dd17U;
    c->h[3] = 0xf70e5939U;
    c->h[4] = 0xffc00b31U;
    c->h[5] = 0x68581511U;
    c->h[6] = 0x64f98fa7U;
    c->h[7] = 0xbefa4fa4U;
  } else {
    c->h[0] = 0x6a09e667U;
    c->h[1] = 0xbb67ae85U;
    c->h[2] = 0x3c6ef372U;
    c->h[3] = 0xa54ff53aU;
    c->h[4] = 0x510e527fU;
    c->h[5] = 0x9b05688cU;
    c->h[6] = 0x1f83d9abU;
    c->h[7] = 0x5be0cd19U;
  }
  c->total = 0;
  c->blen = 0;
}

static void
sha256_block(sha256_ctx *c, const uint8_t *blk)
{
  uint32_t w[64];
  uint32_t a, b, cc, d, e, f, g, h;
  int t;

  for(t = 0; t < 16; t++) {
    w[t] = ((uint32_t)blk[t*4] << 24) |
           ((uint32_t)blk[t*4 + 1] << 16) |
           ((uint32_t)blk[t*4 + 2] << 8) |
           (uint32_t)blk[t*4 + 3];
  }
  for(t = 16; t < 64; t++) {
    uint32_t s0 = ror32(w[t-15], 7) ^ ror32(w[t-15], 18) ^ (w[t-15] >> 3);
    uint32_t s1 = ror32(w[t-2], 17) ^ ror32(w[t-2], 19) ^ (w[t-2] >> 10);
    w[t] = w[t-16] + s0 + w[t-7] + s1;
  }

  a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
  e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];

  for(t = 0; t < 64; t++) {
    uint32_t S1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
    uint32_t ch = (e & f) ^ ((~e) & g);
    uint32_t temp1 = h + S1 + ch + sha256_k[t] + w[t];
    uint32_t S0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
    uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
    uint32_t temp2 = S0 + maj;

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = cc;
    cc = b;
    b = a;
    a = temp1 + temp2;
  }

  c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
  c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

static void
sha256_update(sha256_ctx *c, const uint8_t *p, int n)
{
  int i = 0;

  c->total += (uint64_t)n;

  if(c->blen) {
    while(i < n && c->blen < 64)
      c->buf[c->blen++] = p[i++];
    if(c->blen == 64) {
      sha256_block(c, c->buf);
      c->blen = 0;
    }
  }

  while(i + 64 <= n) {
    sha256_block(c, p + i);
    i += 64;
  }

  while(i < n)
    c->buf[c->blen++] = p[i++];
}

static void
sha256_final(sha256_ctx *c, uint8_t *out, int out_words)
{
  uint64_t bits = c->total * 8;
  int i;

  c->buf[c->blen++] = 0x80;
  if(c->blen > 56) {
    while(c->blen < 64)
      c->buf[c->blen++] = 0;
    sha256_block(c, c->buf);
    c->blen = 0;
  }
  while(c->blen < 56)
    c->buf[c->blen++] = 0;

  for(i = 0; i < 8; i++)
    c->buf[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
  sha256_block(c, c->buf);

  for(i = 0; i < out_words; i++) {
    out[i*4] = (uint8_t)(c->h[i] >> 24);
    out[i*4 + 1] = (uint8_t)(c->h[i] >> 16);
    out[i*4 + 2] = (uint8_t)(c->h[i] >> 8);
    out[i*4 + 3] = (uint8_t)c->h[i];
  }
}

/* ---------------- SHA-384 / SHA-512 ---------------- */

typedef struct {
  uint64_t h[8];
  uint64_t total_hi;
  uint64_t total_lo;
  uint8_t buf[128];
  int blen;
} sha512_ctx;

static const uint64_t sha512_k[80] = {
  0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL,
  0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
  0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
  0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
  0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
  0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
  0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL,
  0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
  0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
  0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
  0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL,
  0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
  0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL,
  0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
  0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
  0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
  0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL,
  0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
  0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL,
  0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
  0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
  0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
  0xd192e819d6ef5218ULL, 0xd69906245565a910ULL,
  0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
  0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
  0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
  0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
  0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
  0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL,
  0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
  0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL,
  0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
  0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
  0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
  0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
  0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
  0x28db77f523047d84ULL, 0x32caab7b40c72493ULL,
  0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
  0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
  0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

static void
sha512_init(sha512_ctx *c, int is384)
{
  if(is384) {
    c->h[0] = 0xcbbb9d5dc1059ed8ULL;
    c->h[1] = 0x629a292a367cd507ULL;
    c->h[2] = 0x9159015a3070dd17ULL;
    c->h[3] = 0x152fecd8f70e5939ULL;
    c->h[4] = 0x67332667ffc00b31ULL;
    c->h[5] = 0x8eb44a8768581511ULL;
    c->h[6] = 0xdb0c2e0d64f98fa7ULL;
    c->h[7] = 0x47b5481dbefa4fa4ULL;
  } else {
    c->h[0] = 0x6a09e667f3bcc908ULL;
    c->h[1] = 0xbb67ae8584caa73bULL;
    c->h[2] = 0x3c6ef372fe94f82bULL;
    c->h[3] = 0xa54ff53a5f1d36f1ULL;
    c->h[4] = 0x510e527fade682d1ULL;
    c->h[5] = 0x9b05688c2b3e6c1fULL;
    c->h[6] = 0x1f83d9abfb41bd6bULL;
    c->h[7] = 0x5be0cd19137e2179ULL;
  }
  c->total_hi = 0;
  c->total_lo = 0;
  c->blen = 0;
}

static void
sha512_add_len(sha512_ctx *c, uint64_t add)
{
  uint64_t old = c->total_lo;
  c->total_lo += add;
  if(c->total_lo < old)
    c->total_hi++;
}

static void
sha512_block(sha512_ctx *c, const uint8_t *blk)
{
  uint64_t w[80];
  uint64_t a, b, cc, d, e, f, g, h;
  int t;

  for(t = 0; t < 16; t++) {
    int i = t * 8;
    w[t] = ((uint64_t)blk[i] << 56) |
           ((uint64_t)blk[i + 1] << 48) |
           ((uint64_t)blk[i + 2] << 40) |
           ((uint64_t)blk[i + 3] << 32) |
           ((uint64_t)blk[i + 4] << 24) |
           ((uint64_t)blk[i + 5] << 16) |
           ((uint64_t)blk[i + 6] << 8) |
           (uint64_t)blk[i + 7];
  }
  for(t = 16; t < 80; t++) {
    uint64_t s0 = ror64(w[t-15], 1) ^ ror64(w[t-15], 8) ^ (w[t-15] >> 7);
    uint64_t s1 = ror64(w[t-2], 19) ^ ror64(w[t-2], 61) ^ (w[t-2] >> 6);
    w[t] = w[t-16] + s0 + w[t-7] + s1;
  }

  a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
  e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];

  for(t = 0; t < 80; t++) {
    uint64_t S1 = ror64(e, 14) ^ ror64(e, 18) ^ ror64(e, 41);
    uint64_t ch = (e & f) ^ ((~e) & g);
    uint64_t temp1 = h + S1 + ch + sha512_k[t] + w[t];
    uint64_t S0 = ror64(a, 28) ^ ror64(a, 34) ^ ror64(a, 39);
    uint64_t maj = (a & b) ^ (a & cc) ^ (b & cc);
    uint64_t temp2 = S0 + maj;

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = cc;
    cc = b;
    b = a;
    a = temp1 + temp2;
  }

  c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
  c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

static void
sha512_update(sha512_ctx *c, const uint8_t *p, int n)
{
  int i = 0;

  sha512_add_len(c, (uint64_t)n);

  if(c->blen) {
    while(i < n && c->blen < 128)
      c->buf[c->blen++] = p[i++];
    if(c->blen == 128) {
      sha512_block(c, c->buf);
      c->blen = 0;
    }
  }

  while(i + 128 <= n) {
    sha512_block(c, p + i);
    i += 128;
  }

  while(i < n)
    c->buf[c->blen++] = p[i++];
}

static void
sha512_final(sha512_ctx *c, uint8_t *out, int out_words)
{
  uint64_t bit_hi, bit_lo;
  int i;

  bit_hi = (c->total_hi << 3) | (c->total_lo >> 61);
  bit_lo = c->total_lo << 3;

  c->buf[c->blen++] = 0x80;
  if(c->blen > 112) {
    while(c->blen < 128)
      c->buf[c->blen++] = 0;
    sha512_block(c, c->buf);
    c->blen = 0;
  }
  while(c->blen < 112)
    c->buf[c->blen++] = 0;

  for(i = 0; i < 8; i++)
    c->buf[112 + i] = (uint8_t)(bit_hi >> (56 - 8 * i));
  for(i = 0; i < 8; i++)
    c->buf[120 + i] = (uint8_t)(bit_lo >> (56 - 8 * i));

  sha512_block(c, c->buf);

  for(i = 0; i < out_words; i++) {
    out[i*8] = (uint8_t)(c->h[i] >> 56);
    out[i*8 + 1] = (uint8_t)(c->h[i] >> 48);
    out[i*8 + 2] = (uint8_t)(c->h[i] >> 40);
    out[i*8 + 3] = (uint8_t)(c->h[i] >> 32);
    out[i*8 + 4] = (uint8_t)(c->h[i] >> 24);
    out[i*8 + 5] = (uint8_t)(c->h[i] >> 16);
    out[i*8 + 6] = (uint8_t)(c->h[i] >> 8);
    out[i*8 + 7] = (uint8_t)(c->h[i]);
  }
}

/* ---------------- Public API ---------------- */

int
aux_hash_digest_len(enum aux_hash_algo algo)
{
  switch(algo) {
  case AUX_HASH_MD5: return AUX_MD5_DIGEST_LEN;
  case AUX_HASH_SHA1: return AUX_SHA1_DIGEST_LEN;
  case AUX_HASH_SHA224: return AUX_SHA224_DIGEST_LEN;
  case AUX_HASH_SHA256: return AUX_SHA256_DIGEST_LEN;
  case AUX_HASH_SHA384: return AUX_SHA384_DIGEST_LEN;
  case AUX_HASH_SHA512: return AUX_SHA512_DIGEST_LEN;
  }
  return -1;
}

void
aux_hex_encode(const uint8_t *in, int len, char *out)
{
  static const char *hex = "0123456789abcdef";
  int i;

  for(i = 0; i < len; i++) {
    out[i*2] = hex[in[i] >> 4];
    out[i*2 + 1] = hex[in[i] & 0x0f];
  }
  out[len * 2] = 0;
}

int
aux_hash_stream(int fd, enum aux_hash_algo algo, uint8_t *digest, int digest_cap)
{
  uint8_t buf[AUX_HASH_READ_CHUNK];
  int n;
  int need;

  md5_ctx md5;
  sha1_ctx sha1;
  sha256_ctx sha256;
  sha512_ctx sha512;

  need = aux_hash_digest_len(algo);
  if(need < 0 || digest == 0 || digest_cap < need)
    return -1;

  switch(algo) {
  case AUX_HASH_MD5:
    md5_init(&md5);
    break;
  case AUX_HASH_SHA1:
    sha1_init(&sha1);
    break;
  case AUX_HASH_SHA224:
    sha256_init(&sha256, 1);
    break;
  case AUX_HASH_SHA256:
    sha256_init(&sha256, 0);
    break;
  case AUX_HASH_SHA384:
    sha512_init(&sha512, 1);
    break;
  case AUX_HASH_SHA512:
    sha512_init(&sha512, 0);
    break;
  }

  while((n = read(fd, buf, sizeof(buf))) > 0) {
    switch(algo) {
    case AUX_HASH_MD5:
      md5_update(&md5, buf, n);
      break;
    case AUX_HASH_SHA1:
      sha1_update(&sha1, buf, n);
      break;
    case AUX_HASH_SHA224:
    case AUX_HASH_SHA256:
      sha256_update(&sha256, buf, n);
      break;
    case AUX_HASH_SHA384:
    case AUX_HASH_SHA512:
      sha512_update(&sha512, buf, n);
      break;
    }
  }
  if(n < 0)
    return -1;

  switch(algo) {
  case AUX_HASH_MD5:
    md5_final(&md5, digest);
    break;
  case AUX_HASH_SHA1:
    sha1_final(&sha1, digest);
    break;
  case AUX_HASH_SHA224:
    sha256_final(&sha256, digest, 7);
    break;
  case AUX_HASH_SHA256:
    sha256_final(&sha256, digest, 8);
    break;
  case AUX_HASH_SHA384:
    sha512_final(&sha512, digest, 6);
    break;
  case AUX_HASH_SHA512:
    sha512_final(&sha512, digest, 8);
    break;
  }

  return need;
}
