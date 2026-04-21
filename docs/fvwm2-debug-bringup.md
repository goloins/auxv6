# fvwm2 Debug Bringup — Temporary Port Edits

> **These edits violate the normal "never edit ports" convention.**
> They MUST be reverted once the segfault is root-caused.

## What was changed and where

### `ports/fvwm-fvwm2-stable/fvwm/fvwm.c`

Just before `int main(...)`, added:

```c
/* AUXV6-DEBUG: temporary bringup tracing. See docs/fvwm2-debug-bringup.md */
#include <unistd.h>
#define AUXDBG(msg) do { \
    static const char _m[] = "fvwm[DBG]: " msg "\n"; \
    write(2, _m, sizeof(_m) - 1); \
} while (0)
```

Checkpoints added inside `main()`:

| Tag | Location (approx) | Description |
|-----|-------------------|-------------|
| `[1]` | top of main | very first line executed |
| `[2]` | after XOpenDisplay | display connection established |
| `[3]` | before FScreenInit | multi-screen init |
| `[4]` | before RootWindow | root window lookup |
| `[5]` | before PictureSetupWhiteAndBlack | color init |
| `[6]` | before PictureSaveFvwmVisual | visual save |
| `[7]` | before XScreenOfDisplay | Scr.pscreen = ... |
| `[8]` | before InternUsefulAtoms | atom table |
| `[9]` | before frame_init | frame subsystem |
| `[10]` | before SetupICCCM2 | ICCCM wm setup |
| `[11]` | before initPanFrames | pan frames |
| `[12]` | before SessionInit/GNOME_Init | SM + GNOME hints |
| `[13]` | before HandleEvents | main event loop entry |

Checkpoint added inside `StartupStuff()`:

| Tag | Description |
|-----|-------------|
| `[SS]` | very first line of StartupStuff — CaptureAllWindows etc. comes after |

### `ports/fvwm-fvwm2-stable/fvwm/events.c`

Same `AUXDBG` macro added in the imports section (file already has `<unistd.h>`).

Checkpoints added at the two `StartupStuff()` call sites inside `My_XNextEvent`:

| Tag | Path |
|-----|------|
| `[EV] StartupStuff via normal path` | modules exited cleanly |
| `[EV] StartupStuff via timeout path` | module timeout fallback |

## How to read the output

Run fvwm2 from xinit as normal. The `write(2, ...)` calls go to stderr
(fd 2), which xinit inherits from the terminal. The last checkpoint
printed before the crash is the stage that segfaulted (or the very
next stage after it).

Example session (hypothetical):
```
fvwm[DBG]: [1] entered main
fvwm[DBG]: [2] XOpenDisplay succeeded
fvwm[DBG]: [3] FScreenInit
fvwm[DBG]: [4] getting RootWindow
Segfault (sig=11)
```
→ crash is inside `RootWindow()` or immediately after, before `[5]`.

## How to revert

```sh
cd /home/dakota/auxv6
# The port source was fetched from the upstream zip, so re-sync:
make ports-sync
```
`ports-sync` re-extracts the zip archive, which overwrites the edited files.
Alternatively, manually revert the two files with `git checkout` if the
archive was unpacked into a git repo, or just delete the two files and
re-run `make ports-sync`.

After revert, delete the port stamp to force a clean rebuild:
```sh
rm -f ports/fvwm-fvwm2-stable/built.auxv6
make PORTS=1 ports-progs
```
