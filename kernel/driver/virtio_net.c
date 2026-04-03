/*
 * Virtio Network Device Driver for auxv6
 *
 * Implements virtio-net specification for paravirtualized networking.
 *
 * Architecture:
 * - Two virtqueues: RX (receive) and TX (transmit)
 * - Optional control queue for advanced features
 * - Integrates with ifnet layer via if_register()
 *
 * TODO Phase 1:
 * - [ ] Basic TX/RX with single-buffer packets
 * - [ ] ifnet integration
 * - [ ] MAC address configuration
 * - [ ] Link status
 *
 * TODO Phase 2:
 * - [ ] Checksum offload
 * - [ ] TSO/GSO support
 * - [ ] Multi-queue support
 * - [ ] VLAN support
 *
 * Reference: Virtio 1.1 Specification Section 5.1
 * See also: Linux drivers/net/virtio_net.c
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "virtio.h"
#include "net.h"

/* Queue indices */
#define VIRTIO_NET_Q_RX       0
#define VIRTIO_NET_Q_TX       1
#define VIRTIO_NET_Q_CTRL     2

/* Virtio net feature bits */
#define VIRTIO_NET_F_CSUM           (1ULL << 0)
#define VIRTIO_NET_F_GUEST_CSUM     (1ULL << 1)
#define VIRTIO_NET_F_CTRL_GUEST_OFFLOADS (1ULL << 2)
#define VIRTIO_NET_F_MTU            (1ULL << 3)
#define VIRTIO_NET_F_MAC            (1ULL << 5)
#define VIRTIO_NET_F_GUEST_TSO4     (1ULL << 7)
#define VIRTIO_NET_F_GUEST_TSO6     (1ULL << 8)
#define VIRTIO_NET_F_GUEST_ECN      (1ULL << 9)
#define VIRTIO_NET_F_GUEST_UFO      (1ULL << 10)
#define VIRTIO_NET_F_HOST_TSO4      (1ULL << 11)
#define VIRTIO_NET_F_HOST_TSO6      (1ULL << 12)
#define VIRTIO_NET_F_HOST_ECN       (1ULL << 13)
#define VIRTIO_NET_F_HOST_UFO       (1ULL << 14)
#define VIRTIO_NET_F_MRG_RXBUF      (1ULL << 15)
#define VIRTIO_NET_F_STATUS         (1ULL << 16)
#define VIRTIO_NET_F_CTRL_VQ        (1ULL << 17)
#define VIRTIO_NET_F_CTRL_RX        (1ULL << 18)
#define VIRTIO_NET_F_CTRL_VLAN      (1ULL << 19)
#define VIRTIO_NET_F_GUEST_ANNOUNCE (1ULL << 21)
#define VIRTIO_NET_F_MQ             (1ULL << 22)
#define VIRTIO_NET_F_CTRL_MAC_ADDR  (1ULL << 23)

/* Virtio net header (prepended to all packets) */
struct virtio_net_hdr {
    uint8_t  flags;
    uint8_t  gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    /* uint16_t num_buffers; if VIRTIO_NET_F_MRG_RXBUF */
} __attribute__((packed));

#define VIRTIO_NET_HDR_F_NEEDS_CSUM     1
#define VIRTIO_NET_HDR_F_DATA_VALID     2
#define VIRTIO_NET_HDR_F_RSC_INFO       4

#define VIRTIO_NET_HDR_GSO_NONE         0
#define VIRTIO_NET_HDR_GSO_TCPV4        1
#define VIRTIO_NET_HDR_GSO_UDP          3
#define VIRTIO_NET_HDR_GSO_TCPV6        4
#define VIRTIO_NET_HDR_GSO_ECN          0x80

/* Virtio net config space */
struct virtio_net_config {
    uint8_t  mac[6];
    uint16_t status;
    uint16_t max_virtqueue_pairs;
    uint16_t mtu;
} __attribute__((packed));

#define VIRTIO_NET_S_LINK_UP     1
#define VIRTIO_NET_S_ANNOUNCE    2

struct virtio_net_rxbuf {
    char data[MBUF_SIZE];
    int in_use;
};

struct virtio_net_tx {
    struct virtio_net_hdr hdr;
    struct mbuf *m;
    int in_use;
};

struct virtio_net_eth_hdr {
    uchar dst[ETH_ADDR_LEN];
    uchar src[ETH_ADDR_LEN];
    ushort type;
} __attribute__((packed));

