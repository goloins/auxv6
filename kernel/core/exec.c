#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "stat.h"
#include "x86.h"
#include "elf.h"
#include "fs.h"
#include "vfs.h"

#define EXEC_SHEBANG_LINE_MAX 128
#define EXEC_SHEBANG_MAX_DEPTH 2

static int
is_shebang_space(char c)
{
  return c == ' ' || c == '\t' || c == '\r';
}

// Returns 1 when a valid shebang line is parsed, 0 when there is no shebang,
// and -1 for malformed shebangs.
static int
parse_shebang(const char *line, int n, char *interp, uint interp_sz,
              char *interp_arg, uint interp_arg_sz, int *has_interp_arg)
{
  int i, j;

  if(n < 2 || line[0] != '#' || line[1] != '!')
    return 0;

  i = 2;
  while(i < n && is_shebang_space(line[i]))
    i++;
  if(i >= n || line[i] == '\n' || line[i] == 0)
    return -1;

  for(j = 0; i < n && line[i] != 0 && line[i] != '\n' &&
           !is_shebang_space(line[i]); i++){
    if((uint)(j + 1) >= interp_sz)
      return -1;
    interp[j++] = line[i];
  }
  if(j == 0)
    return -1;
  interp[j] = 0;

  while(i < n && is_shebang_space(line[i]))
    i++;
  if(i >= n || line[i] == 0 || line[i] == '\n'){
    *has_interp_arg = 0;
    return 1;
  }

  for(j = 0; i < n && line[i] != 0 && line[i] != '\n' &&
           !is_shebang_space(line[i]); i++){
    if((uint)(j + 1) >= interp_arg_sz)
      return -1;
    interp_arg[j++] = line[i];
  }
  if(j == 0)
    return -1;
  interp_arg[j] = 0;

  while(i < n && is_shebang_space(line[i]))
    i++;
  if(i < n && line[i] != 0 && line[i] != '\n')
    return -1;

  *has_interp_arg = 1;
  return 1;
}

int
exec_internal(char *path, char **argv, int depth)
{
  char *s, *last;
  int i, off;
  int (*read_fn)(struct inode*, char*, uint64_t, uint);
  int (*access_fn)(struct inode*, int);
  const struct vnode_ops *ops;
  uint argc, sz, sp;
  uint arg_bytes;
  uint *ustack;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  char shebang_line[EXEC_SHEBANG_LINE_MAX];
  char interp[EXEC_SHEBANG_LINE_MAX];
  char interp_arg[EXEC_SHEBANG_LINE_MAX];
  char *script_argv[EXEC_ARGC_MAX+3];
  pde_t *pgdir, *oldpgdir;
  struct proc *curproc = myproc();

#if ((3 + EXEC_ARGC_MAX + 1) * 4 > PGSIZE)
#error "exec ustack staging exceeds one page; lower EXEC_ARGC_MAX or refactor staging"
#endif

  begin_op();

  if((ip = vfs_namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  ops = vfs_dev_vops(inode_get_dev(ip));
  read_fn = readi;
  access_fn = iaccess;
  if(ops && ops->read)
    read_fn = ops->read;
  if(ops && ops->access)
    access_fn = ops->access;
  pgdir = 0;
  ustack = 0;

  if(access_fn(ip, IACC_EXEC) < 0)
    goto bad;

  // Check ELF header
  if(read_fn(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC){
    int shebang_n, shebang_rc, has_interp_arg, aidx, argi;

    if(depth >= EXEC_SHEBANG_MAX_DEPTH)
      goto bad;

    memset(shebang_line, 0, sizeof(shebang_line));
    shebang_n = read_fn(ip, shebang_line, 0, sizeof(shebang_line) - 1);
    if(shebang_n <= 0)
      goto bad;
    shebang_line[shebang_n] = 0;

    shebang_rc = parse_shebang(shebang_line, shebang_n, interp, sizeof(interp),
                               interp_arg, sizeof(interp_arg), &has_interp_arg);
    if(shebang_rc <= 0)
      goto bad;

    aidx = 0;
    script_argv[aidx++] = interp;
    if(has_interp_arg)
      script_argv[aidx++] = interp_arg;
    script_argv[aidx++] = path;
    for(argi = 1; argv[argi]; argi++){
      if(aidx >= EXEC_ARGC_MAX)
        goto bad;
      script_argv[aidx++] = argv[argi];
    }
    script_argv[aidx] = 0;

    iunlockput(ip);
    end_op();
    ip = 0;
    return exec_internal(interp, script_argv, depth + 1);
  }

  if((pgdir = setupkvm()) == 0)
    goto bad;

  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(read_fn(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  // Allocate guard + stack pages at the next page boundary.
  // Pre-allocate the full USER_STACK_MAX_PAGES to support demand-growth.
  // All pages except the initial USER_STACK_PAGES usable pages at the top
  // are marked inaccessible via clearpteu; the page-fault handler makes them
  // accessible one at a time as the stack grows downward.
  {
    uint stack_total = (USER_STACK_GUARD_PAGES + USER_STACK_MAX_PAGES) * PGSIZE;
    uint stack_base;
    int g;

    sz = PGROUNDUP(sz);
    if((sz = allocuvm(pgdir, sz, sz + stack_total)) == 0)
      goto bad;

    stack_base = sz - stack_total;
    // Guard page(s) plus the not-yet-usable headroom below the initial stack
    // are all marked inaccessible.  They become user-accessible one page at a
    // time as proc_try_grow_stack() services page faults.
    for(g = 0; g < USER_STACK_GUARD_PAGES + (USER_STACK_MAX_PAGES - USER_STACK_PAGES); g++)
      clearpteu(pgdir, (char*)(stack_base + g * PGSIZE));

    EXECDBG("exec: %s stack guard=%d pages stack=%d pages max=%d pages total=%d bytes\n",
            path, USER_STACK_GUARD_PAGES, USER_STACK_PAGES,
            USER_STACK_MAX_PAGES, stack_total);
  }
  sp = sz;

  // Stage argv pointers in a single page to keep kernel stack bounded even
  // when EXEC_ARGC_MAX increases over time.
  ustack = (uint*)kalloc();
  if(ustack == 0)
    goto bad;

  // Push argument strings, prepare rest of stack in ustack.
  arg_bytes = 0;
  for(argc = 0; argv[argc]; argc++) {
    uint arglen;

    if(argc >= EXEC_ARGC_MAX)
      goto bad;
    arglen = strlen(argv[argc]) + 1;
    if(arg_bytes + arglen < arg_bytes || arg_bytes + arglen > EXEC_ARG_BYTES_MAX)
      goto bad;
    arg_bytes += arglen;
    sp = (sp - arglen) & ~3;
    if(copyout(pgdir, sp, argv[argc], arglen) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  kfree((char*)ustack);
  ustack = 0;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  // Commit to the user image.
  oldpgdir = curproc->pgdir;
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->stack_top = sz;
  curproc->stack_bot = sz - USER_STACK_PAGES * PGSIZE;
  curproc->tf->eip = elf.entry;  // main
  curproc->tf->esp = sp;
  switchuvm(curproc);
  freevm(oldpgdir);
  return 0;

 bad:
  if(ustack)
    kfree((char*)ustack);
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}

int
exec(char *path, char **argv)
{
  return exec_internal(path, argv, 0);
}
