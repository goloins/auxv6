#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#define stat xv6_stat  // avoid clash with host struct stat
#include "../include/types.h"
#include "../include/fs.h"
#include "../include/stat.h"
#include "../include/param.h"

#ifndef static_assert
#define static_assert(a, b) do { switch (0) case 0: case (a): ; } while (0)
#endif

#define NINODES 200

// Disk layout:
// [ boot block | sb block | log | inode blocks | free bit map | data blocks ]

int nbitmap = FSSIZE/(BSIZE*8) + 1;
int ninodeblocks = NINODES / IPB + 1;
int nlog = LOGSIZE;
int nmeta;    // Number of meta blocks (boot, sb, nlog, inode, bitmap)
int nblocks;  // Number of data blocks

int fsfd;
struct superblock sb;
char zeroes[BSIZE];
uint freeinode = 1;
uint freeblock;


void balloc(int);
void wsect(uint, void*);
void winode(uint, struct dinode*);
void rinode(uint inum, struct dinode *ip);
void rsect(uint sec, void *buf);
uint ialloc(ushort type);
void iappend(uint inum, void *p, int n);
uint alloc_block(void);
uint mkfs_mkdir(uint parent, const char *name);
void dirlink_inum(uint dirino, const char *name, uint inum);
uint install_file(uint dirino, const char *name, const char *srcpath);
const char* pathbase(const char *path);

// convert to intel byte order
ushort
xshort(ushort x)
{
  ushort y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

int
main(int argc, char *argv[])
{
  int i;
  uint rootino, binino, etcino, devino, homeino, inum, off;
  struct dirent de;
  char buf[BSIZE];
  struct dinode din;


  static_assert(sizeof(int) == 4, "Integers must be 4 bytes!");

  if(argc < 2){
    fprintf(stderr, "Usage: mkfs fs.img files...\n");
    exit(1);
  }

  assert((BSIZE % sizeof(struct dinode)) == 0);
  assert((BSIZE % sizeof(struct dirent)) == 0);

  fsfd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
  if(fsfd < 0){
    perror(argv[1]);
    exit(1);
  }

  // 1 fs block = 1 disk sector
  nmeta = 2 + nlog + ninodeblocks + nbitmap;
  nblocks = FSSIZE - nmeta;

  sb.size = xint(FSSIZE);
  sb.nblocks = xint(nblocks);
  sb.ninodes = xint(NINODES);
  sb.nlog = xint(nlog);
  sb.logstart = xint(2);
  sb.inodestart = xint(2+nlog);
  sb.bmapstart = xint(2+nlog+ninodeblocks);

  printf("nmeta %d (boot, super, log blocks %u inode blocks %u, bitmap blocks %u) blocks %d total %d\n",
         nmeta, nlog, ninodeblocks, nbitmap, nblocks, FSSIZE);

  freeblock = nmeta;     // the first free block that we can allocate

  for(i = 0; i < FSSIZE; i++)
    wsect(i, zeroes);

  memset(buf, 0, sizeof(buf));
  memmove(buf, &sb, sizeof(sb));
  wsect(1, buf);

  rootino = ialloc(T_DIR);
  assert(rootino == ROOTINO);

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, ".");
  iappend(rootino, &de, sizeof(de));

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, "..");
  iappend(rootino, &de, sizeof(de));

  // Seed the base rootfs hierarchy.
  binino = mkfs_mkdir(rootino, "bin");
  etcino = mkfs_mkdir(rootino, "etc");
  devino = mkfs_mkdir(rootino, "dev");
  homeino = mkfs_mkdir(rootino, "home");
  mkfs_mkdir(rootino, "root");
  mkfs_mkdir(homeino, "aux");
  (void)devino;

  for(i = 2; i < argc; i++){
    const char *src = argv[i];
    const char *base = pathbase(src);

    if(base[0] == '_'){
      // Install all user binaries in /bin; keep /init as a bootstrap hard link.
      if(strcmp(base, "_init") == 0){
        inum = install_file(binino, "init", src);
        dirlink_inum(rootino, "init", inum);
      } else
        install_file(binino, base + 1, src);
    } else if(strcmp(base, "etc.hosts") == 0){
      install_file(etcino, "hosts", src);
    } else if(strcmp(base, "etc.fstab") == 0){
      install_file(etcino, "fstab", src);
    } else if(strcmp(base, "etc.profile") == 0){
      install_file(etcino, "profile", src);
    } else if(strcmp(base, "etc.passwd") == 0){
      install_file(etcino, "passwd", src);
    } else if(strcmp(base, "etc.hostname") == 0){
      install_file(etcino, "hostname", src);
    } else {
      install_file(rootino, base, src);
    }
  }

  // fix size of root inode dir
  rinode(rootino, &din);
  off = xint(din.size);
  off = ((off/BSIZE) + 1) * BSIZE;
  din.size = xint(off);
  winode(rootino, &din);

  balloc(freeblock);

  exit(0);
}

