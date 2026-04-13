#include "../../include/types.h"
#include "../../include/defs.h"
#include "../../include/net.h"

static ushort ip_next_id = 1;

static ushort
ip_checksum(void *buf, uint len)
{
	ushort *w;
	uint sum;

	w = (ushort*)buf;
	sum = 0;
	while(len > 1){
		sum += *w++;
		len -= 2;
	}
	if(len)
		sum += *(uchar*)w;
	while(sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return (ushort)(~sum);
}

int
ip_output(struct ifnet *ifp, uchar proto, uint src, uint dst,
					char *payload, uint len)
{
	return ip_output_ttl(ifp, proto, src, dst, payload, len, 64);
}

int
ip_output_ttl(struct ifnet *ifp, uchar proto, uint src, uint dst,
              char *payload, uint len, uchar ttl)
{
	struct mbuf *m;
	struct ip_hdr *ip;
	uint gateway;
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
	ip->tos = 0;
	ip->proto = proto;
	ip->len = net_htons((ushort)(hlen + len));
	ip->id = net_htons(ip_next_id++);
	ip->off = 0;
	ip->ttl = ttl;
	ip->sum = 0;
	ip->src = net_htonl(src);
	ip->dst = net_htonl(dst);

	if(len > 0)
		memmove(m->data + hlen, payload, len);
	ip->sum = ip_checksum(ip, hlen);
	m->len = hlen + len;

	if(ifp->if_flags & IFF_LOOPBACK){
		if(if_output(ifp, m) < 0){
			mbuf_free(m);
			return -1;
		}
		return len;
	}

	if(ifp->if_flags & IFF_POINTOPOINT){
		if(if_output(ifp, m) < 0){
			mbuf_free(m);
			return -1;
		}
		return len;
	}

	gateway = 0;
	route_lookup(dst, 0, &gateway);
	if(ether_output_ip(ifp, m, gateway ? gateway : dst) < 0){
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
	uint total_len;
	uint plen;

	if(m == 0)
		return;

	if(m->len < sizeof(struct ip_hdr)){
		mbuf_free(m);
		return;
	}

	ip = (struct ip_hdr*)m->data;
	hlen = (uint)((ip->vhl & 0x0f) * 4);
	if((ip->vhl >> 4) != 4 || hlen < sizeof(struct ip_hdr) || hlen > m->len){
		mbuf_free(m);
		return;
	}
	if(ip_checksum(ip, hlen) != 0){
		if(ifp) ifp->if_ierrors++;
		mbuf_free(m);
		return;
	}
	total_len = net_ntohs(ip->len);
	if(total_len < hlen || total_len > m->len){
		if(ifp) ifp->if_ierrors++;
		mbuf_free(m);
		return;
	}

	plen = total_len - hlen;
	if(ip->proto == NET_IP_ICMP)
		icmp_input(ifp, ip, m->data + hlen, plen);
	else if(ip->proto == NET_IP_UDP)
		udp_input(ifp, ip, m->data + hlen, plen);
	else if(ip->proto == NET_IP_TCP)
		tcp_input(ifp, ip, m->data + hlen, plen);

	mbuf_free(m);
}
