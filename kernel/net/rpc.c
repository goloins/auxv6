/*
 * RPC (Remote Procedure Call) - RFC 1057 client implementation
 */

#include "types.h"
#include "defs.h"
#include "rpc.h"
#include "xdr.h"
#include "socket.h"
#include "stddef.h"

extern void *memcpy(void*, void*, uint);
extern void *memset(void*, int, uint);

/*
 * XDR codecs for RPC structures
 */

int
xdr_rpc_auth(XDR *xdrs, rpc_auth *auth)
{
  if (!xdr_uint(xdrs, (uint *)&auth->flavor))
    return 0;
  if (!xdr_bytes(xdrs, (char **)&auth->data, &auth->len, sizeof(auth->data)))
    return 0;
  return 1;
}

int
xdr_rpc_call_header(XDR *xdrs, rpc_call_header *call)
{
  if (!xdr_uint(xdrs, &call->rpcvers))
    return 0;
  if (!xdr_uint(xdrs, &call->prog))
    return 0;
  if (!xdr_uint(xdrs, &call->vers))
    return 0;
  if (!xdr_uint(xdrs, &call->proc))
    return 0;
  if (!xdr_rpc_auth(xdrs, &call->cred))
    return 0;
  if (!xdr_rpc_auth(xdrs, &call->verf))
    return 0;
  return 1;
}

int
xdr_rpc_reply_header(XDR *xdrs, rpc_reply_header *reply)
{
  if (!xdr_uint(xdrs, (uint *)&reply->stat))
    return 0;
  if (!xdr_rpc_auth(xdrs, &reply->verf))
    return 0;

  if (reply->stat == MSG_ACCEPTED) {
    if (!xdr_uint(xdrs, (uint *)&reply->u.accept_stat))
      return 0;
  } else {
    if (!xdr_uint(xdrs, (uint *)&reply->u.reject_stat))
      return 0;
  }
  return 1;
}

int
xdr_rpc_msg_header(XDR *xdrs, rpc_msg_header *msg)
{
  if (!xdr_uint(xdrs, &msg->xid))
    return 0;
  if (!xdr_uint(xdrs, (uint *)&msg->mtype))
    return 0;
  return 1;
}

/*
 * RPC client creation
 */
rpc_client *
rpc_create(uint prog, uint vers, struct in_addr server, ushort port)
{
  rpc_client *client;

  client = (rpc_client *)kalloc();
  if (client == NULL)
    return NULL;

  memset(client, 0, sizeof(*client));
  client->prog = prog;
  client->vers = vers;
  client->server = server;
  client->port = port;
  client->timeout_ms = 5000;  /* Default 5-second timeout */
  client->xid = 1;            /* Start with XID 1 */
  return client;
}

/*
 * RPC client destruction
 */
void
rpc_destroy(rpc_client *client)
{
  if (client == NULL)
    return;
  kfree((char *)client);
}

int
rpc_udp_send(rpc_client *client, char *callbuf, uint calllen)
{
  struct socket *sock;
  struct sockaddr_in dst;
  int sent;

  if (client == NULL || callbuf == NULL || calllen == 0)
    return -1;

  if (ksock_open_udp(&sock) < 0)
    return -1;

  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_addr = client->server.s_addr;
  dst.sin_port = client->port;

  sent = ksock_sendto(sock, &dst, callbuf, calllen);
  socket_close(sock);
  if (sent < 0)
    return -1;

  return 0;
}

int
rpc_udp_exchange(rpc_client *client, char *callbuf, uint calllen,
                 char *replybuf, uint replycap, uint *replylen)
{
  struct socket *sock;
  struct sockaddr_in dst;
  struct sockaddr_in src;
  int sent;
  int timeout_ticks;
  int n;

  if (client == NULL || callbuf == NULL || calllen == 0 ||
      replybuf == NULL || replylen == NULL)
    return -1;

  if (ksock_open_udp(&sock) < 0)
    return -1;

  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_addr = client->server.s_addr;
  dst.sin_port = client->port;

  sent = ksock_sendto(sock, &dst, callbuf, calllen);
  if (sent < 0) {
    socket_close(sock);
    return -1;
  }

  timeout_ticks = (int)((client->timeout_ms + 9) / 10);
  if (timeout_ticks <= 0)
    timeout_ticks = 1;

  n = ksock_recvfrom_timeout(sock, replybuf, replycap, timeout_ticks, &src);
  socket_close(sock);
  if (n <= 0)
    return -1;

  if (src.sin_addr != client->server.s_addr)
    return -1;
  if (src.sin_port != client->port)
    return -1;

  *replylen = (uint)n;
  return 0;
}

/*
 * Make an RPC call
 *
 * Currently a stub that builds the CALL message but doesn't send it.
 * Will be extended to integrate with actual UDP socket I/O.
 *
 * Returns: 0 on success, -1 on error
 */
int
rpc_call(rpc_client *client, uint proc,
         int (*xdr_args)(XDR *, void *),
         void *args,
         int (*xdr_result)(XDR *, void *),
         void *result)
{
  char callbuf[2048];
  char replybuf[2048];
  XDR callxdr;
  XDR replyxdr;
  rpc_msg_header msg_hdr;
  rpc_msg_header reply_msg;
  rpc_call_header call_hdr;
  rpc_reply_header reply_hdr;
  uint calllen;
  uint replylen;

  if (client == NULL)
    return -1;

  /* Build the CALL message header */
  msg_hdr.xid = client->xid++;
  msg_hdr.mtype = CALL;

  /* Build the call header */
  memset(&call_hdr, 0, sizeof(call_hdr));
  call_hdr.rpcvers = RPC_MSG_VERSION;
  call_hdr.prog = client->prog;
  call_hdr.vers = client->vers;
  call_hdr.proc = proc;
  call_hdr.cred.flavor = AUTH_NONE;
  call_hdr.cred.len = 0;
  call_hdr.verf.flavor = AUTH_NONE;
  call_hdr.verf.len = 0;

  /* Encode the CALL message */
  xdr_init(&callxdr, callbuf, sizeof(callbuf), XDR_ENCODE);

  if (!xdr_rpc_msg_header(&callxdr, &msg_hdr))
    return -1;

  if (!xdr_rpc_call_header(&callxdr, &call_hdr))
    return -1;

  /* Encode arguments */
  if (xdr_args != NULL && !xdr_args(&callxdr, args))
    return -1;

  calllen = xdr_getpos(&callxdr);

  if (xdr_result == NULL)
    return rpc_udp_send(client, callbuf, calllen);

  if (rpc_udp_exchange(client, callbuf, calllen,
                       replybuf, sizeof(replybuf), &replylen) < 0)
    return -1;

  xdr_init(&replyxdr, replybuf, replylen, XDR_DECODE);

  if (!xdr_rpc_msg_header(&replyxdr, &reply_msg))
    return -1;
  if (reply_msg.mtype != REPLY)
    return -1;
  if (reply_msg.xid != msg_hdr.xid)
    return -1;

  if (!xdr_rpc_reply_header(&replyxdr, &reply_hdr))
    return -1;
  if (reply_hdr.stat != MSG_ACCEPTED)
    return -1;
  if (reply_hdr.u.accept_stat != SUCCESS)
    return -1;

  if (!xdr_result(&replyxdr, result))
    return -1;

  return 0;
}
