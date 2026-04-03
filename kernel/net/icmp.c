#include "../../include/types.h"
#include "../../include/defs.h"
#include "../../include/socket.h"
#include "../../include/net.h"

static ushort
icmp_csum(void *buf, uint len)
{
  ushort *w;
  uint sum;

  w = (ushort*)buf;
  sum = 0;
  while(len > 1) {
    sum += *w++;
    len -= 2;
  }
  if(len)
    sum += *(uchar*)w;

  while(sum >> 16)
    sum = (sum & 0xffff) + (sum >> 16);
  return (ushort)(~sum);
}

void
icmp_input(struct ifnet *ifp, struct ip_hdr *ip, char *payload, uint len)
{
  struct icmp_hdr *ic;
  struct sockaddr_in src;
  struct sockaddr_in dst;

  if(ifp == 0 || ip == 0 || payload == 0)
    return;
  if(len < sizeof(struct icmp_hdr))
    return;

  ic = (struct icmp_hdr*)payload;

  memset(&src, 0, sizeof(src));
  src.sin_family = AF_INET;
  src.sin_addr = net_ntohl(ip->src);

  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_addr = net_ntohl(ip->dst);

  // Raw sockets should be able to observe inbound ICMP packets.
  socket_deliver_raw(IPPROTO_ICMP, &src, &dst, payload, len);

  if(ic->type == ICMP_ECHO && ic->code == 0) {
    ic->type = ICMP_ECHO_REPLY;
    ic->csum = 0;
    ic->csum = icmp_csum(payload, len);
    ip_output(ifp, NET_IP_ICMP, dst.sin_addr, src.sin_addr, payload, len);

  }
}

// Send ICMP Destination Unreachable (type 3) back to the original sender.
// RFC 792: ICMP error payload = 8-byte ICMP header + original IP header
//          + first 8 bytes of original transport header.
void
icmp_send_unreach(struct ifnet *ifp, struct ip_hdr *orig_ip,
                  char *orig_transport, uint orig_tlen, uchar code)
{
  char buf[MBUF_SIZE];
  struct icmp_hdr *ic;
  uint ip_hdr_len;
  uint copy_bytes;
  uint total;

  if(ifp == 0 || orig_ip == 0)
    return;

  ip_hdr_len = (orig_ip->vhl & 0x0f) * 4;
  copy_bytes = ip_hdr_len + (orig_tlen < 8 ? orig_tlen : 8);

  total = sizeof(struct icmp_hdr) + copy_bytes;
  if(total > MBUF_SIZE)
    return;

  memset(buf, 0, total);

  ic = (struct icmp_hdr*)buf;
  ic->type = ICMP_UNREACH;
  ic->code = code;
  ic->csum = 0;

  memmove(buf + sizeof(struct icmp_hdr), orig_ip, copy_bytes);

  ic->csum = icmp_csum(buf, total);

  ip_output(ifp, NET_IP_ICMP, ifp->if_addr, net_ntohl(orig_ip->src), buf, total);
}
