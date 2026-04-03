# auxv6 Man Pages

Generated: 2026-04-03

## Scope
- Enriched all utility man pages using source-derived usage and option extraction from user/*.c.
- Added markdown formatting support in user/man.c for headings, lists, code fences, inline emphasis/code, links, ordered lists, block quotes, and horizontal rules.
- ports directory deep analysis remains intentionally skipped; dash is baseline-documented.

## Tracking
| Utility | Man Page | Last Updated | Source Audited | Notes |
|---|---|---|---|---|
| 6get | targetfs/usr/share/man/6get.md | 2026-04-02 | user/6get.c | source-derived usage/options; see docs/6get-http-transfer-notes.md |
| ahcitest | targetfs/usr/share/man/ahcitest.md | 2026-04-02 | user/ahcitest.c | AHCI regression binary |
| arp | targetfs/usr/share/man/arp.md | 2026-04-01 | user/arp.c | source-derived usage/options |
| cat | targetfs/usr/share/man/cat.md | 2026-04-01 | user/cat.c | source-derived usage/options |
| chgrp | targetfs/usr/share/man/chgrp.md | 2026-04-01 | user/chgrp.c | source-derived usage/options |
| chmod | targetfs/usr/share/man/chmod.md | 2026-04-01 | user/chmod.c | source-derived usage/options |
| chown | targetfs/usr/share/man/chown.md | 2026-04-01 | user/chown.c | source-derived usage/options |
| chvt | targetfs/usr/share/man/chvt.md | 2026-04-01 | user/chvt.c | source-derived usage/options |
| cp | targetfs/usr/share/man/cp.md | 2026-04-02 | user/cp.c | source-derived usage/options |
| clear | targetfs/usr/share/man/clear.md | 2026-04-01 | user/clear.c | source-derived usage/options |
| dash | targetfs/usr/share/man/dash.md | 2026-04-01 | ports/ignored | manual baseline page |
| date | targetfs/usr/share/man/date.md | 2026-04-01 | user/date.c | source-derived usage/options |
| devman | targetfs/usr/share/man/devman.md | 2026-04-02 | user/devman.c | source-derived usage/options |
| df | targetfs/usr/share/man/df.md | 2026-04-01 | user/df.c | source-derived usage/options |
| dmesg | targetfs/usr/share/man/dmesg.md | 2026-04-01 | user/dmesg.c | source-derived usage/options |
| echo | targetfs/usr/share/man/echo.md | 2026-04-01 | user/echo.c | source-derived usage/options |
| fatregress | targetfs/usr/share/man/fatregress.md | 2026-04-01 | user/fatregress.c | source-derived usage/options |
| forktest | targetfs/usr/share/man/forktest.md | 2026-04-01 | user/forktest.c | source-derived usage/options |
| free | targetfs/usr/share/man/free.md | 2026-04-01 | user/free.c | source-derived usage/options |
| fsregress | targetfs/usr/share/man/fsregress.md | 2026-04-01 | user/fsregress.c | source-derived usage/options |
| getty | targetfs/usr/share/man/getty.md | 2026-04-01 | user/getty.c | source-derived usage/options |
| grep | targetfs/usr/share/man/grep.md | 2026-04-01 | user/grep.c | source-derived usage/options |
| id | targetfs/usr/share/man/id.md | 2026-04-01 | user/id.c | source-derived usage/options |
| ifconfig | targetfs/usr/share/man/ifconfig.md | 2026-04-01 | user/ifconfig.c | source-derived usage/options |
| init | targetfs/usr/share/man/init.md | 2026-04-01 | user/init.c | source-derived usage/options |
| ip | targetfs/usr/share/man/ip.md | 2026-04-01 | user/ip.c | source-derived usage/options |
| isotest | targetfs/usr/share/man/isotest.md | 2026-04-01 | user/isotest.c | source-derived usage/options |
| killall | targetfs/usr/share/man/killall.md | 2026-04-01 | user/killall.c | source-derived usage/options |
| kill | targetfs/usr/share/man/kill.md | 2026-04-01 | user/kill.c | source-derived usage/options |
| ln | targetfs/usr/share/man/ln.md | 2026-04-01 | user/ln.c | source-derived usage/options |
| login | targetfs/usr/share/man/login.md | 2026-04-01 | user/login.c | source-derived usage/options |
| losetup | targetfs/usr/share/man/losetup.md | 2026-04-01 | user/losetup.c | source-derived usage/options |
| lsblk | targetfs/usr/share/man/lsblk.md | 2026-04-01 | user/lsblk.c | source-derived usage/options |
| lspci | targetfs/usr/share/man/lspci.md | 2026-04-01 | user/lspci.c | source-derived usage/options |
| ls | targetfs/usr/share/man/ls.md | 2026-04-02 | user/ls.c | source-derived usage/options |
| man | targetfs/usr/share/man/man.md | 2026-04-02 | user/man.c | source-derived usage/options |
| mkdir | targetfs/usr/share/man/mkdir.md | 2026-04-01 | user/mkdir.c | source-derived usage/options |
| mount | targetfs/usr/share/man/mount.md | 2026-04-02 | user/mount.c | source-derived usage/options |
| mktmpfs | targetfs/usr/share/man/mktmpfs.md | 2026-04-02 | user/mktmpfs.c | source-derived usage/options |
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
| sh | targetfs/usr/share/man/sh.md | 2026-04-01 | user/sh.c | source-derived usage/options |
| sigtest | targetfs/usr/share/man/sigtest.md | 2026-04-01 | user/sigtest.c | source-derived usage/options |
| sockettest | targetfs/usr/share/man/sockettest.md | 2026-04-01 | user/sockettest.c | source-derived usage/options |
| sgrep | targetfs/usr/share/man/sgrep.md | 2026-04-01 | ports/sbase/grep.c | upstream sbase port via auxv6 Makefile |
| stressfs | targetfs/usr/share/man/stressfs.md | 2026-04-01 | user/stressfs.c | source-derived usage/options |
| su | targetfs/usr/share/man/su.md | 2026-04-01 | user/su.c | source-derived usage/options |
| symlinktest | targetfs/usr/share/man/symlinktest.md | 2026-04-01 | user/symlinktest.c | source-derived usage/options |
| tail | targetfs/usr/share/man/tail.md | 2026-04-01 | user/tail.c | source-derived usage/options |
| tcptest | targetfs/usr/share/man/tcptest.md | 2026-04-01 | user/tcptest.c | source-derived usage/options |
| telinit | targetfs/usr/share/man/telinit.md | 2026-04-01 | user/telinit.c | source-derived usage/options |
| telnet | targetfs/usr/share/man/telnet.md | 2026-04-01 | user/telnet.c | source-derived usage/options |
| termcheck | targetfs/usr/share/man/termcheck.md | 2026-04-01 | user/termcheck.c | source-derived usage/options |
| termdemo | targetfs/usr/share/man/termdemo.md | 2026-04-01 | user/termdemo.c | source-derived usage/options |
| time | targetfs/usr/share/man/time.md | 2026-04-02 | user/time.c | source-derived usage/options |
| umount | targetfs/usr/share/man/umount.md | 2026-04-01 | user/umount.c | source-derived usage/options |
| uname | targetfs/usr/share/man/uname.md | 2026-04-01 | user/uname.c | source-derived usage/options |
| usertests | targetfs/usr/share/man/usertests.md | 2026-04-01 | user/usertests.c | source-derived usage/options |
| v6dhcpd | targetfs/usr/share/man/v6dhcpd.md | 2026-04-01 | user/v6dhcpd.c | source-derived usage/options |
| wc | targetfs/usr/share/man/wc.md | 2026-04-01 | user/wc.c | source-derived usage/options |
| whoami | targetfs/usr/share/man/whoami.md | 2026-04-01 | user/whoami.c | source-derived usage/options |
| zombie | targetfs/usr/share/man/zombie.md | 2026-04-01 | user/zombie.c | source-derived usage/options |

## Task Notes
- Utility pages are generated by tools/gen-man-pages.sh.
- Option lists include explicit '-x'/'--long' tokens detected in source strings and usage checks.
- Re-run tools/gen-man-pages.sh after userland changes to keep pages and dates current.
- man now paginates terminal output using the current tty winsize instead of a fixed 80x24 assumption.
- 6get transfer semantics and progress behavior are documented in docs/6get-http-transfer-notes.md.
- ext2 dirent emission now guards against 32-bit inode to 16-bit dirent truncation yielding zero inum, preserving entry visibility for tools that skip inum==0.
