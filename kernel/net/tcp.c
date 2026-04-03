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
#define TCP_MAX_SEGMENT_DATA (MBUF_SIZE - TCP_HDR_LEN - 40)

extern struct socket sockets[NSOCKET];
extern struct spinlock socket_lock;
extern struct spinlock tickslock;
extern uint ticks;
// Shared ISN counter and ephemeral-port allocator defined in socket.c.
extern uint tcp_iss;

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

static ushort
tcp_recv_window_locked(struct socket *s)
{
	uint win;

	if(s == 0 || s->recv_cap <= s->recv_len)
		return 0;

	win = s->recv_cap - s->recv_len;
	if(win > 0xffffU)
		win = 0xffffU;
	return (ushort)win;
}

static int
tcp_send_segment(struct ifnet *ifp, struct sockaddr_in *src, struct sockaddr_in *dst,
								 uint seq, uint ack, uchar flags, ushort win,
								 char *payload, uint len)
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
	th->win = net_htons(win);
	th->csum = 0;
	th->urg = 0;

	if(len > 0)
		memmove(buf + TCP_HDR_LEN, payload, len);

	seglen = TCP_HDR_LEN + len;
	th->csum = net_htons(tcp_checksum(src->sin_addr, dst->sin_addr, buf, seglen));

	return ip_output(ifp, NET_IP_TCP, src->sin_addr, dst->sin_addr, buf, seglen);
}

int
tcp_send_ack(struct socket *s)
{
	struct sockaddr_in src;
	struct sockaddr_in dst;
	struct ifnet *ifp;
	uint route_src;
	uint seq;
	uint ack;
	ushort win;

	if(s == 0 || s->type != SOCK_STREAM)
		return -1;

	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	seq = 0;
	ack = 0;
	win = 0;

	acquire(&socket_lock);
	if(s->state != SOCK_ESTAB ||
	   (s->tcp.state != TCPS_ESTABLISHED && s->tcp.state != TCPS_CLOSE_WAIT &&
	    s->tcp.state != TCPS_FIN_WAIT_1 && s->tcp.state != TCPS_FIN_WAIT_2) ||
	   s->remote_addr.sin_port == 0) {
		release(&socket_lock);
		return -1;
	}

	memmove(&src, &s->local_addr, sizeof(src));
	memmove(&dst, &s->remote_addr, sizeof(dst));
	seq = s->tcp.snd_nxt;
	ack = s->tcp.rcv_nxt;
	win = tcp_recv_window_locked(s);
	release(&socket_lock);

	route_src = 0;
	ifp = route_lookup(dst.sin_addr, &route_src, 0);
	if(ifp == 0)
		return -1;
	if(src.sin_addr == 0)
		src.sin_addr = route_src;

	return tcp_send_segment(ifp, &src, &dst, seq, ack, TCP_FLAG_ACK, win, 0, 0);
}

// Sequence number comparison (handles wraparound)
// Returns true if a <= b in sequence space
static int
tcp_seq_leq(uint a, uint b)
{
	return (int)(a - b) <= 0;
}

// TCP close - initiate graceful shutdown
// Called when socket is being closed
int
tcp_close(struct socket *s, struct ifnet *ifp)
{
	struct sockaddr_in src, dst;
	int send_fin = 0;
	uint fin_seq;
	uint ack_ack;
	ushort win;

	if(s == 0 || s->type != SOCK_STREAM)
		return -1;
	if(ifp == 0 || (ifp->if_flags & IFF_LOOPBACK))
		return 0;  // Loopback handled elsewhere

	acquire(&socket_lock);
	
	switch(s->tcp.state) {
	case TCPS_ESTABLISHED:
		// Active close: send FIN, move to FIN_WAIT_1
		s->tcp.fin_seq = s->tcp.snd_nxt;
		s->tcp.snd_nxt++;
		s->tcp.state = TCPS_FIN_WAIT_1;
		send_fin = 1;
		break;
		
	case TCPS_CLOSE_WAIT:
		// Passive close: peer already sent FIN, send ours
		s->tcp.fin_seq = s->tcp.snd_nxt;
		s->tcp.snd_nxt++;
		s->tcp.state = TCPS_LAST_ACK;
		send_fin = 1;
		break;
		
	case TCPS_SYN_SENT:
		// Haven't connected yet, just reset
		s->tcp.state = TCPS_CLOSED;
		s->state = SOCK_CLOSED;
		break;
		
	default:
		// Already closing or closed
		break;
	}
	
	if(send_fin) {
		fin_seq = s->tcp.fin_seq;
		ack_ack = s->tcp.rcv_nxt;
		win = tcp_recv_window_locked(s);
		memmove(&src, &s->local_addr, sizeof(src));
		memmove(&dst, &s->remote_addr, sizeof(dst));
	}
	
	// Free any unacked buffer
	if(s->tcp.unacked_buf) {
		kfree(s->tcp.unacked_buf);
		s->tcp.unacked_buf = 0;
	}

	// Record send time so the retransmit timer can fire on FIN loss.
	if(send_fin) {
		acquire(&tickslock);
		s->tcp.last_send = ticks;
		release(&tickslock);
		s->tcp.retransmits = 0;
	}

	release(&socket_lock);
	
	if(send_fin) {
		tcp_send_segment(ifp, &src, &dst, fin_seq, ack_ack, 
			TCP_FLAG_FIN | TCP_FLAG_ACK, win, 0, 0);
	}
	
	return 0;
}

