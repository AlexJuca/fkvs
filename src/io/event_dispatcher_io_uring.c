#ifdef __linux__
#include "../client.h"
#include "../commands/common/command_registry.h"
#include "../core/list.h"
#include "../networking/modes.h"
#include "../networking/networking.h"
#include "../server.h"
#include "../server_lifecycle.h"
#include "../server_limits.h"
#include "../ttl.h"
#include "../utils.h"
#include "event_dispatcher.h"

#include <errno.h>
#include <liburing.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>

#define FKVS_IO_URING_MIN_QUEUE_DEPTH 256U
#define FKVS_IO_URING_MAX_QUEUE_DEPTH 32768U
#define FKVS_IO_URING_QUEUE_HEADROOM 8U
#define FKVS_TTL_SWEEP_BATCH 20U

typedef enum {
    FKVS_URING_ACCEPT_READY,
    FKVS_URING_CLIENT_READ_READY,
    FKVS_URING_CLIENT_WRITE_READY,
    FKVS_URING_TIMER_READY,
} fkvs_uring_op_kind_t;

typedef struct fkvs_uring_op {
    fkvs_uring_op_kind_t kind;
    client_t *client;
    struct fkvs_uring_op *next;
} fkvs_uring_op_t;

typedef struct {
    struct io_uring ring;
    fkvs_uring_op_t *ops;
    unsigned int outstanding_ops;
    int timer_fd;
} fkvs_uring_dispatcher_t;

static bool reject_if_server_at_capacity(const int cfd)
{
    if (fkvs_server_can_accept_client(&server))
        return false;

    if (server.verbose) {
        fprintf(stderr,
                "Rejecting client fd=%d: max-clients limit reached (%u)\n",
                cfd, server.max_clients);
    }

    close(cfd);
    fkvs_server_record_rejected_client(&server);
    return true;
}

static unsigned int queue_depth_for_server(void)
{
    uint32_t max_clients = server.max_clients;
    if (max_clients == 0)
        max_clients = FKVS_DEFAULT_MAX_CLIENTS;

    if (max_clients > FKVS_IO_URING_MAX_QUEUE_DEPTH -
                          FKVS_IO_URING_QUEUE_HEADROOM) {
        return FKVS_IO_URING_MAX_QUEUE_DEPTH;
    }

    const unsigned int needed =
        (unsigned int)max_clients + FKVS_IO_URING_QUEUE_HEADROOM;
    return needed < FKVS_IO_URING_MIN_QUEUE_DEPTH
               ? FKVS_IO_URING_MIN_QUEUE_DEPTH
               : needed;
}

static void track_op(fkvs_uring_dispatcher_t *dispatcher,
                     fkvs_uring_op_t *op)
{
    op->next = dispatcher->ops;
    dispatcher->ops = op;
    dispatcher->outstanding_ops += 1;
}

static void untrack_op(fkvs_uring_dispatcher_t *dispatcher,
                       fkvs_uring_op_t *op)
{
    fkvs_uring_op_t **cursor = &dispatcher->ops;
    while (*cursor) {
        if (*cursor == op) {
            *cursor = op->next;
            if (dispatcher->outstanding_ops > 0)
                dispatcher->outstanding_ops -= 1;
            return;
        }
        cursor = &(*cursor)->next;
    }
}

static void free_tracked_ops(fkvs_uring_dispatcher_t *dispatcher)
{
    fkvs_uring_op_t *op = dispatcher->ops;
    while (op) {
        fkvs_uring_op_t *next = op->next;
        free(op);
        op = next;
    }
    dispatcher->ops = NULL;
    dispatcher->outstanding_ops = 0;
}

static struct io_uring_sqe *get_sqe(fkvs_uring_dispatcher_t *dispatcher)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(&dispatcher->ring);
    if (sqe)
        return sqe;

    const int res = io_uring_submit(&dispatcher->ring);
    if (res < 0) {
        fprintf(stderr, "io_uring submit failed: %s\n", strerror(-res));
        return NULL;
    }

    return io_uring_get_sqe(&dispatcher->ring);
}

static int submit_poll_op(fkvs_uring_dispatcher_t *dispatcher,
                          const fkvs_uring_op_kind_t kind, client_t *client,
                          const int fd, const unsigned int poll_mask)
{
    if (fd < 0)
        return -1;

    fkvs_uring_op_t *op = calloc(1, sizeof(*op));
    if (!op) {
        perror("calloc io_uring op");
        return -1;
    }
    op->kind = kind;
    op->client = client;

    struct io_uring_sqe *sqe = get_sqe(dispatcher);
    if (!sqe) {
        free(op);
        return -1;
    }

    io_uring_prep_poll_add(sqe, fd, poll_mask);
    io_uring_sqe_set_data(sqe, op);
    track_op(dispatcher, op);

    const int res = io_uring_submit(&dispatcher->ring);
    if (res <= 0) {
        if (res == 0) {
            fprintf(stderr, "io_uring submit queued no operations\n");
            return -1;
        }
        fprintf(stderr, "io_uring submit failed: %s\n", strerror(-res));
        return -1;
    }

    return 0;
}

