#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "blockdev.h"
#include "fcntl.h"

#define USB_MSC_MAX_DEV         8
#define USB_MSC_MAX_LUN         0
#define USB_MSC_SIG_CBW         0x43425355U
#define USB_MSC_SIG_CSW         0x53425355U
#define USB_MSC_CBW_LEN         31
#define USB_MSC_CSW_LEN         13
#define USB_MSC_CSW_STATUS_OK   0

struct usb_msc_cbw {
  uint sig;
  uint tag;
  uint xfer_len;
  uchar flags;
  uchar lun;
  uchar cdb_len;
  uchar cdb[16];
};

struct usb_msc_csw {
  uint sig;
  uint tag;
  uint residue;
  uchar status;
};

struct usb_msc_dev {
  uchar active;
  uchar online;
  uchar bdev_registered;
  uchar bdev_pending;
  uchar unit;
  uint bind_id;
  uint dev_id;
  ushort vendor_id;
  ushort product_id;
  uchar ifnum;
  uchar ifalt;
  uchar bulk_in_ep;
  uchar bulk_out_ep;
  uchar dev_speed;
  uint block_count;
  uint block_size;
  uint tag_next;
  uint probe_attempts;
  uint probe_successes;
  uint probe_failures;
  uint rw_ok;
  uint rw_fail;
  int last_rc;
  struct sleeplock io_lock;
};

static struct spinlock usb_msc_lock;
static int usb_msc_lock_ready;
static struct usb_msc_dev usb_msc_devtab[USB_MSC_MAX_DEV];
static uint usb_msc_dev_count;
static uint usb_msc_next_bind_id;
static uint usb_msc_runtime_pulses;

static int usb_msc_buf_putc(char *buf, uint max, uint *len, char c);
static int usb_msc_buf_puts(char *buf, uint max, uint *len, const char *s);
static int usb_msc_buf_putu(char *buf, uint max, uint *len, uint v);
static int usb_msc_buf_puthex16(char *buf, uint max, uint *len, ushort v);
static int usb_msc_buf_puthex32(char *buf, uint max, uint *len, uint v);
static uint usb_msc_be32(const uchar *p);
static int usb_msc_bulk_out(struct usb_msc_dev *sc, uchar *buf, ushort len);
static int usb_msc_bulk_in(struct usb_msc_dev *sc, uchar *buf, ushort len,
                           ushort *done_len, int allow_short);
static int usb_msc_bot_xfer(struct usb_msc_dev *sc,
                            const uchar *cdb, uchar cdb_len,
                            uchar *data, ushort data_len,
                            int dir_in, int allow_data_short);
static int usb_msc_scsi_tur(struct usb_msc_dev *sc);
static int usb_msc_scsi_inquiry(struct usb_msc_dev *sc, uchar *buf, ushort len);
static int usb_msc_scsi_read_capacity10(struct usb_msc_dev *sc,
                                        uint *last_lba, uint *block_size);
static int usb_msc_scsi_read10(struct usb_msc_dev *sc, uint lba, uchar *buf, ushort len);
static int usb_msc_scsi_write10(struct usb_msc_dev *sc, uint lba, uchar *buf, ushort len);
static int usb_msc_probe_media_locked(struct usb_msc_dev *sc);
static struct usb_msc_dev *usb_msc_find_by_bind_locked(uint bind_handle);
static struct usb_msc_dev *usb_msc_find_by_dev_locked(uint dev);
static int usb_msc_alloc_unit_locked(void);
static int usb_msc_register_bdev_locked(struct usb_msc_dev *sc);
static int usb_msc_rw(struct buf *b);
static uint usb_msc_nblocks(uint dev);

static const struct bdevsw usb_msc_bdevsw = {
  .rw = usb_msc_rw,
  .nblocks = usb_msc_nblocks,
  .name = "usb_msc",
};

