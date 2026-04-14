# usermod(1)

## Name
usermod - Modify an existing local user account.

## Synopsis
```
usermod [-l login] [-u uid] [-g group|gid] [-d home] [-s shell] [-c gecos] user
```

## Duty
Update fields of an existing user in `/etc/passwd`. If `-l` is used, group
membership lists in `/etc/group` are updated from old username to new username.

## Options
- `-l login` - Rename user login name.
- `-u uid` - Set numeric UID.
- `-g group|gid` - Set primary group by name or numeric GID.
- `-d home` - Set home directory path.
- `-s shell` - Set login shell path.
- `-c gecos` - Set comment/full-name field.

## Notes
- Must be run as root.
- Existing IDs must remain unique.

## Examples
```
usermod -s /bin/sh alice
usermod -l alice2 -u 2100 alice
```

## Source Audit
- Source file: user/usermod.c
- Last updated: 2026-04-14
