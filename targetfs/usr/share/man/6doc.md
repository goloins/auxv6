# 6doc(1)

## Name
6doc - Convert a Markdown file to HTML.

## Synopsis
```
6doc input.md
6doc input.md output.html
```

## Duty
Read a Markdown file and emit a simple HTML document.

## Arguments
- `input.md` - Source Markdown file.
- `output.html` - Optional output path. If omitted, HTML is written to stdout.

## Notes
- This utility intentionally supports a small Markdown subset.
- Supported constructs: headings (`#` through `######`), unordered lists (`-` or `*`),
  ordered lists (`1.`), fenced code blocks (```), blank-line paragraph breaks,
  and plain paragraph text.
- HTML special characters are escaped in rendered text.

## Examples
```
6doc /usr/share/man/ls.md /tmp/ls.html
6doc README.md > README.html
```

## Source Audit
- Source file: user/6doc.c
- Last updated: 2026-04-09
