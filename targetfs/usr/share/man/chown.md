# chown(1)

## Name
chown - Change file owner and/or group.

## Synopsis
```
chown [owner][:group] file...
```

## Duty
Change the owner, group, or both, of one or more files.  Accepts the
standard POSIX `owner:group` syntax.  Passing only an owner changes the
UID without touching the GID; passing `:group` changes only the GID.

## Options
None.

## Arguments
- `owner` — New owner. Accepts a numeric UID or a username from `/etc/passwd`.
- `group` — New group. Accepts a numeric GID or a group name from `/etc/group`.
  Separated from owner by a colon (`:`).  May be omitted to leave the group
  unchanged, or the owner may be omitted (`:group`) to change only the group.
- `file...` — One or more files to modify.

## Notes
- Stops at the first failure; remaining files are not processed.
- Only root may change file ownership.
- `chgrp group file` is equivalent to `chown :group file`.

## Examples
```
chown 0 /etc/passwd          # set uid=0, group unchanged
chown root /bin/su           # set uid=0 by name, group unchanged
chown root:wheel /bin/su     # set uid=0 and gid=10
chown :wheel /bin/su         # set gid=10 only
chown 1000:1000 /home/aux    # numeric uid and gid
```

## Source Audit
- Source file: user/chown.c
- Last updated: 2026-04-03
