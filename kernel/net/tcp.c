#include "../../include/types.h"
#include "../../include/defs.h"
#include "../../include/socket.h"
#include "../../include/net.h"

int
tcp_connect(struct ifnet *ifp, struct socket *s, struct sockaddr_in *dst)
{
	int state;

	if(s == 0 || dst == 0)
		return -1;
	if(s->type != SOCK_STREAM)
		return -1;
	if(ifp == 0 || (ifp->if_flags & IFF_LOOPBACK) == 0)
		return -1;

	// Minimal control path skeleton:
	// SYN-SENT -> SYN-RCVD -> ESTABLISHED
	state = TCPS_SYN_SENT;
	if(state == TCPS_SYN_SENT)
		state = TCPS_SYN_RECEIVED;
	if(state == TCPS_SYN_RECEIVED)
		state = TCPS_ESTABLISHED;

	return socket_stream_connect(s, dst);
}

int
tcp_output(struct ifnet *ifp, struct sockaddr_in *src,
					 struct sockaddr_in *dst, char *payload, uint len)
{
	if(src == 0 || dst == 0 || payload == 0)
		return -1;
	if(src->sin_port == 0 || dst->sin_port == 0)
		return -1;
	if(ifp == 0 || (ifp->if_flags & IFF_LOOPBACK) == 0)
		return -1;

	// For now, stream payload delivery reuses socket-level demux.
	// Sequence numbers and reliability will be implemented in the next phase.
	if(socket_deliver(src, dst, payload, len) < 0)
		return -1;

	return (int)len;
}

void
tcp_input(struct ifnet *ifp, struct ip_hdr *ip, char *payload, uint len)
{
	(void)ifp;
	(void)ip;
	(void)payload;
	(void)len;

	// Placeholder: full TCP parsing and socket demux will be added next.
}
