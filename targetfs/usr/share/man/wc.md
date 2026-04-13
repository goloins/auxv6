# wc(1)

## Name
wc - Count lines, words, and bytes.

## Synopsis
```
wc [-lwcm] [file...]
```

## Duty
Read files (or standard input) and count lines, words, and bytes.  By default
all three counts are printed.  Flags restrict the output to selected counters.
When multiple files are given a `total` summary line is appended.

## Options
- `-l` — Print only the line count.
- `-w` — Print only the word count.
- `-c` — Print only the byte count.
- `-m` — Print only the character count (equivalent to `-c` on this system;
  auxv6 uses single-byte character encoding).

Multiple flags may be combined (e.g. `-lw` prints lines and words).

## Arguments
- `file...` — Files to count. Reads from stdin if none are given.

## Output Format
```
[lines] [words] [bytes] [filename]
```
Only the selected columns are printed, separated by a single space.
When reading stdin the filename field is omitted.

## Notes
- Words are sequences of non-whitespace characters separated by space, tab,
  newline, carriage return, or vertical tab.
- When more than one file is given a final `total` row is printed.

## Examples
```
wc /etc/passwd
wc -l /etc/passwd
wc -lw file1.txt file2.txt
cat large.txt | wc -c
```

## Source Audit
- Source file: user/wc.c
- Last updated: 2026-04-03
