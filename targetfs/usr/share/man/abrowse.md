# abrowse

## Name
abrowse - basic text-mode HTTP browser for auxv6

## Synopsis
abrowse http://host[:port]/path
abrowse -V

## Description
abrowse is a lightweight terminal web browser for auxv6.

Current behavior:
- Fetches pages over plain HTTP only.
- Renders text/plain directly.
- Renders markdown-like content (text/markdown, text/x-markdown, or .md URLs).
- Renders basic HTML in a best-effort text mode with link extraction.
- Supports in-page scrolling and keyboard-driven link navigation.

Not supported yet:
- HTTPS/TLS
- Gemini over TLS
- JavaScript/CSS layout fidelity
- Chunked transfer decoding

## Keys
- j / k: scroll line down/up
- Space / b: page down/up
- [ / ]: previous/next detected link
- Enter: follow selected link
- g: prompt for URL
- r: reload current page
- q: quit

## Notes
- abrowse follows HTTP redirects (up to a small fixed limit).
- Relative links are resolved against the current page URL.
- HTTPS and Gemini links are currently shown but cannot be opened.

## Examples
abrowse http://example.com/
abrowse http://example.com/docs/readme.md
