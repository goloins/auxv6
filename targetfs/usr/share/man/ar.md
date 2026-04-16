# ar(1)

## Name
ar - Maintain portable archive and library files.

## Synopsis
```sh
ar [-t|-x|-r|-q] [-c] [-v] archive [member ...]
```

## Duty
`ar` creates, maintains, and extracts from POSIX archive files. Archives can
contain object files, text files, or other data, and are useful for bundling
multiple files into a single container for distribution or linking.

## Options
- `-t` : list contents of archive (table of contents).
- `-x` : extract members from archive.
- `-r` : replace (append) files in archive; create archive if it doesn't exist.
- `-q` : quick append; like `-r` but faster (no replacement check).
- `-c` : create archive silently (suppress creation message).
- `-v` : verbose output; print each operation.

## Behavior
- Archive format is POSIX `ar` format with file headers and member data.
- Archive names are stored with maximum 15 characters.
- File data is padded to even byte boundaries within the archive.
- If no members are specified with `-x`, all members are extracted.
- Timestamps, permissions, and owner UIDs/GIDs are preserved.

## Exit Status
- `0` on success.
- `1` if archive is invalid, file I/O fails, or argument is malformed.

## Examples
```sh
ar t libtest.a           # List archive contents
ar x libtest.a foo.o     # Extract specific object
ar r libfoo.a bar.o      # Append/replace object in archive
ar tv libfoo.a           # Verbose archive listing
```

## Source Audit
- Source file: user/ar.c
- Last updated: 2026-04-14
