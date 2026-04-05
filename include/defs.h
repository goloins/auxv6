#include "stdint.h"  /* uint64_t for file-offset parameters */
struct buf;
struct console_gfx_debug_info;
struct context;
struct dirent;
struct file;
struct inode;
struct pipe;
struct proc;
struct procinfo_k;
struct procfdinfo_k;
struct procfdlimitinfo_k;
struct rtcdate;
struct spinlock;
struct sleeplock;
struct stat;
struct socket;
struct socket_info_k;
struct sockaddr_in;
struct ifnet;
struct arp_info;
struct netif_info;
struct route_info;
struct mbuf;
struct ip_hdr;
struct timespec;
struct superblock;
struct termios;
struct bdevsw;
struct vfs;
struct vnode_ops;
struct mount;
struct vfs_mount_info;
struct vnode;

struct kalloc_stats_k {
	uint total_pages;
	uint free_pages;
	uint allocated_pages;
	uint shared_pages;
	uint alloc_calls;
	uint free_calls;
	uint cache_alloc_hits;
	uint cache_alloc_misses;
	uint cache_free_inserts;
	uint global_refill_batches;
	uint global_refill_pages;
	uint global_drain_batches;
	uint global_drain_pages;
	uint ref_increments;
	uint deferred_frees;
};

// bio.c
void            binit(void);
struct buf*     bread(uint, uint);
int             berror(struct buf*);
int             bread_ok(uint, uint, struct buf**);
void            brelse(struct buf*);
void            bwrite(struct buf*);
int             bwrite_ok(struct buf*);

// blockdev.c
void            bdevinit(void);
int             bdev_register(uint dev, const struct bdevsw *ops);
int             bdev_register_part(uint dev, uint parent, uint start, uint nblocks);
int             bdev_set_nblocks(uint dev, uint nblocks);
int             bdevrw(struct buf *b);
uint            bdev_nblocks(uint dev);
int             bdev_format_table(char *buf, int max);

// console.c
struct console_gfx_debug_info {
	uint sync_calls;
	uint cells_changed;
	uint cells_rendered;
	uint flush_calls;
	uint flush_pixels;
	uint flush_blocked_tickslock;
	uint boot_ready;
	uint has_dev;
	uint has_fb;
	uint has_ctx;
	uint has_vt;
	uint active_tty;
	uint mode_width;
	uint mode_height;
	uint fb_width;
	uint fb_height;
	uint fb_stride;
	uint fb_bpp;
	uint cell_width;
	uint cell_height;
	uint tty_cols;
	uint tty_rows;
	uint tty_cursor;
	uint tty_cursor_row;
	uint tty_cursor_col;
	uint tty_nonblank_cells;
	uint hw_view_row0;
	uint hw_view_col0;
	uint vt_cols;
	uint vt_rows;
	uint vt_origin_x;
	uint vt_origin_y;
	uint vt_cursor_x;
	uint vt_cursor_y;
	uint vt_nonblank_cells;
	int gfx_owner_pid;
	uint input_events;
};

void            consoleinit(void);
void            console_gfx_late_enable(void);
void            cprintf(char*, ...);
void            consoleintr(int(*)(void));
void            console_set_foreground_pgid(int tty, int pgid);
int             console_get_foreground_pgid(int tty);
int             console_get_termios(int tty, struct termios *tp);
int             console_set_termios(int tty, const struct termios *tp, int optional_actions);
void            console_set_active_tty(int tty);
int             console_get_active_tty(void);
int             console_logo_get_enabled(void);
int             console_logo_set_enabled(int enabled);
int             console_gfx_server_claim(int pid);
int             console_gfx_server_release(int pid);
int             console_gfx_server_owner(void);
uint            console_input_events(void);
uint            console_gfx_stats_sync_calls(void);
uint            console_gfx_stats_cells_changed(void);
uint            console_gfx_stats_cells_rendered(void);
uint            console_gfx_stats_flush_calls(void);
uint            console_gfx_stats_flush_pixels(void);
int             console_gfx_debug_snapshot(struct console_gfx_debug_info *out);
int             console_ioctl(int fd, int request, uint arg);
int             console_kmsg_read(char *dst, int max);
void            panic(char*) __attribute__((noreturn));

// kalloc.c
char*           kalloc(void);
void            kfree(char*);
char*           kalloc_contiguous(uint npages);

