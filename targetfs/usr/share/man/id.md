# id(1)

## Name
id - Print user and group identity information.

## Synopsis
```
id
```

## Duty
Display the effective user ID and group ID of the calling process.
The username is looked up in `/etc/passwd` and the group name in
`/etc/group`.

## Options
None.

## Output Format
```
uid=N(name) gid=N(grpname)
```
- `uid` — Effective user ID with username in parentheses if resolvable
- `gid` — Effective group ID with group name in parentheses if resolvable

## Examples
```
id
# uid=0(root) gid=0(root)
```

## Notes
Supplementary groups are not yet supported by the kernel.  Only the
effective UID and primary GID are shown.

## Source Audit
- Source file: user/id.c
- Last updated: 2026-04-03
