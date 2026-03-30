/*
 * ARP (Address Resolution Protocol) for auxv6
 *
 * Handles:
 * - IP to MAC address resolution
 * - ARP cache management
 * - ARP request/reply processing
 * - Gratuitous ARP
 *
 * TODO Phase 1:
 * - [ ] ARP cache with timeout
 * - [ ] ARP request sending
 * - [ ] ARP reply processing
 * - [ ] Queue packets pending resolution
 *
 * TODO Phase 2:
 * - [ ] Gratuitous ARP
 * - [ ] Proxy ARP
 * - [ ] ARP announcement on interface up
 *
 * Reference: RFC 826 - Address Resolution Protocol
 * See also: NetBSD sys/netinet/if_arp.c
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "net.h"

/* ARP packet structure */
struct arp_hdr {
    uint16_t htype;         /* Hardware type (1 = Ethernet) */
    uint16_t ptype;         /* Protocol type (0x0800 = IPv4) */
    uint8_t  hlen;          /* Hardware address length (6 for Ethernet) */
    uint8_t  plen;          /* Protocol address length (4 for IPv4) */
    uint16_t oper;          /* Operation */
    /* Followed by variable-length addresses */
} __attribute__((packed));

/* ARP Ethernet+IPv4 payload */
struct arp_eth_ipv4 {
    struct arp_hdr hdr;
    uint8_t  sha[6];        /* Sender hardware address */
    uint8_t  spa[4];        /* Sender protocol address */
    uint8_t  tha[6];        /* Target hardware address */
    uint8_t  tpa[4];        /* Target protocol address */
} __attribute__((packed));

#define ARP_HW_ETHER        1
#define ARP_PROTO_IP        0x0800

#define ARP_OP_REQUEST      1
#define ARP_OP_REPLY        2

/* ARP cache entry */
struct arp_entry {
    uint32_t ip;            /* IP address */
    uint8_t  mac[6];        /* MAC address */
    uint     expire;        /* Expiration time (ticks) */
    int      state;         /* Entry state */
    struct ifnet *ifp;      /* Associated interface */
    
    /* Pending packet queue */
    struct mbuf *pending;   /* Packet waiting for resolution */
};

#define ARP_STATE_FREE      0
#define ARP_STATE_PENDING   1   /* Waiting for reply */
#define ARP_STATE_RESOLVED  2   /* Valid entry */
#define ARP_STATE_STALE     3   /* Needs refresh */

/* ARP cache */
#define ARP_CACHE_SIZE      64
#define ARP_TIMEOUT_TICKS   (60 * 100)   /* 60 seconds at 100 ticks/sec */
#define ARP_PENDING_TIMEOUT (3 * 100)    /* 3 seconds for pending */

static struct {
    struct spinlock lock;
    struct arp_entry entries[ARP_CACHE_SIZE];
} arp_cache;

/* External declarations */
extern uint ticks;  /* From kernel time */
int ether_output_arp(struct ifnet *ifp, struct mbuf *m, const uint8_t *dst_mac);

/*
 * Network byte order helpers
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

static inline uint32_t
htonl(uint32_t x)
{
    return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) |
           ((x >> 8) & 0xFF00) | ((x >> 24) & 0xFF);
}

static inline uint32_t
ntohl(uint32_t x)
{
    return htonl(x);
}

/*
 * Initialize ARP subsystem
 */
void
arp_init(void)
{
    initlock(&arp_cache.lock, "arp");
    memset(arp_cache.entries, 0, sizeof(arp_cache.entries));
}

/*
 * Find entry in ARP cache
 */
static struct arp_entry *
arp_lookup(uint32_t ip)
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache.entries[i].state != ARP_STATE_FREE &&
            arp_cache.entries[i].ip == ip) {
            return &arp_cache.entries[i];
        }
    }
    return 0;
}

/*
 * Find a free entry or expire an old one
 */
