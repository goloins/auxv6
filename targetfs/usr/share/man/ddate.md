# ddate(1)

## Name
ddate - print the current Discordian date.

## Synopsis
```
ddate
```

## Description
`ddate` reads the current local wall clock and converts it to the
Discordian calendar.

The output names the Discordian weekday, day-of-season, season name,
and Year of Our Lady of Discord (YOLD).

On leap years, February 29 is rendered as:
`St. Tib's Day, in the YOLD N`

## Options
None.

## Output
Outputs a single line. Example formats:
```
Pungenday, the 57th day of Discord in the YOLD 3192
St. Tib's Day, in the YOLD 3192
```

## Examples
```
ddate
```

## Source Audit
- Source file: user/ddate.c
- Last updated: 2026-04-13