const char*
pathbase(const char *path)
{
  const char *base;

  if(path == 0)
    return "";
  base = strrchr(path, '/');
  if(base == 0)
    return path;
  return base + 1;
}

void
dirlink_inum(uint dirino, const char *name, uint inum)
{
  struct dirent de;

  bzero(&de, sizeof(de));
  de.inum = xshort(inum);
  strncpy(de.name, name, DIRSIZ);
  iappend(dirino, &de, sizeof(de));
}

uint
mkfs_mkdir(uint parent, const char *name)
{
  struct dinode pdin;
  uint inum;

  inum = ialloc(T_DIR);
  dirlink_inum(inum, ".", inum);
  dirlink_inum(inum, "..", parent);
  dirlink_inum(parent, name, inum);

  // Each child directory increments the parent's link count via "..".
  rinode(parent, &pdin);
  pdin.nlink = xshort(xshort(pdin.nlink) + 1);
  winode(parent, &pdin);

  return inum;
}

uint
install_file(uint dirino, const char *name, const char *srcpath)
{
  int cc;
  int fd;
  uint inum;
  char buf[BSIZE];

  if((fd = open(srcpath, 0)) < 0){
    perror(srcpath);
    exit(1);
  }

  inum = ialloc(T_FILE);
  dirlink_inum(dirino, name, inum);

  while((cc = read(fd, buf, sizeof(buf))) > 0)
    iappend(inum, buf, cc);

  close(fd);
  return inum;
}

void
wsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE){
    perror("lseek");
    exit(1);
  }
  if(write(fsfd, buf, BSIZE) != BSIZE){
    perror("write");
    exit(1);
  }
}

void
winode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *dip = *ip;
  wsect(bn, buf);
}

void
rinode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *ip = *dip;
}

void
rsect(uint sec, void *buf)
{
  int cc;

  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE){
    perror("lseek");
    exit(1);
  }
  cc = read(fsfd, buf, BSIZE);
  if(cc != BSIZE){
    fprintf(stderr, "mkfs: short read at sector %u: got %d bytes, expected %d\n", sec, cc, BSIZE);
    perror("read");
    exit(1);
  }
}

uint
ialloc(ushort type)
{
  uint inum = freeinode++;
  struct dinode din;

  bzero(&din, sizeof(din));
  din.type = xshort(type);
  din.nlink = xshort(1);
  din.size = xint(0);
  winode(inum, &din);
  return inum;
}

uint
alloc_block(void)
{
  if(freeblock >= FSSIZE){
    fprintf(stderr, "mkfs: out of data blocks (freeblock=%u, FSSIZE=%u)\n", freeblock, FSSIZE);
    exit(1);
  }
  return freeblock++;
}

void
balloc(int used)
{
  uchar buf[BSIZE];
  int i;

  printf("balloc: first %d blocks have been allocated\n", used);
  assert(used < BSIZE*8);
  bzero(buf, BSIZE);
  for(i = 0; i < used; i++){
    buf[i/8] = buf[i/8] | (0x1 << (i%8));
  }
  printf("balloc: write bitmap block at sector %d\n", sb.bmapstart);
  wsect(sb.bmapstart, buf);
}

#define min(a, b) ((a) < (b) ? (a) : (b))

void
iappend(uint inum, void *xp, int n)
{
  char *p = (char*)xp;
  uint fbn, off, n1;
  struct dinode din;
  char buf[BSIZE];
  uint indirect[NINDIRECT];
  uint x;

  rinode(inum, &din);
  off = xint(din.size);
  // printf("append inum %d at off %d sz %d\n", inum, off, n);
  while(n > 0){
    fbn = off / BSIZE;
    assert(fbn < MAXFILE);
    if(fbn < NDIRECT){
      if(xint(din.addrs[fbn]) == 0){
        din.addrs[fbn] = xint(alloc_block());
      }
      x = xint(din.addrs[fbn]);
    } else {
      if(xint(din.addrs[NDIRECT]) == 0){
        din.addrs[NDIRECT] = xint(alloc_block());
      }
      rsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      if(indirect[fbn - NDIRECT] == 0){
        indirect[fbn - NDIRECT] = xint(alloc_block());
        wsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      }
      x = xint(indirect[fbn-NDIRECT]);
    }
    n1 = min(n, (fbn + 1) * BSIZE - off);
    rsect(x, buf);
    bcopy(p, buf + off - (fbn * BSIZE), n1);
    wsect(x, buf);
    n -= n1;
    off += n1;
    p += n1;
  }
  din.size = xint(off);
  winode(inum, &din);
}