// kmalloc.c — Phase 1A: variable-sized kernel memory allocator
void            kmalloc_init(void);
void*           kmalloc(uint size);
void            kmalloc_free(void *ptr);
void*           kmalloc_realloc(void *ptr, uint size);

// pty.c
void            ptyinit(void);
int             pty_open(struct file *f, int minor);
void            pty_close(struct file *f);
int             pty_ioctl_file(struct file *f, int request, uint arg);
int             pty_get_termios_file(struct file *f, struct termios *tp);
int             pty_set_termios_file(struct file *f, const struct termios *tp, int optional_actions);
int             pty_fileread(struct file *f, char *dst, int n);
int             pty_filewrite(struct file *f, char *src, int n);
void            pty_poll_events(struct file *f, int *rd, int *wr, int *err);

// Master debug flag - set to 1 to enable boot diagnostics and subsystem logging.
// Controlled via -DAUXV6_DEBUG=1 or by editing here.
#ifndef AUXV6_DEBUG
#define AUXV6_DEBUG 0
#endif

// Debug logging toggles (set to 0 to compile out noisy diagnostics).
// By default these follow AUXV6_DEBUG, but can be overridden individually.
#ifndef DBG_VFS
#define DBG_VFS AUXV6_DEBUG
#endif

#ifndef DBG_MOUNT
#define DBG_MOUNT AUXV6_DEBUG
#endif

#ifndef DBG_EXT2
#define DBG_EXT2 AUXV6_DEBUG
#endif

#ifndef DBG_IDE
#define DBG_IDE AUXV6_DEBUG
#endif

#ifndef DBG_EXEC
#define DBG_EXEC AUXV6_DEBUG
#endif

#ifndef DBG_AHCI
#define DBG_AHCI 1
#endif

#ifndef DBG_NVME
#define DBG_NVME 0
#endif

#ifndef DBG_VIRTIO_NET
#define DBG_VIRTIO_NET 0
#endif

// Stack demand-growth debug flag.
#ifndef DBG_STACK
#define DBG_STACK AUXV6_DEBUG
#endif

// Boot-time verbosity flag - gates PCI discovery, device enumeration details, etc.
#ifndef AUXV6_BOOTINFO
#define AUXV6_BOOTINFO AUXV6_DEBUG
#endif

#define VFSDBG(...)   do { if(DBG_VFS) cprintf(__VA_ARGS__); } while(0)
#define MOUNTDBG(...) do { if(DBG_MOUNT) cprintf(__VA_ARGS__); } while(0)
#define EXT2DBG(...)  do { if(DBG_EXT2) cprintf(__VA_ARGS__); } while(0)
#define IDEDBG(...)   do { if(DBG_IDE) cprintf(__VA_ARGS__); } while(0)
#define EXECDBG(...)  do { if(DBG_EXEC) cprintf(__VA_ARGS__); } while(0)
#define AHCIDBG(...)  do { if(DBG_AHCI) cprintf(__VA_ARGS__); } while(0)
#define NVMEDBG(...)  do { if(DBG_NVME) cprintf(__VA_ARGS__); } while(0)
#define VNETDBG(...)  do { if(DBG_VIRTIO_NET) cprintf(__VA_ARGS__); } while(0)
#define STACKDBG(...) do { if(DBG_STACK) cprintf(__VA_ARGS__); } while(0)

// Boot information macro - for verbose discovery and enumeration details
#define BOOTDBG(...)  do { if(AUXV6_BOOTINFO) cprintf(__VA_ARGS__); } while(0)

// Runtime network verbosity flag - gates socket operations, connection details, etc.
#ifndef AUXV6_NET_DEBUG
#define AUXV6_NET_DEBUG 0
#endif

// Network debug macro - for socket ops, bind, connect, packet flow, etc.
#define NETDBG(...)   do { if(AUXV6_NET_DEBUG) cprintf(__VA_ARGS__); } while(0)

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

// sysfile.c — Phase 1A: file descriptor table (fdtable) functions
struct fdtable* fdtable_alloc(void);
void            fdtable_free(struct fdtable*);
int             fdtable_dup(struct fdtable*, struct fdtable*);
int             fdtable_grow(struct fdtable*);
int             proc_fd_limit(struct proc*);

