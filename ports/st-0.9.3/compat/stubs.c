/* AUXV6_ST_HACK: minimal libc/termios stubs needed by st while
 * auxv6 userland compatibility catches up.
 */

int
tcsendbreak(int fd, int duration)
{
	(void)fd;
	(void)duration;
	return 0;
}