// Retransmit timeout check - called periodically
// Returns 1 if a timeout occurred and was handled
int
tcp_retransmit_check(struct socket *s, struct ifnet *ifp)
{
	struct sockaddr_in src, dst;
	char *data;
	uint len, seq, ack;
	uint now;
	ushort win;
	int do_retransmit = 0;
	int do_reset = 0;

	if(s == 0 || s->type != SOCK_STREAM)
		return 0;
	if(ifp == 0 || (ifp->if_flags & IFF_LOOPBACK))
		return 0;

	// Initialize for safety
	seq = 0;
	ack = 0;
	win = 0;
	data = 0;
	len = 0;
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));

	acquire(&socket_lock);
	
	if(s->tcp.state != TCPS_ESTABLISHED && s->tcp.state != TCPS_FIN_WAIT_1)
		goto out;
	if(s->tcp.unacked_buf == 0)
		goto out;

	acquire(&tickslock);
	now = ticks;
	release(&tickslock);
	
	if(now - s->tcp.last_send < s->tcp.rto)
		goto out;
	
	// Timeout expired
	s->tcp.retransmits++;
	seq = s->tcp.unacked_seq;
	ack = s->tcp.rcv_nxt;
	win = tcp_recv_window_locked(s);
	memmove(&src, &s->local_addr, sizeof(src));
	memmove(&dst, &s->remote_addr, sizeof(dst));
	
	if(s->tcp.retransmits > TCP_MAX_RETRANSMIT) {
		// Give up - reset connection
		do_reset = 1;
		kfree(s->tcp.unacked_buf);
		s->tcp.unacked_buf = 0;
		s->tcp.state = TCPS_CLOSED;
		s->state = SOCK_CLOSED;
		// do_retransmit is repurposed: -1 means "also deref close_pending"
		if(s->tcp.close_pending) {
			s->tcp.close_pending = 0;
			do_retransmit = -1;
		}
		wakeup(s);
	} else {
		// Retransmit
		do_retransmit = 1;
		data = s->tcp.unacked_buf;
		len = s->tcp.unacked_len;
		
		// Exponential backoff
		s->tcp.rto *= 2;
		if(s->tcp.rto > TCP_RTO_MAX)
			s->tcp.rto = TCP_RTO_MAX;
		
		acquire(&tickslock);
		s->tcp.last_send = ticks;
		release(&tickslock);
	}

out:
	release(&socket_lock);
	
	if(do_retransmit) {
		// do_retransmit == -1 means giveup + close_pending deref needed
		if(do_retransmit == -1) {
			socket_deref(s);
			// Also notify peer with RST if we have an address.
			if(dst.sin_addr)
				tcp_send_segment(ifp, &src, &dst, seq, ack, TCP_FLAG_RST, 0, 0, 0);
			return 0;
		}
		tcp_send_segment(ifp, &src, &dst, seq, ack, 
			TCP_FLAG_ACK | TCP_FLAG_PSH, win, data, len);
		return 1;
	}
	
	if(do_reset) {
		// Send RST
		tcp_send_segment(ifp, &src, &dst, seq, ack, TCP_FLAG_RST, 0, 0, 0);
	}
	
	return 0;
}