// fs.c
void            readsb(int dev, struct superblock *sb);
int             dirlink(struct inode*, char*, uint);
struct inode*   dirlookup(struct inode*, char*, uint*);
struct inode*   ialloc(uint, short);
struct inode*   iget(uint, uint);
struct inode*   idup(struct inode*);
void            icache_init(void);
void            iinit(int dev);
int             inode_on_dev(struct inode*, uint);
uint            inode_get_dev(struct inode*);
void            ilock(struct inode*);
int             iaccess(struct inode*, int);
void            iput(struct inode*);
void            iunlock(struct inode*);
void            iunlockput(struct inode*);
void            iupdate(struct inode*);
void            itruncate(struct inode*);
int             namecmp(const char*, const char*);
struct inode*   namei(char*);
struct inode*   nameiparent(char*, char*);
int             readi(struct inode*, char*, uint64_t, uint);
int             procfs_readi(struct inode*, char*, uint64_t, uint);
void            stati(struct inode*, struct stat*);
int             writei(struct inode*, char*, uint64_t, uint);
void            vfs_init(void);
struct inode*   vfs_namei(char*);
struct inode*   vfs_nameiparent(char*, char*);
int             vfs_lookup(char*, struct vnode*);
int             vfs_lookup_parent(char*, char*, struct vnode*);
void            vfs_vnode_drop(struct vnode*);
int             vfs_register_mount(struct vfs*, int, int, char*, const void*, int);
int             vfs_remount(char*, int);
int             vfs_unmount(char*);
uint            vfs_root_dev(void);
int             vfs_is_system_root_inode(struct inode*);
int             vfs_dirent_visible(struct inode*, struct dirent*);
struct inode*   vfs_mount_crossover(struct inode*, char*);
int             vfs_mount_count(void);
int             vfs_get_mounts(struct vfs_mount_info*, int);
int             vfs_dev_is_mounted(uint);
int             vfs_dev_has_cap(uint, uint);
const struct vnode_ops* vfs_dev_vops(uint);
void*           vfs_dev_fs_data(uint);
int             vfs_dev_faultctl(uint, int, int);
int             ext2_block_usage(uint dev, uint *total_blocks, uint *free_blocks,
								 uint *block_size);
int             tmpfs_block_usage(uint dev, uint *total_blocks, uint *free_blocks,
								  uint *block_size);
void            vfs_xv6fs_init(struct vfs*);
void            vfs_ext2_init(struct vfs*);
void            vfs_msdosfs_init(struct vfs*);
void            vfs_btrfs_init(struct vfs*);
void            vfs_ufs2_init(struct vfs*);
void            vfs_procfs_init(struct vfs*);
void            vfs_isofs_init(struct vfs*);
void            vfs_tmpfs_init(struct vfs*);
// ide.c
void            ideinit(void);
void            ideintr(void);
void            iderw(struct buf*);

// ahci.c
void            ahci_init(void);
int             ahci_get_tune(char *buf, int max);
int             ahci_set_tune(const char *buf, int n);

// nvme.c
void            nvme_init(void);
void            nvme_shutdown(void);
int             nvme_get_tune(char *buf, int max);
int             nvme_set_tune(const char *buf, int n);

// ioapic.c
void            ioapicenable(int irq, int cpu);
extern uchar    ioapicid;
void            ioapicinit(void);

// pci.c
void            pci_init(void);
int             pci_format_devices(char*, int);

// virtio_blk.c
void            virtio_blk_init(void);
int             virtio_blk_get_flush_every_writes(void);
int             virtio_blk_set_flush_every_writes(int value);
int             virtio_blk_get_tune(char *buf, int max);
int             virtio_blk_set_tune(const char *buf, int n);

// virtio_gpu.c
void            virtio_gpu_init(void);

// graphics/display.c
void            display_init(void);

// dma.c
void            dma_init(void);
void*           dma_alloc(uint, uint*);
void            dma_free(void*, uint);
uint            dma_virt_to_phys(void*);
void*           dma_alloc_aligned(uint, uint, uint*);
void            dma_sync_for_device(void*, uint);
void            dma_sync_for_cpu(void*, uint);

// loop.c
void            loop_init(void);
int             loop_setup(int loopnum, struct inode *ip, uint offset, uint nblocks);
int             loop_teardown(int loopnum);
int             loop_status(int loopnum, uint *backing_inum, uint *offset,
							uint *nblocks, uint *flags);
int             loop_devnum(int loopnum);
int             loop_find_free(void);

// kalloc.c
char*           kalloc(void);
void            kfree(char*);
void            kinit1(void*, void*);
void            kinit2(void*, void*);
void            kalloc_meminfo(uint *total_pages, uint *free_pages);
void            kalloc_stats(struct kalloc_stats_k *out);
void            kpage_incref(uint pa);
uint            kpage_refcount(uint pa);
int             kpage_is_managed(uint pa);

