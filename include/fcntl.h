#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200

#define MNT_RDONLY 0x0001
#define MNT_NOSUID 0x0002
#define MNT_NODEV  0x0004
#define MNT_NOEXEC 0x0008
#define MNT_SYNC   0x0010
#define MNT_REMOUNT 0x0020

#define MNT_DEVSHIFT 16
#define MNT_DEVMASK  0x00ff0000
// Encode dev override as (dev + 1) so dev=0 is representable.
#define MNT_MAKEDEV(d) ((((d) + 1) & 0xff) << MNT_DEVSHIFT)
#define MNT_HASDEV(f) (((f) & MNT_DEVMASK) != 0)
#define MNT_GETDEV(f) ((((f) & MNT_DEVMASK) >> MNT_DEVSHIFT) - 1)

#define HD_DISK_UNITS 4
#define HD_PARTS_PER_DISK 4
#define HD_DISK_DEV(unit) (unit)
#define HD_PART_BASE HD_DISK_UNITS
#define HD_PART_DEV(unit, partno) (HD_PART_BASE + (unit) * HD_PARTS_PER_DISK + ((partno) - 1))
