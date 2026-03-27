#include "../../include/types.h"
#include "../../include/defs.h"
#include "../../include/spinlock.h"
#include "../../include/net.h"

static struct spinlock if_lock;
static struct ifnet *if_list;

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
	ifp->if_next = if_list;
	if_list = ifp;
	release(&if_lock);

	cprintf("net: attached %s\n", ifp->if_xname);
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
