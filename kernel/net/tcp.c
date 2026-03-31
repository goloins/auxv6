#include "../../include/types.h"
#include "../../include/defs.h"
#include "../../include/socket.h"
#include "../../include/net.h"

#define NSOCKET 64

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

#define TCP_HDR_LEN 20
#define TCP_CONNECT_TIMEOUT_TICKS 300

extern struct socket sockets[NSOCKET];
extern struct spinlock socket_lock;

static uint tcp_iss = 50000;
static ushort tcp_next_ephemeral = 45000;

static ushort
tcp_cksum_finalize(uint sum)
{
	while(sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return (ushort)(~sum);
}

static uint
tcp_cksum_add(uint sum, ushort v)
{
	return sum + (uint)v;
}

static uint
tcp_cksum_add_buf(uint sum, const char *buf, uint len)
{
	uint i;

	for(i = 0; i + 1 < len; i += 2)
		sum = tcp_cksum_add(sum, (ushort)(((uchar)buf[i] << 8) | (uchar)buf[i + 1]));
	if(i < len)
		sum = tcp_cksum_add(sum, (ushort)((uchar)buf[i] << 8));
	return sum;
}

static ushort
tcp_checksum(uint src, uint dst, const char *seg, uint seglen)
{
	uint sum;

	sum = 0;
	sum = tcp_cksum_add(sum, (ushort)(src >> 16));
	sum = tcp_cksum_add(sum, (ushort)(src & 0xffff));
	sum = tcp_cksum_add(sum, (ushort)(dst >> 16));
	sum = tcp_cksum_add(sum, (ushort)(dst & 0xffff));
	sum = tcp_cksum_add(sum, (ushort)NET_IP_TCP);
	sum = tcp_cksum_add(sum, (ushort)seglen);
	sum = tcp_cksum_add_buf(sum, seg, seglen);
	return tcp_cksum_finalize(sum);
}

static int
tcp_send_segment(struct ifnet *ifp, struct sockaddr_in *src, struct sockaddr_in *dst,
								 uint seq, uint ack, uchar flags, char *payload, uint len)
{
	char buf[MBUF_SIZE];
	struct tcp_hdr *th;
	uint seglen;

	if(ifp == 0 || src == 0 || dst == 0)
		return -1;
	if(src->sin_port == 0 || dst->sin_port == 0)
		return -1;
	if(len > MBUF_SIZE - TCP_HDR_LEN)
		return -1;
	if(len > 0 && payload == 0)
		return -1;

	th = (struct tcp_hdr*)buf;
	th->src_port = net_htons(src->sin_port);
	th->dst_port = net_htons(dst->sin_port);
	th->seq = net_htonl(seq);
	th->ack = net_htonl(ack);
	th->off = (uchar)(5 << 4);
	th->flags = flags;
	th->win = net_htons(4096);
	th->csum = 0;
	th->urg = 0;

	if(len > 0)
		memmove(buf + TCP_HDR_LEN, payload, len);

	seglen = TCP_HDR_LEN + len;
	th->csum = net_htons(tcp_checksum(src->sin_addr, dst->sin_addr, buf, seglen));

	return ip_output(ifp, NET_IP_TCP, src->sin_addr, dst->sin_addr, buf, seglen);
}

static ushort
tcp_alloc_ephemeral_locked(void)
{
	int i;
	int tries;
	ushort p;
	int inuse;

	for(tries = 0; tries < 20000; tries++) {
		p = tcp_next_ephemeral++;
		if(tcp_next_ephemeral < 45000)
			tcp_next_ephemeral = 45000;

		inuse = 0;
		for(i = 0; i < NSOCKET; i++) {
			if(sockets[i].ref > 0 && sockets[i].local_addr.sin_port == p) {
				inuse = 1;
				break;
			}
		}
		if(!inuse)
			return p;
	}

	return 0;
}

static struct socket*
tcp_find_client_socket_locked(struct sockaddr_in *src, struct sockaddr_in *dst)
{
	int i;
	struct socket *s;

	for(i = 0; i < NSOCKET; i++) {
		s = &sockets[i];
		if(s->ref == 0)
			continue;
		if(s->family != AF_INET || s->type != SOCK_STREAM)
			continue;
		if(s->local_addr.sin_port != dst->sin_port)
			continue;
		if(s->remote_addr.sin_port != 0 && s->remote_addr.sin_port != src->sin_port)
			continue;
		if(s->remote_addr.sin_addr != 0 && s->remote_addr.sin_addr != src->sin_addr)
			continue;
		if(s->local_addr.sin_addr != 0 && s->local_addr.sin_addr != dst->sin_addr)
			continue;
		return s;
	}

	return 0;
}

int
tcp_connect(struct ifnet *ifp, struct socket *s, struct sockaddr_in *dst)
{
	struct sockaddr_in src;
	uint start;
	uint now;
	ushort p;

	if(s == 0 || dst == 0)
		return -1;
	if(s->type != SOCK_STREAM)
		return -1;
	if(ifp == 0)
		return -1;

	if(ifp->if_flags & IFF_LOOPBACK)
		return socket_stream_connect(s, dst);

	acquire(&socket_lock);
	if(s->state == SOCK_LISTEN || s->tcp.state != TCPS_CLOSED) {
		release(&socket_lock);
		return -1;
	}

	if(s->local_addr.sin_port == 0) {
		p = tcp_alloc_ephemeral_locked();
		if(p == 0) {
			release(&socket_lock);
			return -1;
		}
		s->local_addr.sin_family = AF_INET;
		s->local_addr.sin_port = p;
	}

	memmove(&s->remote_addr, dst, sizeof(*dst));
	s->state = SOCK_CONNECT;
	s->tcp.state = TCPS_SYN_SENT;
	s->tcp.iss = tcp_iss++;
	s->tcp.snd_nxt = s->tcp.iss + 1;
	s->tcp.irs = 0;
	s->tcp.rcv_nxt = 0;
	memmove(&src, &s->local_addr, sizeof(src));
	release(&socket_lock);

	if(tcp_send_segment(ifp, &src, dst, s->tcp.iss, 0, TCP_FLAG_SYN, 0, 0) < 0)
		return -1;

	acquire(&tickslock);
	start = ticks;
	release(&tickslock);

	for(;;) {
		acquire(&socket_lock);
		if(s->state == SOCK_ESTAB && s->tcp.state == TCPS_ESTABLISHED) {
			release(&socket_lock);
			return 0;
		}
		release(&socket_lock);

		acquire(&tickslock);
		now = ticks;
		if(now - start >= TCP_CONNECT_TIMEOUT_TICKS) {
			release(&tickslock);
			break;
		}
		sleep(&ticks, &tickslock);
		release(&tickslock);
	}

	acquire(&socket_lock);
	if(s->tcp.state == TCPS_SYN_SENT) {
		s->tcp.state = TCPS_CLOSED;
		s->state = SOCK_CLOSED;
		memset(&s->remote_addr, 0, sizeof(s->remote_addr));
	}
	release(&socket_lock);

	return -1;
}

int
tcp_output(struct ifnet *ifp, struct sockaddr_in *src,
					 struct sockaddr_in *dst, char *payload, uint len)
{
	int i;
	struct socket *s;
	uint seq;
	uint ack;

	if(src == 0 || dst == 0 || payload == 0)
		return -1;
	if(src->sin_port == 0 || dst->sin_port == 0)
		return -1;
	if(ifp == 0)
		return -1;

	if(ifp->if_flags & IFF_LOOPBACK) {
		// Preserve loopback local-delivery path used by tcptest.
		if(socket_deliver(src, dst, payload, len) < 0)
			return -1;
		return (int)len;
	}

	seq = 0;
	ack = 0;
	acquire(&socket_lock);
	for(i = 0; i < NSOCKET; i++) {
		s = &sockets[i];
		if(s->ref == 0)
			continue;
		if(s->family != AF_INET || s->type != SOCK_STREAM)
			continue;
		if(s->tcp.state != TCPS_ESTABLISHED)
			continue;
		if(s->local_addr.sin_port != src->sin_port)
			continue;
		if(s->remote_addr.sin_port != dst->sin_port)
			continue;
		if(s->remote_addr.sin_addr != dst->sin_addr)
			continue;
		if(s->local_addr.sin_addr != 0 && s->local_addr.sin_addr != src->sin_addr)
			continue;
		seq = s->tcp.snd_nxt;
		ack = s->tcp.rcv_nxt;
		s->tcp.snd_nxt += len;
		break;
	}
	release(&socket_lock);

	if(seq == 0)
		return -1;

	if(tcp_send_segment(ifp, src, dst, seq, ack, TCP_FLAG_ACK | TCP_FLAG_PSH, payload, len) < 0)
		return -1;

	return (int)len;
}

void
tcp_input(struct ifnet *ifp, struct ip_hdr *ip, char *payload, uint len)
{
	struct tcp_hdr *th;
	struct sockaddr_in src;
	struct sockaddr_in dst;
	struct socket *s;
	uint hlen;
	uint plen;
	uint seq;
	uint ack;
	uchar flags;
	uint copylen;
	int need_ack;
	uint ack_seq;
	uint ack_ack;

	if(ifp == 0 || ip == 0 || payload == 0)
		return;
	if(len < TCP_HDR_LEN)
		return;

	th = (struct tcp_hdr*)payload;
	hlen = (uint)((th->off >> 4) * 4);
	if(hlen < TCP_HDR_LEN || hlen > len)
		return;

	seq = net_ntohl(th->seq);
	ack = net_ntohl(th->ack);
	flags = th->flags;
	plen = len - hlen;

	memset(&src, 0, sizeof(src));
	src.sin_family = AF_INET;
	src.sin_port = net_ntohs(th->src_port);
	src.sin_addr = net_ntohl(ip->src);

	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_port = net_ntohs(th->dst_port);
	dst.sin_addr = net_ntohl(ip->dst);

	need_ack = 0;
	ack_seq = 0;
	ack_ack = 0;

	acquire(&socket_lock);
	s = tcp_find_client_socket_locked(&src, &dst);
	if(s == 0) {
		release(&socket_lock);
		return;
	}

	if(s->tcp.state == TCPS_SYN_SENT) {
		if((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
		   ack == s->tcp.snd_nxt) {
			s->tcp.irs = seq;
			s->tcp.rcv_nxt = seq + 1;
			s->tcp.state = TCPS_ESTABLISHED;
			s->state = SOCK_ESTAB;
			need_ack = 1;
			ack_seq = s->tcp.snd_nxt;
			ack_ack = s->tcp.rcv_nxt;
			wakeup(s);
		}
		release(&socket_lock);
		if(need_ack)
			tcp_send_segment(ifp, &dst, &src, ack_seq, ack_ack, TCP_FLAG_ACK, 0, 0);
		return;
	}

	if(s->tcp.state != TCPS_ESTABLISHED) {
		release(&socket_lock);
		return;
	}

	if(plen > 0) {
		if(seq == s->tcp.rcv_nxt && s->recv_buf && s->recv_cap > s->recv_len) {
			copylen = plen;
			if(copylen > s->recv_cap - s->recv_len)
				copylen = s->recv_cap - s->recv_len;
			memmove(s->recv_buf + s->recv_len, payload + hlen, copylen);
			s->recv_len += copylen;
			s->tcp.rcv_nxt += copylen;
			memmove(&s->remote_addr, &src, sizeof(src));
			wakeup(s);
		}
		need_ack = 1;
		ack_seq = s->tcp.snd_nxt;
		ack_ack = s->tcp.rcv_nxt;
	}

	if(flags & TCP_FLAG_RST) {
		s->tcp.state = TCPS_CLOSED;
		s->state = SOCK_CLOSED;
		wakeup(s);
	}

	release(&socket_lock);

	if(need_ack)
		tcp_send_segment(ifp, &dst, &src, ack_seq, ack_ack, TCP_FLAG_ACK, 0, 0);
}
