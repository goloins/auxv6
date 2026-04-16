# groupdel(1)

## Name
groupdel - Delete a local group.

## Synopsis
```
groupdel group
```

## Duty
Remove a group entry from `/etc/group`.

## Options
None.

## Notes
- Must be run as root.
- Refuses deletion if the group is a primary GID of any user in `/etc/passwd`.

## Examples
```
groupdel ci
```

## Source Audit
- Source file: user/groupdel.c
- Last updated: 2026-04-14
