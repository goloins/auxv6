# 6get HTTP Transfer Notes

Date: 2026-04-02

## Scope

This note documents current `6get` transfer behavior in auxv6 userland.

## URL and Protocol Support

- Supports `http://host[:port]/path`.
- Does not support TLS/HTTPS transport.
- `https://...` input fails with an explicit error.

## Request Behavior

- Sends HTTP/1.0 request line.
- Sends `Host` header (includes `:port` when non-default).
- Sends `Connection: close`.

## Redirect Handling

- Follows up to 4 redirects (`3xx`) when `Location` is HTTP.
- Relative `Location` values are resolved against the current URL path.
- Redirects to HTTPS are reported and not followed.

## Completion and Hang Avoidance

- If `Content-Length` is present, transfer completes exactly when that many body bytes are written.
- If `Content-Length` is missing, transfer relies on connection close.
- After headers are parsed, `recvtimeout` is used with an idle-limit cutoff to avoid hanging on servers that keep sockets open.

## Progress Output

- If `Content-Length` is present, `6get` prints a progress bar in-place:
  - percentage
  - bytes received
  - total bytes
- Progress output is suppressed by `-q`.

## Known Limits

- No chunked-transfer decoding yet.
- No resume/range support yet.
- No authentication/cookies.

## Operational Notes

- In constrained stack environments, keep large transfer buffers out of local stack frames.
- Current implementation uses static transfer/header buffers to avoid one-page stack overflow failures seen during development.
