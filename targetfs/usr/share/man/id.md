# id(1)

## Name
id - Print user and group identity information.

## Synopsis
```
id
```

## Duty
Display the effective user ID, group ID, and process group ID of the
calling process. The username is looked up in `/etc/passwd`.

## Options
None.

## Output Format
```
uid=N(name) gid=N [passwd_gid=N] pgrp=N
```
- `uid` — Effective user ID with username in parentheses if found in `/etc/passwd`
- `gid` — Effective group ID
- `passwd_gid` — Shown only if it differs from the current `gid`
- `pgrp` — Process group ID

## Examples
```
id
# uid=0(root) gid=0 pgrp=1
```

## Source Audit
- Source file: user/id.c
- Last updated: 2026-04-02
