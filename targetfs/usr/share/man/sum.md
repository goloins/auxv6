# sum(1)

## Name
sum - Compute BSD-style checksum and block count.

## Synopsis
```
sum [file ...]
```

## Description
Prints a 16-bit rotating checksum and 1K block count.

## Output
- With files: `checksum blocks file`
- With stdin: `checksum blocks`

## Source Audit
- Source file: user/sum.c
- Last updated: 2026-04-06
