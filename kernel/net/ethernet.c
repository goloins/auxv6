#include "types.h"
#include "defs.h"
#include "net.h"

#define ETHER_HDR_LEN 14
#define ETHER_MIN_FRAME 60
#define ETHERTYPE_ARP 0x0806

struct ether_header {
	uchar dst[ETH_ADDR_LEN];
	uchar src[ETH_ADDR_LEN];
	ushort type;
} __attribute__((packed));

static const uchar ether_bcast[ETH_ADDR_LEN] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static int
ether_ipv4_broadcast(struct ifnet *ifp, uint ip)
{
	uint directed;

	if(ip == 0xffffffffU)
		return 1;
	if(ifp == 0 || ifp->if_addr == 0 || ifp->if_netmask == 0)
		return 0;
	directed = (ifp->if_addr & ifp->if_netmask) | (~ifp->if_netmask);
	return ip == directed;
}

static int
ether_addr_equal(const uchar *a, const uchar *b)
{
	return memcmp(a, b, ETH_ADDR_LEN) == 0;
}

int
ether_output(struct ifnet *ifp, struct mbuf *m, const uchar *dst, ushort type)
{
	struct ether_header *eh;

	if(ifp == 0 || m == 0 || dst == 0)
		return -1;
	if(m->len + ETHER_HDR_LEN > MBUF_SIZE)
		return -1;

	memmove(m->data + ETHER_HDR_LEN, m->data, m->len);
	eh = (struct ether_header*)m->data;
	memmove(eh->dst, dst, ETH_ADDR_LEN);
	memmove(eh->src, ifp->if_hwaddr, ETH_ADDR_LEN);
	eh->type = net_htons(type);
	m->len += ETHER_HDR_LEN;
	if(m->len < ETHER_MIN_FRAME){
		memset(m->data + m->len, 0, ETHER_MIN_FRAME - m->len);
		m->len = ETHER_MIN_FRAME;
	}

	{
		uint pktlen = m->len;
		if(if_output(ifp, m) < 0){
			ifp->if_oerrors++;
			return -1;
		}
		ifp->if_opackets++;
		ifp->if_obytes += pktlen;
	}
	return 0;
}

int
ether_output_ip(struct ifnet *ifp, struct mbuf *m, uint next_hop)
{
	uchar dst[ETH_ADDR_LEN];
	int ret;

	if(ether_ipv4_broadcast(ifp, next_hop))
		return ether_output(ifp, m, ether_bcast, NET_PROTO_IP);

	ret = arp_resolve(ifp, next_hop, dst, m);
	if(ret < 0)
		return -1;
	if(ret > 0)
		return 0;

	return ether_output(ifp, m, dst, NET_PROTO_IP);
}

void
ether_input(struct ifnet *ifp, struct mbuf *m)
{
	struct ether_header *eh;
	ushort type;

	if(ifp == 0 || m == 0){
		mbuf_free(m);
		return;
	}
	if(m->len < ETHER_HDR_LEN){
		mbuf_free(m);
		return;
	}

	eh = (struct ether_header*)m->data;
	if(!ether_addr_equal(eh->dst, ether_bcast) &&
	   !ether_addr_equal(eh->dst, ifp->if_hwaddr) &&
	   (eh->dst[0] & 0x01) == 0){
		mbuf_free(m);
		return;
	}

	type = net_ntohs(eh->type);
	memmove(m->data, m->data + ETHER_HDR_LEN, m->len - ETHER_HDR_LEN);
	m->len -= ETHER_HDR_LEN;

	ifp->if_ipackets++;
	ifp->if_ibytes += m->len;

	if(type == NET_PROTO_IP)
		ip_input(ifp, m);
	else if(type == ETHERTYPE_ARP)
		arp_input(ifp, m);
	else
		mbuf_free(m);
}
