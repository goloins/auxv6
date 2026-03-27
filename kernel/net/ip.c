#include "../../include/types.h"
#include "../../include/defs.h"
#include "../../include/net.h"

int
ip_output(struct ifnet *ifp, uchar proto, uint src, uint dst,
					char *payload, uint len)
{
	struct mbuf *m;
	struct ip_hdr *ip;
	uint hlen;

	hlen = sizeof(struct ip_hdr);
	if(ifp == 0 || payload == 0)
		return -1;
	if(len > MBUF_SIZE - hlen)
		return -1;

	m = mbuf_alloc();
	if(m == 0)
		return -1;

	ip = (struct ip_hdr*)m->data;
	ip->vhl = 0x45;
	ip->proto = proto;
	ip->len = (ushort)(hlen + len);
	ip->src = src;
	ip->dst = dst;

	if(len > 0)
		memmove(m->data + hlen, payload, len);
	m->len = hlen + len;

	if(if_output(ifp, m) < 0){
		mbuf_free(m);
		return -1;
	}

	return len;
}

void
ip_input(struct ifnet *ifp, struct mbuf *m)
{
	struct ip_hdr *ip;
	uint hlen;
	uint plen;

	if(m == 0)
		return;

	hlen = sizeof(struct ip_hdr);
	if(m->len < hlen){
		mbuf_free(m);
		return;
	}

	ip = (struct ip_hdr*)m->data;
	if(ip->len < hlen || ip->len > m->len){
		mbuf_free(m);
		return;
	}

	plen = ip->len - hlen;
	if(ip->proto == NET_IP_ICMP)
		icmp_input(ifp, ip, m->data + hlen, plen);
	else if(ip->proto == NET_IP_UDP)
		udp_input(ifp, ip, m->data + hlen, plen);
	else if(ip->proto == NET_IP_TCP)
		tcp_input(ifp, ip, m->data + hlen, plen);

	mbuf_free(m);
}