static struct arp_entry *
arp_alloc(void)
{
    struct arp_entry *oldest = 0;
    uint oldest_time = 0xFFFFFFFF;
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache.entries[i].state == ARP_STATE_FREE)
            return &arp_cache.entries[i];
        
        if (arp_cache.entries[i].expire < oldest_time) {
            oldest_time = arp_cache.entries[i].expire;
            oldest = &arp_cache.entries[i];
        }
    }
    
    /* Expire the oldest entry */
    if (oldest) {
        if (oldest->pending) {
            m_free(oldest->pending);
            oldest->pending = 0;
        }
        memset(oldest, 0, sizeof(*oldest));
    }
    
    return oldest;
}

/*
 * Send an ARP request
 */
static int
arp_send_request(struct ifnet *ifp, uint32_t target_ip)
{
    struct mbuf *m;
    struct arp_eth_ipv4 *arp;
    static const uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    
    m = m_alloc();
    if (!m)
        return -1;
    
    arp = (struct arp_eth_ipv4 *)m->data;
    
    arp->hdr.htype = htons(ARP_HW_ETHER);
    arp->hdr.ptype = htons(ARP_PROTO_IP);
    arp->hdr.hlen = 6;
    arp->hdr.plen = 4;
    arp->hdr.oper = htons(ARP_OP_REQUEST);
    
    /* Sender: our MAC and IP */
    memmove(arp->sha, ifp->if_hwaddr, 6);
    
    /* Get our IP (from first address on interface) */
    uint32_t src_ip = ifp->if_addr;  /* TODO: proper address lookup */
    arp->spa[0] = src_ip & 0xFF;
    arp->spa[1] = (src_ip >> 8) & 0xFF;
    arp->spa[2] = (src_ip >> 16) & 0xFF;
    arp->spa[3] = (src_ip >> 24) & 0xFF;
    
    /* Target: unknown MAC, known IP */
    memset(arp->tha, 0, 6);
    arp->tpa[0] = target_ip & 0xFF;
    arp->tpa[1] = (target_ip >> 8) & 0xFF;
    arp->tpa[2] = (target_ip >> 16) & 0xFF;
    arp->tpa[3] = (target_ip >> 24) & 0xFF;
    
    m->len = sizeof(*arp);
    
    return ether_output_arp(ifp, m, bcast);
}

/*
 * Resolve IP to MAC address
 * Returns 0 if resolved, -1 if pending (packet queued)
 */
int
arp_resolve(struct ifnet *ifp, uint32_t ip, uint8_t *mac)
{
    struct arp_entry *entry;
    
    acquire(&arp_cache.lock);
    
    entry = arp_lookup(ip);
    
    if (entry && entry->state == ARP_STATE_RESOLVED) {
        /* Check if still valid */
        if (entry->expire > ticks) {
            memmove(mac, entry->mac, 6);
            release(&arp_cache.lock);
            return 0;
        }
        
        /* Entry expired, refresh */
        entry->state = ARP_STATE_STALE;
    }
    
    if (!entry) {
        /* Create new pending entry */
        entry = arp_alloc();
        if (!entry) {
            release(&arp_cache.lock);
            return -1;
        }
        
        entry->ip = ip;
        entry->state = ARP_STATE_PENDING;
        entry->expire = ticks + ARP_PENDING_TIMEOUT;
        entry->ifp = ifp;
        entry->pending = 0;
    }
    
    release(&arp_cache.lock);
    
    /* Send ARP request */
    arp_send_request(ifp, ip);
    
    return -1;  /* Resolution pending */
}

/*
 * Process received ARP packet
 */
