# which(1)

## Name
which - Locate an executable in PATH.

## Synopsis
```
which name...
```

## Duty
Search each directory in `PATH` for an executable matching `name` and print
the full path. Reports each name as found or not found.

## Options
None.

## Arguments
- `name...` — One or more command names to search for.

## Notes
- `PATH` defaults to `/:/bin:/sbin` if the environment variable is unset.
- Names containing `/` are checked directly without PATH search.
- Checks execute permission (`X_OK`) on candidate files.
- Prints one result per name; if not found, prints `name: not found`.

## Examples
```
which sh
which ls grep cat
which doesnotexist
```

## Source Audit
- Source file: user/which.c
- Last updated: 2026-04-02
