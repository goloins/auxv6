# Future Device-Node Daemon Notes

## Context

This document captures practical notes from dynamic PTY work in auxv6 and
the devman phase-2 implementation (policy rules + stale-node cleanup).

## What Exists Today (post-phase-2)

- `devman -s`: kernel inventory scan → node creation via configurable policy rules
  from `/etc/devman.conf` (glob-pattern + octal-mode format).
- `devman -rr`: remove managed nodes then rescan.
- `devman -c`: compare `/dev` contents against current inventory and unlink stale
  nodes.
- `/etc/devman.conf` ships policy rules for all standard auxv6 devices.

## Why Static Scan Is Still Temporary

- `devman -s` runs once at boot; hardware attached after that point is invisible.
- No mechanism to remove nodes when hardware is detached at runtime.
- Rule set does not yet express `owner` / `group` policy (uid/gid fields).

## Recommendations For A Future Daemon

- Add a small userspace daemon that blocks on a kernel hotplug event fd.
- On hotplug event: re-run enumeration + rule application for the affected device.
- On hotunplug event: identify node(s) mapped to the removed (major, minor) tuple
  and unlink them.
- Keep node policy centralized in `/etc/devman.conf`; daemon inherits same rule
  format and lookup path as `devman -s`.

## Hotplug Event Interface Proposal

### Kernel side

Expose a character device `/dev/devevent` (major 1, minor 7) or a procfs node
`/proc/devevents` that returns a stream of fixed-size records:

```c
/* Proposed ABI — not yet implemented */
#define DEVEV_ADD    1
#define DEVEV_REMOVE 2

struct devman_event {
  uint8_t  ev_type;   /* DEVEV_ADD or DEVEV_REMOVE       */
  uint8_t  ev_class;  /* M_IFBLK or M_IFCHR              */
  uint16_t ev_major;
  uint16_t ev_minor;
  char     ev_hint[32]; /* kernel-suggested node base name */
};
```

`read()` on the fd blocks until an event is available, then returns one
`struct devman_event`.  `poll()`/`select()` readability is set when events are
queued.  Events are produced by the block-device and character-device layers on
device attach/detach.

### Userspace daemon sketch

```
loop:
  read(evfd, &ev, sizeof ev)
  if ev.ev_type == DEVEV_ADD:
    devman_enumerate_all()        # rebuild full inventory
    devman_scan_and_create()      # apply rules for new node
  if ev.ev_type == DEVEV_REMOVE:
    devman_enumerate_all()
    devman_cleanup_stale()        # unlink node(s) no longer in inventory
```

### Kernel implementation notes

- The event queue can be a simple ring buffer (e.g. 32 entries) in a kernel
  global, protected by a spinlock.
- `read()` sleeps on a `wakeup()` channel when the queue is empty.
- Overflow: drop oldest events and set an overflow flag bit in the next record.

## Suggested Rollout

1. ✅ Implement `devman -s` static-scan mode with configurable policy rules.
2. ✅ Implement `devman -c` stale-node cleanup.
3. Add `owner`/`group` fields to `devman_rule` and extend devman.conf format.
4. Implement `/dev/devevent` kernel ring buffer.
5. Implement `devman -d` daemon mode that blocks on `/dev/devevent`.
6. Update `/etc/init` to launch `devman -d &` once devevent is available.