static int submit_accept_ready(fkvs_uring_dispatcher_t *dispatcher)
{
    return submit_poll_op(dispatcher, FKVS_URING_ACCEPT_READY, NULL, server.fd,
                          POLLIN);
}

static int submit_client_read_ready(fkvs_uring_dispatcher_t *dispatcher,
                                    client_t *client)
{
    return submit_poll_op(dispatcher, FKVS_URING_CLIENT_READ_READY, client,
                          client ? client->fd : -1, POLLIN);
}

static int submit_client_write_ready(fkvs_uring_dispatcher_t *dispatcher,
                                     client_t *client)
{
    const int res =
        submit_poll_op(dispatcher, FKVS_URING_CLIENT_WRITE_READY, client,
                       client ? client->fd : -1, POLLOUT);
    if (res == 0)
        client->write_registered = true;
    return res;
}

static int submit_timer_ready(fkvs_uring_dispatcher_t *dispatcher)
{
    if (dispatcher->timer_fd < 0)
        return 0;

    return submit_poll_op(dispatcher, FKVS_URING_TIMER_READY, NULL,
                          dispatcher->timer_fd, POLLIN);
}

static void close_untracked_client(client_t *client)
{
    if (!client)
        return;

    if (client->fd >= 0) {
        close(client->fd);
        client->fd = -1;
    }
    free_client(client);
}

static void close_and_drop_client(client_t *client)
{
    if (!client)
        return;

    server_drop_client(&server, client);
}

static bool client_has_oversized_partial_frame(const client_t *client)
{
    return client->buf_used == sizeof(client->buffer) &&
           client->frame_need > 0 &&
           (ssize_t)client->buf_used < client->frame_need;
}

static int setup_timer(fkvs_uring_dispatcher_t *dispatcher)
{
    dispatcher->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (dispatcher->timer_fd < 0) {
        perror("timerfd_create");
        return 0;
    }

    const struct itimerspec its = {
        .it_interval = {0, 100000000},
        .it_value = {0, 100000000},
    };

    if (timerfd_settime(dispatcher->timer_fd, 0, &its, NULL) == -1) {
        perror("timerfd_settime");
        close(dispatcher->timer_fd);
        dispatcher->timer_fd = -1;
    }

    return 0;
}

static int handle_accept_ready(fkvs_uring_dispatcher_t *dispatcher,
                               const int cqe_res)
{
    if (cqe_res < 0 && !server_shutdown_requested()) {
        fprintf(stderr, "io_uring accept readiness failed: %s\n",
                strerror(-cqe_res));
        return -1;
    }

    for (;;) {
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);
        const int cfd = accept(server.fd, (struct sockaddr *)&ss, &slen);

        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            if (errno == EINTR)
                continue;
            perror("accept");
            break;
        }

        if (reject_if_server_at_capacity(cfd))
            continue;

        set_nonblocking(cfd);
        if (server.socket_domain == TCP_IP)
            set_tcp_no_delay(cfd);

        client_t *client = init_client(cfd, ss, server.socket_domain);
        if (!client)
            continue;

        list_t *updated_clients = listAddNodeToTail(server.clients, client);
        if (!updated_clients) {
            fprintf(stderr, "Unable to allocate client list node\n");
            close_untracked_client(client);
            continue;
        }
        server.clients = updated_clients;
        server.num_clients += 1;

        if (server.verbose) {
            printf("Client connected fd=%d %s:%d (total=%d)\n", client->fd,
                   client->ip_str, client->port, (int)server.num_clients);
        }

        if (submit_client_read_ready(dispatcher, client) == -1) {
            close_and_drop_client(client);
            return -1;
        }
    }

    if (!server_shutdown_requested() && submit_accept_ready(dispatcher) == -1)
        return -1;

    return 0;
}

