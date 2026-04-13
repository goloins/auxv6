# stest

## Synopsis

`stest [-abcdefghlpqrsuvwx] [-n file] [-o file] [file ...]`

## Description

`stest` filters pathnames according to file-type and attribute predicates and prints matching entries to standard output. In auxv6 it is primarily staged as a helper for `dmenu_path`, which uses it to enumerate executable candidates from `$PATH`.

If no pathname arguments are provided, `stest` reads newline-delimited paths from standard input.

## Options

- `-a` include hidden names.
- `-b` require a block device.
- `-c` require a character device.
- `-d` require a directory.
- `-e` require that the path exists.
- `-f` require a regular file.
- `-g` require the set-group-ID bit.
- `-h` require a symbolic link.
- `-l` when an argument is a directory, test its contents instead of the directory itself.
- `-n file` require entries newer than `file`.
- `-o file` require entries older than `file`.
- `-p` require a FIFO.
- `-q` exit successfully on the first match without printing all matches.
- `-r` require read permission.
- `-s` require non-empty size.
- `-u` require the set-user-ID bit.
- `-v` invert the final match result.
- `-w` require write permission.
- `-x` require execute permission.

## Notes

- `dmenu_path` uses `stest -flx` to find executable regular files in each `$PATH` directory.
- When used with `-l`, each directory argument is expanded with `readdir(3)` before predicate checks are applied.