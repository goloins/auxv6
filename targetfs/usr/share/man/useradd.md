# useradd(1)

## Name
useradd - Create a new local user account.

## Synopsis
```
useradd [-u uid] [-g group|gid] [-d home] [-s shell] [-c gecos] [-M] user
```

## Duty
Add a user record to `/etc/passwd`. By default, if no `-g` is provided,
`useradd` creates or reuses a private group matching the username and uses
that as the primary group.

## Options
- `-u uid` - Set explicit numeric UID.
- `-g group|gid` - Set existing primary group by name or numeric GID.
- `-d home` - Set home directory path.
- `-s shell` - Set login shell path.
- `-c gecos` - Set comment/full-name field.
- `-M` - Do not create the home directory.

## Notes
- Must be run as root.
- Default shell is `/bin/sh`.
- Default home is `/home/<user>`.

## Examples
```
useradd alice
useradd -u 2001 -g wheel -s /bin/sh -c "Alice Admin" alice
```

## Source Audit
- Source file: user/useradd.c
- Last updated: 2026-04-14