static void
usb_msc_ensure_lock(void)
{
  if(!usb_msc_lock_ready){
    initlock(&usb_msc_lock, "usb_msc");
    lockdep_set_rank(&usb_msc_lock, LOCK_RANK_DEFAULT, "usb_msc");
    usb_msc_lock_ready = 1;
  }
}

static int
usb_msc_buf_putc(char *buf, uint max, uint *len, char c)
{
  if(*len >= max)
    return -1;
  buf[(*len)++] = c;
  return 0;
}

static int
usb_msc_buf_puts(char *buf, uint max, uint *len, const char *s)
{
  if(!s)
    return 0;
  while(*s){
    if(usb_msc_buf_putc(buf, max, len, *s++) < 0)
      return -1;
  }
  return 0;
}

static int
usb_msc_buf_putu(char *buf, uint max, uint *len, uint v)
{
  char tmp[12];
  uint n;

  n = 0;
  do {
    tmp[n++] = '0' + (v % 10);
    v /= 10;
  } while(v);

  while(n--){
    if(usb_msc_buf_putc(buf, max, len, tmp[n]) < 0)
      return -1;
  }
  return 0;
}

static int
usb_msc_buf_puthex16(char *buf, uint max, uint *len, ushort v)
{
  static const char hex[] = "0123456789abcdef";
  int shift;

  for(shift = 12; shift >= 0; shift -= 4){
    if(usb_msc_buf_putc(buf, max, len, hex[(v >> shift) & 0xf]) < 0)
      return -1;
  }
  return 0;
}

static int
usb_msc_buf_puthex32(char *buf, uint max, uint *len, uint v)
{
  if(usb_msc_buf_puthex16(buf, max, len, (ushort)(v >> 16)) < 0)
    return -1;
  return usb_msc_buf_puthex16(buf, max, len, (ushort)(v & 0xffff));
}

static uint
usb_msc_be32(const uchar *p)
{
  if(!p)
    return 0;
  return ((uint)p[0] << 24) |
         ((uint)p[1] << 16) |
         ((uint)p[2] << 8) |
         (uint)p[3];
}

static struct usb_msc_dev *
usb_msc_find_by_bind_locked(uint bind_handle)
{
  uint i;

  if(bind_handle == 0)
    return 0;

  for(i = 0; i < usb_msc_dev_count; i++){
    struct usb_msc_dev *sc;

    sc = &usb_msc_devtab[i];
    if(!sc->active)
      continue;
    if(sc->bind_id == bind_handle)
      return sc;
  }
  return 0;
}

static struct usb_msc_dev *
usb_msc_find_by_dev_locked(uint dev)
{
  uint i;

  for(i = 0; i < usb_msc_dev_count; i++){
    struct usb_msc_dev *sc;

    sc = &usb_msc_devtab[i];
    if(!sc->active)
      continue;
    if(sc->dev_id == dev)
      return sc;
  }
  return 0;
}

static int
usb_msc_alloc_unit_locked(void)
{
  int used[USB_DISK_UNITS];
  uint i;
  int unit;

  for(unit = 0; unit < USB_DISK_UNITS; unit++)
    used[unit] = 0;

  for(i = 0; i < usb_msc_dev_count; i++){
    struct usb_msc_dev *sc;

    sc = &usb_msc_devtab[i];
    if(!sc->active)
      continue;
    if(sc->unit < USB_DISK_UNITS)
      used[sc->unit] = 1;
  }

  for(unit = 0; unit < USB_DISK_UNITS; unit++){
    if(!used[unit])
      return unit;
  }

  return -1;
}

static int
usb_msc_bulk_out(struct usb_msc_dev *sc, uchar *buf, ushort len)
{
  ushort done_len;
  int rc;

  if(!sc || !buf || len == 0)
    return -1;

  done_len = 0;
  rc = usb_driver_bulk_probe_xfer(sc->bind_id,
                                  0, sc->bulk_out_ep,
                                  buf, len, &done_len);
  if(rc < 0)
    return rc;
  if(done_len != len)
    return -1;
  return 0;
}

