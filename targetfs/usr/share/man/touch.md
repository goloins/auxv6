# touch(1)

## Name
touch - Create files or refresh modification timestamp.

## Synopsis
```
touch [-c] file...
```

## Description
Creates missing files unless `-c` is used. For existing regular files,
`touch` performs a content-preserving update to refresh mtime.

## Options
- `-c` - Do not create files.

## Source Audit
- Source file: user/touch.c
- Last updated: 2026-04-06
