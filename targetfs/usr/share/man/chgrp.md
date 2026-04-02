# chgrp(1)

## Name
chgrp - Change file group ownership.

## Synopsis
```
chgrp group file...
```

## Duty
Change the group of one or more files.

## Options
None.

## Arguments
- `group` — New group. Accepts either:
  - A numeric GID (digits only), or
  - A group name looked up in `/etc/passwd` (uses the GID of the matching entry)
- `file...` — One or more files to modify.

## Notes
- Stops at the first failure; remaining files are not processed.
- To change both owner and group at once, use `chown`.

## Examples
```
chgrp 0 /etc/passwd
chgrp users /home/user/file.txt
```

## Source Audit
- Source file: user/chgrp.c
- Last updated: 2026-04-02
