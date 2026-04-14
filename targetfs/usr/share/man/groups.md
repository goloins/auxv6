# groups(1)

## Name
groups - Show supplementary and primary groups for user accounts.

## Synopsis
```
groups [user...]
```

## Duty
Display the group memberships of one or more users by reading `/etc/passwd`
and `/etc/group`.

## Options
None.

## Notes
- Without arguments, prints groups for the current user.

## Examples
```
groups
groups root aux
```

## Source Audit
- Source file: user/groups.c
- Last updated: 2026-04-14
