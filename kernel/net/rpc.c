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
  /* TODO: Close socket when UDP layer is integrated */
  kfree((char *)client);
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
  XDR callxdr;
  rpc_msg_header msg_hdr;
  rpc_call_header call_hdr;

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

  /* uint calllen = xdr_getpos(&callxdr); */  /* TODO: Send via UDP */

  /* TODO: Send callbuf via UDP to client->server:client->port */
  /* TODO: Receive reply and decode using xdr_result */

  return 0;
}
