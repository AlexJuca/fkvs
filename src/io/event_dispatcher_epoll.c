#ifdef __linux__
#include "../client.h"
#include "../commands/common/command_registry.h"
#include "../core/list.h"
#include "../networking/modes.h"
#include "../networking/networking.h"
#include "../server.h"
#include "../server_lifecycle.h"
#include "../ttl.h"
#include "../utils.h"
#include "event_dispatcher.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

static void close_and_drop_client(const int epfd, client_t *c)
{
    if (!c)
        return;

    if (server.verbose) {
        printf("Dropping client fd=%d (%s:%d)\n", c->fd, c->ip_str, c->port);
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, &ev);

    list_node_t *node = listFindNode(server.clients, NULL, c);
    if (node) {
        listDeleteNode(server.clients, node);
    }

    close(c->fd);
    server.num_clients -= 1;
    server.num_disconnected_clients += 1;
    update_disconnected_clients(&server.metrics,
                                server.num_disconnected_clients);
    free_client(c);
}

static int sync_client_write_interest(const int epfd, client_t *c)
{
    const bool want_write = c->wbuf_used > 0;
    if (c->write_registered == want_write)
        return 0;

    struct epoll_event cev;
    memset(&cev, 0, sizeof(cev));
    cev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if (want_write)
        cev.events |= EPOLLOUT;
    cev.data.ptr = c;

    if (epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &cev) == -1)
        return -1;

    c->write_registered = want_write;
    return 0;
}

