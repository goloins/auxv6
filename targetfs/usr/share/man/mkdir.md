# mkdir(1)

## Name
mkdir - Create directories.

## Synopsis
```
mkdir directory...
```

## Duty
Create one or more directories. Stops at the first failure.

## Options
None. Parent directories are not created automatically (no `-p` flag).

## Arguments
- `directory...` — One or more directory paths to create.

## Notes
- The parent directory must already exist.
- Stops and prints an error message on the first failure.

## Examples
```
mkdir /tmp/work
mkdir dir1 dir2 dir3
```

## Source Audit
- Source file: user/mkdir.c
- Last updated: 2026-04-02
