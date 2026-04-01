# which(1)

## Name
which - Show command path resolution using PATH.

## Synopsis
- usage: which name...

## Duty
Search PATH and print executable path(s) for command names.

## Options
- none detected

## Examples
- which sh
- which ls grep

## Notes
- PATH defaults to `/:/bin:/sbin` when unset.
- Names containing `/` are checked directly.

## Source Audit
- Source file: user/which.c
- Last updated: 2026-04-01
