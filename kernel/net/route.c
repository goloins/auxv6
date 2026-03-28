#include "../../include/types.h"
#include "../../include/defs.h"
#include "../../include/spinlock.h"
#include "../../include/net.h"

#define NROUTE 32

struct {
	struct spinlock lock;
	struct route tab[NROUTE];
} rtable;

static uint
mask_prefix_len(uint mask)
{
	uint n;

	n = 0;
	while(mask & 0x80000000){
		n++;
		mask <<= 1;
	}
	return n;
}

void
route_init(void)
{
	int i;

	initlock(&rtable.lock, "route");
	acquire(&rtable.lock);
	for(i = 0; i < NROUTE; i++){
		rtable.tab[i].rt_dst = 0;
		rtable.tab[i].rt_mask = 0;
		rtable.tab[i].rt_gateway = 0;
		rtable.tab[i].rt_src = 0;
		rtable.tab[i].rt_flags = 0;
		rtable.tab[i].rt_ifp = 0;
	}
	release(&rtable.lock);
}

int
route_add(uint dst, uint mask, uint gateway, uint src, struct ifnet *ifp, uint flags)
{
	int i;
	struct route *slot;

	if(ifp == 0)
		return -1;

	slot = 0;
	acquire(&rtable.lock);
	for(i = 0; i < NROUTE; i++){
		if((rtable.tab[i].rt_flags & RTF_UP) == 0){
			slot = &rtable.tab[i];
			break;
		}
	}
	if(slot == 0){
		release(&rtable.lock);
		return -1;
	}

	slot->rt_dst = dst;
	slot->rt_mask = mask;
	slot->rt_gateway = gateway;
	slot->rt_src = src;
	slot->rt_flags = flags | RTF_UP;
	slot->rt_ifp = ifp;
	
	release(&rtable.lock);
	return 0;
}

struct ifnet*
route_lookup(uint dst, uint *src, uint *gateway)
{
	int i;
	struct route *rt;
	struct route *best;
	uint bestlen;
	uint rlen;

	best = 0;
	bestlen = 0;

	acquire(&rtable.lock);
	for(i = 0; i < NROUTE; i++){
		rt = &rtable.tab[i];
		if((rt->rt_flags & RTF_UP) == 0)
			continue;
		if(rt->rt_ifp == 0)
			continue;
		if((rt->rt_ifp->if_flags & IFF_UP) == 0)
			continue;
		if((dst & rt->rt_mask) != (rt->rt_dst & rt->rt_mask))
			continue;
		rlen = mask_prefix_len(rt->rt_mask);
		if(best == 0 || rlen > bestlen){
			best = rt;
			bestlen = rlen;
		}
	}

	if(best){
		if(src)
			*src = best->rt_src;
		if(gateway)
			*gateway = best->rt_gateway;
	}
	release(&rtable.lock);

	if(best)
		return best->rt_ifp;
	return 0;
}

int
route_dump(struct route_info *out, int max)
{
	int i;
	int n;
	struct route *rt;

	if(out == 0 || max <= 0)
		return -1;

	n = 0;
	acquire(&rtable.lock);
	for(i = 0; i < NROUTE && n < max; i++){
		rt = &rtable.tab[i];
		if((rt->rt_flags & RTF_UP) == 0)
			continue;
		out[n].rt_dst = rt->rt_dst;
		out[n].rt_mask = rt->rt_mask;
		out[n].rt_gateway = rt->rt_gateway;
		out[n].rt_src = rt->rt_src;
		out[n].rt_flags = rt->rt_flags;
		out[n].if_index = rt->rt_ifp ? rt->rt_ifp->if_index : 0;
		n++;
	}
	release(&rtable.lock);

	return n;
}
