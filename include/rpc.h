#ifndef _RPC_H_
#define _RPC_H_

#include "types.h"
#include "xdr.h"
#include "socket.h"

/*
 * RPC (Remote Procedure Call) - RFC 1057
 * Built on top of XDR for program/version/procedure dispatch
 */

/* RPC message version */
#define RPC_MSG_VERSION  2

/* Message types */
typedef enum {
  CALL = 0,
  REPLY = 1
} rpc_msg_type;

/* Reply status */
typedef enum {
  MSG_ACCEPTED = 0,
  MSG_DENIED = 1
} rpc_reply_stat;

/* Accepted reply status */
typedef enum {
  SUCCESS = 0,
  PROG_UNAVAIL = 1,
  PROG_MISMATCH = 2,
  PROC_UNAVAIL = 3,
  GARBAGE_ARGS = 4,
  SYSTEM_ERR = 5
} rpc_accept_stat;

/* Rejected reply status */
typedef enum {
  RPC_MISMATCH = 0,
  AUTH_ERROR = 1
} rpc_reject_stat;

/* Authentication types */
typedef enum {
  AUTH_NONE = 0,
  AUTH_UNIX = 1,
  AUTH_SHORT = 2,
  AUTH_DES = 3
} rpc_auth_flavor;

/*
 * RPC authentication info
 */
typedef struct {
  rpc_auth_flavor flavor;
  uint len;
  char data[400];  /* Variable-length auth data */
} rpc_auth;

/*
 * RPC call header (follows msg_type and xid in call message)
 */
typedef struct {
  uint rpcvers;           /* Always RPC_MSG_VERSION (2) */
  uint prog;              /* Program number */
  uint vers;              /* Program version */
  uint proc;              /* Procedure number */
  rpc_auth cred;          /* Caller's credentials */
  rpc_auth verf;          /* Caller's verifier */
} rpc_call_header;

/*
 * RPC reply header
 */
typedef struct {
  rpc_reply_stat stat;    /* MSG_ACCEPTED or MSG_DENIED */
  rpc_auth verf;          /* Server verifier */
  union {
    rpc_accept_stat accept_stat;
    rpc_reject_stat reject_stat;
  } u;
} rpc_reply_header;

/*
 * RPC message header (common to all messages)
 */
typedef struct {
  uint xid;               /* Transaction ID */
  rpc_msg_type mtype;     /* CALL or REPLY */
} rpc_msg_header;

/*
 * RPC client state
 */
typedef struct {
  uint prog;              /* Program number */
  uint vers;              /* Version number */
  uint xid;               /* Next transaction ID */
  struct in_addr server;  /* Server IP address */
  ushort port;            /* Server port (usually 111 for portmapper) */
  uint timeout_ms;        /* Timeout in milliseconds */
  int sock;               /* UDP socket (if needed) */
} rpc_client;

/* XDR codecs for RPC structures */
int xdr_rpc_auth(XDR *xdrs, rpc_auth *auth);
int xdr_rpc_call_header(XDR *xdrs, rpc_call_header *call);
int xdr_rpc_reply_header(XDR *xdrs, rpc_reply_header *reply);
int xdr_rpc_msg_header(XDR *xdrs, rpc_msg_header *msg);
int rpc_udp_send(rpc_client *client, char *callbuf, uint calllen);
int rpc_udp_exchange(rpc_client *client, char *callbuf, uint calllen,
                     char *replybuf, uint replycap, uint *replylen);

/* RPC client functions */
rpc_client *rpc_create(uint prog, uint vers, struct in_addr server, ushort port);
void rpc_destroy(rpc_client *client);
int rpc_call(rpc_client *client, uint proc,
             int (*xdr_args)(XDR *, void *),
             void *args,
             int (*xdr_result)(XDR *, void *),
             void *result);

#endif /* _RPC_H_ */
