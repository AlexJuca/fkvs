#ifdef __APPLE__

#include "client.h"
#include "command_registry.h"
#include "event_dispatcher.h"
#include "list.h"
#include "server.h"
#include "utils.h"

#include <arpa/inet.h>
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

extern server_t server;

#define MAX_EVENTS 4096

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) flags = 0;
    (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void set_tcp_no_delay(int fd) {
    const int one = 1;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

static void close_and_drop_client(int kq, client_t *c) {
    if (!c) return;

    if (server.verbose) {
        printf("Dropping client fd=%d (%s:%d)\n", c->fd, c->ip_str, c->port);
    }

    // deregister from kqueue
    struct kevent ch;
    EV_SET(&ch, c->fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    (void)kevent(kq, &ch, 1, NULL, 0, NULL);

    // remove from the server list
    list_node_t *node = listFindNode(server.clients, NULL, (void *)(intptr_t)c->fd);
    if (node) {
        listDeleteNode(server.clients, node);
        free(node->val); // free(client_t)
        server.numClients -= 1;
    }

    close(c->fd);
}

static void try_process_frames(client_t *c) {
    // Parse as many complete frames as possible.
    printf("Attempting to process frames");
    for (;;) {
        if (c->buf_used < 2) return; // need length prefix

        if (c->frame_need < 0) {
            uint16_t core_len = ((uint16_t)c->buffer[0] << 8) | c->buffer[1];
            c->frame_need = 2 + (ssize_t)core_len; // total frame bytes (prefix + core)
            if ((size_t)c->frame_need > sizeof(c->buffer)) {
                fprintf(stderr, "Frame too large: %zd > %zu\n",
                        c->frame_need, sizeof(c->buffer));
                // Drop the buffer contents to resync; caller should disconnect the client.
                c->buf_used = 0;
                c->frame_need = -1;
                return;
            }
        }

        if ((ssize_t)c->buf_used < c->frame_need)
            return; // incomplete frame; we wait for more data

        // We have a complete frame.
        const size_t frame_len = (size_t)c->frame_need;

        if (server.verbose) {
            printf("Complete frame (%zu bytes) from fd=%d\n", frame_len, c->fd);
            print_binary_data(c->buffer, frame_len);
        }

        // Dispatch exactly one frame.
        dispatch_command(c->fd, c->buffer, frame_len);

        // Shift any remaining bytes (back-to-back frames).
        size_t remain = c->buf_used - frame_len;
        if (remain) memmove(c->buffer, c->buffer + frame_len, remain);
        c->buf_used = remain;
        c->frame_need = -1; // recompute for next frame
    }
}

int run_event_loop(int server_fd) {
    if (server.verbose) {
        printf("Connected clients: %d\n", (int)server.numClients);
    }

    set_nonblocking(server_fd);

    const int kq = kqueue();
    if (kq == -1) {
        perror("kqueue");
        return -1;
    }

    struct kevent ch;
    EV_SET(&ch, server_fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, NULL);
    if (kevent(kq, &ch, 1, NULL, 0, NULL) == -1) {
        perror("kevent register (server)");
        close(kq);
        return -1;
    }

    struct kevent evs[MAX_EVENTS];

    for (;;) {
        int n = kevent(kq, NULL, 0, evs, MAX_EVENTS, NULL);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("kevent wait");
            break;
        }

        // We have new events
        for (int i = 0; i < n; i++) {
            const int ident_fd = (int)evs[i].ident;

            // New connection on the server listening socket.
            if (ident_fd == server_fd) {
                for (;;) {
                    struct sockaddr_storage ss;
                    socklen_t slen = sizeof(ss);
                    const int cfd = accept(server_fd, (struct sockaddr *)&ss, &slen);
                    if (cfd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        perror("accept");
                        break;
                    }

                    set_nonblocking(cfd);
                    set_tcp_no_delay(cfd);

                    // We probably want to move the client construction out of here, since this is
                    // not the direct responsibility of the event loop.
                    client_t *c = (client_t *)calloc(1, sizeof(*c));
                    if (!c) {
                        perror("calloc client");
                        close(cfd);
                        continue;
                    }

                    c->fd = cfd;
                    c->buf_used = 0;
                    c->frame_need = -1;
                    c->ip_str[0] = '\0';
                    c->ip_address = c->ip_str; // conform to struct; alias to ip_str
                    c->port = 0;

                    // Peer address → ip_str & port
                    if (ss.ss_family == AF_INET) {
                        struct sockaddr_in *sin = (struct sockaddr_in *)&ss;
                        inet_ntop(AF_INET, &sin->sin_addr, c->ip_str, sizeof(c->ip_str));
                        c->port = (int)ntohs(sin->sin_port);
                    } else if (ss.ss_family == AF_INET6) {
                        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&ss;
                        inet_ntop(AF_INET6, &sin6->sin6_addr, c->ip_str, sizeof(c->ip_str));
                        c->port = (int)ntohs(sin6->sin6_port);
                    } else {
                        snprintf(c->ip_str, sizeof(c->ip_str), "unknown");
                        c->port = 0;
                    }

                    server.clients = listAddNodeToTail(server.clients, c);
                    server.numClients += 1;

                    if (server.verbose) {
                        printf("Client connected fd=%d %s:%d (total=%d)\n",
                               c->fd, c->ip_str, c->port, (int)server.numClients);
                    }

                    // Register the client fd with kqueue and stash client* in udata
                    EV_SET(&ch, c->fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, c);

                    if (kevent(kq, &ch, 1, NULL, 0, NULL) == -1) {
                        perror("kevent add client");
                        close_and_drop_client(kq, c);
                    }
                }
                continue;
            }

            // Existing client readable or closed.
            client_t *c = (client_t *)evs[i].udata;

            // Fallback: if udata is missing, find by fd (kept for compatibility).
            if (!c) {
                list_node_t *node = listFindNode(server.clients, NULL, (void *)(intptr_t)ident_fd);
                c = node ? (client_t *)node->val : NULL;
                if (!c) {
                    // Unknown fd; close it defensively.
                    close(ident_fd);
                    continue;
                }
            }

            // Did peer hangup?
            if (evs[i].flags & EV_EOF) {
                if (server.verbose) {
                    printf("Client fd=%d closed (EV_EOF)\n", c->fd);
                }
                close_and_drop_client(kq, c);
                continue;
            }

            // Drain socket data (non-blocking)
            for (;;) {
                ssize_t nread = recv(c->fd,
                                     c->buffer + c->buf_used,
                                     sizeof(c->buffer) - c->buf_used,
                                     0);
                if (nread > 0) {
                    c->buf_used += (size_t)nread;
                    if (server.verbose) {
                        printf("fd=%d read %zd bytes (buf_used=%zu)\n",
                               c->fd, nread, c->buf_used);
                    }
                    print_binary_data(c->buffer, nread);

                    // Process all complete frames currently in buffer
                    try_process_frames(c);

                    // If the buffer is full, but we still need more for a frame → protocol error.
                    if (c->buf_used == sizeof(c->buffer) && c->frame_need > 0 &&
                        (ssize_t)c->buf_used < c->frame_need) {
                        fprintf(stderr, "fd=%d frame exceeds buffer capacity; dropping client\n", c->fd);
                        close_and_drop_client(kq, c);
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
                    break;
                }

                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // No more data for now
                    break;
                }
                if (errno == EINTR) {
                    // Interrupted — retry
                    continue;
                }

                // Real error
                perror("recv");
                close_and_drop_client(kq, c);
                break;
            }
        }
    }

    close(kq);
    return 0;
}

#endif