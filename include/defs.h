struct buf;
struct context;
struct file;
struct inode;
struct pipe;
struct proc;
struct rtcdate;
struct spinlock;
struct sleeplock;
struct stat;
struct socket;
struct sockaddr_in;
struct ifnet;
struct netif_info;
struct route_info;
struct mbuf;
struct ip_hdr;
struct superblock;
struct termios;
struct bdevsw;
struct vfs;
struct vnode_ops;
struct mount;
struct vfs_mount_info;
struct vnode;

// bio.c
void            binit(void);
struct buf*     bread(uint, uint);
void            brelse(struct buf*);
void            bwrite(struct buf*);

// blockdev.c
void            bdevinit(void);
int             bdev_register(uint dev, const struct bdevsw *ops);
int             bdev_register_part(uint dev, uint parent, uint start, uint nblocks);
int             bdevrw(struct buf *b);
uint            bdev_nblocks(uint dev);

// console.c
void            consoleinit(void);
void            cprintf(char*, ...);
void            consoleintr(int(*)(void));
void            console_set_foreground_pgid(int);
int             console_get_foreground_pgid(void);
int             console_get_termios(struct termios *tp);
int             console_set_termios(const struct termios *tp, int optional_actions);
void            panic(char*) __attribute__((noreturn));

// exec.c
int             exec(char*, char**);

// file.c
struct file*    filealloc(void);
void            fileclose(struct file*);
struct file*    filedup(struct file*);
void            fileinit(void);
int             fileread(struct file*, char*, int n);
int             filestat(struct file*, struct stat*);
int             filewrite(struct file*, char*, int n);
int             file_has_refs_on_dev(uint dev);

// fs.c
void            readsb(int dev, struct superblock *sb);
int             dirlink(struct inode*, char*, uint);
struct inode*   dirlookup(struct inode*, char*, uint*);
struct inode*   ialloc(uint, short);
struct inode*   iget(uint, uint);
struct inode*   idup(struct inode*);
void            iinit(int dev);
int             inode_on_dev(struct inode*, uint);
uint            inode_get_dev(struct inode*);
void            ilock(struct inode*);
int             iaccess(struct inode*, int);
void            iput(struct inode*);
void            iunlock(struct inode*);
void            iunlockput(struct inode*);
void            iupdate(struct inode*);
int             namecmp(const char*, const char*);
struct inode*   namei(char*);
struct inode*   nameiparent(char*, char*);
int             readi(struct inode*, char*, uint, uint);
int             procfs_readi(struct inode*, char*, uint, uint);
void            stati(struct inode*, struct stat*);
int             writei(struct inode*, char*, uint, uint);
void            vfs_init(void);
struct inode*   vfs_namei(char*);
struct inode*   vfs_nameiparent(char*, char*);
int             vfs_lookup(char*, struct vnode*);
int             vfs_lookup_parent(char*, char*, struct vnode*);
void            vfs_vnode_drop(struct vnode*);
int             vfs_register_mount(struct vfs*, int, int, char*);
int             vfs_unmount(char*);
int             vfs_mount_count(void);
int             vfs_get_mounts(struct vfs_mount_info*, int);
int             vfs_dev_has_cap(uint, uint);
const struct vnode_ops* vfs_dev_vops(uint);
void*           vfs_dev_fs_data(uint);
void            vfs_xv6fs_init(struct vfs*);
void            vfs_ext2_init(struct vfs*);
void            vfs_procfs_init(struct vfs*);

// ide.c
void            ideinit(void);
void            ideintr(void);
void            iderw(struct buf*);

// ioapic.c
void            ioapicenable(int irq, int cpu);
extern uchar    ioapicid;
void            ioapicinit(void);

// kalloc.c
char*           kalloc(void);
void            kfree(char*);
void            kinit1(void*, void*);
void            kinit2(void*, void*);

// kbd.c
void            kbdintr(void);

// lapic.c
void            cmostime(struct rtcdate *r);
int             lapicid(void);
extern volatile uint*    lapic;
void            lapiceoi(void);
void            lapicinit(void);
void            lapicstartap(uchar, uint);
void            microdelay(int);

// log.c
void            initlog(int dev);
void            log_write(struct buf*);
void            begin_op();
void            end_op();

// mp.c
extern int      ismp;
void            mpinit(void);

// picirq.c
void            picenable(int);
void            picinit(void);

// pipe.c
int             pipealloc(struct file**, struct file**);
void            pipeclose(struct pipe*, int);
int             piperead(struct pipe*, char*, int);
int             pipewrite(struct pipe*, char*, int);

//PAGEBREAK: 16
// proc.c
int             cpuid(void);
void            exit(void);
int             fork(void);
int             growproc(int);
int             kill(int);
int             proc_kill_with_signal(int pid, int signo);
int             proc_getppid(void);
int             proc_getpgrp(void);
int             proc_getuid(void);
int             proc_getgid(void);
int             proc_setpgid(int pid, int pgid);
int             proc_setsid(void);
int             proc_setuid(int uid);
int             proc_setgid(int gid);
int             proc_waitpid(int pid, int *status, int options);
int             proc_wait4(int pid, int *status, int options, uint rusage_addr);
int             proc_waitid(int idtype, int id, int *infop, int options);
int             proc_sigaction(int signo, uint act_addr, uint oldact_addr);
int             proc_sigprocmask(int how, uint set_addr, uint oldset_addr);
int             proc_signal_pgid(int pgid, int signo);
int             proc_tcsetpgrp(int pgid);
int             proc_tcgetpgrp(void);
int             proc_tcgetattr(int fd, uint termios_addr);
int             proc_tcsetattr(int fd, int optional_actions, uint termios_addr);
void            proc_apply_pending_signals(struct proc *p);
void            proc_maybe_stop_current(void);
struct cpu*     mycpu(void);
struct proc*    myproc();
void            pinit(void);
int             proc_has_cwd_on_dev(uint dev);
uint            proc_cwd_dev(void);
void            procdump(void);
void            scheduler(void) __attribute__((noreturn));
void            sched(void);
void            setproc(struct proc*);
void            sleep(void*, struct spinlock*);
void            userinit(void);
int             wait(void);
void            wakeup(void*);
void            yield(void);

