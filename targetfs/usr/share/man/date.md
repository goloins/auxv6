# date(1)

## Name
date - Print current date and time.

## Synopsis
```
date
```

## Duty
Read the hardware real-time clock (RTC) and print the current UTC date and time.

## Options
None. Setting the date/time is not supported.

## Output
Outputs a single line in the format:
```
YYYY-MM-DD HH:MM:SS UTC
```

## Examples
```
date
# 2026-04-02 14:30:00 UTC
```

## Source Audit
- Source file: user/date.c
- Last updated: 2026-04-02
