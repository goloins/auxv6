/*
 * user/setjmp.S — setjmp / longjmp for auxv6 i386
 *
 * jmp_buf layout (6 ints):
 *   [0] = ebx   [1] = esi   [2] = edi
 *   [3] = ebp   [4] = esp   [5] = eip  (return addr of setjmp's caller)
 */
.text

/*
 * int setjmp(jmp_buf env)
 *   Saves callee-saved registers + stack state into env[].
 *   Returns 0 to the direct caller.
 */
.globl setjmp
.type  setjmp, @function
setjmp:
    movl 4(%esp), %eax          /* eax = env (first arg) */
    movl %ebx,  0(%eax)
    movl %esi,  4(%eax)
    movl %edi,  8(%eax)
    movl %ebp, 12(%eax)
    movl %esp, 16(%eax)         /* save esp as it is inside setjmp (points to ret addr) */
    movl (%esp), %ecx           /* ecx = return address (eip to restore) */
    movl %ecx, 20(%eax)
    xorl %eax, %eax             /* return 0 */
    ret

/*
 * void longjmp(jmp_buf env, int val)   noreturn
 *   Restores register state from env[], then returns val (or 1 if val==0)
 *   to setjmp's original caller.
 */
.globl longjmp
.type  longjmp, @function
longjmp:
    movl 4(%esp), %eax          /* eax = env */
    movl 8(%esp), %ecx          /* ecx = val */

    /* Restore callee-saved registers */
    movl  0(%eax), %ebx
    movl  4(%eax), %esi
    movl  8(%eax), %edi
    movl 12(%eax), %ebp

    /* Restore stack pointer: (%esp) will be setjmp's saved return address */
    movl 16(%eax), %esp

    /* Map val==0 to 1 (setjmp must not see 0 on longjmp resume) */
    testl %ecx, %ecx
    jnz   1f
    movl  $1, %ecx
1:  movl %ecx, %eax

    /* ret pops the saved eip and jumps there, restoring esp correctly */
    ret
