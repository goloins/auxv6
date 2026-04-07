# sed(1)

## Name
sed - Stream editor (baseline command subset).

## Synopsis
```sh
sed [-n] script [file ...]
sed [-n] -e script [-e script ...] [file ...]
```

## Supported Commands
- `s/regex/repl/[g]` - substitute first match, or all matches with `g`.
- `p` - print current pattern space.
- `d` - delete current pattern space.

## Addressing
- Optional single regex address: `/regex/command`

## Notes
- `-n` suppresses default output.
- Replacement supports `&` for the whole match.
- Multiple commands can be separated with `;`.

## Examples
```sh
sed 's/foo/bar/g' file.txt
sed -n '/ERROR/p' kernel.log
sed -e '/^#/d' -e 's/[[:space:]]\+$//' conf
```

## Source Audit
- Source file: user/sed.c
- Last updated: 2026-04-06
