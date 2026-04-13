# auxv6 Man Pages

Generated: 2026-04-11

## Scope
- Enriched all utility man pages using source-derived usage and option extraction from user/*.c.
- Added markdown formatting support in user/man.c for headings, lists, code fences, inline emphasis/code, links, ordered lists, block quotes, and horizontal rules.
- ports directory deep analysis remains intentionally skipped; dash is baseline-documented.

## Tracking
| Utility | Man Page | Last Updated | Source Audited | Notes |
|---|---|---|---|---|
| abrowse | targetfs/usr/share/man/abrowse.md | 2026-04-03 | user/abrowse.c | basic terminal HTTP browser (text/html/markdown/plain) |
| 6fetch | targetfs/usr/share/man/6fetch.md | 2026-04-07 | user/6fetch.c | compact screenfetch-style system summary (user/host/os/kernel/machine/uptime/memory) |
| 6get | targetfs/usr/share/man/6get.md | 2026-04-02 | user/6get.c | source-derived usage/options; see docs/6get-http-transfer-notes.md |
| 6vi | targetfs/usr/share/man/6vi.md | 2026-04-11 | user/6vi.c | minimal vi-style modal text editor with arrow-key navigation and `:w/:q/:wq` commands |
| 6nano | targetfs/usr/share/man/6nano.md | 2026-04-12 | user/6nano.c | minimal nano/pico-style editor with `Ctrl+O` save, `Ctrl+X` quit, and arrow-key navigation |
| ahcitest | targetfs/usr/share/man/ahcitest.md | 2026-04-02 | user/ahcitest.c | AHCI regression binary |
| audioctl | targetfs/usr/share/man/audioctl.md | 2026-04-05 | user/audioctl.c | Stage-0 audio ioctl control/query utility |
| audiod | targetfs/usr/share/man/audiod.md | 2026-04-05 | user/audiod.c | Stage-2 audio daemon scaffold (single native sink service loop) |
| audiodctl | targetfs/usr/share/man/audiodctl.md | 2026-04-05 | user/audiodctl.c | Stage-2 audiod runtime control helper (mailbox command writer) |
| audiostat | targetfs/usr/share/man/audiostat.md | 2026-04-05 | user/audiostat.c | Stage-1 audio procfs summary/counter/clients reader |
| audiotest | targetfs/usr/share/man/audiotest.md | 2026-04-05 | user/audiotest.c | Stage-0 audio write-path/xrun recovery exerciser |
| audiotone | targetfs/usr/share/man/audiotone.md | 2026-04-05 | user/audiotone.c | deterministic S16_LE square-wave generator for audiod track tests |
| bcachestress | targetfs/usr/share/man/bcachestress.md | 2026-04-05 | user/bcachestress.c | concurrent fs I/O stress utility with /proc/bcache_health fail-fast checks and vmstat/meminfo context dump |
| cowsay | targetfs/usr/share/man/cowsay.md | 2026-04-13 | user/cowsay.c | speech-bubble ASCII cow renderer with template files under `/usr/share/games/cows` |
| cowtest | targetfs/usr/share/man/cowtest.md | 2026-04-11 | user/cowtest.c | COW fork correctness test validating parent/child write isolation across data, heap, and stack |
| cowexectest | targetfs/usr/share/man/cowexectest.md | 2026-04-11 | user/cowexectest.c | fork-plus-exec COW correctness test validating old-image teardown and parent isolation across data, heap, and stack |
| vmreservetest | targetfs/usr/share/man/vmreservetest.md | 2026-04-12 | user/vmreservetest.c | explicit vmreserve regression covering first-touch zeroes, syscall copyout materialization into reserved pages, sparse page activation, and fork isolation |
| arp | targetfs/usr/share/man/arp.md | 2026-04-01 | user/arp.c | source-derived usage/options |
| awk | targetfs/usr/share/man/awk.md | 2026-04-06 | user/awk.c | baseline pattern/action processor (`/regex/ { print ... }`) with `$0`/`$N`/`NR` and `-F` field separator |
| cat | targetfs/usr/share/man/cat.md | 2026-04-01 | user/cat.c | source-derived usage/options |
| chgrp | targetfs/usr/share/man/chgrp.md | 2026-04-01 | user/chgrp.c | source-derived usage/options |
| chmod | targetfs/usr/share/man/chmod.md | 2026-04-01 | user/chmod.c | source-derived usage/options |
| chown | targetfs/usr/share/man/chown.md | 2026-04-01 | user/chown.c | source-derived usage/options |
| chvt | targetfs/usr/share/man/chvt.md | 2026-04-01 | user/chvt.c | source-derived usage/options |
| cp | targetfs/usr/share/man/cp.md | 2026-04-02 | user/cp.c | source-derived usage/options |
| clear | targetfs/usr/share/man/clear.md | 2026-04-01 | user/clear.c | source-derived usage/options |
| dash | targetfs/usr/share/man/dash.md | 2026-04-01 | ports/ignored | manual baseline page |
| dd | targetfs/usr/share/man/dd.md | 2026-04-06 | user/dd.c | block-oriented copy utility with standard `key=value` args and core conversion flags |
| ddate | targetfs/usr/share/man/ddate.md | 2026-04-13 | user/ddate.c | Discordian date renderer with YOLD output and leap-year St. Tib's Day handling |
| date | targetfs/usr/share/man/date.md | 2026-04-01 | user/date.c | source-derived usage/options |
| devman | targetfs/usr/share/man/devman.md | 2026-04-02 | user/devman.c | source-derived usage/options |
| df | targetfs/usr/share/man/df.md | 2026-04-01 | user/df.c | source-derived usage/options |
| dmesg | targetfs/usr/share/man/dmesg.md | 2026-04-01 | user/dmesg.c | source-derived usage/options |
| dmenu | targetfs/usr/share/man/dmenu.md | 2026-04-09 | ports/dmenu-5.4/dmenu.c | first-class auxv6 build of the vendored X11 menu; `/usr/bin/dmenu_run` and `/usr/bin/dmenu_path` are staged alongside it |
| echo | targetfs/usr/share/man/echo.md | 2026-04-01 | user/echo.c | source-derived usage/options |
| find | targetfs/usr/share/man/find.md | 2026-04-06 | user/find.c | recursive file-tree walker with `-name/-path/-type/-mindepth/-maxdepth/-print` |
| fatregress | targetfs/usr/share/man/fatregress.md | 2026-04-01 | user/fatregress.c | source-derived usage/options |
| forktest | targetfs/usr/share/man/forktest.md | 2026-04-01 | user/forktest.c | source-derived usage/options |
| free | targetfs/usr/share/man/free.md | 2026-04-01 | user/free.c | source-derived usage/options |
| fsperf | targetfs/usr/share/man/fsperf.md | 2026-04-03 | user/fsperf.c | kernel fs/inode/bcache perf score utility |
| gfxperf | targetfs/usr/share/man/gfxperf.md | 2026-04-04 | user/gfxperf.c | framebuffer console perf probe using /proc/gfxstats deltas |
| fsregress | targetfs/usr/share/man/fsregress.md | 2026-04-01 | user/fsregress.c | source-derived usage/options |
| getty | targetfs/usr/share/man/getty.md | 2026-04-01 | user/getty.c | source-derived usage/options |
| grep | targetfs/usr/share/man/grep.md | 2026-04-01 | user/grep.c | source-derived usage/options |
| gunzip | targetfs/usr/share/man/gunzip.md | 2026-04-06 | user/gunzip.c | gzip decompression utility with stdout/keep modes |
| id | targetfs/usr/share/man/id.md | 2026-04-01 | user/id.c | source-derived usage/options |
| ifconfig | targetfs/usr/share/man/ifconfig.md | 2026-04-01 | user/ifconfig.c | source-derived usage/options |
| init | targetfs/usr/share/man/init.md | 2026-04-01 | user/init.c | source-derived usage/options |
| ip | targetfs/usr/share/man/ip.md | 2026-04-01 | user/ip.c | source-derived usage/options |
| isotest | targetfs/usr/share/man/isotest.md | 2026-04-01 | user/isotest.c | source-derived usage/options |
| kernperf | targetfs/usr/share/man/kernperf.md | 2026-04-06 | user/kernperf.c | general kernel before/after perf ruler (syscall/proc/ipc/vm/fs) |
| killall | targetfs/usr/share/man/killall.md | 2026-04-01 | user/killall.c | source-derived usage/options |
| kill | targetfs/usr/share/man/kill.md | 2026-04-01 | user/kill.c | source-derived usage/options |
| ln | targetfs/usr/share/man/ln.md | 2026-04-01 | user/ln.c | source-derived usage/options |
| login | targetfs/usr/share/man/login.md | 2026-04-01 | user/login.c | source-derived usage/options |
| lockprobe | targetfs/usr/share/man/lockprobe.md | 2026-04-05 | user/lockprobe.c | lock modernization validation utility for console/ftable paths plus lockdep handoff selftest mode (`-L`) |
| losetup | targetfs/usr/share/man/losetup.md | 2026-04-01 | user/losetup.c | source-derived usage/options |
| less | targetfs/usr/share/man/less.md | 2026-04-06 | user/more.c | pager alias built from the same binary image as `more` |
| lsblk | targetfs/usr/share/man/lsblk.md | 2026-04-01 | user/lsblk.c | source-derived usage/options |
| lspci | targetfs/usr/share/man/lspci.md | 2026-04-01 | user/lspci.c | source-derived usage/options |
| ls | targetfs/usr/share/man/ls.md | 2026-04-02 | user/ls.c | source-derived usage/options |
| man | targetfs/usr/share/man/man.md | 2026-04-02 | user/man.c | source-derived usage/options |
| mkdir | targetfs/usr/share/man/mkdir.md | 2026-04-01 | user/mkdir.c | source-derived usage/options |
| mount | targetfs/usr/share/man/mount.md | 2026-04-02 | user/mount.c | source-derived usage/options |
| mktmpfs | targetfs/usr/share/man/mktmpfs.md | 2026-04-02 | user/mktmpfs.c | source-derived usage/options |
| more | targetfs/usr/share/man/more.md | 2026-04-06 | user/more.c | interactive pager with tty prompt and page sizing (`-n`) |
| mounts | targetfs/usr/share/man/mounts.md | 2026-04-01 | user/mounts.c | source-derived usage/options |
| mounttest | targetfs/usr/share/man/mounttest.md | 2026-04-01 | user/mounttest.c | source-derived usage/options |
| mv | targetfs/usr/share/man/mv.md | 2026-04-01 | user/mv.c | source-derived usage/options |
| netcat | targetfs/usr/share/man/netcat.md | 2026-04-01 | user/netcat.c | source-derived usage/options |
| netinfo | targetfs/usr/share/man/netinfo.md | 2026-04-01 | user/netinfo.c | source-derived usage/options |
| netstat | targetfs/usr/share/man/netstat.md | 2026-04-01 | user/netstat.c | source-derived usage/options |
| nslookup | targetfs/usr/share/man/nslookup.md | 2026-04-01 | user/nslookup.c | source-derived usage/options |
| ntpd | targetfs/usr/share/man/ntpd.md | 2026-04-03 | user/ntpd.c | daemonized NTP sync service |
| passwd | targetfs/usr/share/man/passwd.md | 2026-04-01 | user/passwd.c | source-derived usage/options |
| ping | targetfs/usr/share/man/ping.md | 2026-04-02 | user/ping.c | source-derived usage/options |
| ps | targetfs/usr/share/man/ps.md | 2026-04-01 | user/ps.c | source-derived usage/options |
| pwd | targetfs/usr/share/man/pwd.md | 2026-04-01 | user/pwd.c | source-derived usage/options |
| rarp | targetfs/usr/share/man/rarp.md | 2026-04-01 | user/rarp.c | source-derived usage/options |
| reset | targetfs/usr/share/man/reset.md | 2026-04-02 | user/reset.c | source-derived usage/options |
| rm | targetfs/usr/share/man/rm.md | 2026-04-01 | user/rm.c | source-derived usage/options |
| route | targetfs/usr/share/man/route.md | 2026-04-01 | user/route.c | source-derived usage/options |
| runlevel | targetfs/usr/share/man/runlevel.md | 2026-04-01 | user/runlevel.c | source-derived usage/options |
| sed | targetfs/usr/share/man/sed.md | 2026-04-06 | user/sed.c | baseline stream editor with `/addr/`, `s///[g]`, `p`, `d`, and `-n` |
| schedperf | targetfs/usr/share/man/schedperf.md | 2026-04-03 | user/schedperf.c | kernel scheduler/process perf score utility |
| server7 | targetfs/usr/share/man/server7.md | 2026-04-03 | user/server7.c | bootstrap display-server daemon entrypoint |
| sh | targetfs/usr/share/man/sh.md | 2026-04-01 | user/sh.c | source-derived usage/options |
| sigtest | targetfs/usr/share/man/sigtest.md | 2026-04-01 | user/sigtest.c | source-derived usage/options |
| sockettest | targetfs/usr/share/man/sockettest.md | 2026-04-01 | user/sockettest.c | source-derived usage/options |
| sgrep | targetfs/usr/share/man/sgrep.md | 2026-04-01 | ports/sbase/grep.c | upstream sbase port via auxv6 Makefile |
| startx | targetfs/usr/share/man/startx.md | 2026-04-07 | user/startx.c | compiled wrapper that execs /bin/xinit; staged in /bin and /usr/bin |
| stressfs | targetfs/usr/share/man/stressfs.md | 2026-04-01 | user/stressfs.c | source-derived usage/options |
| stest | targetfs/usr/share/man/stest.md | 2026-04-09 | ports/dmenu-5.4/stest.c | dmenu helper utility used by `dmenu_path` to enumerate executable candidates |
| su | targetfs/usr/share/man/su.md | 2026-04-01 | user/su.c | source-derived usage/options |
| symlinktest | targetfs/usr/share/man/symlinktest.md | 2026-04-01 | user/symlinktest.c | source-derived usage/options |
| tail | targetfs/usr/share/man/tail.md | 2026-04-01 | user/tail.c | source-derived usage/options |
| tcptest | targetfs/usr/share/man/tcptest.md | 2026-04-01 | user/tcptest.c | source-derived usage/options |
| telinit | targetfs/usr/share/man/telinit.md | 2026-04-01 | user/telinit.c | source-derived usage/options |
| telnet | targetfs/usr/share/man/telnet.md | 2026-04-01 | user/telnet.c | source-derived usage/options |
| termcheck | targetfs/usr/share/man/termcheck.md | 2026-04-01 | user/termcheck.c | source-derived usage/options |
| termdemo | targetfs/usr/share/man/termdemo.md | 2026-04-01 | user/termdemo.c | source-derived usage/options |
| time | targetfs/usr/share/man/time.md | 2026-04-02 | user/time.c | source-derived usage/options |
| tar | targetfs/usr/share/man/tar.md | 2026-04-06 | user/tar.c | ustar create/list/extract with gzip read support |
| tuntest | targetfs/usr/share/man/tuntest.md | 2026-04-05 | user/tuntest.c | tun/tap regression utility: empty-queue readiness, tun ICMP self-test, and tap ARP self-test coverage |
| tuntapctl | targetfs/usr/share/man/tuntapctl.md | 2026-04-05 | user/tuntapctl.c | baseline `/dev/net/tun` control utility for create/get/persist/owner/group |
| umount | targetfs/usr/share/man/umount.md | 2026-04-01 | user/umount.c | source-derived usage/options |
| uname | targetfs/usr/share/man/uname.md | 2026-04-01 | user/uname.c | source-derived usage/options |
| usertests | targetfs/usr/share/man/usertests.md | 2026-04-01 | user/usertests.c | source-derived usage/options |
| v6dhcpd | targetfs/usr/share/man/v6dhcpd.md | 2026-04-01 | user/v6dhcpd.c | source-derived usage/options |
| vmguardtest | targetfs/usr/share/man/vmguardtest.md | 2026-04-11 | user/vmguardtest.c | VM address-space guard regression test for bypass/deny leakage across procfs, pipe, and forked-child phases |
| vmprobe | targetfs/usr/share/man/vmprobe.md | 2026-04-06 | user/vmprobe.c | targeted VM/scheduler slowdown hypothesis probe correlating fork/tick phases with vmstat/schedstat deltas |
| wallpaper | targetfs/usr/share/man/wallpaper.md | 2026-04-07 | user/wallpaper.c | framebuffer background setter for `#RRGGBB` colors and PNG image files |
| wc | targetfs/usr/share/man/wc.md | 2026-04-01 | user/wc.c | source-derived usage/options |
| whoami | targetfs/usr/share/man/whoami.md | 2026-04-01 | user/whoami.c | source-derived usage/options |
| x6 | targetfs/usr/share/man/x6.md | 2026-04-06 | user/x6.c | phase-1 local display-server scaffold with simple bring-up protocol |
| xinit | targetfs/usr/share/man/xinit.md | 2026-04-06 | user/xinit.c | launcher that starts x6, waits for readiness, and runs a client session |
| xtermprobe | targetfs/usr/share/man/xtermprobe.md | 2026-04-12 | user/xtermprobe.c | interactive xterm input/mouse protocol probe for raw byte-level inspection of key, query, and mouse-report sequences |
| zombie | targetfs/usr/share/man/zombie.md | 2026-04-01 | user/zombie.c | source-derived usage/options |

## Task Notes
- Utility pages are generated by tools/gen-man-pages.sh.
- Option lists include explicit '-x'/'--long' tokens detected in source strings and usage checks.
- Re-run tools/gen-man-pages.sh after userland changes to keep pages and dates current.
- man now paginates terminal output using the current tty winsize instead of a fixed 80x24 assumption.
- 6get transfer semantics and progress behavior are documented in docs/6get-http-transfer-notes.md.
- ext2 dirent emission now guards against 32-bit inode to 16-bit dirent truncation yielding zero inum, preserving entry visibility for tools that skip inum==0.
