# bunzip2(1)

## Name
bunzip2 - Decompress bzip2-compressed files.

## Synopsis
```sh
bunzip2 [-c] [-k] [file ...]
```

## Duty
`bunzip2` reads bzip2 compressed files and writes uncompressed output either
to files or to standard output.

## Options
- `-c` : write uncompressed data to stdout (do not create output files).
- `-k` : keep input files after successful decompression.

## Behavior
- Default output naming:
  - `name.bz2` -> `name`
  - `name.tar.bz2` -> `name.tar`
  - other names -> `name.out`
- Without `-k`, input files are removed after successful decompression.
- With no `file` arguments, `bunzip2` reads stdin and writes stdout.

## Exit Status
- `0` on success.
- `1` if any input fails to decompress or file operations fail.

## Examples
```sh
bunzip2 archive.tar.bz2
bunzip2 -k kernel.log.bz2
bunzip2 -c image.bz2 > image.raw
```

## Source Audit
- Source file: user/bunzip2.c
- Shared decompression code: libc/bzip2.c
- Last updated: 2026-04-14