int run_event_loop()
{
    set_nonblocking(server.fd);

    const int epfd = epoll_create1(0);
    if (epfd == -1) {
        perror("epoll_create1");
        return -1;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLET; // edge-triggered like EV_CLEAR
    ev.data.fd = server.fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, server.fd, &ev) == -1) {
        perror("epoll_ctl (server)");
        close(epfd);
        return -1;
    }

    // Create a 100ms timerfd for active key expiration sweep
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (tfd >= 0) {
        struct itimerspec its = {
            .it_interval = {0, 100000000}, // 100ms
            .it_value = {0, 100000000}     // 100ms
        };
        timerfd_settime(tfd, 0, &its, NULL);

        struct epoll_event tev;
        memset(&tev, 0, sizeof(tev));
        tev.events = EPOLLIN;
        tev.data.fd = tfd;
        epoll_ctl(epfd, EPOLL_CTL_ADD, tfd, &tev);
    }

    const int max_evs =
        server.event_loop_max_events > 1024 ? 1024 : server.event_loop_max_events;
    struct epoll_event events[max_evs];

    while (!server_shutdown_requested()) {
        const int n = epoll_wait(epfd, events, max_evs, -1);
        if (n < 0) {
            if (errno == EINTR && !server_shutdown_requested())
                continue;
            if (errno == EINTR && server_shutdown_requested())
                break;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; i++) {
            const uint32_t evt = events[i].events;

            // Timer event for active expiration sweep
            if (tfd >= 0 && events[i].data.fd == tfd) {
                uint64_t expirations = 0;
                for (;;) {
                    const ssize_t nread =
                        read(tfd, &expirations, sizeof(expirations));
                    if (nread == (ssize_t)sizeof(expirations)) {
                        expire_sweep(server.database->store,
                                     server.database->expires, 20);
                        break;
                    }
                    if (nread < 0 && errno == EINTR)
                        continue;
                    if (nread < 0 &&
                        (errno == EAGAIN || errno == EWOULDBLOCK))
                        break;
                    if (nread < 0)
                        perror("timerfd read");
                    break;
                }
                continue;
            }

            // New connections on the listening socket
            if (events[i].data.fd == server.fd) {
                for (;;) {
                    struct sockaddr_storage ss;
                    socklen_t slen = sizeof(ss);
                    const int cfd =
                        accept(server.fd, (struct sockaddr *)&ss, &slen);
                    if (cfd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        perror("accept");
                        break;
                    }

                    set_nonblocking(cfd);
                    if (server.socket_domain == TCP_IP) {
                        set_tcp_no_delay(cfd);
                    }

                    client_t *c = init_client(cfd, ss, server.socket_domain);
                    if (!c) {
                        // already closed by init_client on failure
                        continue;
                    }

                    server.clients = listAddNodeToTail(server.clients, c);
                    server.num_clients += 1;

                    if (server.verbose) {
                        printf("Client connected fd=%d %s:%d (total=%d)\n",
                               c->fd, c->ip_str, c->port,
                               (int)server.num_clients);
                    }

                    struct epoll_event cev;
                    memset(&cev, 0, sizeof(cev));
                    // EPOLLRDHUP to detect peer half-close; EPOLLHUP/ERR also
                    // handled
                    cev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
                    cev.data.ptr = c; // stash client*
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, c->fd, &cev) == -1) {
                        perror("epoll_ctl add client");
                        close_and_drop_client(epfd, c);
                    }
                }
                continue;
            }

            // Existing client events
            client_t *c = (client_t *)events[i].data.ptr;

            // If somehow ptr is missing, try to find by fd (defensive)
            if (!c && (evt & (EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP))) {
                // We don't have ident fd here unless we store it; skip this
                // path in practice. Could be extended by keeping fd in data.u32
                // and mapping to client.
                continue;
            }

            // Hangup/half-close/errors
            if ((evt & EPOLLRDHUP) || (evt & EPOLLHUP) || (evt & EPOLLERR)) {
                if (server.verbose) {
                    printf("Client fd=%d closed (EPOLL flags=0x%x)\n",
                           c ? c->fd : -1, evt);
                }
                close_and_drop_client(epfd, c);
                for (int j = i + 1; j < n; j++) {
                    if (events[j].data.ptr == c)
                        events[j].data.ptr = NULL;
                }
                continue;
            }

            if (evt & EPOLLOUT) {
                wbuf_flush(c);
                if (c->write_failed ||
                    sync_client_write_interest(epfd, c) == -1) {
                    close_and_drop_client(epfd, c);
                    for (int j = i + 1; j < n; j++) {
                        if (events[j].data.ptr == c)
                            events[j].data.ptr = NULL;
                    }
                    continue;
                }
            }

            // Drain readable data (edge-triggered)
            if (evt & EPOLLIN) {
                for (;;) {
                    ssize_t nread = recv(c->fd, c->buffer + c->buf_used,
                                         sizeof(c->buffer) - c->buf_used, 0);
                    if (nread > 0) {
                        c->buf_used += (size_t)nread;
                        if (server.verbose) {
                            printf("fd=%d read %zd bytes (buf_used=%zu)\n",
                                   c->fd, nread, c->buf_used);
                        }

                        // Process as many complete frames as possible
                        if (try_process_frames(c) < 0) {
                            close_and_drop_client(epfd, c);
                            for (int j = i + 1; j < n; j++) {
                                if (events[j].data.ptr == c)
                                    events[j].data.ptr = NULL;
                            }
                            break;
                        }

                        // If buffer is full but frame needs more → protocol
                        // error
                        if (c->buf_used == sizeof(c->buffer) &&
                            c->frame_need > 0 &&
                            (ssize_t)c->buf_used < c->frame_need) {
                            fprintf(stderr,
                                    "fd=%d frame exceeds buffer capacity; "
                                    "dropping client\n",
                                    c->fd);
                            close_and_drop_client(epfd, c);
                            for (int j = i + 1; j < n; j++) {
                                if (events[j].data.ptr == c)
                                    events[j].data.ptr = NULL;
                            }
                            break;
                        }

                        // keep draining in this readiness window
                        continue;
                    }

                    if (nread == 0) {
                        if (server.verbose) {
                            printf("Client fd=%d closed (recv=0)\n", c->fd);
                        }
                        close_and_drop_client(epfd, c);
                        for (int j = i + 1; j < n; j++) {
                            if (events[j].data.ptr == c)
                                events[j].data.ptr = NULL;
                        }
                        break;
                    }

                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // fully drained for now
                        if (sync_client_write_interest(epfd, c) == -1) {
                            close_and_drop_client(epfd, c);
                            for (int j = i + 1; j < n; j++) {
                                if (events[j].data.ptr == c)
                                    events[j].data.ptr = NULL;
                            }
                        }
                        break;
                    }
                    if (errno == EINTR) {
                        // retry
                        continue;
                    }

                    perror("recv");
                    close_and_drop_client(epfd, c);
                    for (int j = i + 1; j < n; j++) {
                        if (events[j].data.ptr == c)
                            events[j].data.ptr = NULL;
                    }
                    break;
                }
            }
        }
    }

    if (tfd >= 0)
        close(tfd);
    close(epfd);
    return 0;
}

#endif
