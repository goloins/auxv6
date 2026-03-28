#include "../../include/types.h"
#include "../../include/defs.h"
#include "../../include/spinlock.h"
#include "../../include/net.h"

static struct spinlock if_lock;
static struct ifnet *if_list;
static uint if_next_index;

static void
if_default_input(struct ifnet *ifp, struct mbuf *m)
{
	(void)ifp;
	mbuf_free(m);
}

void
netdev_init(void)
{
	initlock(&if_lock, "ifnet");
	if_list = 0;
	if_next_index = 1;
	route_init();
	loopback_attach();
}

int
if_register(struct ifnet *ifp)
{
	if(ifp == 0 || ifp->if_ops == 0 || ifp->if_ops->if_output == 0)
		return -1;

	if(ifp->if_input == 0)
		ifp->if_input = if_default_input;

	acquire(&if_lock);
	if(if_next_index == 0 || if_next_index > MAXNETIF){
		release(&if_lock);
		return -1;
	}
	ifp->if_index = if_next_index++;
	ifp->if_next = if_list;
	if_list = ifp;
	release(&if_lock);

	cprintf("net: attached %s (if%d)\n", ifp->if_xname, ifp->if_index);
	return 0;
}

struct ifnet*
if_get(char *name)
{
	struct ifnet *ifp;

	if(name == 0)
		return 0;

	acquire(&if_lock);
	for(ifp = if_list; ifp; ifp = ifp->if_next){
		if(strncmp(name, ifp->if_xname, IFNAMSIZ) == 0){
			release(&if_lock);
			return ifp;
		}
	}
	release(&if_lock);

	return 0;
}

struct ifnet*
if_byindex(uint ifindex)
{
	struct ifnet *ifp;

	if(ifindex == 0)
		return 0;

	acquire(&if_lock);
	for(ifp = if_list; ifp; ifp = ifp->if_next){
		if(ifp->if_index == ifindex){
			release(&if_lock);
			return ifp;
		}
	}
	release(&if_lock);

	return 0;
}

struct ifnet*
if_first(void)
{
	struct ifnet *ifp;

	acquire(&if_lock);
	ifp = if_list;
	release(&if_lock);
	return ifp;
}

struct ifnet*
if_next(struct ifnet *ifp)
{
	if(ifp == 0)
		return 0;
	return ifp->if_next;
}

int
if_dump(struct netif_info *out, int max)
{
	int n;
	struct ifnet *ifp;

	if(out == 0 || max <= 0)
		return -1;

	n = 0;
	acquire(&if_lock);
	for(ifp = if_list; ifp && n < max; ifp = ifp->if_next){
		out[n].if_index = ifp->if_index;
		out[n].if_mtu = ifp->if_mtu;
		out[n].if_flags = ifp->if_flags;
		safestrcpy(out[n].if_name, ifp->if_xname, sizeof(out[n].if_name));
		n++;
	}
	release(&if_lock);

	return n;
}

int
if_output(struct ifnet *ifp, struct mbuf *m)
{
	if(ifp == 0 || m == 0 || ifp->if_ops == 0 || ifp->if_ops->if_output == 0)
		return -1;
	if((ifp->if_flags & IFF_UP) == 0)
		return -1;

	return ifp->if_ops->if_output(ifp, m);
}

void
if_input(struct ifnet *ifp, struct mbuf *m)
{
	if(ifp == 0 || m == 0){
		mbuf_free(m);
		return;
	}

	if(ifp->if_input)
		ifp->if_input(ifp, m);
	else
		mbuf_free(m);
}

struct mbuf*
mbuf_alloc(void)
{
	struct mbuf *m;

	m = (struct mbuf*)kalloc();
	if(m == 0)
		return 0;

	m->len = 0;
	m->rcvif = 0;
	m->next = 0;
	return m;
}

void
mbuf_free(struct mbuf *m)
{
	if(m)
		kfree((char*)m);
}
