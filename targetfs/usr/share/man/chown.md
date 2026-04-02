# chown(1)

## Name
chown - Change file owner.

## Synopsis
```
chown owner file...
```

## Duty
Change the owner of one or more files. The group is also updated to the
primary group recorded in `/etc/passwd` when a username is specified.

## Options
None.

## Arguments
- `owner` — New owner. Accepts either:
  - A numeric UID (digits only), or
  - A username looked up in `/etc/passwd` (sets both UID and primary GID)
- `file...` — One or more files to modify.

## Notes
- Stops at the first failure; remaining files are not processed.
- `chgrp` can be used to change only the group.

## Examples
```
chown 0 /etc/passwd
chown root /bin/su
chown 1000 /home/user/file.txt
```

## Source Audit
- Source file: user/chown.c
- Last updated: 2026-04-02
