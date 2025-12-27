#ifdef __linux__
#include "../client.h"
#include "../core/list.h"
#include "../networking/modes.h"
#include "../networking/networking.h"
#include "../server.h"
#include "../utils.h"
#include "event_dispatcher.h"

#include <liburing.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define QUEUE_DEPTH 256
#define BATCH_SUBMIT_THRESHOLD 32

static void close_and_drop_client(struct io_uring *ring, client_t *c)
{
    if (!c)
        return;

    if (server.verbose) {
        printf("Dropping client fd=%d (%s:%d)\n", c->fd, c->ip_str, c->port);
    }

    list_node_t *node =
        listFindNode(server.clients, NULL, (void *)(intptr_t)c->fd);
    if (node) {
        listDeleteNode(server.clients, node);
        free(node->val);
    }

    close(c->fd);
    server.num_clients -= 1;
    server.num_disconnected_clients += 1;
    update_disconnected_clients(&server.metrics,
                                server.num_disconnected_clients);
    free(c);
}

int run_event_loop()
{
    struct io_uring ring;
    int res = io_uring_queue_init(QUEUE_DEPTH, &ring, 0);
    if (res < 0) {
        fprintf(stderr, "Unable to setup io_uring: %s\n", strerror(-res));
        return -1;
    }

    unsigned int sqe_count = 0;

    for (;;) {
        struct io_uring_cqe *cqe;
        res = io_uring_wait_cqe(&ring, &cqe);
        if (res < 0) {
            fprintf(stderr, "Wait for completion queue entry failed: %s\n",
                    strerror(-res));
            break;
        }

        client_t *c = io_uring_cqe_get_data(cqe);
        if (cqe->res < 0) {
            if (server.verbose) {
                printf("Client fd=%d operation failed (res=%d)\n",
                       c ? c->fd : -1, cqe->res);
            }
            close_and_drop_client(&ring, c);
            io_uring_cqe_seen(&ring, cqe);
            continue;
        }

        if (cqe->res == 0) {
            // New connection handling.
            for (;;) {
                struct sockaddr_storage ss;
                socklen_t slen = sizeof(ss);
                const int cfd = accept(c->fd, (struct sockaddr *)&ss, &slen);

                if (cfd < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    perror("accept");
                    break;
                }

                set_nonblocking(cfd);

                if (server.socket_domain == TCP_IP) {
                    set_tcp_no_delay(cfd);
                }

                client_t *new_client =
                    init_client(cfd, ss, server.socket_domain);
                if (!new_client) {
                    continue; // Already closed on failure.
                }

                server.clients = listAddNodeToTail(server.clients, new_client);
                server.num_clients++;

                if (server.verbose) {
                    printf("Client connected fd=%d %s:%d (total=%d)\n",
                           new_client->fd, new_client->ip_str, new_client->port,
                           (int)server.num_clients);
                }

                struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
                io_uring_prep_recv(sqe, new_client->fd, new_client->buffer,
                                   sizeof(new_client->buffer), 0);
                io_uring_sqe_set_data(sqe, new_client);
                sqe_count++;

                // Check if batch submission threshold is met
                if (sqe_count >= BATCH_SUBMIT_THRESHOLD) {
                    io_uring_submit(&ring);
                    sqe_count = 0;
                }
            }
        } else {
            // Data read for existing connections.
            c->buf_used += cqe->res;

            if (server.verbose) {
                printf("fd=%d read %d bytes (buf_used=%zu)\n", c->fd, cqe->res,
                       c->buf_used);
            }

            // Process as many complete frames as possible.
            try_process_frames(c);

            if (c->buf_used == sizeof(c->buffer) && c->frame_need > 0 &&
                (ssize_t)c->buf_used < c->frame_need) {
                fprintf(
                    stderr,
                    "fd=%d frame exceeds buffer capacity; dropping client\n",
                    c->fd);
                close_and_drop_client(&ring, c);
            } else {
                // Re-add to the ring for further reads.
                struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
                io_uring_prep_recv(sqe, c->fd, c->buffer + c->buf_used,
                                   sizeof(c->buffer) - c->buf_used, 0);
                io_uring_sqe_set_data(sqe, c);
                sqe_count++;

                // Check if batch submission threshold is met
                if (sqe_count >= BATCH_SUBMIT_THRESHOLD) {
                    io_uring_submit(&ring);
                    sqe_count = 0;
                }
            }
        }

        io_uring_cqe_seen(&ring, cqe);
    }

    // Ensure any remaining SQEs are submitted
    if (sqe_count > 0) {
        io_uring_submit(&ring);
    }

    io_uring_queue_exit(&ring);
    return 0;
}
#endif