// TIME_WAIT cleanup - called periodically
void
tcp_timewait_check(struct socket *s)
{
	uint now;
	int do_deref;

	if(s == 0 || s->type != SOCK_STREAM)
		return;

	acquire(&socket_lock);
	
	if(s->tcp.state != TCPS_TIME_WAIT) {
		release(&socket_lock);
		return;
	}

	acquire(&tickslock);
	now = ticks;
	release(&tickslock);
	
	if(now - s->tcp.time_wait_start >= TCP_TIME_WAIT_TICKS) {
		s->tcp.state = TCPS_CLOSED;
		s->state = SOCK_CLOSED;
		do_deref = s->tcp.close_pending;
		if(do_deref)
			s->tcp.close_pending = 0;
		wakeup(s);
		release(&socket_lock);
		if(do_deref)
			socket_deref(s);
		return;
	}
	
	release(&socket_lock);
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
		p = socket_alloc_port_locked();
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
	s->tcp.snd_una = s->tcp.iss;
	s->tcp.snd_nxt = s->tcp.iss + 1;
	s->tcp.irs = 0;
	s->tcp.rcv_nxt = 0;
	s->tcp.rcv_wnd = 4096;
	s->tcp.rto = TCP_RTO_INIT;
	s->tcp.rtt_est = 0;
	s->tcp.retransmits = 0;
	s->tcp.unacked_buf = 0;
	s->tcp.unacked_len = 0;
	s->tcp.unacked_seq = 0;
	s->tcp.fin_seq = 0;
	s->tcp.time_wait_start = 0;
	
	acquire(&tickslock);
	s->tcp.last_send = ticks;
	release(&tickslock);
	
	memmove(&src, &s->local_addr, sizeof(src));
	release(&socket_lock);

	if(tcp_send_segment(ifp, &src, dst, s->tcp.iss, 0, TCP_FLAG_SYN,
		   tcp_recv_window_locked(s), 0, 0) < 0)
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
	ushort win;

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

	// Clamp to max segment size
	if(len > TCP_MAX_SEGMENT_DATA)
		len = TCP_MAX_SEGMENT_DATA;

	seq = 0;
	ack = 0;
	win = 0;
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
		win = tcp_recv_window_locked(s);
		s->tcp.snd_nxt += len;
		
		// Track unacked data for potential retransmission
		// Simplified: only keep one outstanding segment
		if(s->tcp.unacked_buf == 0) {
			s->tcp.unacked_buf = kalloc();
			if(s->tcp.unacked_buf) {
				memmove(s->tcp.unacked_buf, payload, len);
				s->tcp.unacked_len = len;
				s->tcp.unacked_seq = seq;
				acquire(&tickslock);
				s->tcp.last_send = ticks;
				release(&tickslock);
			}
		}
		break;
	}
	release(&socket_lock);

	if(seq == 0)
		return -1;

	if(tcp_send_segment(ifp, src, dst, seq, ack, TCP_FLAG_ACK | TCP_FLAG_PSH,
		   win, payload, len) < 0)
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
	uint ack_num;
	uchar flags;
	uint copylen;
	int need_ack;
	uint ack_seq;
	uint ack_ack;
	ushort ack_win;
	uchar ack_flags;

	if(ifp == 0 || ip == 0 || payload == 0)
		return;
	if(len < TCP_HDR_LEN)
		return;

	th = (struct tcp_hdr*)payload;
	hlen = (uint)((th->off >> 4) * 4);
	if(hlen < TCP_HDR_LEN || hlen > len)
		return;

	// Verify TCP checksum before doing any processing.
	{
		ushort expected;
		ushort received;
		received = net_ntohs(th->csum);
		th->csum = 0;
		expected = tcp_checksum(net_ntohl(ip->src), net_ntohl(ip->dst), payload, len);
		th->csum = net_htons(received);
		if(expected != received)
			return;
	}

	seq = net_ntohl(th->seq);
	ack_num = net_ntohl(th->ack);
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
	ack_win = 0;
	ack_flags = TCP_FLAG_ACK;

	acquire(&socket_lock);
	s = tcp_find_client_socket_locked(&src, &dst);
	if(s == 0) {
		release(&socket_lock);
		// Send RST for packets to non-existent connections
		if(!(flags & TCP_FLAG_RST)) {
			tcp_send_segment(ifp, &dst, &src, ack_num, seq + plen + 
				((flags & TCP_FLAG_SYN) ? 1 : 0) + ((flags & TCP_FLAG_FIN) ? 1 : 0),
				TCP_FLAG_RST | TCP_FLAG_ACK, 0, 0, 0);
		}
		return;
	}

	// Handle RST - always reset connection
	if(flags & TCP_FLAG_RST) {
		int do_deref;
		if(s->tcp.unacked_buf) {
			kfree(s->tcp.unacked_buf);
			s->tcp.unacked_buf = 0;
		}
		s->tcp.state = TCPS_CLOSED;
		s->state = SOCK_CLOSED;
		do_deref = s->tcp.close_pending;
		if(do_deref)
			s->tcp.close_pending = 0;
		wakeup(s);
		release(&socket_lock);
		if(do_deref)
			socket_deref(s);
		return;
	}

	// Process ACK - free unacked data if acknowledged
	if((flags & TCP_FLAG_ACK) && s->tcp.unacked_buf) {
		// Check if ACK covers our unacked data
		if(tcp_seq_leq(s->tcp.unacked_seq + s->tcp.unacked_len, ack_num)) {
			kfree(s->tcp.unacked_buf);
			s->tcp.unacked_buf = 0;
			s->tcp.unacked_len = 0;
			s->tcp.snd_una = ack_num;
			s->tcp.retransmits = 0;
			
			// Update RTT estimate (simplified)
			acquire(&tickslock);
			uint rtt = ticks - s->tcp.last_send;
			release(&tickslock);
			if(s->tcp.rtt_est == 0)
				s->tcp.rtt_est = rtt;
			else
				s->tcp.rtt_est = (s->tcp.rtt_est * 7 + rtt) / 8;
			s->tcp.rto = s->tcp.rtt_est * 2;
			if(s->tcp.rto < TCP_RTO_MIN)
				s->tcp.rto = TCP_RTO_MIN;
			if(s->tcp.rto > TCP_RTO_MAX)
				s->tcp.rto = TCP_RTO_MAX;
		}
	}

	switch(s->tcp.state) {
	case TCPS_SYN_SENT:
		// Expecting SYN-ACK
		if((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
		   ack_num == s->tcp.snd_nxt) {
			s->tcp.irs = seq;
			s->tcp.rcv_nxt = seq + 1;
			s->tcp.snd_una = ack_num;
			s->tcp.state = TCPS_ESTABLISHED;
			s->state = SOCK_ESTAB;
			need_ack = 1;
			ack_seq = s->tcp.snd_nxt;
			ack_ack = s->tcp.rcv_nxt;
			ack_win = tcp_recv_window_locked(s);
			wakeup(s);
		}
		break;

	case TCPS_ESTABLISHED:
		// Track peer's advertised receive window.
		if(flags & TCP_FLAG_ACK)
			s->tcp.snd_wnd = (uint)net_ntohs(th->win);

		// Process incoming data
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
			} else if(seq != s->tcp.rcv_nxt) {
				// Out-of-order segment: RFC 5681 requires an immediate duplicate ACK.
				ack_seq = s->tcp.snd_nxt;
				ack_ack = s->tcp.rcv_nxt;
				ack_win = tcp_recv_window_locked(s);
				release(&socket_lock);
				tcp_send_segment(ifp, &dst, &src, ack_seq, ack_ack,
					TCP_FLAG_ACK, ack_win, 0, 0);
				return;
			}
			need_ack = 1;
		}
		
		// Handle FIN from peer (passive close)
		if((flags & TCP_FLAG_FIN) && seq + plen == s->tcp.rcv_nxt) {
			s->tcp.rcv_nxt++;  // FIN consumes a sequence number after all payload bytes.
			s->tcp.state = TCPS_CLOSE_WAIT;
			need_ack = 1;
			wakeup(s);  // Wake up any readers to see EOF
		}
		
		if(need_ack) {
			ack_seq = s->tcp.snd_nxt;
			ack_ack = s->tcp.rcv_nxt;
			ack_win = tcp_recv_window_locked(s);
		}
		break;

	case TCPS_FIN_WAIT_1:
		// We sent FIN, waiting for ACK and/or peer's FIN
		if((flags & TCP_FLAG_ACK) && ack_num == s->tcp.fin_seq + 1) {
			// Our FIN was ACKed
			s->tcp.snd_una = ack_num;
			if((flags & TCP_FLAG_FIN) && seq + plen == s->tcp.rcv_nxt) {
				// Simultaneous close: FIN+ACK, go to TIME_WAIT
				s->tcp.rcv_nxt++;
				s->tcp.state = TCPS_TIME_WAIT;
				acquire(&tickslock);
				s->tcp.time_wait_start = ticks;
				release(&tickslock);
				need_ack = 1;
			} else {
				// Just ACK, go to FIN_WAIT_2
				s->tcp.state = TCPS_FIN_WAIT_2;
			}
		} else if((flags & TCP_FLAG_FIN) && seq + plen == s->tcp.rcv_nxt) {
			// Peer's FIN without ACKing ours (simultaneous close)
			s->tcp.rcv_nxt++;
			need_ack = 1;
			// Stay in FIN_WAIT_1 until our FIN is ACKed
		}
		if(need_ack) {
			ack_seq = s->tcp.snd_nxt;
			ack_ack = s->tcp.rcv_nxt;
			ack_win = tcp_recv_window_locked(s);
		}
		break;

	case TCPS_FIN_WAIT_2:
		// Our FIN was ACKed, waiting for peer's FIN
		if((flags & TCP_FLAG_FIN) && seq + plen == s->tcp.rcv_nxt) {
			s->tcp.rcv_nxt++;
			s->tcp.state = TCPS_TIME_WAIT;
			acquire(&tickslock);
			s->tcp.time_wait_start = ticks;
			release(&tickslock);
			need_ack = 1;
			ack_seq = s->tcp.snd_nxt;
			ack_ack = s->tcp.rcv_nxt;
			ack_win = tcp_recv_window_locked(s);
		}
		break;

	case TCPS_CLOSE_WAIT:
		// Peer sent FIN, we haven't closed yet - just ACK anything
		if(plen > 0 || (flags & TCP_FLAG_FIN)) {
			need_ack = 1;
			ack_seq = s->tcp.snd_nxt;
			ack_ack = s->tcp.rcv_nxt;
			ack_win = tcp_recv_window_locked(s);
		}
		break;

	case TCPS_LAST_ACK:
		// We sent FIN after CLOSE_WAIT, waiting for ACK
		if((flags & TCP_FLAG_ACK) && ack_num == s->tcp.fin_seq + 1) {
			s->tcp.state = TCPS_CLOSED;
			s->state = SOCK_CLOSED;
			if(s->tcp.close_pending) {
				s->tcp.close_pending = 0;
				need_ack = -1;  // signal: deref after release
			}
			wakeup(s);
		}
		break;

	case TCPS_TIME_WAIT:
		// Handle retransmitted FINs
		if(flags & TCP_FLAG_FIN) {
			need_ack = 1;
			ack_seq = s->tcp.snd_nxt;
			ack_ack = s->tcp.rcv_nxt;
			ack_win = tcp_recv_window_locked(s);
		}
		break;
	}

	release(&socket_lock);

	if(need_ack == -1)
		socket_deref(s);
	else if(need_ack)
		tcp_send_segment(ifp, &dst, &src, ack_seq, ack_ack, ack_flags, ack_win, 0, 0);
}