// kbd.c
void            kbdintr(void);

// ktime.c
void            ktime_init(void);
void            ktime_tick(uint current_ticks);
void            ktime_get_monotonic(struct timespec *ts);
void            ktime_get_realtime(struct timespec *ts);
int             ktime_set_realtime(const struct timespec *ts);

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
int             pipe_readable(struct pipe*);
int             pipe_writable(struct pipe*);

//PAGEBREAK: 16
// proc.c
int             cpuid(void);
void            exit(int status);
int             fork(void);
int             growproc(int);
int             kill(int pid, int sig);
int             proc_kill_with_signal(int pid, int signo);
int             proc_getppid(void);
int             proc_getpgrp(void);
int             proc_getsid(int pid);
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
int             proc_is_tty_fd(int fd);
int             proc_tty_major(int fd);
int             proc_tcgetattr(int fd, uint termios_addr);
int             proc_tcsetattr(int fd, int optional_actions, uint termios_addr);
void            proc_apply_pending_signals(struct proc *p);
void            proc_maybe_stop_current(void);
void            proc_check_alarms(uint current_ticks);
void            proc_set_alarm(struct proc *p, uint deadline_ticks);
int             proc_try_grow_stack(struct proc *p, uint fault_addr);
void            proc_tick_loadavg(void);
void            proc_get_loadavg(uint *la1, uint *la5, uint *la15);
void            proc_count_active(int *nrunning, int *ntotal);
void            proc_get_sched_stats(uint *passes, uint *idle_halts, uint *picks);
int             proc_deliver_signal(struct proc *p);
void            proc_handle_signals_on_return(struct proc *p);
struct cpu*     mycpu(void);
struct proc*    myproc();
void            pinit(void);
int             proc_has_cwd_on_dev(uint dev);
uint            proc_cwd_dev(void);
struct inode*   proc_cwd_idup(void);
int             proc_snapshot(struct procinfo_k *out, int max);
int             proc_fd_snapshot(struct procfdinfo_k *out, int max, int skip);
int             proc_fd_limits_snapshot(struct procfdlimitinfo_k *out, int max,
										int skip);
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
void            lockdep_set_rank(struct spinlock*, int, char*);
void            lockdep_enable(void);
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
int             strcmp(const char*, const char*);
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
int             sys_getsid(void);
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
int             sys_sigreturn(void);
int             sys_alarm(void);
int             sys_tcsetpgrp(void);
int             sys_tcgetpgrp(void);
int             sys_tcgetattr(void);
int             sys_tcsetattr(void);
int             sys_waitpid(void);
int             sys_rename(void);
int             sys_ext2fail(void);
int             sys_fsfault(void);
int             sys_loopsetup(void);
int             sys_loopteardown(void);
int             sys_loopstatus(void);
int             sys_select(void);
int             sys_poll(void);
int             sys_date(void);

// timer.c
void            timerinit(void);

// trap.c
void            idtinit(void);
extern uint     ticks;
void            tvinit(void);
extern struct spinlock tickslock;
typedef void (*irq_handler_t)(int irq, void *arg);
int             irq_register(int irq, irq_handler_t handler, void *arg, const char *name);
int             irq_unregister(int irq, const char *name);

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
int             copyin(pde_t*, void*, uint, uint);
void            clearpteu(pde_t *pgdir, char *uva);
void            setpteu(pde_t *pgdir, char *uva);
int             user_page_state(pde_t *pgdir, char *uva);
int             pte_is_cow(uint pte);
int             pte_is_writable(uint pte);
int             pte_is_user(uint pte);
void            pte_mark_cow(uint *pte);
void            pte_mark_writable(uint *pte);
void            pte_mark_user(uint *pte, int enabled);
int             uvm_release_pte(uint *pte);
int             cow_fault(pde_t *pgdir, uint va);

