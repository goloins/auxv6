# echo(1)

## Name
echo - Print arguments to standard output.

## Synopsis
```
echo [string...]
```

## Duty
Print each argument separated by a single space, followed by a newline.

## Options
None. Escape sequences (e.g. `\n`, `\t`) are not interpreted. No `-n` flag to
suppress the trailing newline.

## Arguments
- `string...` — Zero or more strings to print. If none are given, a blank line
  is printed.

## Examples
```
echo hello world
echo "current dir is:" $PWD
echo
```

## Source Audit
- Source file: user/echo.c
- Last updated: 2026-04-02
