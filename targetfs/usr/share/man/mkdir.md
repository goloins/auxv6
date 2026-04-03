# mkdir(1)

## Name
mkdir - Create directories.

## Synopsis
```
mkdir [-pv] [-m mode] directory...
```

## Duty
Create one or more directories.  Reports an error for each directory that
cannot be created and continues with the remaining arguments.

## Options
- `-p` — Create parent directories as needed.  No error if the directory
  already exists.  Useful for scripted setup of deep path hierarchies.
- `-m mode` — Set the file permission bits of the created directory to the
  octal value `mode` (e.g. `755`).  Applied after creation via `chmod(2)`.
- `-v` — Verbose: print `mkdir: created directory 'path'` for each directory
  actually created.

## Arguments
- `directory...` — One or more directory paths to create.

## Notes
- Without `-p` the parent directory must already exist.
- `-m` accepts only numeric octal modes (e.g. `755`, `0700`).

## Examples
```
mkdir /tmp/work
mkdir -p /tmp/a/b/c
mkdir -p -v /home/user/projects
mkdir -m 700 /etc/secret
mkdir dir1 dir2 dir3
```

## Source Audit
- Source file: user/mkdir.c
- Last updated: 2026-04-03
