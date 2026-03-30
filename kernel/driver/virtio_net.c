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

/* Driver-specific data */
struct virtio_net_softc {
    struct virtio_dev  vdev;
    struct spinlock    lock;
    struct ifnet      *ifp;
    
    /* MAC address */
    uint8_t mac[6];
    
    /* RX buffer pool */
    #define VIRTIO_NET_RX_BUFS 64
    struct {
        char data[2048];
        int  in_use;
    } rx_bufs[VIRTIO_NET_RX_BUFS];
    
    /* TX tracking */
    int tx_pending;
};

/* Global array of virtio-net devices */
#define MAX_VIRTIO_NET 4
static struct virtio_net_softc virtio_net_devices[MAX_VIRTIO_NET];
static int virtio_net_count = 0;

/*
 * Initialize RX queue with buffers
 */
static void
virtio_net_fill_rx(struct virtio_net_softc *sc)
{
    struct virtqueue *vq = sc->vdev.vqs[VIRTIO_NET_Q_RX];
    if (!vq)
        return;
    
    for (int i = 0; i < VIRTIO_NET_RX_BUFS; i++) {
        if (sc->rx_bufs[i].in_use)
            continue;
        
        if (vq->num_free < 1)
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
    
    if (!vq)
        return -1;
    
    acquire(&sc->lock);
    
    if (vq->num_free < 2) {
        /* No room, drop packet */
        release(&sc->lock);
        /* TODO: Free mbuf */
        return -1;
    }
    
    /* Prepend virtio net header */
    static struct virtio_net_hdr hdr;  /* TODO: per-packet allocation */
    memset(&hdr, 0, sizeof(hdr));
    
    void *bufs[2] = { &hdr, m->data };
    uint32_t lens[2] = { sizeof(hdr), m->len };
    
    virtq_add_buf(vq, bufs, lens, 2, 0, m);
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
    struct mbuf *m;
    
    while ((m = virtq_get_buf(vq, &len)) != 0) {
        sc->tx_pending--;
        /* TODO: Free mbuf */
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
    void *buf;
    
    while ((buf = virtq_get_buf(vq, &len)) != 0) {
        /* buf points to our rx_bufs entry */
        int idx = ((char *)buf - (char *)sc->rx_bufs[0].data) / sizeof(sc->rx_bufs[0]);
        
        if (idx >= 0 && idx < VIRTIO_NET_RX_BUFS && len > sizeof(struct virtio_net_hdr)) {
            /* Skip virtio net header */
            char *pkt = sc->rx_bufs[idx].data + sizeof(struct virtio_net_hdr);
            int pkt_len = len - sizeof(struct virtio_net_hdr);
            
            /* TODO: Create mbuf and call if_input on ifp */
            /* For now, just log */
            cprintf("virtio_net: rx %d bytes\n", pkt_len);
        }
        
        sc->rx_bufs[idx].in_use = 0;
    }
    
    /* Refill RX queue */
    virtio_net_fill_rx(sc);
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
    virtio_net_rx_complete(sc);
    
    release(&sc->lock);
}

/*
 * Read MAC address from config space
 */
static void
virtio_net_read_mac(struct virtio_net_softc *sc)
{
    for (int i = 0; i < 6; i++) {
        sc->mac[i] = virtio_config_read8(&sc->vdev, i);
    }
    
    cprintf("virtio_net: MAC %x:%x:%x:%x:%x:%x\n",
            sc->mac[0], sc->mac[1], sc->mac[2],
            sc->mac[3], sc->mac[4], sc->mac[5]);
}

/*
 * PCI probe function
 */
int
virtio_net_probe(struct pci_dev *pci)
{
    if (virtio_net_count >= MAX_VIRTIO_NET)
        return -1;
    
    struct virtio_net_softc *sc = &virtio_net_devices[virtio_net_count];
    memset(sc, 0, sizeof(*sc));
    initlock(&sc->lock, "virtio_net");
    
    /* Initialize virtio device */
    if (virtio_probe_pci(pci, &sc->vdev) < 0)
        return -1;
    
    if (sc->vdev.device_id != VIRTIO_DEV_NET) {
        virtio_reset(&sc->vdev);
        return -1;
    }
    
    /* Set ACKNOWLEDGE and DRIVER status */
    virtio_set_status(&sc->vdev, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);
    
    /* Negotiate features */
    uint64_t features = VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS;
    virtio_negotiate_features(&sc->vdev, features);
    
    if (virtio_finalize_features(&sc->vdev) < 0) {
        virtio_reset(&sc->vdev);
        return -1;
    }
    
    /* Read MAC address */
    virtio_net_read_mac(sc);
    
    /* Create virtqueues */
    struct virtqueue *rxq = virtq_create(&sc->vdev, VIRTIO_NET_Q_RX, 0);
    struct virtqueue *txq = virtq_create(&sc->vdev, VIRTIO_NET_Q_TX, 0);
    
    if (!rxq || !txq) {
        virtio_reset(&sc->vdev);
        return -1;
    }
    
    /* Set driver_data for interrupt handler */
    sc->vdev.driver_data = sc;
    sc->vdev.isr_handler = virtio_net_intr;
    
    /* Enable interrupts */
    pci_enable_irq(pci, ncpu - 1);
    
    /* Fill RX queue with buffers */
    virtio_net_fill_rx(sc);
    
    /* Mark device ready */
    virtio_set_status(&sc->vdev, VIRTIO_STATUS_DRIVER_OK);
    
    /* Register with network stack */
    /* TODO: Create ifnet and register */
    /* sc->ifp = ... */
    /* sc->ifp->if_output = virtio_net_output; */
    /* if_register(sc->ifp); */
    
    virtio_net_count++;
    cprintf("virtio_net: attached device %d\n", virtio_net_count - 1);
    
    return 0;
}

/*
 * Module init - register with PCI
 */
void
virtio_net_init(void)
{
    cprintf("virtio_net: initializing driver\n");
    
    /* Look for virtio-net PCI devices */
    for (int i = 0; i < pci_device_count(); i++) {
        struct pci_dev *dev = pci_get_device(i);
        if (!dev)
            continue;
        
        if (dev->vendor_id == PCI_VENDOR_VIRTIO &&
            (dev->device_id == PCI_DEVICE_VIRTIO_NET ||
             (dev->device_id >= 0x1000 && dev->device_id <= 0x103F &&
              dev->device_id - 0x1000 == VIRTIO_DEV_NET))) {
            virtio_net_probe(dev);
        }
    }
}
