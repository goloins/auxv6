#ifndef _USER_NETCOMMON_H_
#define _USER_NETCOMMON_H_

#include "../include/types.h"
#include "../include/user.h"

static void __attribute__((unused))
net_print_ipv4(uint ip)
{
  dprintf(1, "%d.%d.%d.%d",
         (ip >> 24) & 0xff,
         (ip >> 16) & 0xff,
         (ip >> 8) & 0xff,
         ip & 0xff);
}

static void __attribute__((unused))
net_print_mac(uchar *mac)
{
  int i;
  int nonzero;

  nonzero = 0;
  for(i = 0; i < 6; i++) {
    if(mac[i] != 0)
      nonzero = 1;
  }
  if(!nonzero) {
    dprintf(1, "-");
    return;
  }

  for(i = 0; i < 6; i++) {
    if(i > 0)
      dprintf(1, ":");
    dprintf(1, "%02x", mac[i]);
  }
}

static int __attribute__((unused))
net_parse_ipv4(const char *s, uint *out)
{
  int i;
  int part;
  uint ip;

  if(s == 0 || out == 0)
    return -1;
  if(strcmp(s, "-") == 0 || strcmp(s, "0") == 0) {
    *out = 0;
    return 0;
  }

  ip = 0;
  for(i = 0; i < 4; i++) {
    if(*s < '0' || *s > '9')
      return -1;
    part = 0;
    while(*s >= '0' && *s <= '9') {
      part = part * 10 + (*s - '0');
      if(part > 255)
        return -1;
      s++;
    }
    ip = (ip << 8) | (uint)part;
    if(i < 3) {
      if(*s != '.')
        return -1;
      s++;
    }
  }
  if(*s != '\0')
    return -1;

  *out = ip;
  return 0;
}

static int __attribute__((unused))
net_parse_prefix(const char *s, uint *mask)
{
  int prefix;

  if(s == 0 || mask == 0)
    return -1;
  prefix = atoi(s);
  if(prefix < 0 || prefix > 32)
    return -1;
  if(prefix == 0)
    *mask = 0;
  else
    *mask = 0xffffffffU << (32 - prefix);
  return 0;
}

static int __attribute__((unused))
net_parse_cidr(const char *s, uint *addr, uint *mask)
{
  char buf[32];
  char *slash;

  if(s == 0 || addr == 0 || mask == 0)
    return -1;
  if(strlen(s) >= sizeof(buf))
    return -1;
  strcpy(buf, s);
  slash = strchr(buf, '/');
  if(slash == 0)
    return -1;
  *slash = '\0';
  slash++;

  if(net_parse_ipv4(buf, addr) < 0)
    return -1;
  return net_parse_prefix(slash, mask);
}

static int __attribute__((unused))
net_load_ifs(struct netifinfo *ifs, int max)
{
  int n;

  n = netifinfo(ifs, max);
  if(n < 0)
    dprintf(2, "net: netifinfo failed\n");
  return n;
}

static __attribute__((unused)) struct netifinfo*
net_find_if(struct netifinfo *ifs, int n, const char *name)
{
  int i;

  for(i = 0; i < n; i++) {
    if(strcmp(ifs[i].if_name, name) == 0)
      return &ifs[i];
  }
  return 0;
}

static void __attribute__((unused))
net_show_interfaces(struct netifinfo *ifs, int n)
{
  int i;

  for(i = 0; i < n; i++) {
    dprintf(1, "%s: flags=0x%x mtu %d\n", ifs[i].if_name, ifs[i].if_flags, ifs[i].if_mtu);
    dprintf(1, "    inet ");
    if(ifs[i].if_addr)
      net_print_ipv4(ifs[i].if_addr);
    else
      dprintf(1, "-");
    dprintf(1, " netmask ");
    if(ifs[i].if_netmask)
      net_print_ipv4(ifs[i].if_netmask);
    else
      dprintf(1, "-");
    dprintf(1, " ether ");
    net_print_mac(ifs[i].if_hwaddr);
    dprintf(1, "\n");
  }
}

#endif