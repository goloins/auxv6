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

static int virtio_net_output(struct ifnet *ifp, struct mbuf *m);

static struct ifnet_ops virtio_net_ops = {
    .if_output = virtio_net_output,
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
};

/* Global array of virtio-net devices */
#define MAX_VIRTIO_NET 4
static struct virtio_net_softc virtio_net_devices[MAX_VIRTIO_NET];
static int virtio_net_count = 0;
extern int ncpu;

static void
virtio_net_irq_handler(int irq, void *arg)
{
    struct virtio_net_softc *sc;

    (void)irq;
    sc = arg;
    if (sc)
        virtio_handle_interrupt(&sc->vdev);
}

/*
 * Initialize RX queue with buffers
 */
static void
virtio_net_fill_rx(struct virtio_net_softc *sc)
{
    struct virtqueue *vq = sc->vdev.vqs[VIRTIO_NET_Q_RX];
    int i;

    if (!vq)
        return;
    
    for (i = 0; i < VIRTIO_NET_RX_BUFS; i++) {
        if (sc->rx_bufs[i].in_use)
            continue;
        
        if (virtq_num_free(vq) < 1)
            break;
        
        void *bufs[1] = { sc->rx_bufs[i].data };
        uint32_t lens[1] = { sizeof(sc->rx_bufs[i].data) };
        
        if (virtq_add_buf(vq, bufs, lens, 0, 1, &sc->rx_bufs[i]) == 0) {
            sc->rx_bufs[i].in_use = 1;
        }
    }
    
    virtq_kick(vq);
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
        release(&sc->lock);
        return -1;
    }
    
    if (virtq_num_free(vq) < 2) {
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
        tx->in_use = 0;
        tx->m = 0;
        release(&sc->lock);
        return -1;
    }
    virtq_kick(vq);
    
    sc->tx_pending++;
    
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
    struct virtio_net_tx *tx;
    
    while ((tx = virtq_get_buf(vq, &len)) != 0) {
        (void)len;
        if (sc->tx_pending > 0)
            sc->tx_pending--;
        if (tx->m)
            mbuf_free(tx->m);
        tx->m = 0;
        tx->in_use = 0;
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
    struct mbuf *m;
    struct virtio_net_rxbuf *slot;
    
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
                }
            }
        }

        acquire(&sc->lock);
        slot->in_use = 0;
        virtio_net_fill_rx(sc);
        release(&sc->lock);

        if (m)
            if_input(&sc->ifp, m);
    }
}

/*
 * Interrupt handler
 */
static void
virtio_net_intr(struct virtio_dev *vdev)
{
    struct virtio_net_softc *sc = vdev->driver_data;
    
    acquire(&sc->lock);
    virtio_net_tx_complete(sc);
    release(&sc->lock);

    virtio_net_rx_complete(sc);
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
    
    sc = &virtio_net_devices[virtio_net_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "virtio_net");
    
    /* Initialize virtio device */
    if (virtio_probe_pci(pci, &sc->vdev) < 0)
        return -1;
    
    if (sc->vdev.device_id != VIRTIO_DEV_NET) {
        virtio_reset(&sc->vdev);
        return -1;
    }
    
    virtio_set_status(&sc->vdev, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);
    
    uint64_t features = VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS | VIRTIO_NET_F_MTU;
    virtio_negotiate_features(&sc->vdev, features);

    if ((sc->vdev.features & VIRTIO_NET_F_MAC) == 0) {
        virtio_reset(&sc->vdev);
        return -1;
    }
    
    if (virtio_finalize_features(&sc->vdev) < 0) {
        virtio_reset(&sc->vdev);
        return -1;
    }
    
    virtio_net_read_mac(sc);
    
    rxq = virtq_create(&sc->vdev, VIRTIO_NET_Q_RX, 0);
    txq = virtq_create(&sc->vdev, VIRTIO_NET_Q_TX, 0);
    
    if (!rxq || !txq) {
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
        virtq_destroy(rxq);
        virtq_destroy(txq);
        virtio_reset(&sc->vdev);
        return -1;
    }
    
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
    memmove(sc->ifp.if_hwaddr, sc->mac, sizeof(sc->ifp.if_hwaddr));
    sc->ifp.if_softc = sc;
    sc->ifp.if_input = ether_input;
    sc->ifp.if_ops = &virtio_net_ops;

    if (if_register(&sc->ifp) < 0) {
        irq_unregister(sc->vdev.irq, "virtio_net");
        virtq_destroy(rxq);
        virtq_destroy(txq);
        virtio_reset(&sc->vdev);
        return -1;
    }

    virtio_set_status(&sc->vdev, VIRTIO_STATUS_DRIVER_OK);
    
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
              dev->device_id - 0x1000 == VIRTIO_DEV_NET))) {
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
