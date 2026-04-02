#include "../include/types.h"
#include "../include/user.h"

void *
calloc(uint nmemb, uint size)
{
  uint total;
  void *p;

  if(nmemb == 0 || size == 0)
    total = 0;
  else {
    if(nmemb > 0xffffffffU / size)
      return 0;
    total = nmemb * size;
  }

  p = malloc(total);
  if(p == 0)
    return 0;
  memset(p, 0, total);
  return p;
}
