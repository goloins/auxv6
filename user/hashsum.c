#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "string.h"
#include "checksum.h"

static enum aux_hash_algo
algo_from_prog(const char *prog)
{
  const char *p = prog;

  while(*p)
    p++;
  while(p > prog && p[-1] != '/')
    p--;

  if(strcmp(p, "md5sum") == 0)
    return AUX_HASH_MD5;
  if(strcmp(p, "sha1sum") == 0)
    return AUX_HASH_SHA1;
  if(strcmp(p, "sha224sum") == 0)
    return AUX_HASH_SHA224;
  if(strcmp(p, "sha256sum") == 0)
    return AUX_HASH_SHA256;
  if(strcmp(p, "sha384sum") == 0)
    return AUX_HASH_SHA384;
  return AUX_HASH_SHA512;
}

static int
hash_one(enum aux_hash_algo algo, const char *name, int fd)
{
  uint8_t digest[AUX_SHA512_DIGEST_LEN];
  char hex[AUX_SHA512_DIGEST_LEN * 2 + 1];
  int n;

  n = aux_hash_stream(fd, algo, digest, sizeof(digest));
  if(n < 0)
    return -1;

  aux_hex_encode(digest, n, hex);
  dprintf(1, "%s  %s\n", hex, name);
  return 0;
}

int
main(int argc, char *argv[])
{
  enum aux_hash_algo algo;
  int i;
  int rc;

  algo = algo_from_prog(argv[0]);
  rc = 0;

  if(argc == 1) {
    if(hash_one(algo, "-", 0) < 0) {
      dprintf(2, "%s: read error\n", argv[0]);
      return 1;
    }
    return 0;
  }

  for(i = 1; i < argc; i++) {
    int fd;

    if(strcmp(argv[i], "-") == 0) {
      if(hash_one(algo, "-", 0) < 0) {
        dprintf(2, "%s: -: read error\n", argv[0]);
        rc = 1;
      }
      continue;
    }

    fd = open(argv[i], O_RDONLY);
    if(fd < 0) {
      dprintf(2, "%s: %s: cannot open\n", argv[0], argv[i]);
      rc = 1;
      continue;
    }

    if(hash_one(algo, argv[i], fd) < 0) {
      dprintf(2, "%s: %s: read error\n", argv[0], argv[i]);
      rc = 1;
    }
    close(fd);
  }

  return rc;
}