static int
usb_msc_bulk_in(struct usb_msc_dev *sc, uchar *buf, ushort len,
                ushort *done_len, int allow_short)
{
  ushort got;
  int rc;

  if(done_len)
    *done_len = 0;
  if(!sc || !buf || len == 0)
    return -1;

  got = 0;
  rc = usb_driver_bulk_probe_xfer(sc->bind_id,
                                  sc->bulk_in_ep, 0,
                                  buf, len, &got);
  if(done_len)
    *done_len = got;
  if(rc == 0)
    return 0;
  if(rc == -3 && allow_short)
    return 0;
  return rc;
}

static int
usb_msc_bot_xfer(struct usb_msc_dev *sc,
                 const uchar *cdb, uchar cdb_len,
                 uchar *data, ushort data_len,
                 int dir_in, int allow_data_short)
{
  struct usb_msc_cbw cbw;
  struct usb_msc_csw csw;
  ushort done_len;
  uint tag;
  int rc;

  if(!sc || !cdb)
    return -1;
  if(cdb_len == 0 || cdb_len > sizeof(cbw.cdb))
    return -1;
  if(data_len > 0 && !data)
    return -1;

  memset(&cbw, 0, sizeof(cbw));
  memset(&csw, 0, sizeof(csw));

  tag = ++sc->tag_next;
  if(tag == 0)
    tag = ++sc->tag_next;

  cbw.sig = USB_MSC_SIG_CBW;
  cbw.tag = tag;
  cbw.xfer_len = data_len;
  cbw.flags = dir_in ? 0x80 : 0x00;
  cbw.lun = USB_MSC_MAX_LUN;
  cbw.cdb_len = cdb_len;
  memmove(cbw.cdb, cdb, cdb_len);

  rc = usb_msc_bulk_out(sc, (uchar*)&cbw, USB_MSC_CBW_LEN);
  if(rc < 0)
    return rc;

  if(data_len > 0){
    if(dir_in)
      rc = usb_msc_bulk_in(sc, data, data_len, &done_len, allow_data_short);
    else
      rc = usb_msc_bulk_out(sc, data, data_len);
    if(rc < 0)
      return rc;
  }

  done_len = 0;
  rc = usb_msc_bulk_in(sc, (uchar*)&csw, USB_MSC_CSW_LEN, &done_len, 0);
  if(rc < 0)
    return rc;
  if(done_len != USB_MSC_CSW_LEN)
    return -1;
  if(csw.sig != USB_MSC_SIG_CSW)
    return -1;
  if(csw.tag != tag)
    return -1;
  if(csw.residue > cbw.xfer_len)
    return -1;
  if(csw.status != USB_MSC_CSW_STATUS_OK)
    return -1;

  return 0;
}

static int
usb_msc_scsi_tur(struct usb_msc_dev *sc)
{
  uchar cdb[6];

  memset(cdb, 0, sizeof(cdb));
  cdb[0] = 0x00;
  return usb_msc_bot_xfer(sc, cdb, 6, 0, 0, 1, 0);
}

static int
usb_msc_scsi_inquiry(struct usb_msc_dev *sc, uchar *buf, ushort len)
{
  uchar cdb[6];

  if(!buf || len < 36)
    return -1;

  memset(cdb, 0, sizeof(cdb));
  cdb[0] = 0x12;
  cdb[4] = (uchar)len;
  memset(buf, 0, len);
  return usb_msc_bot_xfer(sc, cdb, 6, buf, len, 1, 1);
}

