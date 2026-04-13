#include "../../include/types.h"
#include "../../include/defs.h"
#include "../../include/socket.h"
#include "../../include/net.h"

int
udp_output(struct ifnet *ifp, struct sockaddr_in *src,
					 struct sockaddr_in *dst, char *payload, uint len)
{
	char buf[MBUF_SIZE];
	struct udp_hdr *uh;
	uint hlen;

	hlen = sizeof(struct udp_hdr);
	if(ifp == 0 || src == 0 || dst == 0 || payload == 0)
		return -1;
	if(len > MBUF_SIZE - hlen)
		return -1;

	uh = (struct udp_hdr*)buf;
	uh->src_port = net_htons(src->sin_port);
	uh->dst_port = net_htons(dst->sin_port);
	uh->len = net_htons((ushort)(hlen + len));
	uh->csum = 0;

	if(len > 0)
		memmove(buf + hlen, payload, len);

	return ip_output(ifp, NET_IP_UDP, src->sin_addr.s_addr, dst->sin_addr.s_addr, buf, hlen + len);
}

void
udp_input(struct ifnet *ifp, struct ip_hdr *ip, char *payload, uint len)
{
	struct udp_hdr *uh;
	struct sockaddr_in src;
	struct sockaddr_in dst;
	uint hlen;
	uint dlen;

	if(ip == 0 || payload == 0)
		return;

	hlen = sizeof(struct udp_hdr);
	if(len < hlen)
		return;

	uh = (struct udp_hdr*)payload;
	if(net_ntohs(uh->len) < hlen || net_ntohs(uh->len) > len)
		return;

	dlen = net_ntohs(uh->len) - hlen;

	memset(&src, 0, sizeof(src));
	src.sin_family = AF_INET;
	src.sin_port = net_ntohs(uh->src_port);
	src.sin_addr.s_addr = net_ntohl(ip->src);

	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_port = net_ntohs(uh->dst_port);
	dst.sin_addr.s_addr = net_ntohl(ip->dst);

	if(socket_deliver(&src, &dst, payload + hlen, dlen) < 0)
		icmp_send_unreach(ifp, ip, payload, len, ICMP_UNREACH_PORT);
}