// SYN retransmit helper: called from tcp_slowtimo when a socket is in SYN_SENT.
static void
tcp_syn_retransmit_ifp(struct socket *s, struct ifnet *ifp)
{
	struct sockaddr_in src, dst;
	uint iss;
	ushort win;
	uint now;
	int do_retransmit = 0;

	acquire(&socket_lock);
	if(s->tcp.state != TCPS_SYN_SENT) {
		release(&socket_lock);
		return;
	}

	acquire(&tickslock);
	now = ticks;
	release(&tickslock);

	if(now - s->tcp.last_send < s->tcp.rto) {
		release(&socket_lock);
		return;
	}

	s->tcp.retransmits++;
	if(s->tcp.retransmits > TCP_MAX_RETRANSMIT) {
		s->tcp.state = TCPS_CLOSED;
		s->state = SOCK_CLOSED;
		wakeup(s);
	} else {
		do_retransmit = 1;
		s->tcp.rto *= 2;
		if(s->tcp.rto > TCP_RTO_MAX)
			s->tcp.rto = TCP_RTO_MAX;
		acquire(&tickslock);
		s->tcp.last_send = ticks;
		release(&tickslock);
	}
	iss = s->tcp.iss;
	win = tcp_recv_window_locked(s);
	memmove(&src, &s->local_addr, sizeof(src));
	memmove(&dst, &s->remote_addr, sizeof(dst));
	release(&socket_lock);

	if(do_retransmit)
		tcp_send_segment(ifp, &src, &dst, iss, 0, TCP_FLAG_SYN, win, 0, 0);
}

