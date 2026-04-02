# netcat(1)

## Name
netcat - TCP/UDP network relay.

## Synopsis
```
netcat [-u] host port
netcat -l [-u] [host] port
```

## Duty
Connect to (or listen for) a TCP or UDP socket and relay data between the
socket and standard I/O. Useful for testing network connectivity and
protocols.

## Options
- `-l` — Listen mode. Bind to `port` (and optionally `host`) and accept
  an incoming connection before relaying data.
- `-u` — Use UDP instead of TCP. In listen mode, uses `recvfrom`/`sendto`
  rather than `accept`.

## Arguments
- `host` — Remote hostname or IP address (client mode) or local bind
  address (server mode, optional).
- `port` — Port number to connect to or listen on.

## Notes
- Without `-l`, operates as a client connecting to `host:port`.
- Data from stdin is sent to the socket; data from the socket is written
  to stdout.

## Examples
```
netcat 192.168.1.1 80          # TCP client
netcat -l 1234                 # TCP server on port 1234
netcat -u 192.168.1.1 5000     # UDP client
netcat -l -u 5000              # UDP server on port 5000
```

## Source Audit
- Source file: user/netcat.c
- Last updated: 2026-04-02
