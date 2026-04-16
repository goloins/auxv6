# groupmod(1)

## Name
groupmod - Modify an existing local group.

## Synopsis
```
groupmod [-n newname] [-g gid] group
```

## Duty
Rename a group and/or change its GID in `/etc/group`. When changing GID,
primary group references in `/etc/passwd` are also updated.

## Options
- `-n newname` - Rename the group.
- `-g gid` - Change numeric GID.

## Notes
- Must be run as root.
- Group names and IDs must remain unique.

## Examples
```
groupmod -n ops developers
groupmod -g 2300 ops
```

## Source Audit
- Source file: user/groupmod.c
- Last updated: 2026-04-14