void
arp_input(struct ifnet *ifp, struct mbuf *m)
{
    struct arp_eth_ipv4 *arp;
    uint32_t spa, tpa;
    struct arp_entry *entry;
    
    if (m->len < sizeof(*arp)) {
        m_free(m);
        return;
    }
    
    arp = (struct arp_eth_ipv4 *)m->data;
    
    /* Validate ARP header */
    if (ntohs(arp->hdr.htype) != ARP_HW_ETHER ||
        ntohs(arp->hdr.ptype) != ARP_PROTO_IP ||
        arp->hdr.hlen != 6 ||
        arp->hdr.plen != 4) {
        m_free(m);
        return;
    }
    
    /* Extract addresses */
    spa = arp->spa[0] | (arp->spa[1] << 8) | (arp->spa[2] << 16) | (arp->spa[3] << 24);
    tpa = arp->tpa[0] | (arp->tpa[1] << 8) | (arp->tpa[2] << 16) | (arp->tpa[3] << 24);
    
    acquire(&arp_cache.lock);
    
    /* Update cache with sender's info */
    entry = arp_lookup(spa);
    if (!entry) {
        entry = arp_alloc();
    }
    
    if (entry) {
        entry->ip = spa;
        memmove(entry->mac, arp->sha, 6);
        entry->expire = ticks + ARP_TIMEOUT_TICKS;
        entry->ifp = ifp;
        
        /* Send any pending packet */
        if (entry->state == ARP_STATE_PENDING && entry->pending) {
            struct mbuf *pending = entry->pending;
            entry->pending = 0;
            entry->state = ARP_STATE_RESOLVED;
            release(&arp_cache.lock);
            
            /* Try to send pending packet */
            ether_output(ifp, pending, spa);
            
            acquire(&arp_cache.lock);
        } else {
            entry->state = ARP_STATE_RESOLVED;
        }
    }
    
    release(&arp_cache.lock);
    
    /* Handle ARP request */
    if (ntohs(arp->hdr.oper) == ARP_OP_REQUEST) {
        /* Check if target is us */
        if (tpa == ifp->if_addr) {
            /* Send reply */
            struct mbuf *reply = m_alloc();
            if (reply) {
                struct arp_eth_ipv4 *r = (struct arp_eth_ipv4 *)reply->data;
                
                r->hdr.htype = htons(ARP_HW_ETHER);
                r->hdr.ptype = htons(ARP_PROTO_IP);
                r->hdr.hlen = 6;
                r->hdr.plen = 4;
                r->hdr.oper = htons(ARP_OP_REPLY);
                
                /* Sender: us */
                memmove(r->sha, ifp->if_hwaddr, 6);
                memmove(r->spa, arp->tpa, 4);
                
                /* Target: original sender */
                memmove(r->tha, arp->sha, 6);
                memmove(r->tpa, arp->spa, 4);
                
                reply->len = sizeof(*r);
                
                ether_output_arp(ifp, reply, arp->sha);
            }
        }
    }
    
    m_free(m);
}

/*
 * Periodic ARP cache maintenance
 * Called from timer interrupt
 */
void
arp_timer(void)
{
    acquire(&arp_cache.lock);
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        struct arp_entry *e = &arp_cache.entries[i];
        
        if (e->state == ARP_STATE_FREE)
            continue;
        
        if (e->expire <= ticks) {
            /* Entry expired */
            if (e->pending) {
                m_free(e->pending);
            }
            memset(e, 0, sizeof(*e));
        }
    }
    
    release(&arp_cache.lock);
}

/*
 * Add static ARP entry
 */
int
arp_add_static(uint32_t ip, const uint8_t *mac, struct ifnet *ifp)
{
    struct arp_entry *entry;
    
    acquire(&arp_cache.lock);
    
    entry = arp_lookup(ip);
    if (!entry)
        entry = arp_alloc();
    
    if (!entry) {
        release(&arp_cache.lock);
        return -1;
    }
    
    entry->ip = ip;
    memmove(entry->mac, mac, 6);
    entry->expire = 0xFFFFFFFF;  /* Never expire */
    entry->state = ARP_STATE_RESOLVED;
    entry->ifp = ifp;
    
    release(&arp_cache.lock);
    return 0;
}

/*
 * Dump ARP cache for debugging
 */
void
arp_dump(void)
{
    char mac_str[18];
    
    cprintf("ARP cache:\n");
    
    acquire(&arp_cache.lock);
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        struct arp_entry *e = &arp_cache.entries[i];
        
        if (e->state == ARP_STATE_FREE)
            continue;
        
        ether_sprintf(mac_str, e->mac);
        cprintf("  %d.%d.%d.%d -> %s %s\n",
                e->ip & 0xFF, (e->ip >> 8) & 0xFF,
                (e->ip >> 16) & 0xFF, (e->ip >> 24) & 0xFF,
                mac_str,
                e->state == ARP_STATE_PENDING ? "(pending)" :
                e->state == ARP_STATE_STALE ? "(stale)" : "");
    }
    
    release(&arp_cache.lock);
}