// socket.c
void            socket_init(void);
struct socket*  socket_alloc(void);
struct socket*  socket_ref(struct socket*);
void            socket_deref(struct socket*);
void            socket_close(struct socket*);
struct socket*  getfd_socket(int fd);
ushort          socket_alloc_port_locked(void);  // allocate ephemeral port (socket_lock held)
int             sys_socket(void);
int             sys_bind(void);
int             sys_connect(void);
int             sys_send(void);
int             sys_recv(void);
int             sys_recvtimeout(void);
int             sys_sendto(void);
int             sys_recvfrom(void);
int             sys_listen(void);
int             sys_accept(void);
int             sys_shutdown(void);
int             sys_getsockname(void);
int             sys_getpeername(void);
int             sys_netifinfo(void);
int             sys_routeinfo(void);
int             sys_arpinfo(void);
int             sys_routeadd(void);
int             sys_routedel(void);
int             sys_netifsetaddr(void);
int             sys_setsockopt(void);
int             sys_getsockopt(void);
int             socket_deliver(struct sockaddr_in*, struct sockaddr_in*, char*, uint);
int             socket_deliver_raw(uchar, struct sockaddr_in*, struct sockaddr_in*, char*, uint);
int             socket_stream_connect(struct socket*, struct sockaddr_in*);
void            socket_poll_events(struct socket*, int*, int*, int*);
int             ksock_open_udp(struct socket**);
int             ksock_sendto(struct socket*, struct sockaddr_in*, char*, uint);
int             ksock_recvfrom_timeout(struct socket*, char*, uint, int, struct sockaddr_in*);
int             socket_get_table(struct socket_info_k *out, int max);

// net device layer
void            netdev_init(void);
void            netdev_poll(void);
int             if_register(struct ifnet*);
struct ifnet*   if_get(char*);
struct ifnet*   if_byindex(uint);
struct ifnet*   if_first(void);
struct ifnet*   if_next(struct ifnet*);
int             if_dump(struct netif_info*, int);
int             if_output(struct ifnet*, struct mbuf*);
void            if_input(struct ifnet*, struct mbuf*);
int             if_set_addr(struct ifnet*, uint, uint);
int             if_set_addr_byindex(uint, uint, uint);
void            loopback_attach(void);
void            route_init(void);
int             route_add(uint, uint, uint, uint, struct ifnet*, uint);
int             route_delete(uint, uint, struct ifnet*);
struct ifnet*   route_lookup(uint, uint*, uint*);
int             route_dump(struct route_info*, int);
struct mbuf*    mbuf_alloc(void);
void            mbuf_free(struct mbuf*);
void            arp_init(void);
int             arp_resolve(struct ifnet*, uint, uchar*, struct mbuf*);
int             arp_dump(struct arp_info*, int);
void            arp_input(struct ifnet*, struct mbuf*);
int             ether_output(struct ifnet*, struct mbuf*, const uchar*, ushort);
int             ether_output_ip(struct ifnet*, struct mbuf*, uint);
void            ether_input(struct ifnet*, struct mbuf*);
int             ip_output(struct ifnet*, uchar, uint, uint, char*, uint);
void            ip_input(struct ifnet*, struct mbuf*);
int             udp_output(struct ifnet*, struct sockaddr_in*, struct sockaddr_in*, char*, uint);
void            udp_input(struct ifnet*, struct ip_hdr*, char*, uint);
void            icmp_input(struct ifnet*, struct ip_hdr*, char*, uint);
void            icmp_send_unreach(struct ifnet*, struct ip_hdr*, char*, uint, uchar);
int             tcp_connect(struct ifnet*, struct socket*, struct sockaddr_in*);
int             tcp_output(struct ifnet*, struct sockaddr_in*, struct sockaddr_in*, char*, uint);
int             tcp_send_ack(struct socket*);
void            tcp_input(struct ifnet*, struct ip_hdr*, char*, uint);
int             tcp_close(struct socket*, struct ifnet*);
int             tcp_retransmit_check(struct socket*, struct ifnet*);
void            tcp_timewait_check(struct socket*);
void            tcp_slowtimo(void);
void            virtio_net_init(void);
void            e1000_init(void);
void            i219_init(void);
void            i226_init(void);
void            ax88179_pci_init(void);
void            pcnet_init(void);
void            rtl8111_init(void);
void            rtl8125_init(void);
void            rtl8139_init(void);
void            tg3_init(void);
void            bnxt_init(void);
void            atlantic_init(void);
void            skge_init(void);
void            via_rhine_init(void);
void            igb_init(void);
void            ixgbe_init(void);
void            i40e_init(void);
void            ice_init(void);
void            bnx2_init(void);
void            bnx2x_init(void);
void            mlx4_en_init(void);
void            mlx5e_init(void);
void            ena_init(void);
void            alx_init(void);
void            vmxnet3_init(void);
void            netvsc_init(void);

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))
