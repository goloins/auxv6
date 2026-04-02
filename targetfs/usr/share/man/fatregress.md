# fatregress(1)

## Name
fatregress - FAT filesystem regression test suite.

## Synopsis
```
fatregress
```

## Duty
Run a regression test pass against the FAT filesystem driver. Tests are
performed on `/dev/fat` (or the configured FAT partition).

## Options
None.

## Tests Performed
1. **Small file write/read** — Creates a small file and verifies its contents.
2. **File growth** — Appends data and confirms the updated size.
3. **Pattern fill and verify** — Writes a repeating byte pattern across a
   larger buffer and reads it back.
4. **Truncation** — Reduces file size and verifies the shortened content.
5. **Unlink** — Deletes the test file and confirms removal.

## Examples
```
fatregress
```

## Source Audit
- Source file: user/fatregress.c
- Last updated: 2026-04-02
