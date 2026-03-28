#include "../../include/types.h"
#include "../../include/defs.h"
#include "../../include/net.h"

static int
lo_output(struct ifnet *ifp, struct mbuf *m)
{
	if(ifp == 0 || m == 0)
		return -1;

	// Loopback short-circuits transmit into local input.
	m->rcvif = ifp;
	if_input(ifp, m);
	return 0;
}

static struct ifnet_ops lo_ops = {
	lo_output,
};

static struct ifnet lo_if;

void
loopback_attach(void)
{
	memset(&lo_if, 0, sizeof(lo_if));
	safestrcpy(lo_if.if_xname, "lo0", sizeof(lo_if.if_xname));
	lo_if.if_mtu = MBUF_SIZE;
	lo_if.if_flags = IFF_UP | IFF_LOOPBACK;
	lo_if.if_ops = &lo_ops;
	lo_if.if_input = ip_input;

	if(if_register(&lo_if) < 0)
		panic("loopback_attach");
	if(route_add(0x7f000000, 0xff000000, 0, 0x7f000001, &lo_if, RTF_UP) < 0)
		panic("loopback_route");
}
