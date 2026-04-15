#include "types.h"
#include "stdint.h"
#include "string.h"
#include "stdio.h"
#include "errno.h"

#define AUX_CRYPT_PREFIX "$aux$"

static uint64_t
fnv1a64_init(void)
{
  return 1469598103934665603ULL;
}

static uint64_t
fnv1a64_update(uint64_t h, const char *s)
{
  int i;

  if(s == 0)
    return h;
  for(i = 0; s[i]; i++) {
    h ^= (uchar)s[i];
    h *= 1099511628211ULL;
  }
  return h;
}

static int
extract_aux_salt(const char *salt, char *out, int outsz)
{
  const char *start;
  const char *end;
  int n;

  if(salt == 0 || out == 0 || outsz <= 1)
    return -1;

  if(strncmp(salt, AUX_CRYPT_PREFIX, strlen(AUX_CRYPT_PREFIX)) != 0)
    return -1;

  start = salt + strlen(AUX_CRYPT_PREFIX);
  end = strchr(start, '$');
  if(end == 0)
    end = start + strlen(start);

  n = (int)(end - start);
  if(n <= 0 || n >= outsz)
    return -1;
  memmove(out, start, n);
  out[n] = 0;
  return 0;
}

char *
crypt(const char *key, const char *salt)
{
  static char out[128];
  char sbuf[32];
  uint64_t h;

  if(key == 0 || salt == 0) {
    errno = EINVAL;
    return 0;
  }

  if(extract_aux_salt(salt, sbuf, sizeof(sbuf)) < 0) {
    int n;

    n = strlen(salt);
    if(n <= 0 || n >= (int)sizeof(sbuf)) {
      errno = EINVAL;
      return 0;
    }
    memmove(sbuf, salt, n);
    sbuf[n] = 0;
  }

  h = fnv1a64_init();
  h = fnv1a64_update(h, "auxv6");
  h = fnv1a64_update(h, ":");
  h = fnv1a64_update(h, sbuf);
  h = fnv1a64_update(h, ":");
  h = fnv1a64_update(h, key);

  snprintf(out, sizeof(out), "%s%s$%016llx", AUX_CRYPT_PREFIX, sbuf,
           (unsigned long long)h);
  return out;
}
