# xwmselftest

`xwmselftest` runs a small Xlib-based window-manager-path self-test for coordinate translation and child hit detection.

## Synopsis

```sh
xwmselftest
```

## Description

`xwmselftest` creates a parent window and two child windows, maps them, then runs a focused set of `XTranslateCoordinates` checks.

It verifies:

- root to non-root translation offsets are correct
- non-root `child_return` resolution finds the correct immediate child
- non-root `child_return` returns `None` when no child is hit
- parent to root translation reports the expected root child

Results are printed as console `PASS`/`FAIL` lines and a final summary.

Exit status is `0` on full pass and non-zero on any failure.

## .xinitrc Integration

`/root/.xinitrc` can run `xwmselftest` before launching the WM so results appear in the startup console.
