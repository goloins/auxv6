#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "net.h"

#define ARP_HW_ETHER 1
#define ARP_PROTO_IP 0x0800
#define ETHERTYPE_ARP 0x0806
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY 2
#define ARP_CACHE_SIZE 32
#define ARP_PENDING_TICKS 20
#define ARP_RESOLVED_TICKS 6000

struct arp_hdr {
	ushort htype;
	ushort ptype;
	uchar hlen;
	uchar plen;
	ushort oper;
} __attribute__((packed));

struct arp_eth_ipv4 {
	struct arp_hdr hdr;
	uchar sha[ETH_ADDR_LEN];
	uchar spa[4];
	uchar tha[ETH_ADDR_LEN];
	uchar tpa[4];
} __attribute__((packed));

struct arp_entry {
	uint ip;
	uchar mac[ETH_ADDR_LEN];
	uint expire;
	uint state;
	struct ifnet *ifp;
	struct mbuf *pending;
};

#define ARP_FREE 0
#define ARP_PENDING 1
#define ARP_RESOLVED 2

static struct {
	struct spinlock lock;
	struct arp_entry entries[ARP_CACHE_SIZE];
} arptab;

static void
arp_ip_encode(uchar out[4], uint ip)
{
	uint be;

	be = net_htonl(ip);
	memmove(out, &be, sizeof(be));
}

static uint
arp_ip_decode(const uchar in[4])
{
	uint be;

	memmove(&be, in, sizeof(be));
	return net_ntohl(be);
}

static struct arp_entry*
arp_lookup_locked(struct ifnet *ifp, uint ip)
{
	int i;

	for(i = 0; i < ARP_CACHE_SIZE; i++){
		if(arptab.entries[i].state == ARP_FREE)
			continue;
		if(arptab.entries[i].ifp != ifp)
			continue;
		if(arptab.entries[i].ip == ip)
			return &arptab.entries[i];
	}
	return 0;
}

static struct arp_entry*
arp_alloc_locked(void)
{
	int i;
	struct arp_entry *oldest;

	oldest = &arptab.entries[0];
	for(i = 0; i < ARP_CACHE_SIZE; i++){
		if(arptab.entries[i].state == ARP_FREE)
			return &arptab.entries[i];
		if(arptab.entries[i].expire < oldest->expire)
			oldest = &arptab.entries[i];
	}
	if(oldest->pending)
		mbuf_free(oldest->pending);
	memset(oldest, 0, sizeof(*oldest));
	return oldest;
}

static int
arp_send_request(struct ifnet *ifp, uint ip)
{
	struct mbuf *m;
	struct arp_eth_ipv4 *arp;

	if(ifp == 0 || ifp->if_addr == 0)
		return -1;

	m = mbuf_alloc();
	if(m == 0)
		return -1;
	arp = (struct arp_eth_ipv4*)m->data;
	memset(arp, 0, sizeof(*arp));
	arp->hdr.htype = net_htons(ARP_HW_ETHER);
	arp->hdr.ptype = net_htons(ARP_PROTO_IP);
	arp->hdr.hlen = ETH_ADDR_LEN;
	arp->hdr.plen = 4;
	arp->hdr.oper = net_htons(ARP_OP_REQUEST);
	memmove(arp->sha, ifp->if_hwaddr, ETH_ADDR_LEN);
	arp_ip_encode(arp->spa, ifp->if_addr);
	arp_ip_encode(arp->tpa, ip);
	m->len = sizeof(*arp);

	if(ether_output(ifp, m, (const uchar*)"\xff\xff\xff\xff\xff\xff", ETHERTYPE_ARP) < 0){
		mbuf_free(m);
		return -1;
	}
	return 0;
}

static int
arp_send_reply(struct ifnet *ifp, const uchar *dst_mac, uint dst_ip)
{
	struct mbuf *m;
	struct arp_eth_ipv4 *arp;

	if(ifp == 0 || dst_mac == 0)
		return -1;

	m = mbuf_alloc();
	if(m == 0)
		return -1;
	arp = (struct arp_eth_ipv4*)m->data;
	memset(arp, 0, sizeof(*arp));
	arp->hdr.htype = net_htons(ARP_HW_ETHER);
	arp->hdr.ptype = net_htons(ARP_PROTO_IP);
	arp->hdr.hlen = ETH_ADDR_LEN;
	arp->hdr.plen = 4;
	arp->hdr.oper = net_htons(ARP_OP_REPLY);
	memmove(arp->sha, ifp->if_hwaddr, ETH_ADDR_LEN);
	arp_ip_encode(arp->spa, ifp->if_addr);
	memmove(arp->tha, dst_mac, ETH_ADDR_LEN);
	arp_ip_encode(arp->tpa, dst_ip);
	m->len = sizeof(*arp);

	if(ether_output(ifp, m, dst_mac, ETHERTYPE_ARP) < 0){
		mbuf_free(m);
		return -1;
	}
	return 0;
}