struct virtio_net_udp_hdr {
    ushort src_port;
    ushort dst_port;
    ushort len;
    ushort csum;
} __attribute__((packed));

static int virtio_net_output(struct ifnet *ifp, struct mbuf *m);
static void virtio_net_poll(struct ifnet *ifp);

static struct ifnet_ops virtio_net_ops = {
    .if_output = virtio_net_output,
    .if_poll = virtio_net_poll,
};

/* Driver-specific data */
struct virtio_net_softc {
    struct virtio_dev  vdev;
    struct spinlock    lock;
    struct ifnet       ifp;
    
    /* MAC address */
    uint8_t mac[6];
    
    /* RX buffer pool */
    #define VIRTIO_NET_RX_BUFS 64
    struct virtio_net_rxbuf rx_bufs[VIRTIO_NET_RX_BUFS];

    /* Fixed TX cookie slots to avoid per-packet page allocation. */
    struct virtio_net_tx tx_slots[VIRTQ_SIZE_DEFAULT];
    
    /* TX tracking */
    int tx_pending;

    /* Debug snapshot state to avoid poll spam when queues are unchanged. */
    ushort dbg_last_rx_used;
    ushort dbg_last_rx_avail;
    ushort dbg_last_tx_used;
    ushort dbg_last_tx_avail;
    int dbg_last_tx_pending;
};

/* Global array of virtio-net devices */
#define MAX_VIRTIO_NET 4
static struct virtio_net_softc virtio_net_devices[MAX_VIRTIO_NET];
static int virtio_net_count = 0;
extern int ncpu;

static void
virtio_net_dbg_queue_state(struct virtio_net_softc *sc, const char *tag)
{
    struct virtqueue *rxq;
    struct virtqueue *txq;

    if (!sc || !tag)
        return;
    rxq = sc->vdev.vqs[VIRTIO_NET_Q_RX];
    txq = sc->vdev.vqs[VIRTIO_NET_Q_TX];
    VNETDBG("virtio_net[%s]: tx_pending=%d if_flags=0x%x vdev_status=0x%x feats=0x%x\n",
            tag, sc->tx_pending, sc->ifp.if_flags, sc->vdev.status,
            (uint)sc->vdev.features);
    if (rxq) {
        VNETDBG("virtio_net[%s]: rxq size=%u free=%u avail_idx=%u used_idx=%u last_used=%u\n",
                tag, rxq->size, rxq->num_free, rxq->avail->idx,
                rxq->used->idx, rxq->last_used_idx);
    }
    if (txq) {
        VNETDBG("virtio_net[%s]: txq size=%u free=%u avail_idx=%u used_idx=%u last_used=%u\n",
                tag, txq->size, txq->num_free, txq->avail->idx,
                txq->used->idx, txq->last_used_idx);
    }
}

static int
virtio_net_dbg_poll_changed(struct virtio_net_softc *sc)
{
    struct virtqueue *rxq;
    struct virtqueue *txq;

    rxq = sc->vdev.vqs[VIRTIO_NET_Q_RX];
    txq = sc->vdev.vqs[VIRTIO_NET_Q_TX];
    if (!rxq || !txq)
        return 1;
    if (sc->dbg_last_tx_pending != sc->tx_pending ||
        sc->dbg_last_rx_used != rxq->used->idx ||
        sc->dbg_last_rx_avail != rxq->avail->idx ||
        sc->dbg_last_tx_used != txq->used->idx ||
        sc->dbg_last_tx_avail != txq->avail->idx) {
        sc->dbg_last_tx_pending = sc->tx_pending;
        sc->dbg_last_rx_used = rxq->used->idx;
        sc->dbg_last_rx_avail = rxq->avail->idx;
        sc->dbg_last_tx_used = txq->used->idx;
        sc->dbg_last_tx_avail = txq->avail->idx;
        return 1;
    }
    return 0;
}

