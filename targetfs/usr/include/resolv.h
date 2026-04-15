/*
 * <resolv.h> - resolver compatibility shim
 *
 * Provides the minimal BSD/glibc resolver API surface expected by
 * OpenSSH openbsd-compat (getrrsetbyname path).
 */

#ifndef _RESOLV_H_
#define _RESOLV_H_

#include "sys/types.h"
#include "netinet/in.h"
#include "arpa/nameser.h"
#include "netdb.h"

#ifndef MAXNS
#define MAXNS 3
#endif

#ifndef RES_INIT
#define RES_INIT 0x00000001UL
#endif
#ifndef RES_DEBUG
#define RES_DEBUG 0x00000002UL
#endif
#ifndef RES_USE_EDNS0
#define RES_USE_EDNS0 0x00100000UL
#endif
#ifndef RES_USE_DNSSEC
#define RES_USE_DNSSEC 0x00800000UL
#endif

struct __res_state {
  int retrans;
  int retry;
  unsigned long options;
  int nscount;
  struct {
    struct in_addr addr;
    uint16_t port;
  } nsaddr_list[MAXNS];
};

extern struct __res_state _res;

int res_init(void);
int res_query(const char *dname, int class, int type,
              unsigned char *answer, int anslen);
int dn_expand(const unsigned char *msg, const unsigned char *eomorig,
              const unsigned char *comp_dn, char *exp_dn, int length);

#endif /* _RESOLV_H_ */