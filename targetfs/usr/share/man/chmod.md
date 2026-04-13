# chmod(1)

## Name
chmod - Change file permission bits.

## Synopsis
```
chmod mode file...
```

## Duty
Set the permission bits on one or more files to `mode`. The mode is always
interpreted as an octal integer.

## Options
None. Symbolic mode strings (e.g. `u+x`) are not supported.

## Arguments
- `mode` — Octal permission value. Common values:
  - `755` — rwxr-xr-x (owner full, group/other read+execute)
  - `644` — rw-r--r-- (owner read/write, group/other read-only)
  - `600` — rw------- (owner read/write only)
  - `777` — rwxrwxrwx (full access for all)
- `file...` — One or more files to modify.

## Notes
- Stops at the first failure; remaining files are not processed.
- Mode digits must be valid octal (`0`–`7`); any other character is an error.

## Examples
```
chmod 755 /bin/myprogram
chmod 644 /etc/config.txt
chmod 600 /etc/shadow
```

## Source Audit
- Source file: user/chmod.c
- Last updated: 2026-04-02