static void
virtio_net_dbg_log_rx_packet(char *pkt, uint pkt_len)
{
    struct virtio_net_eth_hdr *eh;
    struct ip_hdr *ip;
    struct virtio_net_udp_hdr *uh;
    uchar *udp_payload;
    uint ip_hlen;
    uint udp_len;
    uint xid;
    int msg_type;
    uint off;

    if (pkt_len < sizeof(*eh))
        return;
    eh = (struct virtio_net_eth_hdr *)pkt;
    VNETDBG("virtio_net: rx frame len=%u eth=0x%x src=%x:%x:%x:%x:%x:%x dst=%x:%x:%x:%x:%x:%x\n",
            pkt_len, net_ntohs(eh->type),
            eh->src[0], eh->src[1], eh->src[2], eh->src[3], eh->src[4], eh->src[5],
            eh->dst[0], eh->dst[1], eh->dst[2], eh->dst[3], eh->dst[4], eh->dst[5]);

    if (net_ntohs(eh->type) != NET_PROTO_IP)
        return;
    if (pkt_len < sizeof(*eh) + sizeof(struct ip_hdr))
        return;

    ip = (struct ip_hdr *)(pkt + sizeof(*eh));
    ip_hlen = (uint)(ip->vhl & 0x0F) * 4;
    if (ip_hlen < sizeof(struct ip_hdr) || pkt_len < sizeof(*eh) + ip_hlen)
        return;

    VNETDBG("virtio_net: rx ip proto=%d src=0x%x dst=0x%x\n",
            ip->proto, net_ntohl(ip->src), net_ntohl(ip->dst));

    if (ip->proto != NET_IP_UDP)
        return;
    if (pkt_len < sizeof(*eh) + ip_hlen + sizeof(*uh))
        return;

    uh = (struct virtio_net_udp_hdr *)(pkt + sizeof(*eh) + ip_hlen);
    VNETDBG("virtio_net: rx udp sport=%u dport=%u len=%u\n",
            net_ntohs(uh->src_port), net_ntohs(uh->dst_port), net_ntohs(uh->len));

    udp_payload = (uchar *)uh + sizeof(*uh);
    udp_len = net_ntohs(uh->len);
    if (udp_len < sizeof(*uh) + 240)
        return;

    xid = ((uint)udp_payload[4] << 24) |
          ((uint)udp_payload[5] << 16) |
          ((uint)udp_payload[6] << 8) |
          (uint)udp_payload[7];
    msg_type = -1;
    off = 240;
    while (off + 1 < udp_len - sizeof(*uh)) {
        uchar opt = udp_payload[off];
        uchar optlen;

        if (opt == 0) {
            off++;
            continue;
        }
        if (opt == 255)
            break;
        if (off + 2 >= udp_len - sizeof(*uh))
            break;
        optlen = udp_payload[off + 1];
        if (off + 2 + optlen > udp_len - sizeof(*uh))
            break;
        if (opt == 53 && optlen == 1)
            msg_type = udp_payload[off + 2];
        off += 2 + optlen;
    }

    VNETDBG("virtio_net: rx dhcp op=%d xid=0x%x msg_type=%d\n",
            udp_payload[0], xid, msg_type);
}

static void
virtio_net_irq_handler(int irq, void *arg)
{
    struct virtio_net_softc *sc;

    (void)irq;
    sc = arg;
    if (sc) {
        VNETDBG("virtio_net: irq=%d dispatch if=%s pending=%d\n",
                sc->vdev.irq, sc->ifp.if_xname, sc->tx_pending);
        virtio_handle_interrupt(&sc->vdev);
    }
}

/*
 * Initialize RX queue with buffers
 */
static void
virtio_net_fill_rx(struct virtio_net_softc *sc)
{
    struct virtqueue *vq = sc->vdev.vqs[VIRTIO_NET_Q_RX];
    uint16_t free_before;
    int added;
    int i;

    if (!vq)
        return;
    free_before = vq->num_free;
    added = 0;
    
    for (i = 0; i < VIRTIO_NET_RX_BUFS; i++) {
        if (sc->rx_bufs[i].in_use)
            continue;
        
        if (virtq_num_free(vq) < 1)
            break;
        
        void *bufs[1] = { sc->rx_bufs[i].data };
        uint32_t lens[1] = { sizeof(sc->rx_bufs[i].data) };
        
        if (virtq_add_buf(vq, bufs, lens, 0, 1, &sc->rx_bufs[i]) == 0) {
            sc->rx_bufs[i].in_use = 1;
            added++;
        }
    }
    
    virtq_kick(vq);
    if (added > 0 || free_before < 8) {
        VNETDBG("virtio_net: refill rx added=%d free_before=%u free_after=%u avail_idx=%u used_idx=%u\n",
                added, free_before, vq->num_free, vq->avail->idx, vq->used->idx);
    }
}

/*
 * ifnet output function
 */
