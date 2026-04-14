# groupadd(1)

## Name
groupadd - Create a new local group.

## Synopsis
```
groupadd [-g gid] group
```

## Duty
Add a new group entry to `/etc/group`.

## Options
- `-g gid` - Set explicit numeric GID.

## Notes
- Must be run as root.
- Without `-g`, next available GID is selected.

## Examples
```
groupadd developers
groupadd -g 2200 ci
```

## Source Audit
- Source file: user/groupadd.c
- Last updated: 2026-04-14
