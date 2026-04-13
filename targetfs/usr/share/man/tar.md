# tar(1)

## Name
tar - Create, list, and extract ustar archives.

## Synopsis
```sh
tar -c [-v] -f archive.tar path ...
tar -t [-v] [-z] -f archive.tar[.gz]
tar -x [-v] [-z] -f archive.tar[.gz]
```

## Duty
`tar` handles POSIX ustar archives for three core workflows:
- Create archive (`-c`)
- List archive contents (`-t`)
- Extract archive contents (`-x`)

It supports regular files, directories, symbolic links, and hard links.

## Options
- `-c` : create archive.
- `-t` : list entries in archive.
- `-x` : extract archive.
- `-f archive` : archive file path (required).
- `-z` : enable gzip input handling for list/extract.
- `-v` : verbose output (prints each extracted path).

## Gzip Support
- `tar -t` and `tar -x` support gzip-compressed archives.
- `tar -c -z` writes gzip archives using valid deflate stored blocks.
- Compression is auto-enabled for archive names ending in `.gz` or `.tgz`.
- `-z` explicitly forces gzip decompression for input.

## Safety
- Extraction rejects unsafe archive paths (`/absolute` and `..` traversal paths).
- Parent directories are created as needed during extraction.

## Exit Status
- `0` on success.
- `1` on usage errors, archive parse/checksum errors, or filesystem failures.

## Examples
```sh
tar -c -f src.tar user include docs
tar -t -f src.tar
tar -x -f src.tar
tar -xzf src.tar.gz
```

## Source Audit
- Source file: user/tar.c
- Shared gzip code: user/gzip.c
- Last updated: 2026-04-06
