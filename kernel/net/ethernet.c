/*
 * Ethernet Layer and Frame Processing for auxv6
 *
 * Handles:
 * - Ethernet frame encapsulation/decapsulation
 * - MAC address management
 * - Protocol demultiplexing (IPv4, ARP)
 * - Broadcast/multicast handling
 *
 * TODO Phase 1:
 * - [ ] Ethernet header construction
 * - [ ] Frame transmission via ifnet
 * - [ ] Frame reception and demux
 * - [ ] MTU handling
 *
 * TODO Phase 2:
 * - [ ] VLAN tagging (802.1Q)
 * - [ ] Jumbo frame support
 * - [ ] MAC-based filtering
 *
 * Reference: IEEE 802.3 Ethernet Standard
 * See also: NetBSD sys/net/if_ethersubr.c
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "net.h"

/* Ethernet header */
struct ether_header {
    uint8_t  dst[6];        /* Destination MAC */
    uint8_t  src[6];        /* Source MAC */
    uint16_t type;          /* EtherType (network byte order) */
} __attribute__((packed));

#define ETHER_HDR_LEN   14
#define ETHER_MIN_LEN   64
#define ETHER_MAX_LEN   1518
#define ETHER_MTU       1500

/* EtherTypes */
#define ETHERTYPE_IP        0x0800
#define ETHERTYPE_ARP       0x0806
#define ETHERTYPE_IPV6      0x86DD
#define ETHERTYPE_VLAN      0x8100

/* Broadcast MAC address */
static const uint8_t ether_bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/*
 * Convert 16-bit value to network byte order (big-endian)
 */
static inline uint16_t
htons(uint16_t x)
{
    return ((x & 0xFF) << 8) | ((x >> 8) & 0xFF);
}

static inline uint16_t
ntohs(uint16_t x)
{
    return htons(x);
}

/*
 * Compare two MAC addresses
 */
int
ether_addr_equal(const uint8_t *a, const uint8_t *b)
{
    return (a[0] == b[0] && a[1] == b[1] && a[2] == b[2] &&
            a[3] == b[3] && a[4] == b[4] && a[5] == b[5]);
}

/*
 * Check if MAC address is broadcast
 */
int
ether_is_broadcast(const uint8_t *addr)
{
    return ether_addr_equal(addr, ether_bcast);
}

/*
 * Check if MAC address is multicast (LSB of first octet is 1)
 */
int
ether_is_multicast(const uint8_t *addr)
{
    return (addr[0] & 0x01) != 0;
}

/*
 * Output an IP packet via Ethernet
 * Handles ARP resolution and frame construction
 */
int
ether_output(struct ifnet *ifp, struct mbuf *m, uint32_t dst_ip)
{
    struct ether_header *eh;
    uint8_t dst_mac[6];
    
    /* Resolve IP to MAC via ARP */
    if (arp_resolve(ifp, dst_ip, dst_mac) < 0) {
        /* ARP resolution pending, packet will be queued */
        return -1;
    }
    
    /* Ensure room for Ethernet header */
    /* TODO: Prepend header to mbuf */
    if (m->len + ETHER_HDR_LEN > sizeof(m->data))
        return -1;
    
    /* Move data to make room for header */
    memmove(m->data + ETHER_HDR_LEN, m->data, m->len);
    
    /* Fill in Ethernet header */
    eh = (struct ether_header *)m->data;
    memmove(eh->dst, dst_mac, 6);
    memmove(eh->src, ifp->if_hwaddr, 6);
    eh->type = htons(ETHERTYPE_IP);
    
    m->len += ETHER_HDR_LEN;
    
    /* Send via interface */
    return if_output(ifp, m);
}

/*
 * Output an ARP packet via Ethernet
 */
int
ether_output_arp(struct ifnet *ifp, struct mbuf *m, const uint8_t *dst_mac)
{
    struct ether_header *eh;
    
    /* Ensure room for Ethernet header */
    if (m->len + ETHER_HDR_LEN > sizeof(m->data))
        return -1;
    
    /* Move data to make room for header */
    memmove(m->data + ETHER_HDR_LEN, m->data, m->len);
    
    /* Fill in Ethernet header */
    eh = (struct ether_header *)m->data;
    memmove(eh->dst, dst_mac, 6);
    memmove(eh->src, ifp->if_hwaddr, 6);
    eh->type = htons(ETHERTYPE_ARP);
    
    m->len += ETHER_HDR_LEN;
    
    /* Send via interface */
    return if_output(ifp, m);
}

/*
 * Process a received Ethernet frame
 * Called by NIC drivers after receiving a frame
 */
void
ether_input(struct ifnet *ifp, struct mbuf *m)
{
    struct ether_header *eh;
    uint16_t type;
    
    if (m->len < ETHER_HDR_LEN) {
        /* Frame too short */
        m_free(m);
        return;
    }
    
    eh = (struct ether_header *)m->data;
    type = ntohs(eh->type);
    
    /* Check destination - unicast must match our MAC, or broadcast/multicast */
    if (!ether_is_broadcast(eh->dst) &&
        !ether_is_multicast(eh->dst) &&
        !ether_addr_equal(eh->dst, ifp->if_hwaddr)) {
        /* Not for us - promiscuous mode only */
        m_free(m);
        return;
    }
    
    /* Strip Ethernet header */
    m->data += ETHER_HDR_LEN;
    m->len -= ETHER_HDR_LEN;
    
    /* Demultiplex based on EtherType */
    switch (type) {
    case ETHERTYPE_IP:
        ip_input(ifp, m);
        break;
    case ETHERTYPE_ARP:
        arp_input(ifp, m);
        break;
    default:
        /* Unknown protocol */
        m_free(m);
        break;
    }
}

/*
 * Format MAC address for printing
 */
void
ether_sprintf(char *buf, const uint8_t *addr)
{
    /* Format: XX:XX:XX:XX:XX:XX */
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 6; i++) {
        if (i > 0)
            *buf++ = ':';
        *buf++ = hex[(addr[i] >> 4) & 0xF];
        *buf++ = hex[addr[i] & 0xF];
    }
    *buf = '\0';
}
