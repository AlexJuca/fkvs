#ifdef __APPLE__

#include "../client.h"
#include "../commands/common/command_registry.h"
#include "../core/list.h"
#include "../networking/networking.h"
#include "../server.h"
#include "../server_lifecycle.h"
#include "../ttl.h"
#include "../utils.h"
#include "event_dispatcher.h"
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static void close_and_drop_client(const int kq, client_t *c)
{
    if (!c)
        return;

    if (server.verbose) {
        printf("Dropping client fd=%d (%s:%d)\n", c->fd, c->ip_str, c->port);
    }

    // deregister from kqueue
    struct kevent ch;
    EV_SET(&ch, c->fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    (void)kevent(kq, &ch, 1, NULL, 0, NULL);

    // remove from the server list
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

static int sync_client_write_interest(const int kq, client_t *c)
{
    const bool want_write = c->wbuf_used > 0;
    if (c->write_registered == want_write)
        return 0;

    struct kevent ch;
    EV_SET(&ch, c->fd, EVFILT_WRITE,
           want_write ? (EV_ADD | EV_ENABLE) : EV_DELETE, 0, 0, c);

    if (kevent(kq, &ch, 1, NULL, 0, NULL) == -1) {
        if (!want_write && errno == ENOENT) {
            c->write_registered = false;
            return 0;
        }
        return -1;
    }

    c->write_registered = want_write;
    return 0;
}

int run_event_loop()
{
    set_nonblocking(server.fd);

    const int kq = kqueue();
    if (kq == -1) {
        perror("kqueue");
        return -1;
    }

    struct kevent ch;
    EV_SET(&ch, server.fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0,
           NULL);
    if (kevent(kq, &ch, 1, NULL, 0, NULL) == -1) {
        perror("kevent register (server)");
        close(kq);
        return -1;
    }

    // Register a 100ms timer for active key expiration sweep
    // EVFILT_TIMER default unit is milliseconds on macOS
    struct kevent timer_ev;
    EV_SET(&timer_ev, 1, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, 100, NULL);
    if (kevent(kq, &timer_ev, 1, NULL, 0, NULL) == -1) {
        perror("kevent register (timer)");
    }

    const int max_evs =
        server.event_loop_max_events > 1024 ? 1024 : server.event_loop_max_events;
    struct kevent evs[max_evs];

    while (!server_shutdown_requested()) {
        const int n = kevent(kq, NULL, 0, evs, max_evs, NULL);
        if (n < 0) {
            if (errno == EINTR && !server_shutdown_requested())
                continue;
            if (errno == EINTR && server_shutdown_requested())
                break;
            perror("kevent wait");
            break;
        }

        // We have new events
        for (int i = 0; i < n; i++) {
            const int ident_fd = (int)evs[i].ident;

            // Timer event for active expiration sweep
            if (evs[i].filter == EVFILT_TIMER) {
                expire_sweep(server.database->store, server.database->expires,
                             20);
                continue;
            }

            // New connection on the server listening socket.
            if (ident_fd == server.fd) {
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
                        continue;
                    }

                    server.clients = listAddNodeToTail(server.clients, c);
                    server.num_clients += 1;

                    if (server.verbose) {
                        printf("Client connected fd=%d %s:%d (total=%d)\n",
                               c->fd, c->ip_str, c->port,
                               (int)server.num_clients);
                    }

                    // Register the client fd with kqueue and stash client* in
                    // udata
                    EV_SET(&ch, c->fd, EVFILT_READ,
                           EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, c);

                    if (kevent(kq, &ch, 1, NULL, 0, NULL) == -1) {
                        perror("kevent add client");
                        close_and_drop_client(kq, c);
                    }
                }
                continue;
            }

            // Existing client readable or closed.
            client_t *c = (client_t *)evs[i].udata;

            // Fallback: if udata is missing, find by fd (kept for
            // compatibility).
            if (!c) {
                const list_node_t *node =
                    listFindNodeByFd(server.clients, ident_fd);
                c = node ? (client_t *)node->val : NULL;
                if (!c) {
                    // Unknown fd; close it defensively.
                    close(ident_fd);
                    continue;
                }
            }

            if (evs[i].filter == EVFILT_WRITE) {
                wbuf_flush(c);
                if (c->write_failed ||
                    sync_client_write_interest(kq, c) == -1) {
                    close_and_drop_client(kq, c);
                    for (int j = i + 1; j < n; j++) {
                        if (evs[j].udata == c)
                            evs[j].udata = NULL;
                    }
                }
                continue;
            }

            // Did peer hangup?
            if (evs[i].flags & EV_EOF) {
                if (server.verbose) {
                    printf("Client fd=%d closed (EV_EOF)\n", c->fd);
                }
                close_and_drop_client(kq, c);
                // Invalidate stale events referencing the freed client
                for (int j = i + 1; j < n; j++) {
                    if (evs[j].udata == c)
                        evs[j].udata = NULL;
                }
                continue;
            }

            // Drain socket data (non-blocking)
            for (;;) {
                ssize_t nread = recv(c->fd, c->buffer + c->buf_used,
                                     sizeof(c->buffer) - c->buf_used, 0);
                if (nread > 0) {
                    c->buf_used += (size_t)nread;
                    if (server.verbose) {
                        printf("fd=%d read %zd bytes (buf_used=%zu)\n", c->fd,
                               nread, c->buf_used);
                    }

                    // Process all complete frames currently in buffer
                    if (try_process_frames(c) < 0) {
                        close_and_drop_client(kq, c);
                        for (int j = i + 1; j < n; j++) {
                            if (evs[j].udata == c)
                                evs[j].udata = NULL;
                        }
                        break;
                    }

                    // If the buffer is full, but we still need more for a frame
                    // → protocol error.
                    if (c->buf_used == sizeof(c->buffer) && c->frame_need > 0 &&
                        (ssize_t)c->buf_used < c->frame_need) {
                        fprintf(stderr,
                                "fd=%d frame exceeds buffer capacity; dropping "
                                "client\n",
                                c->fd);
                        close_and_drop_client(kq, c);
                        for (int j = i + 1; j < n; j++) {
                            if (evs[j].udata == c)
                                evs[j].udata = NULL;
                        }
                        break;
                    }

                    // Try to read again in this readiness window
                    continue;
                }

                if (nread == 0) {
                    if (server.verbose) {
                        printf("Client fd=%d closed (recv=0)\n", c->fd);
                    }
                    close_and_drop_client(kq, c);
                    for (int j = i + 1; j < n; j++) {
                        if (evs[j].udata == c)
                            evs[j].udata = NULL;
                    }
                    break;
                }

                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // No more data for now
                    if (sync_client_write_interest(kq, c) == -1) {
                        close_and_drop_client(kq, c);
                        for (int j = i + 1; j < n; j++) {
                            if (evs[j].udata == c)
                                evs[j].udata = NULL;
                        }
                    }
                    break;
                }
                if (errno == EINTR) {
                    // Interrupted — retry
                    continue;
                }

                perror("recv");
                close_and_drop_client(kq, c);
                for (int j = i + 1; j < n; j++) {
                    if (evs[j].udata == c)
                        evs[j].udata = NULL;
                }
                break;
            }
        }
    }

    close(kq);
    return 0;
}

#endif