static int
usb_msc_scsi_read_capacity10(struct usb_msc_dev *sc,
                             uint *last_lba, uint *block_size)
{
  uchar cdb[10];
  uchar out[8];
  int rc;

  if(!last_lba || !block_size)
    return -1;

  memset(cdb, 0, sizeof(cdb));
  cdb[0] = 0x25;
  memset(out, 0, sizeof(out));

  rc = usb_msc_bot_xfer(sc, cdb, 10, out, sizeof(out), 1, 0);
  if(rc < 0)
    return rc;

  *last_lba = usb_msc_be32(&out[0]);
  *block_size = usb_msc_be32(&out[4]);
  if(*block_size == 0)
    return -1;

  return 0;
}

static int
usb_msc_scsi_read10(struct usb_msc_dev *sc, uint lba, uchar *buf, ushort len)
{
  uchar cdb[10];

  if(!buf || len == 0)
    return -1;

  memset(cdb, 0, sizeof(cdb));
  cdb[0] = 0x28;
  cdb[2] = (uchar)(lba >> 24);
  cdb[3] = (uchar)(lba >> 16);
  cdb[4] = (uchar)(lba >> 8);
  cdb[5] = (uchar)(lba);
  cdb[7] = 0;
  cdb[8] = 1;

  return usb_msc_bot_xfer(sc, cdb, 10, buf, len, 1, 0);
}

static int
usb_msc_scsi_write10(struct usb_msc_dev *sc, uint lba, uchar *buf, ushort len)
{
  uchar cdb[10];

  if(!buf || len == 0)
    return -1;

  memset(cdb, 0, sizeof(cdb));
  cdb[0] = 0x2A;
  cdb[2] = (uchar)(lba >> 24);
  cdb[3] = (uchar)(lba >> 16);
  cdb[4] = (uchar)(lba >> 8);
  cdb[5] = (uchar)(lba);
  cdb[7] = 0;
  cdb[8] = 1;

  return usb_msc_bot_xfer(sc, cdb, 10, buf, len, 0, 0);
}

static int
usb_msc_probe_media_locked(struct usb_msc_dev *sc)
{
  uchar inquiry[36];
  uint last_lba;
  uint block_size;

  if(!sc)
    return -1;

  sc->probe_attempts++;

  if(usb_msc_scsi_tur(sc) < 0)
    return -1;
  if(usb_msc_scsi_inquiry(sc, inquiry, sizeof(inquiry)) < 0)
    return -1;
  if(usb_msc_scsi_read_capacity10(sc, &last_lba, &block_size) < 0)
    return -1;

  if(block_size != BSIZE)
    return -1;
  if(last_lba == 0xffffffffU)
    return -1;

  sc->block_size = block_size;
  sc->block_count = last_lba + 1;
  sc->online = 1;
  sc->probe_successes++;
  return 0;
}

static int
usb_msc_register_bdev_locked(struct usb_msc_dev *sc)
{
  if(!sc)
    return -1;
  if(sc->bdev_registered)
    return 0;
  if(!sc->online || sc->block_count == 0)
    return -1;
  if(!bdev_ready())
    return -1;
  if(bdev_register(sc->dev_id, &usb_msc_bdevsw) < 0)
    return -1;
  if(bdev_set_nblocks(sc->dev_id, sc->block_count) < 0){
    (void)bdev_unregister(sc->dev_id);
    return -1;
  }
  sc->bdev_registered = 1;
  sc->bdev_pending = 0;
  return 0;
}

static int
usb_msc_rw(struct buf *b)
{
  struct usb_msc_dev *sc;
  uint blockno;
  int is_write;
  int rc;

  if(!b)
    return -1;

  usb_msc_ensure_lock();

  acquire(&usb_msc_lock);
  sc = usb_msc_find_by_dev_locked(b->dev);
  if(!sc || !sc->active || !sc->online ||
     !sc->bdev_registered || sc->block_size != BSIZE ||
     b->blockno >= sc->block_count){
    if(sc)
      sc->rw_fail++;
    release(&usb_msc_lock);
    return -1;
  }
  blockno = b->blockno;
  is_write = (b->flags & B_DIRTY) ? 1 : 0;
  release(&usb_msc_lock);

  acquiresleep(&sc->io_lock);
  if(is_write)
    rc = usb_msc_scsi_write10(sc, blockno, b->data, BSIZE);
  else
    rc = usb_msc_scsi_read10(sc, blockno, b->data, BSIZE);
  releasesleep(&sc->io_lock);

  acquire(&usb_msc_lock);
  if(sc->active && rc == 0)
    sc->rw_ok++;
  else if(sc->active)
    sc->rw_fail++;
  if(sc->active)
    sc->last_rc = rc;
  release(&usb_msc_lock);

  if(rc < 0)
    return -1;

  b->flags |= B_VALID;
  b->flags &= ~B_DIRTY;
  return 0;
}

