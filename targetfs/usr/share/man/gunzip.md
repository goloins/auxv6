# gunzip(1)

## Name
gunzip - Decompress gzip-compressed files.

## Synopsis
```sh
gunzip [-c] [-k] [file ...]
```

## Duty
`gunzip` reads gzip streams (`.gz` / `.tgz` or any valid gzip data) and writes
uncompressed output either to files or to standard output.

## Options
- `-c` : write uncompressed data to stdout (do not create output files).
- `-k` : keep input files after successful decompression.

## Behavior
- Default output naming:
- `name.gz` -> `name`
- `name.tgz` -> `name.tar`
- other names -> `name.out`
- Without `-k`, input files are removed after successful decompression.
- With no `file` arguments, `gunzip` reads stdin and writes stdout.

## Exit Status
- `0` on success.
- `1` if any input fails to decompress or file operations fail.

## Examples
```sh
gunzip archive.tar.gz
gunzip -k kernel.log.gz
gunzip -c image.gz > image.raw
```

## Source Audit
- Source file: user/gunzip.c
- Shared inflate code: user/gzip.c
- Last updated: 2026-04-06
