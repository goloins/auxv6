# whoami(1)

## Name
whoami - Print the current username.

## Synopsis
```
whoami
```

## Duty
Look up the effective UID in `/etc/passwd` and print the corresponding
username. If the UID is not found, prints `unknown`.

## Options
None.

## Output
A single line containing the username.

## Examples
```
whoami
# root
```

## Source Audit
- Source file: user/whoami.c
- Last updated: 2026-04-02