static uint
usb_msc_nblocks(uint dev)
{
  struct usb_msc_dev *sc;
  uint nblocks;

  usb_msc_ensure_lock();

  acquire(&usb_msc_lock);
  sc = usb_msc_find_by_dev_locked(dev);
  if(!sc || !sc->active || !sc->online)
    nblocks = 0;
  else
    nblocks = sc->block_count;
  release(&usb_msc_lock);

  return nblocks;
}

int
usb_msc_usb_attach(ushort vendor, ushort product,
                   uchar ifnum, uchar ifalt,
                   uchar bulk_in_ep, uchar bulk_out_ep,
                   uchar dev_speed,
                   uint *bind_handle)
{
  struct usb_msc_dev *sc;
  uint bind_id;
  int unit;

  if(!bind_handle)
    return -1;
  if((bulk_in_ep & 0x80) == 0 || (bulk_out_ep & 0x80) != 0)
    return -1;

  usb_msc_ensure_lock();

  acquire(&usb_msc_lock);
  if(usb_msc_dev_count >= USB_MSC_MAX_DEV){
    release(&usb_msc_lock);
    return -1;
  }

  unit = usb_msc_alloc_unit_locked();
  if(unit < 0){
    release(&usb_msc_lock);
    return -1;
  }

  sc = &usb_msc_devtab[usb_msc_dev_count++];
  memset(sc, 0, sizeof(*sc));
  bind_id = ++usb_msc_next_bind_id;
  if(bind_id == 0)
    bind_id = ++usb_msc_next_bind_id;

  sc->active = 1;
  sc->unit = (uchar)unit;
  sc->bind_id = bind_id;
  sc->dev_id = USB_DISK_DEV((uint)unit);
  sc->vendor_id = vendor;
  sc->product_id = product;
  sc->ifnum = ifnum;
  sc->ifalt = ifalt;
  sc->bulk_in_ep = bulk_in_ep;
  sc->bulk_out_ep = bulk_out_ep;
  sc->dev_speed = dev_speed;
  sc->bdev_pending = 1;
  sc->last_rc = 0;
  initsleeplock(&sc->io_lock, "usb_msc_io");

  *bind_handle = bind_id;
  release(&usb_msc_lock);

  cprintf("usb_msc: attached [%x:%x] bind=%d unit=%d dev=%d if=%d/%d ep=0x%x/0x%x speed=%d\n",
          (uint)vendor, (uint)product, bind_id, unit, sc->dev_id,
          (uint)ifnum, (uint)ifalt,
          (uint)bulk_in_ep, (uint)bulk_out_ep,
          (uint)dev_speed);
  return 0;
}

int
usb_msc_usb_detach(uint bind_handle)
{
  struct usb_msc_dev *sc;
  uint dev_id;
  int was_registered;

  if(bind_handle == 0)
    return -1;

  usb_msc_ensure_lock();

  acquire(&usb_msc_lock);
  sc = usb_msc_find_by_bind_locked(bind_handle);
  if(!sc){
    release(&usb_msc_lock);
    return -1;
  }

  dev_id = sc->dev_id;
  was_registered = sc->bdev_registered;
  sc->active = 0;
  sc->online = 0;
  sc->bdev_registered = 0;
  sc->bdev_pending = 0;
  release(&usb_msc_lock);

  if(was_registered && bdev_ready())
    (void)bdev_unregister(dev_id);

  cprintf("usb_msc: bind=%d detached\n", bind_handle);
  return 0;
}

