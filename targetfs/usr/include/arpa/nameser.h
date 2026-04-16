/*
 * <arpa/nameser.h> - DNS message compatibility shim
 *
 * OpenSSH/openbsd-compat uses this for the legacy HEADER type and
 * associated DNS wire-format constants.
 */

#ifndef _ARPA_NAMESER_H_
#define _ARPA_NAMESER_H_

#include "../sys/types.h"
#include "../endian.h"

#ifndef PACKETSZ
#define PACKETSZ 512
#endif

#ifndef HFIXEDSZ
#define HFIXEDSZ 12
#endif

#ifndef QFIXEDSZ
#define QFIXEDSZ 4
#endif

#ifndef NS_MAXDNAME
#define NS_MAXDNAME 1025
#endif
#ifndef MAXDNAME
#define MAXDNAME NS_MAXDNAME
#endif

typedef struct {
  uint16_t id;
#if BYTE_ORDER == LITTLE_ENDIAN
  unsigned rd:1;
  unsigned tc:1;
  unsigned aa:1;
  unsigned opcode:4;
  unsigned qr:1;
  unsigned rcode:4;
  unsigned cd:1;
  unsigned ad:1;
  unsigned z:1;
  unsigned ra:1;
#else
  unsigned qr:1;
  unsigned opcode:4;
  unsigned aa:1;
  unsigned tc:1;
  unsigned rd:1;
  unsigned ra:1;
  unsigned z:1;
  unsigned ad:1;
  unsigned cd:1;
  unsigned rcode:4;
#endif
  uint16_t qdcount;
  uint16_t ancount;
  uint16_t nscount;
  uint16_t arcount;
} HEADER;

#endif /* _ARPA_NAMESER_H_ */