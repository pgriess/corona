// Benchmark for straight libev I/O loop.

#include <ev.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

typedef struct sock_watcher_s {
    struct ev_io sw_ev_io;
    int sw_sock;
} sock_watcher_t;

static char buf[(1 << 20)];

// stats
errno_cnt_t read_errors_cnt;
errno_cnt_t accept_errors_cnt;
errno_cnt_t fcntl_errors_cnt;

static void sigint_cb(int sig) {
    printf("\n");
    errno_cnt_dump(stdout, "accept", &accept_errors_cnt);
    errno_cnt_dump(stdout, "read", &read_errors_cnt);
    errno_cnt_dump(stdout, "fcntl", &fcntl_errors_cnt);

    exit(0);
}

static void read_watcher_cb(struct ev_loop *el, ev_io *ew, int revents) {
    sock_watcher_t *sw = (sock_watcher_t*) ew;
    int nbytes = 0;

    if (!(revents & EV_READ)) {
        return;
    }

    do {
        nbytes = read(sw->sw_sock, buf, sizeof(buf));
        errno_cnt_incr(&read_errors_cnt, nbytes);
        DBG(1, ("read %d bytes from socket %d\n", nbytes, sw->sw_sock));
    } while (nbytes > 0);

    if (nbytes == 0 ||
        (nbytes < 0 && errno != EAGAIN)) {
        DBG(
            0,
            ("read %d bytes from socket; errno %d\n",
                nbytes, (nbytes < 0) ? errno : 0)
        );

        ev_io_stop(el, &sw->sw_ev_io);
        close(sw->sw_sock);
    }
}

static void accept_watcher_cb(struct ev_loop *el, ev_io *ew, int revents) {
    sock_watcher_t *sw = (sock_watcher_t*) ew;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int sock;
    int err;
    sock_watcher_t *sw2;

    if (!(revents & EV_READ)) {
        return;
    }

    sock = accept(sw->sw_sock, (struct sockaddr*) &addr, &addrlen);
    errno_cnt_incr(&accept_errors_cnt, sock);
    if (sock < 0) {
        return;
    }
   
    err = fcntl(sock, F_SETFL, O_NONBLOCK);
    errno_cnt_incr(&fcntl_errors_cnt, err);
    if (err < 0) {
        close(sock);
        return;
    }

    DBG(0, ("acepted socket %d\n", sock));
    
    sw2 = (sock_watcher_t*) malloc(sizeof(*sw2));

    sw2->sw_sock = sock;
    ev_init(&sw2->sw_ev_io, read_watcher_cb);
    ev_io_set(&sw2->sw_ev_io, sock, EV_READ);
    ev_io_start(el, &sw2->sw_ev_io);
}

void usage(FILE *fp, char *name) {
    fprintf(fp,
"usage: %s [options] <hostname> <port>\n\n", name);
    fprintf(fp,
"Run the TCP benchmarking server, listening on the given hostname and port.\n");
    fprintf(fp,
"\nOptions:\n");
    fprintf(fp,
"  -h                help\n");
    fprintf(fp,
"  -v                be verbose; use multiple times for increased verbosity\n");
}

int main(int argc, char **argv) {
    int sock;
    struct sockaddr_in addr;
    struct ev_loop *el;
    sock_watcher_t sw;
    int c;
    struct hostent *hent;

    optreset = optind = 1;
    while ((c = getopt(argc, argv, "hv")) != -1) {
        switch (c) {
        case 'h':
            usage(stdout, argv[0]);
            return 0;

        case 'v':
            dbg_level++;
            break;

        case '?':
            fprintf(stderr, "%s: unknown option %c\n", argv[0], optopt);
            return 1;
        }
    }

    if ((argc - optind) != 2) {
        usage(stderr, argv[0]);
        return 1;
    }

    if (!(hent = gethostbyname(argv[optind]))) {
        fprintf(stderr, "%s: could not resolve host name\n", argv[0]);
        return 1;
    }

    bzero(&addr, sizeof(addr));
    memcpy(&addr.sin_addr, hent->h_addr, sizeof(addr.sin_addr));
    addr.sin_port = htons(strtoul(argv[optind + 1], NULL, 10));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;

    // Set up a listening socket

    if ((sock = socket(PF_INET, SOCK_STREAM, 6 /* TCP */)) < 0) {
        perror("socket");
        return 1;
    }

    if (fcntl(sock, F_SETFL, O_NONBLOCK)) {
        perror("fcntl(O_NONBLOCK)");
        close(sock);
        return 1;
    }

    if (bind(sock, (struct sockaddr*) &addr, sizeof(addr))) {
        perror("bind");
        close(sock);
        return 1;
    }

    if (listen(sock, 64)) {
        perror("listen");
        close(sock);
        return 1;
    }

    signal(SIGINT, sigint_cb);

    el = ev_default_loop(EVFLAG_AUTO);

    sw.sw_sock = sock;
    ev_init(&sw.sw_ev_io, accept_watcher_cb);
    ev_io_set(&sw.sw_ev_io, sock, EV_READ);
    ev_io_start(el, &sw.sw_ev_io);

    ev_loop(el, 0);
    ev_default_destroy();

    return 0;
}
