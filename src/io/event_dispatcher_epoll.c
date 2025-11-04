#ifdef __linux__
#include "../client.h"
#include "../networking/modes.h"
#include "event_dispatcher.h"
#include "../core/list.h"
#include "../networking/networking.h"
#include "../server.h"
#include "../utils.h"

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

    list_node_t *node =
        listFindNode(server.clients, NULL, (void *)(intptr_t)c->fd);
    if (node) {
        listDeleteNode(server.clients, node);
        free(node->val); // free(client_t) allocated for list storage if any
        server.num_clients -= 1;
    }

    close(c->fd);
    free(c);
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

    struct epoll_event events[MAX_EVENTS];

    for (;;) {
        const int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; i++) {
            const uint32_t evt = events[i].events;

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
                continue;
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
                        try_process_frames(c);

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
                        break;
                    }

                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // fully drained for now
                        break;
                    }
                    if (errno == EINTR) {
                        // retry
                        continue;
                    }

                    perror("recv");
                    close_and_drop_client(epfd, c);
                    break;
                }
            }
        }
    }

    close(epfd);
    return 0;
}

#endif
