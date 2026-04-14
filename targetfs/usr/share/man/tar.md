# tar(1)

## Name
tar - Create, list, and extract ustar archives.

## Synopsis
```sh
tar -c [-v] [-z] [-j] -f archive.tar path ...
tar -t [-v] [-z] [-j] -f archive.tar[.gz|.bz2]
tar -x [-v] [-z] [-j] -f archive.tar[.gz|.bz2]
tar xzvf archive.tar.gz    # Combined format supported
tar xjvf archive.tar.bz2   # Combined format supported
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
- `-j` : enable bzip2 input handling for list/extract.
- `-v` : verbose output (prints each extracted path).

## Compression Support
- `tar -t` and `tar -x` support both gzip and bzip2-compressed archives.
- Compression is auto-detected from archive names ending in `.gz`, `.tgz`, `.bz2`, or `.tar.bz2`.
- `-z` explicitly forces gzip decompression for input.
- `-j` explicitly forces bzip2 decompression for input.
- Combined flag format is supported: `tar xzvf`, `tar xjvf`, etc.

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
tar xzf src.tar.gz           # Combined flags
tar xjf src.tar.bz2          # Bzip2 archive
tar xzvf src.tar.gz          # Verbose extraction with gzip
```

## Source Audit
- Source file: user/tar.c
- Shared compression code: libc/gzip.c, libc/bzip2.c
- Last updated: 2026-04-14
