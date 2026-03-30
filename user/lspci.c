/*
 * lspci - list PCI devices
 *
 * Reads /proc/pci to display PCI devices enumerated at boot.
 * Output format: BB:SS.F VENDOR:DEVICE CLASS:SUBCLASS IRQ
 */

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int
main(int argc, char *argv[])
{
    int fd;
    char buf[2048];
    int n;

    (void)argc;
    (void)argv;

    fd = open("/proc/pci", O_RDONLY);
    if (fd < 0) {
        printf(2, "lspci: cannot open /proc/pci\n");
        printf(2, "hint: mount -t procfs proc /proc\n");
        exit();
    }

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, n);
    }

    close(fd);
    exit();
}
