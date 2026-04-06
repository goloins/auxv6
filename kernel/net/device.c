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
	lockdep_set_rank(&if_lock, LOCK_RANK_DEFAULT, "ifnet");
	if_list = 0;
	if_next_index = 1;
	route_init();
	arp_init();
	loopback_attach();
	virtio_net_init();
	e1000_init();
	i219_init();
	i226_init();
	ax88179_pci_init();
	pcnet_init();
	rtl8111_init();
	rtl8125_init();
	rtl8139_init();
	tg3_init();
	bnxt_init();
	atlantic_init();
	skge_init();
	via_rhine_init();
	igb_init();
	ixgbe_init();
	i40e_init();
	ice_init();
	bnx2_init();
	bnx2x_init();
	mlx4_en_init();
	mlx5e_init();
	ena_init();
	alx_init();
	vmxnet3_init();
	netvsc_init();
}

void
netdev_poll(void)
{
	struct ifnet *snapshot[MAXNETIF];
	struct ifnet *ifp;
	int n = 0;

	acquire(&if_lock);
	for(ifp = if_list; ifp && n < MAXNETIF; ifp = ifp->if_next)
		snapshot[n++] = ifp;
	release(&if_lock);

	for(int i = 0; i < n; i++){
		ifp = snapshot[i];
		if(!ifp || !ifp->if_ops || !ifp->if_ops->if_poll)
			continue;
		if((ifp->if_flags & IFF_UP) == 0)
			continue;
		ifp->if_ops->if_poll(ifp);
	}
}

int
if_register(struct ifnet *ifp)
{
	if(ifp == 0 || ifp->if_ops == 0 || ifp->if_ops->if_output == 0)
		return -1;

	if(ifp->if_input == 0)
		ifp->if_input = if_default_input;

	if(ifp->if_link_state == LINK_STATE_UNKNOWN){
		if((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
			ifp->if_link_state = LINK_STATE_UP;
		else if((ifp->if_flags & IFF_UP) == 0)
			ifp->if_link_state = LINK_STATE_DOWN;
	}

	acquire(&if_lock);
	if(if_next_index == 0 || if_next_index > MAXNETIF){
		release(&if_lock);
		return -1;
	}
	ifp->if_index = if_next_index++;
	ifp->if_next = if_list;
	if_list = ifp;
	release(&if_lock);

	BOOTDBG("net: attached %s (if%d)\n", ifp->if_xname, ifp->if_index);
	return 0;
}

int
if_unregister(struct ifnet *ifp)
{
	struct ifnet **link;
	uint old_addr;
	uint old_mask;

	if(ifp == 0)
		return -1;

	acquire(&if_lock);
	link = &if_list;
	while(*link && *link != ifp)
		link = &(*link)->if_next;
	if(*link == 0){
		release(&if_lock);
		return -1;
	}
	*link = ifp->if_next;
	ifp->if_next = 0;
	old_addr = ifp->if_addr;
	old_mask = ifp->if_netmask;
	ifp->if_addr = 0;
	ifp->if_netmask = 0;
	ifp->if_flags &= ~(IFF_UP | IFF_RUNNING);
	ifp->if_link_state = LINK_STATE_DOWN;
	release(&if_lock);

	if(old_addr != 0 && old_mask != 0)
		(void)route_delete(old_addr & old_mask, old_mask, ifp);

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
		out[n].if_link_state = ifp->if_link_state;
		out[n].if_addr = ifp->if_addr;
		out[n].if_netmask = ifp->if_netmask;
		memmove(out[n].if_hwaddr, ifp->if_hwaddr, sizeof(out[n].if_hwaddr));
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

int
if_set_addr(struct ifnet *ifp, uint addr, uint mask)
{
	uint old_addr;
	uint old_mask;

	if(ifp == 0)
		return -1;

	acquire(&if_lock);
	old_addr = ifp->if_addr;
	old_mask = ifp->if_netmask;
	ifp->if_addr = addr;
	ifp->if_netmask = mask;
	release(&if_lock);

	if(old_addr != 0 && old_mask != 0 &&
	   (old_addr != addr || old_mask != mask))
		route_delete(old_addr & old_mask, old_mask, ifp);

	if(addr != 0 && mask != 0)
		return route_add(addr & mask, mask, 0, addr, ifp, RTF_UP);
	return 0;
}

void
if_link_state_update(struct ifnet *ifp, uint state)
{
	if(ifp == 0)
		return;
	/* Lockless -- caller holds device lock; x86 aligned-word stores are atomic */
	ifp->if_link_state = state;
	if(state == LINK_STATE_UP)
		ifp->if_flags |= IFF_RUNNING;
	else if(state == LINK_STATE_DOWN)
		ifp->if_flags &= ~IFF_RUNNING;
}

int
if_set_addr_byindex(uint ifindex, uint addr, uint mask)
{
	struct ifnet *ifp;

	ifp = if_byindex(ifindex);
	if(ifp == 0)
		return -1;

	return if_set_addr(ifp, addr, mask);
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