static int
virtio_net_output(struct ifnet *ifp, struct mbuf *m)
{
    struct virtio_net_softc *sc = (struct virtio_net_softc *)ifp->if_softc;
    struct virtqueue *vq = sc->vdev.vqs[VIRTIO_NET_Q_TX];
    struct virtio_net_tx *tx;
    void *bufs[2];
    uint32_t lens[2];
    int i;
    
    if (!vq || m == 0)
        return -1;
    tx = 0;
    
    acquire(&sc->lock);

    for (i = 0; i < VIRTQ_SIZE_DEFAULT; i++) {
        if (sc->tx_slots[i].in_use == 0) {
            tx = &sc->tx_slots[i];
            memset(tx, 0, sizeof(*tx));
            tx->in_use = 1;
            tx->m = m;
            break;
        }
    }
    if (tx == 0) {
        VNETDBG("virtio_net: tx drop no free tx slot len=%d\n", m ? m->len : -1);
        release(&sc->lock);
        return -1;
    }
    
    if (virtq_num_free(vq) < 2) {
        VNETDBG("virtio_net: tx stall no vq desc free=%u len=%d\n", vq->num_free, m->len);
        tx->in_use = 0;
        tx->m = 0;
        release(&sc->lock);
        return -1;
    }
    
    bufs[0] = &tx->hdr;
    bufs[1] = m->data;
    lens[0] = sizeof(tx->hdr);
    lens[1] = m->len;
    
    if (virtq_add_buf(vq, bufs, lens, 2, 0, tx) < 0) {
        VNETDBG("virtio_net: tx add_buf failed len=%d free=%u\n", m->len, vq->num_free);
        tx->in_use = 0;
        tx->m = 0;
        release(&sc->lock);
        return -1;
    }
    virtq_kick(vq);
    
    sc->tx_pending++;
        VNETDBG("virtio_net: tx enq len=%d pending=%d free=%u avail_idx=%u used_idx=%u\n",
            m->len, sc->tx_pending, vq->num_free, vq->avail->idx, vq->used->idx);
    
    release(&sc->lock);
    
    return 0;
}

/*
 * Process completed TX
 */
static void
virtio_net_tx_complete(struct virtio_net_softc *sc)
{
    struct virtqueue *vq = sc->vdev.vqs[VIRTIO_NET_Q_TX];
    uint32_t len;
    int completed;
    struct virtio_net_tx *tx;

    completed = 0;
    
    while ((tx = virtq_get_buf(vq, &len)) != 0) {
        (void)len;
        if (sc->tx_pending > 0)
            sc->tx_pending--;
        if (tx->m)
            mbuf_free(tx->m);
        tx->m = 0;
        tx->in_use = 0;
        completed++;
    }
    if (completed > 0) {
        VNETDBG("virtio_net: tx complete count=%d pending=%d free=%u used_idx=%u last_used=%u\n",
                completed, sc->tx_pending, vq->num_free, vq->used->idx, vq->last_used_idx);
    }
}

/*
 * Process received packets
 */
static void
virtio_net_rx_complete(struct virtio_net_softc *sc)
{
    struct virtqueue *vq = sc->vdev.vqs[VIRTIO_NET_Q_RX];
    uint32_t len;
    int delivered;
    int dropped;
    struct mbuf *m;
    struct virtio_net_rxbuf *slot;

    delivered = 0;
    dropped = 0;
    
    for (;;) {
        acquire(&sc->lock);
        slot = virtq_get_buf(vq, &len);
        if (slot == 0) {
            virtio_net_fill_rx(sc);
            release(&sc->lock);
            break;
        }
        release(&sc->lock);

        m = 0;
        if (len > sizeof(struct virtio_net_hdr)) {
            uint pkt_len = len - sizeof(struct virtio_net_hdr);

            if (pkt_len <= MBUF_SIZE) {
                m = mbuf_alloc();
                if (m) {
                    memmove(m->data, slot->data + sizeof(struct virtio_net_hdr), pkt_len);
                    m->len = pkt_len;
                    m->rcvif = &sc->ifp;
                    virtio_net_dbg_log_rx_packet(m->data, pkt_len);
                } else {
                    dropped++;
                    VNETDBG("virtio_net: rx drop mbuf_alloc failed pkt_len=%u\n", pkt_len);
                }
            } else {
                dropped++;
                VNETDBG("virtio_net: rx drop oversize pkt_len=%u max=%u\n", pkt_len, MBUF_SIZE);
            }
        } else {
            dropped++;
            VNETDBG("virtio_net: rx drop short frame len=%u hdr=%u\n",
                    len, (uint)sizeof(struct virtio_net_hdr));
        }

        acquire(&sc->lock);
        slot->in_use = 0;
        virtio_net_fill_rx(sc);
        release(&sc->lock);

        if (m)
            if_input(&sc->ifp, m);
        if (m)
            delivered++;
        }

        if (delivered > 0 || dropped > 0) {
        VNETDBG("virtio_net: rx done delivered=%d dropped=%d free=%u used_idx=%u last_used=%u\n",
            delivered, dropped, vq->num_free, vq->used->idx, vq->last_used_idx);
    }
}