// FIN retransmit helper: retransmit lost FIN in FIN_WAIT_1 or LAST_ACK.
static void
tcp_fin_retransmit(struct socket *s, struct ifnet *ifp)
{
	struct sockaddr_in src, dst;
	uint fin_seq, rcv_nxt;
	ushort win;
	uint now;
	int do_retransmit = 0;
	int do_deref = 0;

	acquire(&socket_lock);
	if(s->tcp.state != TCPS_FIN_WAIT_1 && s->tcp.state != TCPS_LAST_ACK) {
		release(&socket_lock);
		return;
	}
	// FIN outstanding = fin_seq not yet ACKed.
	if(s->tcp.fin_seq == 0 || tcp_seq_leq(s->tcp.fin_seq + 1, s->tcp.snd_una)) {
		release(&socket_lock);
		return;
	}
	// Unacked data in flight: let the data retransmit path handle it first.
	if(s->tcp.unacked_buf != 0) {
		release(&socket_lock);
		return;
	}

	acquire(&tickslock);
	now = ticks;
	release(&tickslock);

	if(now - s->tcp.last_send < s->tcp.rto) {
		release(&socket_lock);
		return;
	}

	s->tcp.retransmits++;
	if(s->tcp.retransmits > TCP_MAX_RETRANSMIT) {
		s->tcp.state = TCPS_CLOSED;
		s->state = SOCK_CLOSED;
		do_deref = s->tcp.close_pending;
		if(do_deref)
			s->tcp.close_pending = 0;
		wakeup(s);
	} else {
		do_retransmit = 1;
		s->tcp.rto *= 2;
		if(s->tcp.rto > TCP_RTO_MAX)
			s->tcp.rto = TCP_RTO_MAX;
		acquire(&tickslock);
		s->tcp.last_send = ticks;
		release(&tickslock);
	}
	fin_seq = s->tcp.fin_seq;
	rcv_nxt = s->tcp.rcv_nxt;
	win = tcp_recv_window_locked(s);
	memmove(&src, &s->local_addr, sizeof(src));
	memmove(&dst, &s->remote_addr, sizeof(dst));
	release(&socket_lock);

	if(do_retransmit)
		tcp_send_segment(ifp, &src, &dst, fin_seq, rcv_nxt,
			TCP_FLAG_FIN | TCP_FLAG_ACK, win, 0, 0);

	if(do_deref)
		socket_deref(s);
}

