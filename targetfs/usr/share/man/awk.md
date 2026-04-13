# awk(1)

## Name
awk - Pattern-driven line processor (baseline command subset).

## Synopsis
```sh
awk [-F fs] '[/regex/] { print [expr[,expr...]] }' [file ...]
```

## Supported Program Form
- Optional line filter pattern: `/regex/`
- Action block with `print` and comma-separated expressions.

## Supported Print Expressions
- `$0` - whole input line.
- `$N` - field number `N` (1-based).
- `NR` - input record number.
- `"text"` - literal string.

## Notes
- Default field splitting is whitespace folding.
- `-F fs` sets a literal field separator string.

## Examples
```sh
awk '{ print $0 }' file
awk -F : '{ print $1, $3 }' /etc/passwd
awk '/error/ { print NR, $0 }' kernel.log
```

## Source Audit
- Source file: user/awk.c
- Last updated: 2026-04-06
