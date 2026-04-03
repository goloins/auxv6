struct buf {
  int flags;
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev;      // LRU cache list
  struct buf *next;
  struct buf *hash_next; // next in bcache hash chain
  struct buf *qnext;     // disk queue
  uchar data[BSIZE];
};
#define B_VALID  0x2   // buffer has been read from disk
#define B_DIRTY  0x4   // buffer needs to be written to disk
#define B_ERROR  0x8   // last I/O on this buffer failed
#define B_INHASH 0x10  // buffer is currently in the bcache hash table

