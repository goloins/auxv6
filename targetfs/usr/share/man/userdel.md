# userdel(1)

## Name
userdel - Delete a local user account.

## Synopsis
```
userdel user
```

## Duty
Remove a user entry from `/etc/passwd` and remove that username from all group
member lists in `/etc/group`.

## Options
None.

## Notes
- Must be run as root.
- Refuses to delete `root`.
- Does not recursively remove home directory contents.

## Examples
```
userdel alice
```

## Source Audit
- Source file: user/userdel.c
- Last updated: 2026-04-14
