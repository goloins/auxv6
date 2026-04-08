# xwmtrace

`xwmtrace` tails X11/x6 debug logs and mirrors key window-map/configure telemetry to console.

## Synopsis

```sh
xwmtrace [-a]
```

## Description

`xwmtrace` reads:

- `/tmp/x6-debug.log`
- `/tmp/x11-debug.log`

By default, it prints only lines relevant to window-manager bring-up and resize/configure debugging:

- WM map flow (`WM_MAP`, `wm_map step=`)
- configure flow (`CONFIGURE`, `ConfigureRequest`, `ConfigureNotify`)
- selected extension notify lines (`ShapeNotify`, `RandRNotify`)
- queue-guard diagnostics (`queue guard hit`)

To keep console output readable at idle, `DamageNotify` lines are suppressed by default.

Use `-a` to print all lines from both logs.

## Options

- `-a`: print all log lines (unfiltered)
- `-h`, `--help`: show usage

## Integration

`/root/.xinitrc` starts `xwmtrace` automatically when present:

```sh
if [ -x /bin/xwmtrace ]; then
  /bin/xwmtrace &
elif [ -x /usr/bin/xwmtrace ]; then
  /usr/bin/xwmtrace &
fi
```

This lets startup telemetry continue even while X clients are launching.
