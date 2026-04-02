# 6get(1)

## Name
6get - download a file over HTTP (without TLS).

## Synopsis
```sh
6get [-d] [-q] [-o output] http://host[:port]/path
6get -h | --help
6get -V | --version
```

## Duty
Connect to an HTTP server over TCP, issue a simple `GET` request, and save the
response body to a local file.

## Options
- `-d` - Enable debug logging to stderr.
- `-q` - Quiet mode; suppress normal progress/status output.
- `-o output` - Write to `output` instead of deriving a filename from the URL path.
- `-h`, `--help` - Print usage.
- `-V`, `--version` - Print tool version string.

## URL Format
- Only `http://` URLs are supported.
- Optional `:port` is supported (default is `80`).
- HTTPS is not supported yet.

## Behavior
- Sends an HTTP/1.0 `GET` request with `Connection: close`.
- Requires a successful `2xx` status code.
- Derived output filename uses the final URL path component.
- If the URL path ends in `/`, `index.html` is used.
- Prints connectivity progress and HTTP status by default.
- Follows up to 4 HTTP redirects when `Location` points to another HTTP URL.

## Examples
```sh
6get http://10.0.2.2/test.txt
6get -o kernel.log http://192.168.1.10:8080/logs/kernel.log
```

## Limitations
- No TLS/HTTPS support.
- Redirect targets requiring HTTPS are not followed.
- No chunked-transfer decoding.

## Diagnostics
- `https://...` URLs fail explicitly because TLS is not implemented yet.
- `3xx` responses print `Location`; HTTP redirects are followed automatically.

## Source Audit
- Source file: user/6get.c
- Last updated: 2026-04-02