void
arp_init(void)
{
	initlock(&arptab.lock, "arp");
	memset(arptab.entries, 0, sizeof(arptab.entries));
}

int
arp_dump(struct arp_info *out, int max)
{
	int i;
	int n;
	uint now;
	struct arp_entry *entry;

	if(out == 0 || max <= 0)
		return -1;

	acquire(&tickslock);
	now = ticks;
	release(&tickslock);

	n = 0;
	acquire(&arptab.lock);
	for(i = 0; i < ARP_CACHE_SIZE && n < max; i++){
		entry = &arptab.entries[i];
		if(entry->state == ARP_FREE || entry->ifp == 0)
			continue;
		out[n].ai_ip = entry->ip;
		memmove(out[n].ai_mac, entry->mac, sizeof(out[n].ai_mac));
		out[n].ai_flags = 0;
		if(entry->state == ARP_PENDING)
			out[n].ai_flags |= 0x1;
		if(entry->state == ARP_RESOLVED)
			out[n].ai_flags |= 0x2;
		out[n].ai_expires = entry->expire > now ? entry->expire - now : 0;
		out[n].if_index = entry->ifp->if_index;
		n++;
	}
	release(&arptab.lock);

	return n;
}

int
arp_resolve(struct ifnet *ifp, uint ip, uchar *mac, struct mbuf *pending)
{
	struct arp_entry *entry;
	int need_request;

	if(ifp == 0 || mac == 0)
		return -1;
	if(ip == ifp->if_addr){
		memmove(mac, ifp->if_hwaddr, ETH_ADDR_LEN);
		return 0;
	}

	need_request = 0;
	acquire(&arptab.lock);
	entry = arp_lookup_locked(ifp, ip);
	if(entry && entry->state == ARP_RESOLVED && entry->expire > ticks){
		memmove(mac, entry->mac, ETH_ADDR_LEN);
		release(&arptab.lock);
		return 0;
	}
	if(entry == 0)
		entry = arp_alloc_locked();
	if(entry == 0){
		release(&arptab.lock);
		return -1;
	}
	if(pending){
		if(entry->pending)
			mbuf_free(entry->pending);
		entry->pending = pending;
	}
	entry->ip = ip;
	entry->ifp = ifp;
	entry->state = ARP_PENDING;
	if(entry->expire <= ticks)
		need_request = 1;
	entry->expire = ticks + ARP_PENDING_TICKS;
	release(&arptab.lock);

	if(need_request && arp_send_request(ifp, ip) < 0){
		acquire(&arptab.lock);
		entry = arp_lookup_locked(ifp, ip);
		if(entry && entry->pending == pending)
			entry->pending = 0;
		if(entry && entry->state == ARP_PENDING){
			entry->ip = 0;
			entry->expire = 0;
			entry->state = ARP_FREE;
			entry->ifp = 0;
		}
		release(&arptab.lock);
		return -1;
	}
	return 1;
}

void
arp_input(struct ifnet *ifp, struct mbuf *m)
{
	struct arp_eth_ipv4 *arp;
	struct arp_entry *entry;
	struct mbuf *pending;
	ushort oper;
	uint spa;
	uint tpa;

	if(ifp == 0 || m == 0)
		goto done;
	if(m->len < sizeof(struct arp_eth_ipv4))
		goto done;

	arp = (struct arp_eth_ipv4*)m->data;
	if(net_ntohs(arp->hdr.htype) != ARP_HW_ETHER ||
	   net_ntohs(arp->hdr.ptype) != ARP_PROTO_IP ||
	   arp->hdr.hlen != ETH_ADDR_LEN ||
	   arp->hdr.plen != 4)
		goto done;

	oper = net_ntohs(arp->hdr.oper);
	spa = arp_ip_decode(arp->spa);
	tpa = arp_ip_decode(arp->tpa);
	pending = 0;

	acquire(&arptab.lock);
	entry = arp_lookup_locked(ifp, spa);
	if(entry == 0)
		entry = arp_alloc_locked();
	if(entry){
		entry->ip = spa;
		entry->ifp = ifp;
		memmove(entry->mac, arp->sha, ETH_ADDR_LEN);
		entry->state = ARP_RESOLVED;
		entry->expire = ticks + ARP_RESOLVED_TICKS;
		pending = entry->pending;
		entry->pending = 0;
	}
	release(&arptab.lock);

	if(pending){
		if(ether_output(ifp, pending, arp->sha, NET_PROTO_IP) < 0)
			mbuf_free(pending);
	}
	if(oper == ARP_OP_REQUEST && ifp->if_addr != 0 && tpa == ifp->if_addr)
		arp_send_reply(ifp, arp->sha, spa);

 done:
	if(m)
		mbuf_free(m);
}
