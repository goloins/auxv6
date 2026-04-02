#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
MANDIR="$ROOT_DIR/targetfs/usr/share/man"
TODAY=$(date +%F)

mkdir -p "$MANDIR"

utility_duty() {
  case "$1" in
    arp) echo "Display ARP cache entries." ;;
    cat) echo "Concatenate files to standard output." ;;
    chgrp) echo "Change group ownership of files." ;;
    chmod) echo "Change mode bits on files." ;;
    chown) echo "Change owner/group on files." ;;
    chvt) echo "Switch active virtual terminal." ;;
    clear) echo "Clear terminal display." ;;
    dash) echo "POSIX shell (ported in ports/; deep source analysis skipped)." ;;
    date) echo "Print current date/time from RTC." ;;
    devman) echo "Scan and print device inventory." ;;
    df) echo "Report filesystem usage." ;;
    dmesg) echo "Print kernel message buffer." ;;
    echo) echo "Print arguments to stdout." ;;
    fatregress) echo "Run FAT filesystem regression checks." ;;
    forktest) echo "Stress-test process forking." ;;
    free) echo "Report memory counters." ;;
    fsregress) echo "Run filesystem regression checks." ;;
    getty) echo "Attach terminal and prompt for login." ;;
    grep) echo "Filter lines by pattern." ;;
    id) echo "Show identity information." ;;
    ifconfig) echo "Show interface address/configuration." ;;
    init) echo "Primary init process." ;;
    ip) echo "Show IP address information." ;;
    isotest) echo "Exercise ISO filesystem support." ;;
    kill) echo "Send signals to process IDs." ;;
    killall) echo "Send signals to process names." ;;
    ln) echo "Create hard/symbolic links." ;;
    login) echo "Authenticate and start user session." ;;
    losetup) echo "Configure loop block devices." ;;
    ls) echo "List directory entries." ;;
    lsblk) echo "List block devices." ;;
    lspci) echo "List PCI devices." ;;
    man) echo "Render and display manual pages." ;;
    mkdir) echo "Create directories." ;;
    mount) echo "Mount filesystems." ;;
    mktmpfs) echo "Create a tmpfs mount with a size limit." ;;
    mounts) echo "List mounted filesystems." ;;
    mounttest) echo "Mount/unmount regression utility." ;;
    mv) echo "Move or rename files." ;;
    netcat) echo "TCP/UDP network client." ;;
    netinfo) echo "Display network stack summary." ;;
    netstat) echo "Show network interfaces/routes." ;;
    nslookup) echo "Resolve DNS names." ;;
    passwd) echo "Change account password." ;;
    ping) echo "Send ICMP echo requests." ;;
    ps) echo "Show process list." ;;
    pwd) echo "Print working directory." ;;
    rarp) echo "Inspect reverse ARP state." ;;
    reset) echo "Reset terminal state." ;;
    rm) echo "Remove files/directories." ;;
    route) echo "Display route table." ;;
    runlevel) echo "Print current runlevel." ;;
    sh) echo "Interactive shell and script runner." ;;
    sigtest) echo "Signal API regression utility." ;;
    sockettest) echo "Socket API regression utility." ;;
    stressfs) echo "Filesystem stress workload." ;;
    su) echo "Switch user identity." ;;
    symlinktest) echo "Symlink regression utility." ;;
    tail) echo "Print tail of file, optional follow." ;;
    tcptest) echo "TCP regression utility." ;;
    telinit) echo "Request init runlevel transition." ;;
    telnet) echo "Telnet-style TCP client." ;;
    termcheck) echo "Terminal behavior verification tool." ;;
    termdemo) echo "Terminal feature demonstration." ;;
    time) echo "Run command and print elapsed time." ;;
    umount) echo "Unmount filesystem path." ;;
    uname) echo "Print system name." ;;
    usertests) echo "Run userland syscall tests." ;;
    v6dhcpd) echo "DHCP server utility." ;;
    wc) echo "Count lines/words/bytes." ;;
    whoami) echo "Print effective username." ;;
    zombie) echo "Create zombie process for testing." ;;
    *) echo "Utility command." ;;
  esac
}

utilities="arp cat chgrp chmod chown chvt clear dash date devman df dmesg echo fatregress forktest free fsregress getty grep id ifconfig init ip isotest kill killall ln login losetup ls lsblk lspci man mkdir mount mktmpfs mounts mounttest mv netcat netinfo netstat nslookup passwd ping ps pwd rarp reset rm route runlevel sh sigtest sockettest stressfs su symlinktest tail tcptest telinit telnet termcheck termdemo time umount uname usertests v6dhcpd wc whoami zombie"

for util in $utilities; do
  src="$ROOT_DIR/user/$util.c"
  page="$MANDIR/$util.md"
  duty=$(utility_duty "$util")

  usage_lines=""
  option_tokens=""

  if [ -f "$src" ]; then
    usage_lines=$(rg -n "usage:|Usage:" "$src" | sed -E 's/^.*\"(.*)\\n\".*$/\1/' | sed '/^$/d' | awk '!seen[$0]++' || true)
    option_tokens=$({
      rg -o --no-filename --pcre2 '"--?[A-Za-z][A-Za-z0-9-]*"' "$src" | tr -d '"' || true
      if [ -n "$usage_lines" ]; then
        printf "%s\n" "$usage_lines" | rg -o --no-filename --pcre2 -- '-{1,2}[A-Za-z][A-Za-z0-9-]*' || true
      fi
    } | sort -u)
  fi

  {
    echo "# $util(1)"
    echo
    echo "## Name"
    echo "$util - $duty"
    echo
    echo "## Synopsis"
    if [ -n "$usage_lines" ]; then
      printf "%s\n" "$usage_lines" | sed '/^$/d' | sed 's/^/- /'
    else
      echo "- $util"
    fi
    echo
    echo "## Duty"
    echo "$duty"
    echo
    echo "## Options"
    if [ -n "$option_tokens" ]; then
      printf "%s\n" "$option_tokens" | sed 's/^/- `/;s/$/` (detected in source usage\/option checks)/'
    else
      echo "- none detected"
    fi
    echo
    echo "## Examples"
    echo "- $util"
    echo
    echo "## Source Audit"
    if [ -f "$src" ]; then
      echo "- Source file: user/$util.c"
    else
      echo "- Source file: ports/ignored"
    fi
    echo "- Last updated: $TODAY"
  } > "$page"
done