// swtch.S
void            swtch(struct context**, struct context*);

// spinlock.c
void            acquire(struct spinlock*);
void            getcallerpcs(void*, uint*);
int             holding(struct spinlock*);
void            initlock(struct spinlock*, char*);
void            release(struct spinlock*);
void            pushcli(void);
void            popcli(void);

// sleeplock.c
void            acquiresleep(struct sleeplock*);
void            releasesleep(struct sleeplock*);
int             holdingsleep(struct sleeplock*);
void            initsleeplock(struct sleeplock*, char*);

// string.c
int             memcmp(const void*, const void*, uint);
void*           memmove(void*, const void*, uint);
void*           memset(void*, int, uint);
char*           safestrcpy(char*, const char*, int);
int             strlen(const char*);
int             strncmp(const char*, const char*, uint);
char*           strncpy(char*, const char*, int);

// syscall.c
int             argint(int, int*);
int             argptr(int, char**, int);
int             argstr(int, char**);
int             fetchint(uint, int*);
int             fetchstr(uint, char**);
void            syscall(void);
int             sys_sigsend(void);
int             sys_getppid(void);
int             sys_getpgrp(void);
int             sys_getuid(void);
int             sys_getgid(void);
int             sys_getcwd(void);
int             sys_setpgid(void);
int             sys_setsid(void);
int             sys_setuid(void);
int             sys_setgid(void);
int             sys_chmod(void);
int             sys_chown(void);
int             sys_mountinfo(void);
int             sys_mount(void);
int             sys_umount(void);
int             sys_uname(void);
int             sys_wait4(void);
int             sys_waitid(void);
int             sys_sigaction(void);
int             sys_sigprocmask(void);
int             sys_tcsetpgrp(void);
int             sys_tcgetpgrp(void);
int             sys_tcgetattr(void);
int             sys_tcsetattr(void);
int             sys_waitpid(void);

// timer.c
void            timerinit(void);

// trap.c
void            idtinit(void);
extern uint     ticks;
void            tvinit(void);
extern struct spinlock tickslock;

// uart.c
void            uartinit(void);
void            uartintr(void);
void            uartputc(int);

// vm.c
void            seginit(void);
void            kvmalloc(void);
pde_t*          setupkvm(void);
char*           uva2ka(pde_t*, char*);
int             allocuvm(pde_t*, uint, uint);
int             deallocuvm(pde_t*, uint, uint);
void            freevm(pde_t*);
void            inituvm(pde_t*, char*, uint);
int             loaduvm(pde_t*, char*, struct inode*, uint, uint);
pde_t*          copyuvm(pde_t*, uint);
void            switchuvm(struct proc*);
void            switchkvm(void);
int             copyout(pde_t*, uint, void*, uint);
void            clearpteu(pde_t *pgdir, char *uva);

// socket.c
void            socket_init(void);
struct socket*  socket_alloc(void);
struct socket*  socket_ref(struct socket*);
void            socket_deref(struct socket*);
void            socket_close(struct socket*);
struct socket*  getfd_socket(int fd);
int             sys_socket(void);
int             sys_bind(void);
int             sys_connect(void);
int             sys_send(void);
int             sys_recv(void);
int             sys_recvtimeout(void);
int             sys_listen(void);
int             sys_accept(void);
int             sys_netifinfo(void);
int             sys_routeinfo(void);
int             sys_routeadd(void);
int             socket_deliver(struct sockaddr_in*, struct sockaddr_in*, char*, uint);
int             socket_deliver_raw(uchar, struct sockaddr_in*, struct sockaddr_in*, char*, uint);
int             socket_stream_connect(struct socket*, struct sockaddr_in*);

// net device layer
void            netdev_init(void);
int             if_register(struct ifnet*);
struct ifnet*   if_get(char*);
struct ifnet*   if_byindex(uint);
struct ifnet*   if_first(void);
struct ifnet*   if_next(struct ifnet*);
int             if_dump(struct netif_info*, int);
int             if_output(struct ifnet*, struct mbuf*);
void            if_input(struct ifnet*, struct mbuf*);
void            loopback_attach(void);
void            route_init(void);
int             route_add(uint, uint, uint, uint, struct ifnet*, uint);
struct ifnet*   route_lookup(uint, uint*, uint*);
int             route_dump(struct route_info*, int);
struct mbuf*    mbuf_alloc(void);
void            mbuf_free(struct mbuf*);
int             ip_output(struct ifnet*, uchar, uint, uint, char*, uint);
void            ip_input(struct ifnet*, struct mbuf*);
int             udp_output(struct ifnet*, struct sockaddr_in*, struct sockaddr_in*, char*, uint);
void            udp_input(struct ifnet*, struct ip_hdr*, char*, uint);
void            icmp_input(struct ifnet*, struct ip_hdr*, char*, uint);
int             tcp_connect(struct ifnet*, struct socket*, struct sockaddr_in*);
int             tcp_output(struct ifnet*, struct sockaddr_in*, struct sockaddr_in*, char*, uint);
void            tcp_input(struct ifnet*, struct ip_hdr*, char*, uint);

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))