void
usb_msc_runtime_service(void)
{
  struct usb_msc_dev *scan[USB_MSC_MAX_DEV];
  uint nscan;
  uint i;

  usb_msc_ensure_lock();

  acquire(&usb_msc_lock);
  usb_msc_runtime_pulses++;
  nscan = usb_msc_dev_count;
  if(nscan > USB_MSC_MAX_DEV)
    nscan = USB_MSC_MAX_DEV;
  for(i = 0; i < nscan; i++)
    scan[i] = &usb_msc_devtab[i];
  release(&usb_msc_lock);

  for(i = 0; i < nscan; i++){
    struct usb_msc_dev *sc;
    int probe_rc;

    sc = scan[i];
    if(!sc)
      continue;

    acquire(&usb_msc_lock);
    if(!sc->active){
      release(&usb_msc_lock);
      continue;
    }
    release(&usb_msc_lock);

    if(!sc->online){
      acquiresleep(&sc->io_lock);
      probe_rc = usb_msc_probe_media_locked(sc);
      releasesleep(&sc->io_lock);

      acquire(&usb_msc_lock);
      if(sc->active && probe_rc < 0){
        sc->probe_failures++;
        sc->last_rc = probe_rc;
      }
      release(&usb_msc_lock);
    }

    acquire(&usb_msc_lock);
    if(sc->active && sc->bdev_pending)
      (void)usb_msc_register_bdev_locked(sc);
    release(&usb_msc_lock);
  }
}

