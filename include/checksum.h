#ifndef AUXV6_CHECKSUM_H
#define AUXV6_CHECKSUM_H

#include "stdint.h"

#define AUX_MD5_DIGEST_LEN 16
#define AUX_SHA1_DIGEST_LEN 20
#define AUX_SHA224_DIGEST_LEN 28
#define AUX_SHA256_DIGEST_LEN 32
#define AUX_SHA384_DIGEST_LEN 48
#define AUX_SHA512_DIGEST_LEN 64

#define AUX_HASH_BUFSZ 1024

enum aux_hash_algo {
  AUX_HASH_MD5 = 0,
  AUX_HASH_SHA1,
  AUX_HASH_SHA224,
  AUX_HASH_SHA256,
  AUX_HASH_SHA384,
  AUX_HASH_SHA512,
};

int aux_hash_digest_len(enum aux_hash_algo algo);
int aux_hash_stream(int fd, enum aux_hash_algo algo, uint8_t *digest, int digest_cap);
void aux_hex_encode(const uint8_t *in, int len, char *out);

#endif
