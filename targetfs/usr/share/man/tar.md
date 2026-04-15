# tar(1)

## Name
tar - Create, list, and extract ustar/pax/GNU tar archives.

## Synopsis
```sh
tar -c [-v] [-z] [-j] -f archive.tar path ...
tar -t [-v] [-z] [-j] -f archive.tar[.gz|.bz2]
tar -x [-v] [-z] [-j] -f archive.tar[.gz|.bz2]
tar xzvf archive.tar.gz    # Combined format supported
tar xjvf archive.tar.bz2   # Combined format supported
tar xvgz archive.tar.gz    # GNU-style old option word supported (no -f)
```

## Duty
`tar` handles POSIX ustar archives plus common pax and GNU tar extensions for three core workflows:
- Create archive (`-c`)
- List archive contents (`-t`)
- Extract archive contents (`-x`)

It supports regular files, directories, symbolic links, hard links, FIFOs, pax extended headers, and GNU longname/longlink records.

## Options
- `-c` : create archive.
- `-t` : list entries in archive.
- `-x` : extract archive.
- `-f archive` : archive file path (required).
- `-z` : enable gzip input handling for list/extract.
- `-g` : alias for `-z` (GNU-style convenience).
- `-j` : enable bzip2 input handling for list/extract.
- `-v` : verbose output (prints each extracted path).
- In old-style mode (no leading `-`), if `f` is omitted, the first non-option argument is treated as the archive path.

## Compression Support
- `tar -t` and `tar -x` support both gzip and bzip2-compressed archives.
- Compression is auto-detected from archive names ending in `.gz`, `.tgz`, `.bz2`, or `.tar.bz2`.
- `-z` explicitly forces gzip decompression for input.
- `-j` explicitly forces bzip2 decompression for input.
- Combined flag format is supported: `tar xzvf`, `tar xjvf`, `tar xvgz`, etc.

## Format Notes
- Long paths and long link targets are read from GNU longname/longlink records and pax `path` / `linkpath` keys.
- Pax `size` overrides are honored during extraction and creation is emitted with pax headers when names or sizes do not fit plain ustar fields.
- GNU sparse archives and special device extraction are rejected with explicit errors instead of being silently mis-handled.

## Safety
- Extraction rejects unsafe archive paths (`/absolute` and `..` traversal paths).
- Unsafe hard-link targets are rejected during extraction.
- Parent directories are created as needed during extraction.
- Intermediate symlink components are rejected during directory creation.

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
tar xvgz src.tar.gz          # GNU-style option word with gzip alias, no -f
```

## Source Audit
- Source file: user/tar.c
- Shared compression code: libc/gzip.c, libc/bzip2.c
- Last updated: 2026-04-14
