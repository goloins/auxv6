#include "types.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "string.h"
#include "sys/ioctl.h"

static void
usage(void)
{
  dprintf(2, "usage: tuntapctl create <tun|tap> [name]\n");
  dprintf(2, "       tuntapctl destroy [<tun|tap>] <name>\n");
  dprintf(2, "       tuntapctl get [<tun|tap>] <name>\n");
  dprintf(2, "       tuntapctl persist [<tun|tap>] <name> <0|1>\n");
  dprintf(2, "       tuntapctl owner [<tun|tap>] <name> <uid>\n");
  dprintf(2, "       tuntapctl group [<tun|tap>] <name> <gid>\n");
  exit(1);
}

static int
parse_mode(const char *s)
{
  if(strcmp(s, "tun") == 0)
    return IFF_TUN;
  if(strcmp(s, "tap") == 0)
    return IFF_TAP;
  return -1;
}

static int
mode_from_ifname(const char *name)
{
  if(name == 0)
    return -1;
  if(strncmp(name, "tun", 3) == 0)
    return IFF_TUN;
  if(strncmp(name, "tap", 3) == 0)
    return IFF_TAP;
  return -1;
}

static int
bind_name(int fd, int mode, const char *name, struct ifreq *ifr)
{
  memset(ifr, 0, sizeof(*ifr));
  ifr->ifr_flags = (short)(mode | IFF_NO_PI);
  if(name && name[0]){
    strncpy(ifr->ifr_name, name, sizeof(ifr->ifr_name) - 1);
    ifr->ifr_name[sizeof(ifr->ifr_name) - 1] = 0;
  }
  if(ioctl(fd, TUNSETIFF, ifr) < 0)
    return -1;
  return 0;
}

int
main(int argc, char **argv)
{
  int fd;
  int mode;
  int val;
  int req;
  const char *ifname;
  const char *valarg;
  struct ifreq ifr;

  if(argc < 3)
    usage();

  fd = open("/dev/net/tun", O_RDWR);
  if(fd < 0){
    dprintf(2, "tuntapctl: open /dev/net/tun failed\n");
    exit(1);
  }

  if(strcmp(argv[1], "create") == 0){
    if(argc != 3 && argc != 4)
      usage();
    mode = parse_mode(argv[2]);
    if(mode < 0)
      usage();
    if(bind_name(fd, mode, argc == 4 ? argv[3] : "", &ifr) < 0){
      dprintf(2, "tuntapctl: TUNSETIFF failed\n");
      close(fd);
      exit(1);
    }
    /* Persist so the interface survives after this fd is closed. */
    val = 1;
    if(ioctl(fd, TUNSETPERSIST, val) < 0){
      dprintf(2, "tuntapctl: TUNSETPERSIST failed\n");
      close(fd);
      exit(1);
    }
    dprintf(1, "%s mode=%s flags=0x%x\n", ifr.ifr_name,
            (mode == IFF_TAP) ? "tap" : "tun", ifr.ifr_flags);
    close(fd);
    exit(0);
  }

  ifname = 0;
  valarg = 0;

  if(strcmp(argv[1], "destroy") == 0 || strcmp(argv[1], "get") == 0){
    if(argc == 4){
      mode = parse_mode(argv[2]);
      ifname = argv[3];
    } else if(argc == 3){
      ifname = argv[2];
      mode = mode_from_ifname(ifname);
    } else {
      usage();
    }
    if(mode < 0)
      usage();
  } else if(strcmp(argv[1], "persist") == 0 ||
            strcmp(argv[1], "owner") == 0 ||
            strcmp(argv[1], "group") == 0){
    if(argc == 5){
      mode = parse_mode(argv[2]);
      ifname = argv[3];
      valarg = argv[4];
    } else if(argc == 4){
      ifname = argv[2];
      mode = mode_from_ifname(ifname);
      valarg = argv[3];
    } else {
      usage();
    }
    if(mode < 0)
      usage();
  } else {
    usage();
  }

  if(bind_name(fd, mode, ifname, &ifr) < 0){
    dprintf(2, "tuntapctl: bind failed for %s\n", ifname);
    close(fd);
    exit(1);
  }

  if(strcmp(argv[1], "get") == 0){
    memset(&ifr, 0, sizeof(ifr));
    if(ioctl(fd, TUNGETIFF, &ifr) < 0){
      dprintf(2, "tuntapctl: TUNGETIFF failed\n");
      close(fd);
      exit(1);
    }
    dprintf(1, "%s flags=0x%x\n", ifr.ifr_name, ifr.ifr_flags);
    close(fd);
    exit(0);
  }

  if(strcmp(argv[1], "destroy") == 0){
    /* Clear persist so the unit can be released when the fd closes. */
    val = 0;
    if(ioctl(fd, TUNSETPERSIST, val) < 0){
      dprintf(2, "tuntapctl: TUNSETPERSIST(0) failed\n");
      close(fd);
      exit(1);
    }
    dprintf(1, "%s destroyed\n", ifname);
    close(fd);
    exit(0);
  }

  val = atoi(valarg);
  req = -1;
  if(strcmp(argv[1], "persist") == 0)
    req = TUNSETPERSIST;
  else if(strcmp(argv[1], "owner") == 0)
    req = TUNSETOWNER;
  else if(strcmp(argv[1], "group") == 0)
    req = TUNSETGROUP;
  else
    usage();

  if(ioctl(fd, req, val) < 0){
    dprintf(2, "tuntapctl: ioctl failed\n");
    close(fd);
    exit(1);
  }

  dprintf(1, "%s ok: %s=%d\n", ifname, argv[1], val);
  close(fd);
  exit(0);
}