// Called from timer interrupt every 100ms (every 10 ticks at 100Hz).
// Handles SYN/data/FIN retransmission timeouts and TIME_WAIT cleanup.
void
tcp_slowtimo(void)
{
	int i;
	struct socket *s;
	struct ifnet *ifp;
	uint route_src;

	for(i = 0; i < NSOCKET; i++) {
		s = &sockets[i];

		// Quick check without lock (safe on uniprocessor).
		if(s->ref == 0)
			continue;
		if(s->family != AF_INET || s->type != SOCK_STREAM)
			continue;

		// TIME_WAIT expiry.
		if(s->tcp.state == TCPS_TIME_WAIT) {
			tcp_timewait_check(s);
			continue;
		}

		// All remaining paths need the outgoing interface.
		ifp = route_lookup(s->remote_addr.sin_addr, &route_src, 0);
		if(ifp == 0)
			continue;

		// Retransmit lost SYN.
		if(s->tcp.state == TCPS_SYN_SENT) {
			tcp_syn_retransmit_ifp(s, ifp);
			continue;
		}

		// Retransmit lost FIN (FIN_WAIT_1 or LAST_ACK, no data in flight).
		if(s->tcp.state == TCPS_FIN_WAIT_1 || s->tcp.state == TCPS_LAST_ACK)
			tcp_fin_retransmit(s, ifp);

		// Retransmit lost data segment.
		if(s->tcp.state == TCPS_ESTABLISHED || s->tcp.state == TCPS_FIN_WAIT_1)
			tcp_retransmit_check(s, ifp);
	}
}
