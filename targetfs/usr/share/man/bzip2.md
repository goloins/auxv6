# bzip2(1)

## Name
bzip2 - Compress files using the Burrows-Wheeler block sorting compression algorithm.

## Synopsis
```sh
bzip2 [-c] [-k] [-1 to -9] [file ...]
```

## Duty
`bzip2` reads input files and writes bzip2-compressed output. Block sorting
compression typically achieves higher compression ratios than gzip, especially
for text and source code.

## Options
- `-c` : write compressed data to stdout (do not create output files).
- `-k` : keep input files after successful compression.
- `-1` to `-9` : set compression block size (1=100KB, 9=900KB); default is 9.

## Behavior
- Default output naming: `name` -> `name.bz2`
- Without `-k`, input files are removed after successful compression.
- With no `file` arguments, `bzip2` reads stdin and writes stdout.
- Each block is independently compressed, allowing parallel decompression.

## Exit Status
- `0` on success.
- `1` if any input fails to compress or file operations fail.

## Examples
```sh
bzip2 largefile.txt
bzip2 -c data.tar > data.tar.bz2
bzip2 -9k sensitive.dat     # Maximum compression, keep input
```

## Source Audit
- Source file: user/bzip2.c
- Shared compression code: libc/bzip2.c
- Last updated: 2026-04-14