static int handle_timer_ready(fkvs_uring_dispatcher_t *dispatcher,
                              const int cqe_res)
{
    if (cqe_res < 0 && !server_shutdown_requested()) {
        fprintf(stderr, "io_uring timer readiness failed: %s\n",
                strerror(-cqe_res));
        return -1;
    }

    for (;;) {
        uint64_t expirations = 0;
        const ssize_t nread =
            read(dispatcher->timer_fd, &expirations, sizeof(expirations));
        if (nread == (ssize_t)sizeof(expirations)) {
            expire_sweep(server.database->store, server.database->expires,
                         FKVS_TTL_SWEEP_BATCH);
            continue;
        }
        if (nread < 0 && errno == EINTR)
            continue;
        if (nread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        if (nread < 0)
            perror("timerfd read");
        break;
    }

    if (!server_shutdown_requested() && submit_timer_ready(dispatcher) == -1)
        return -1;

    return 0;
}

static int rearm_client_after_read(fkvs_uring_dispatcher_t *dispatcher,
                                   client_t *client)
{
    if (client->wbuf_used > 0)
        return submit_client_write_ready(dispatcher, client);

    return submit_client_read_ready(dispatcher, client);
}

static int handle_client_read_ready(fkvs_uring_dispatcher_t *dispatcher,
                                    client_t *client, const int cqe_res)
{
    if (!client)
        return 0;

    if (cqe_res < 0) {
        if (server.verbose) {
            printf("Client fd=%d read readiness failed (%s)\n", client->fd,
                   strerror(-cqe_res));
        }
        close_and_drop_client(client);
        return 0;
    }

    for (;;) {
        const size_t available = sizeof(client->buffer) - client->buf_used;
        if (available == 0) {
            fprintf(stderr,
                    "fd=%d read buffer full before frame completion; dropping "
                    "client\n",
                    client->fd);
            close_and_drop_client(client);
            return 0;
        }

        const ssize_t nread =
            recv(client->fd, client->buffer + client->buf_used, available, 0);
        if (nread > 0) {
            client->buf_used += (size_t)nread;
            if (server.verbose) {
                printf("fd=%d read %zd bytes (buf_used=%zu)\n", client->fd,
                       nread, client->buf_used);
            }

            if (try_process_frames(client) < 0) {
                close_and_drop_client(client);
                return 0;
            }

            if (client_has_oversized_partial_frame(client)) {
                fprintf(stderr,
                        "fd=%d frame exceeds buffer capacity; dropping "
                        "client\n",
                        client->fd);
                close_and_drop_client(client);
                return 0;
            }

            continue;
        }

        if (nread == 0) {
            if (server.verbose)
                printf("Client fd=%d closed (recv=0)\n", client->fd);
            close_and_drop_client(client);
            return 0;
        }

        if (errno == EINTR)
            continue;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return rearm_client_after_read(dispatcher, client);

        perror("recv");
        close_and_drop_client(client);
        return 0;
    }
}

static int handle_client_write_ready(fkvs_uring_dispatcher_t *dispatcher,
                                     client_t *client, const int cqe_res)
{
    if (!client)
        return 0;

    client->write_registered = false;

    if (cqe_res < 0) {
        if (server.verbose) {
            printf("Client fd=%d write readiness failed (%s)\n", client->fd,
                   strerror(-cqe_res));
        }
        close_and_drop_client(client);
        return 0;
    }

    wbuf_flush(client);
    if (client->write_failed) {
        close_and_drop_client(client);
        return 0;
    }

    if (client->wbuf_used > 0)
        return submit_client_write_ready(dispatcher, client);

    return submit_client_read_ready(dispatcher, client);
}

static int handle_cqe(fkvs_uring_dispatcher_t *dispatcher,
                      struct io_uring_cqe *cqe)
{
    fkvs_uring_op_t *op = io_uring_cqe_get_data(cqe);
    const int cqe_res = cqe->res;
    io_uring_cqe_seen(&dispatcher->ring, cqe);

    if (!op)
        return 0;

    const fkvs_uring_op_kind_t kind = op->kind;
    client_t *client = op->client;
    untrack_op(dispatcher, op);
    free(op);

    switch (kind) {
    case FKVS_URING_ACCEPT_READY:
        return handle_accept_ready(dispatcher, cqe_res);
    case FKVS_URING_CLIENT_READ_READY:
        return handle_client_read_ready(dispatcher, client, cqe_res);
    case FKVS_URING_CLIENT_WRITE_READY:
        return handle_client_write_ready(dispatcher, client, cqe_res);
    case FKVS_URING_TIMER_READY:
        return handle_timer_ready(dispatcher, cqe_res);
    }

    return 0;
}

static void cleanup_dispatcher(fkvs_uring_dispatcher_t *dispatcher)
{
    if (dispatcher->timer_fd >= 0) {
        close(dispatcher->timer_fd);
        dispatcher->timer_fd = -1;
    }

    io_uring_queue_exit(&dispatcher->ring);
    free_tracked_ops(dispatcher);
}

int run_event_loop()
{
    fkvs_uring_dispatcher_t dispatcher = {.timer_fd = -1};
    set_nonblocking(server.fd);

    const unsigned int queue_depth = queue_depth_for_server();
    int res = io_uring_queue_init(queue_depth, &dispatcher.ring, 0);
    if (res < 0) {
        fprintf(stderr, "Unable to setup io_uring: %s\n", strerror(-res));
        return -1;
    }

    setup_timer(&dispatcher);

    if (submit_accept_ready(&dispatcher) == -1 ||
        submit_timer_ready(&dispatcher) == -1) {
        cleanup_dispatcher(&dispatcher);
        return -1;
    }

    int status = 0;
    while (!server_shutdown_requested()) {
        struct io_uring_cqe *cqe = NULL;
        res = io_uring_wait_cqe(&dispatcher.ring, &cqe);
        if (res < 0) {
            if (res == -EINTR && server_shutdown_requested())
                break;
            if (res == -EINTR)
                continue;
            fprintf(stderr, "Wait for completion queue entry failed: %s\n",
                    strerror(-res));
            status = -1;
            break;
        }

        if (handle_cqe(&dispatcher, cqe) == -1) {
            status = -1;
            break;
        }
    }

    cleanup_dispatcher(&dispatcher);
    return status;
}
#endif