/*
 * Interrupt handler
 */
static void
virtio_net_intr(struct virtio_dev *vdev)
{
    struct virtio_net_softc *sc = vdev->driver_data;

    VNETDBG("virtio_net: intr begin if=%s status=0x%x\n", sc->ifp.if_xname, vdev->status);
    
    acquire(&sc->lock);
    virtio_net_tx_complete(sc);
    release(&sc->lock);

    virtio_net_rx_complete(sc);
    virtio_net_dbg_queue_state(sc, "intr");
}

/*
 * Poll fallback for hosts where virtio IRQ delivery is delayed or missed.
 */
static void
virtio_net_poll(struct ifnet *ifp)
{
    struct virtio_net_softc *sc;

    if (!ifp)
        return;
    sc = (struct virtio_net_softc *)ifp->if_softc;
    if (!sc)
        return;

    if (!virtio_net_dbg_poll_changed(sc))
        return;

    VNETDBG("virtio_net: poll if=%s pending=%d\n", ifp->if_xname, sc->tx_pending);

    acquire(&sc->lock);
    virtio_net_tx_complete(sc);
    release(&sc->lock);

    virtio_net_rx_complete(sc);
    virtio_net_dbg_queue_state(sc, "poll");
}

/*
 * Read MAC address from config space
 */
