# file(1)

## Name
file - Identify file type by content inspection.

## Synopsis
```
file path...
```

## Duty
Classify files by examining their header bytes and applying magic number
heuristics. Does not rely on a magic database; detection rules are built in.

## Options
None.

## Arguments
- `path...` — One or more files to identify.

## Recognized Types
- **ELF executable** — Identified by `\x7fELF` magic
- **Shell script** — Identified by `#!` shebang line
- **ZIP archive** — `PK\x03\x04` magic
- **gzip compressed** — `\x1f\x8b` magic
- **bzip2 compressed** — `BZh` magic
- **xz compressed** — `\xfd7zXZ` magic
- **7-Zip archive** — `7z\xbc\xaf` magic
- **PNG image** — `\x89PNG` magic
- **JPEG image** — `\xff\xd8\xff` SOI marker
- **GIF image** — `GIF87a` / `GIF89a` magic
- **BMP image** — `BM` magic
- **WebP image** — `RIFF....WEBP` signature
- **WAV audio** — `RIFF....WAVE` signature
- **OGG audio** — `OggS` magic
- **FLAC audio** — `fLaC` magic
- **MP3 audio** — ID3 tag or sync bytes
- **SQLite database** — `SQLite format 3` header
- **tar archive** — POSIX/ustar header at offset 257
- **ISO 9660** — Volume descriptor at offset 32768
- **ext2/3/4 filesystem** — Superblock magic `0xEF53`
- **Text file** — All printable/whitespace bytes
- **Data** — Unrecognized binary content

## Examples
```
file /bin/sh
file README
file /tmp/disk.img
file /dev/cdrom
```

## Source Audit
- Source file: user/file.c
- Last updated: 2026-04-02
