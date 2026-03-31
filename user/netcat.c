#include "../include/types.h"
#include "../include/user.h"
#include "../include/socket.h"

#define MAXBUF 1024

static void
usage(void)
{
    printf(2, "usage: netcat [-u] host port\n");
    printf(2, "       netcat -l [-u] [host] port\n");
    exit();
}

static int
parse_port(const char *s)
{
    int p;

    if(s == 0)
        return -1;
    p = atoi(s);
    if(p < 1 || p > 65535)
        return -1;
    return p;
}

static int
resolve_host(const char *name, uint *out)
{
    if(name == 0 || out == 0)
        return -1;
    return resolve_ipv4(name, out);
}

static void
relay_stdio_socket(int fd)
{
    int pid;
    int n;
    char buf[MAXBUF];

    pid = fork();
    if(pid < 0) {
        printf(2, "netcat: fork failed\n");
        return;
    }

    if(pid == 0) {
        while((n = read(0, buf, sizeof(buf))) > 0) {
            if(send(fd, buf, n) < 0)
                break;
        }
        close(fd);
        exit();
    }

    while((n = recv(fd, buf, sizeof(buf))) > 0) {
        if(write(1, buf, n) != n)
            break;
    }

    kill(pid, SIGTERM);
    wait();
}

static int
open_client_socket(int type, const char *host, int port)
{
    int fd;
    uint ip;
    struct sockaddr_in dst;

    if(resolve_host(host, &ip) < 0) {
        printf(2, "netcat: cannot resolve host %s\n", host);
        return -1;
    }

    fd = socket(AF_INET, type, 0);
    if(fd < 0) {
        printf(2, "netcat: socket failed\n");
        return -1;
    }

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = (ushort)port;
    dst.sin_addr = ip;

    if(connect(fd, &dst, sizeof(dst)) < 0) {
        printf(2, "netcat: connect failed\n");
        close(fd);
        return -1;
    }

    return fd;
}

static int
open_server_socket(int type, const char *host, int port)
{
    int fd;
    uint ip;
    struct sockaddr_in src;

    ip = INADDR_ANY;
    if(host && host[0]) {
        if(resolve_host(host, &ip) < 0) {
            printf(2, "netcat: cannot resolve host %s\n", host);
            return -1;
        }
    }

    fd = socket(AF_INET, type, 0);
    if(fd < 0) {
        printf(2, "netcat: socket failed\n");
        return -1;
    }

    memset(&src, 0, sizeof(src));
    src.sin_family = AF_INET;
    src.sin_port = (ushort)port;
    src.sin_addr = ip;

    if(bind(fd, &src, sizeof(src)) < 0) {
        printf(2, "netcat: bind failed\n");
        close(fd);
        return -1;
    }

    if(type == SOCK_STREAM && listen(fd, 1) < 0) {
        printf(2, "netcat: listen failed\n");
        close(fd);
        return -1;
    }

    return fd;
}

int
main(int argc, char **argv)
{
    int i;
    int mode_listen;
    int socktype;
    int port;
    int fd;
    char *host;
    int n;
    char buf[MAXBUF];

    mode_listen = 0;
    socktype = SOCK_STREAM;

    i = 1;
    while(i < argc && argv[i][0] == '-') {
        if(strcmp(argv[i], "-l") == 0) {
            mode_listen = 1;
        } else if(strcmp(argv[i], "-u") == 0) {
            socktype = SOCK_DGRAM;
        } else {
            usage();
        }
        i++;
    }

    if(!mode_listen) {
        if(i + 2 != argc)
            usage();

        host = argv[i];
        port = parse_port(argv[i + 1]);
        if(port < 0) {
            printf(2, "netcat: invalid port\n");
            exit();
        }

        fd = open_client_socket(socktype, host, port);
        if(fd < 0)
            exit();

        relay_stdio_socket(fd);
        close(fd);
        exit();
    }

    if(i + 1 == argc) {
        host = 0;
        port = parse_port(argv[i]);
    } else if(i + 2 == argc) {
        host = argv[i];
        port = parse_port(argv[i + 1]);
    } else {
        usage();
        port = -1;
    }

    if(port < 0) {
        printf(2, "netcat: invalid port\n");
        exit();
    }

    fd = open_server_socket(socktype, host, port);
    if(fd < 0)
        exit();

    if(socktype == SOCK_STREAM) {
        int cfd;
        cfd = accept(fd);
        if(cfd < 0) {
            printf(2, "netcat: accept failed\n");
            close(fd);
            exit();
        }
        relay_stdio_socket(cfd);
        close(cfd);
    } else {
        while((n = recv(fd, buf, sizeof(buf))) > 0) {
            if(write(1, buf, n) != n)
                break;
        }
    }

    close(fd);
    exit();
}