static void
virtio_net_read_mac(struct virtio_net_softc *sc)
{
    int i;

    for (i = 0; i < 6; i++) {
        sc->mac[i] = virtio_config_read8(&sc->vdev, i);
    }
    
    BOOTDBG("virtio_net: MAC %x:%x:%x:%x:%x:%x\n",
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
}

/*
 * PCI probe function
 */
int
virtio_net_probe(struct pci_dev *pci)
{
    struct virtio_net_softc *sc;
    struct virtqueue *rxq;
    struct virtqueue *txq;
    ushort link_status;

    if (virtio_net_count >= MAX_VIRTIO_NET)
        return -1;

    VNETDBG("virtio_net: probe bdf=%d:%d.%d vid=%x did=%x\n",
            pci->bus, pci->slot, pci->func, pci->vendor_id, pci->device_id);
    
    sc = &virtio_net_devices[virtio_net_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "virtio_net");
    sc->dbg_last_tx_pending = -1;
    
    /* Initialize virtio device */
    if (virtio_probe_pci(pci, &sc->vdev) < 0) {
        VNETDBG("virtio_net: virtio_probe_pci failed bdf=%d:%d.%d\n",
                pci->bus, pci->slot, pci->func);
        return -1;
    }
    
    if (sc->vdev.device_id != VIRTIO_DEV_NET) {
        virtio_reset(&sc->vdev);
        return -1;
    }
    
    virtio_set_status(&sc->vdev, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);
    
    uint64_t features = VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS | VIRTIO_NET_F_MTU;
    if (virtio_negotiate_features(&sc->vdev, features) < 0) {
        VNETDBG("virtio_net: feature negotiation failed req=0x%x offered=0x%x\n",
                (uint)features, (uint)sc->vdev.features);
        virtio_reset(&sc->vdev);
        return -1;
    }
    VNETDBG("virtio_net: negotiated features=0x%x req=0x%x\n",
            (uint)sc->vdev.features, (uint)features);

    if ((sc->vdev.features & VIRTIO_NET_F_MAC) == 0) {
        VNETDBG("virtio_net: missing required MAC feature\n");
        virtio_reset(&sc->vdev);
        return -1;
    }
    
    if (virtio_finalize_features(&sc->vdev) < 0) {
        VNETDBG("virtio_net: finalize_features failed\n");
        virtio_reset(&sc->vdev);
        return -1;
    }
    
    virtio_net_read_mac(sc);
    
    rxq = virtq_create(&sc->vdev, VIRTIO_NET_Q_RX, 0);
    txq = virtq_create(&sc->vdev, VIRTIO_NET_Q_TX, 0);
    
    if (!rxq || !txq) {
        VNETDBG("virtio_net: queue create failed rxq=%p txq=%p\n", rxq, txq);
        if (rxq)
            virtq_destroy(rxq);
        if (txq)
            virtq_destroy(txq);
        virtio_reset(&sc->vdev);
        return -1;
    }
    
    sc->vdev.driver_data = sc;
    sc->vdev.isr_handler = virtio_net_intr;

    if (irq_register(sc->vdev.irq, virtio_net_irq_handler, sc, "virtio_net") < 0) {
        VNETDBG("virtio_net: irq register failed irq=%d\n", sc->vdev.irq);
        virtq_destroy(rxq);
        virtq_destroy(txq);
        virtio_reset(&sc->vdev);
        return -1;
    }

    VNETDBG("virtio_net: irq registered irq=%d enabling ioapic cpu=%d\n",
            sc->vdev.irq, ncpu - 1);
    
    pci_enable_irq(pci, ncpu - 1);
    virtio_net_fill_rx(sc);

    memset(&sc->ifp, 0, sizeof(sc->ifp));
    safestrcpy(sc->ifp.if_xname, "vtnet0", sizeof(sc->ifp.if_xname));
    sc->ifp.if_xname[5] = '0' + virtio_net_count;
    sc->ifp.if_xname[6] = '\0';
    sc->ifp.if_mtu = 1500;
    if (sc->vdev.features & VIRTIO_NET_F_MTU) {
        ushort mtu = virtio_config_read16(&sc->vdev, 10);
        if (mtu > 0)
            sc->ifp.if_mtu = mtu;
    }
    sc->ifp.if_flags = IFF_UP | IFF_BROADCAST;
    link_status = VIRTIO_NET_S_LINK_UP;
    if (sc->vdev.features & VIRTIO_NET_F_STATUS)
        link_status = virtio_config_read16(&sc->vdev, 6);
    if (link_status & VIRTIO_NET_S_LINK_UP)
        sc->ifp.if_flags |= IFF_RUNNING;
    VNETDBG("virtio_net: link_status=0x%x if_flags=0x%x mtu=%u\n",
            link_status, sc->ifp.if_flags, sc->ifp.if_mtu);
    memmove(sc->ifp.if_hwaddr, sc->mac, sizeof(sc->ifp.if_hwaddr));
    sc->ifp.if_softc = sc;
    sc->ifp.if_input = ether_input;
    sc->ifp.if_ops = &virtio_net_ops;

    if (if_register(&sc->ifp) < 0) {
        VNETDBG("virtio_net: if_register failed name=%s irq=%d\n",
                sc->ifp.if_xname, sc->vdev.irq);
        irq_unregister(sc->vdev.irq, "virtio_net");
        virtq_destroy(rxq);
        virtq_destroy(txq);
        virtio_reset(&sc->vdev);
        return -1;
    }

    virtio_set_status(&sc->vdev, VIRTIO_STATUS_DRIVER_OK);
    virtio_net_dbg_queue_state(sc, "attach");
    
    virtio_net_count++;
    cprintf("virtio_net: attached %s irq=%d\n", sc->ifp.if_xname, sc->vdev.irq);
    
    return 0;
}

/*
 * Module init - register with PCI
 */
void
virtio_net_init(void)
{
    int found;

#if DBG_VIRTIO_NET
    cprintf("virtio_net: DBG_VIRTIO_NET=1 verbose tracing enabled\n");
#endif

    BOOTDBG("virtio_net: initializing driver\n");
    found = 0;

    /* Look for virtio-net PCI devices */
    for (int i = 0; i < pci_device_count(); i++) {
        struct pci_dev *dev = pci_get_device(i);
        if (!dev)
            continue;
        
        if (dev->vendor_id == PCI_VENDOR_VIRTIO &&
            (dev->device_id == PCI_DEVICE_VIRTIO_NET ||
             (dev->device_id >= 0x1000 && dev->device_id <= 0x103F &&
              dev->device_id - 0x0FFF == VIRTIO_DEV_NET))) {
            found = 1;
            if (virtio_net_probe(dev) < 0) {
                cprintf("virtio_net: probe failed at %d:%d.%d id=%x:%x\n",
                        dev->bus, dev->slot, dev->func,
                        dev->vendor_id, dev->device_id);
            }
        }
    }

    if (!found)
        cprintf("virtio_net: no compatible PCI device\n");
}