int
usb_msc_procfs_dump(char *buf, uint max)
{
  struct usb_msc_dev snap[USB_MSC_MAX_DEV];
  uint count;
  uint active;
  uint online;
  uint registered;
  uint len;
  uint i;

  if(!buf || max == 0)
    return -1;

  usb_msc_ensure_lock();

  acquire(&usb_msc_lock);
  count = usb_msc_dev_count;
  if(count > USB_MSC_MAX_DEV)
    count = USB_MSC_MAX_DEV;
  for(i = 0; i < count; i++)
    snap[i] = usb_msc_devtab[i];
  release(&usb_msc_lock);

  len = 0;
  active = 0;
  online = 0;
  registered = 0;

  if(usb_msc_buf_puts(buf, max, &len,
                      "# USB mass-storage BOT/SCSI runtime\n") < 0)
    return -1;

  for(i = 0; i < count; i++){
    struct usb_msc_dev *sc;

    sc = &snap[i];
    if(!sc->active)
      continue;
    active++;
    if(sc->online)
      online++;
    if(sc->bdev_registered)
      registered++;

    if(usb_msc_buf_puts(buf, max, &len, "dev bind=") < 0) return -1;
    if(usb_msc_buf_putu(buf, max, &len, sc->bind_id) < 0) return -1;
    if(usb_msc_buf_puts(buf, max, &len, " id=") < 0) return -1;
    if(usb_msc_buf_puthex16(buf, max, &len, sc->vendor_id) < 0) return -1;
    if(usb_msc_buf_putc(buf, max, &len, ':') < 0) return -1;
    if(usb_msc_buf_puthex16(buf, max, &len, sc->product_id) < 0) return -1;
    if(usb_msc_buf_puts(buf, max, &len, " unit=") < 0) return -1;
    if(usb_msc_buf_putu(buf, max, &len, sc->unit) < 0) return -1;
    if(usb_msc_buf_puts(buf, max, &len, " dev=") < 0) return -1;
    if(usb_msc_buf_putu(buf, max, &len, sc->dev_id) < 0) return -1;
    if(usb_msc_buf_puts(buf, max, &len, " if=") < 0) return -1;
    if(usb_msc_buf_putu(buf, max, &len, sc->ifnum) < 0) return -1;
    if(usb_msc_buf_putc(buf, max, &len, '/') < 0) return -1;
    if(usb_msc_buf_putu(buf, max, &len, sc->ifalt) < 0) return -1;
    if(usb_msc_buf_puts(buf, max, &len, " ep=0x") < 0) return -1;
    if(usb_msc_buf_puthex16(buf, max, &len, sc->bulk_in_ep) < 0) return -1;
    if(usb_msc_buf_putc(buf, max, &len, '/') < 0) return -1;
    if(usb_msc_buf_puts(buf, max, &len, "0x") < 0) return -1;
    if(usb_msc_buf_puthex16(buf, max, &len, sc->bulk_out_ep) < 0) return -1;
    if(usb_msc_buf_puts(buf, max, &len, " online=") < 0) return -1;
    if(usb_msc_buf_putu(buf, max, &len, sc->online) < 0) return -1;
    if(usb_msc_buf_puts(buf, max, &len, " bdev=") < 0) return -1;
    if(usb_msc_buf_putu(buf, max, &len, sc->bdev_registered) < 0) return -1;
    if(usb_msc_buf_puts(buf, max, &len, " blocks=") < 0) return -1;
    if(usb_msc_buf_putu(buf, max, &len, sc->block_count) < 0) return -1;
    if(usb_msc_buf_puts(buf, max, &len, " bsz=") < 0) return -1;
    if(usb_msc_buf_putu(buf, max, &len, sc->block_size) < 0) return -1;
    if(usb_msc_buf_puts(buf, max, &len, " probe=") < 0) return -1;
    if(usb_msc_buf_putu(buf, max, &len, sc->probe_attempts) < 0) return -1;
    if(usb_msc_buf_putc(buf, max, &len, '/') < 0) return -1;
    if(usb_msc_buf_putu(buf, max, &len, sc->probe_successes) < 0) return -1;
    if(usb_msc_buf_putc(buf, max, &len, '/') < 0) return -1;
    if(usb_msc_buf_putu(buf, max, &len, sc->probe_failures) < 0) return -1;
    if(usb_msc_buf_puts(buf, max, &len, " rw=") < 0) return -1;
    if(usb_msc_buf_putu(buf, max, &len, sc->rw_ok) < 0) return -1;
    if(usb_msc_buf_putc(buf, max, &len, '/') < 0) return -1;
    if(usb_msc_buf_putu(buf, max, &len, sc->rw_fail) < 0) return -1;
    if(usb_msc_buf_puts(buf, max, &len, " last_rc=0x") < 0) return -1;
    if(usb_msc_buf_puthex32(buf, max, &len, (uint)sc->last_rc) < 0) return -1;
    if(usb_msc_buf_putc(buf, max, &len, '\n') < 0) return -1;
  }

  if(usb_msc_buf_puts(buf, max, &len, "summary active=") < 0) return -1;
  if(usb_msc_buf_putu(buf, max, &len, active) < 0) return -1;
  if(usb_msc_buf_puts(buf, max, &len, " online=") < 0) return -1;
  if(usb_msc_buf_putu(buf, max, &len, online) < 0) return -1;
  if(usb_msc_buf_puts(buf, max, &len, " bdev=") < 0) return -1;
  if(usb_msc_buf_putu(buf, max, &len, registered) < 0) return -1;
  if(usb_msc_buf_puts(buf, max, &len, " seen=") < 0) return -1;
  if(usb_msc_buf_putu(buf, max, &len, count) < 0) return -1;
  if(usb_msc_buf_puts(buf, max, &len, " pulses=") < 0) return -1;
  if(usb_msc_buf_putu(buf, max, &len, usb_msc_runtime_pulses) < 0) return -1;
  if(usb_msc_buf_putc(buf, max, &len, '\n') < 0) return -1;

  return (int)len;
}
