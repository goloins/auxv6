# wc(1)

## Name
wc - Count lines, words, and bytes.

## Synopsis
```
wc [file...]
```

## Duty
Read files (or standard input) and count the number of lines, words, and
bytes. Prints a summary line for each file.

## Options
None. Individual counts for lines, words, or bytes cannot be selected
separately.

## Arguments
- `file...` — Files to count. Reads from stdin if none are given.

## Output Format
```
lines words bytes filename
```

## Notes
- Words are defined as sequences of non-whitespace characters (space, tab,
  newline, carriage return, vertical tab).
- When reading stdin the filename field is printed as empty.

## Examples
```
wc /etc/passwd
wc file1.txt file2.txt
cat large.txt | wc
```

## Source Audit
- Source file: user/wc.c
- Last updated: 2026-04-02
