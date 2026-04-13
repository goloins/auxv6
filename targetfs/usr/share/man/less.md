# less(1)

## Name
less - Pager command alias implemented via the more binary.

## Synopsis
```sh
less [-n lines] [file ...]
```

## Duty
Invoke pager behavior equivalent to `more`; in auxv6, `less` is built from the
same executable image as `more`.

## Options
- `-n lines` - set page size in lines.

## Notes
- This implementation intentionally tracks baseline pager behavior.
- Press `space` for next page, `enter` for one line, `q` to quit.

## Source Audit
- Source file: user/more.c
- Last updated: 2026-04-06
