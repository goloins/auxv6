/*
 * user/randlib.c - Cryptographically secure random number generation
 *
 * Tranche 2: User-space arc4random family and getentropy.
 *
 * Implementations:
 * - getentropy: OpenBSD-compatible, wrapper around getrandom
 * - arc4random: ChaCha20-based CSPRNG with automatic seeding
 * - arc4random_buf: Bulk random bytes
 * - arc4random_uniform: Uniform random in [0, upper_bound)
 * - arc4random_stir: Manual reseed from kernel entropy
 */

#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "sys/random.h"
#include "stdlib.h"
#include "string.h"
#include "errno.h"
#include "time.h"

/* Forward declare kernel syscall */
extern ssize_t getrandom(void *buf, size_t buflen, unsigned int flags);

/* Arc4random global state */
typedef struct {
    uint32_t state[16];       /* ChaCha20 state (same as kernel RNG) */
    uint32_t keystream[16];   /* Current 64-byte block of keystream */
    int keystream_idx;        /* Current position in keystream block (in bytes) */
    int initialized;          /* Whether stir() has been called */
} arc4random_state;

static arc4random_state arc4_state;

/*
 * ChaCha20 quarter-round (same as kernel version)
 */
static inline void
chacha20_qr(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
  *a += *b; *d ^= *a; *d = (*d << 16) | (*d >> 16);
  *c += *d; *b ^= *c; *b = (*b << 12) | (*b >> 20);
  *a += *b; *d ^= *a; *d = (*d << 8)  | (*d >> 24);
  *c += *d; *b ^= *c; *b = (*b << 7)  | (*b >> 25);
}

/*
 * ChaCha20 block generation
 */
static void
chacha20_block_user(uint32_t *state, uint32_t *out)
{
  uint32_t x[16];
  int i;

  for(i = 0; i < 16; i++)
    x[i] = state[i];

  for(i = 0; i < 10; i++) {
    chacha20_qr(&x[0], &x[4], &x[8], &x[12]);
    chacha20_qr(&x[1], &x[5], &x[9], &x[13]);
    chacha20_qr(&x[2], &x[6], &x[10], &x[14]);
    chacha20_qr(&x[3], &x[7], &x[11], &x[15]);
    chacha20_qr(&x[0], &x[5], &x[10], &x[15]);
    chacha20_qr(&x[1], &x[6], &x[11], &x[12]);
    chacha20_qr(&x[2], &x[7], &x[8], &x[13]);
    chacha20_qr(&x[3], &x[4], &x[9], &x[14]);
  }

  for(i = 0; i < 16; i++)
    out[i] = x[i] + state[i];

  state[12]++;
  if(state[12] == 0)
    state[13]++;
}

/*
 * arc4random_stir - Reseed arc4random from kernel entropy
 */
void
arc4random_stir(void)
{
  uint8_t seed[32];
  int i;
  int ret;

  /* Get 32 bytes of kernel entropy */
  ret = getrandom(seed, sizeof(seed), 0);
  if(ret < 0 || ret < (int)sizeof(seed)) {
    /* Fallback: use /dev/urandom if getrandom not available */
    int fd = open("/dev/urandom", O_RDONLY);
    if(fd >= 0) {
      read(fd, seed, sizeof(seed));
      close(fd);
    } else {
      /* Last resort: use time-based seed (weak but better than nothing) */
      uint64_t t = time(0);
      for(i = 0; i < 4; i++) {
        ((uint32_t *)seed)[i] = (uint32_t)(t ^ (t >> 32) ^ i);
      }
    }
  }

  /* Initialize ChaCha20 state */
  arc4_state.state[0]  = 0x61707865;
  arc4_state.state[1]  = 0x3320646e;
  arc4_state.state[2]  = 0x79622d32;
  arc4_state.state[3]  = 0x6b206574;
  
  /* Load 32-byte key into state[4..11] */
  for(i = 0; i < 8; i++)
    arc4_state.state[4 + i] = ((uint32_t *)seed)[i];
  
  /* Zero out counter and nonce for now */
  arc4_state.state[12] = 0;
  arc4_state.state[13] = 0;
  arc4_state.state[14] = 0;
  arc4_state.state[15] = 0;

  arc4_state.keystream_idx = 64;  /* Force rekey on first use */
  arc4_state.initialized = 1;

  /* Burn the seed data (explicit_bzero lands in a later tranche). */
  memset(seed, 0, sizeof(seed));
}

/*
 * Ensure arc4random is initialized; called before first use.
 */
static void
arc4random_ensure_initialized(void)
{
  if(!arc4_state.initialized) {
    arc4random_stir();
  }
}

/*
 * Get next byte from keystream; rekey if necessary
 */
static uint8_t
arc4random_next_byte(void)
{
  uint8_t ret;

  arc4random_ensure_initialized();

  /* Need to rekey? */
  if(arc4_state.keystream_idx >= 64) {
    chacha20_block_user(arc4_state.state, arc4_state.keystream);
    arc4_state.keystream_idx = 0;
  }

  ret = ((uint8_t *)arc4_state.keystream)[arc4_state.keystream_idx];
  arc4_state.keystream_idx++;
  return ret;
}

/*
 * arc4random - Return 32-bit random value
 */
uint32_t
arc4random(void)
{
  uint32_t ret;
  int i;

  ret = 0;
  for(i = 0; i < 4; i++) {
    ret |= ((uint32_t)arc4random_next_byte()) << (i * 8);
  }
  return ret;
}

/*
 * arc4random_buf - Fill buffer with random bytes
 */
void
arc4random_buf(void *buf, size_t n)
{
  uint8_t *p = (uint8_t *)buf;
  size_t i;

  for(i = 0; i < n; i++)
    p[i] = arc4random_next_byte();
}

/*
 * arc4random_uniform - Random uint32 in [0, upper_bound)
 *
 * Uses rejection sampling to ensure uniform distribution.
 */
uint32_t
arc4random_uniform(uint32_t upper_bound)
{
  uint32_t r;
  uint32_t min;

  /* If upper_bound is a power of 2, no rejection needed */
  if((upper_bound & (upper_bound - 1)) == 0) {
    return arc4random() % upper_bound;
  }

  /* Calculate largest multiple of upper_bound that fits in uint32 */
  min = (uint32_t)-upper_bound % upper_bound;

  /* Keep generating until we get a value >= min */
  while((r = arc4random()) < min)
    ;

  return r % upper_bound;
}

/*
 * getentropy - OpenBSD-compatible entropy function
 *
 * Returns 0 on success, -1 on error (sets errno).
 * Blocks until all buflen bytes are available.
 * Fails if buflen > 256.
 */
int
getentropy(void *buf, size_t buflen)
{
  int ret;

  if(buflen > 256) {
    errno = EIO;
    return -1;
  }

  ret = getrandom(buf, buflen, 0);
  if(ret < 0) {
    errno = EIO;
    return -1;
  }

  if(ret != (int)buflen) {
    errno = EIO;
    return -1;
  }

  return 0;
}
