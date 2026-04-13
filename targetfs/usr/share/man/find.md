# find(1)

## Name
find - Walk file trees and print matching paths.

## Synopsis
```sh
find [path ...] [expression]
```

## Supported Expression Terms
- `-name pattern` - match basename with shell-style wildcard pattern.
- `-path pattern` - match full path with wildcard pattern.
- `-type f|d|l|b|c` - match object kind.
- `-mindepth n` - require walk depth >= `n`.
- `-maxdepth n` - require walk depth <= `n`.
- `-print` - print matching path.

## Notes
- If no path is supplied, defaults to `.`.
- If no action is supplied, defaults to `-print`.
- Uses physical walk semantics (`FTW_PHYS`): symbolic links are not followed.

## Examples
```sh
find . -name "*.c"
find /etc -type f -maxdepth 2
find /dev -type b -print
```

## Source Audit
- Source file: user/find.c
- Last updated: 2026-04-06
