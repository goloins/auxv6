/*
 * <netinet/ip.h> - IPv4 header compatibility shim
 *
 * OpenSSH/openbsd-compat expects BSD-style struct ip declarations.
 */

#ifndef _NETINET_IP_H_
#define _NETINET_IP_H_

#include "in_systm.h"
#include "in.h"
#include "../endian.h"

#ifndef IPVERSION
#define IPVERSION 4
#endif

#ifndef IP_DF
#define IP_DF 0x4000
#endif
#ifndef IP_MF
#define IP_MF 0x2000
#endif

struct ip {
#if BYTE_ORDER == LITTLE_ENDIAN
  unsigned int ip_hl:4;
  unsigned int ip_v:4;
#else
  unsigned int ip_v:4;
  unsigned int ip_hl:4;
#endif
  uint8_t      ip_tos;
  uint16_t     ip_len;
  uint16_t     ip_id;
  uint16_t     ip_off;
  uint8_t      ip_ttl;
  uint8_t      ip_p;
  uint16_t     ip_sum;
  struct in_addr ip_src;
  struct in_addr ip_dst;
};

#endif /* _NETINET_IP_H_ */