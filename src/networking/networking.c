#include "networking.h"
#include "../client.h"
#include "../utils.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#ifdef SERVER

#include "../commands/common/command_registry.h"
#include "../counter.h"
#include <errno.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>

static int server_listen_backlog(void)
{
    if (server.max_clients == 0)
        return (int)FKVS_DEFAULT_MAX_CLIENTS;
    if (server.max_clients > (uint32_t)INT_MAX)
        return INT_MAX;
    return (int)server.max_clients;
}

int start_server()
{
    int server_fd;
    struct sockaddr_in server_addr;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        return -1;
    }

    server.fd = server_fd;

    const int one = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server.port);

    if (server_addr.sin_port == 0) {
        fprintf(stderr, "Invalid port 0\n");
        close(server_fd);
        server.fd = -1;
        return -1;
    }

    const char *bind_address =
        server.bind_address ? server.bind_address : FKVS_DEFAULT_BIND_ADDRESS;
    if (inet_pton(AF_INET, bind_address, &server_addr.sin_addr) != 1) {
        fprintf(stderr,
                "Invalid bind address '%s' (expected an IPv4 address)\n",
                bind_address);
        close(server_fd);
        server.fd = -1;
        return -1;
    }

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
        0) {
        perror("bind failed");
        close(server_fd);
        server.fd = -1;
        return -1;
    }

    if (listen(server_fd, server_listen_backlog()) < 0) {
        perror("listen");
        close(server_fd);
        server.fd = -1;
        return -1;
    }

    LOG_INFO("Ready to accept connections via tcp");
    return server_fd;
}

int start_uds_server()
{
    struct sockaddr_un server_addr;

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket failed");
        return -1;
    }

    server.fd = server_fd;
    if (!server.uds_socket_path) {
        server.uds_socket_path = FKVS_SOCK_PATH;
        server.owns_uds_socket_path = false;
    }

    memset(&server_addr, 0, sizeof(struct sockaddr_un));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, server.uds_socket_path,
            sizeof(server_addr.sun_path) - 1);

    char tmp[sizeof(server_addr.sun_path)];
    strncpy(tmp, server.uds_socket_path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *dir = dirname(tmp);
    if (mkdir(dir, 0755) == -1 && errno != EEXIST) {
        perror("failed to create socket directory");
        close(server_fd);
        return -1;
    }

    if (unlink(server.uds_socket_path) == -1 && errno != ENOENT) {
        LOG_INFO("failed to unlink socket path during server start up");
    }

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
        0) {
        perror("bind failed");
        close(server_fd);
        return -1;
    }

    if (chmod(server.uds_socket_path, 0770) == -1) {
        perror("Failed to set permissions on Unix socket");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, server_listen_backlog()) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    LOG_INFO("Ready to accept connections via unix domain socket");
    return server_fd;
}

void set_tcp_no_delay(const int fd)
{
    const int one = 1;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

void set_nonblocking(const int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        flags = 0;
    (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int try_process_frames(client_t *c)
{
    // Parse as many complete frames as possible.
    if (server.verbose) {
        printf("Attempting to process frame \n");
    }
    for (;;) {
        if (c->buf_used < 2)
            break; // need length prefix

        if (c->frame_need < 0) {
            uint16_t core_len = ((uint16_t)c->buffer[0] << 8) | c->buffer[1];
            c->frame_need =
                2 + (ssize_t)core_len; // total frame bytes (prefix + core)
            if ((size_t)c->frame_need > sizeof(c->buffer)) {
                fprintf(stderr, "Frame too large: %zd > %zu\n", c->frame_need,
                        sizeof(c->buffer));
                c->buf_used = 0;
                c->frame_need = -1;
                return -1;
            }
        }

        if ((ssize_t)c->buf_used < c->frame_need)
            break; // incomplete frame; we wait for more data

        // We have a complete frame.
        const size_t frame_len = (size_t)c->frame_need;

        if (server.verbose) {
            printf("Complete frame (%zu bytes) from fd=%d\n", frame_len, c->fd);
        }

        // Dispatch exactly one frame.
        dispatch_command(c, c->buffer, frame_len);
        increment_command_count(&server.metrics);
        if (c->write_failed)
            return -1;

        // Shift any remaining bytes (back-to-back frames).
        size_t remain = c->buf_used - frame_len;
        if (remain)
            memmove(c->buffer, c->buffer + frame_len, remain);
        c->buf_used = remain;
        c->frame_need = -1; // recompute for next frame
    }

    // Flush any batched responses after processing all queued frames.
    if (c->wbuf_used > 0)
        wbuf_flush(c);
    return c->write_failed ? -1 : 0;
}

#endif

#ifdef CLI
int start_client(client_t *client)
{
    int client_fd;
    struct sockaddr_in server_addr;

    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        return -1;
    }
    const int one = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    client->fd = client_fd;

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(client->port);

    if (!client->ip_address) {
        close(client->fd);
        return -1;
    }
    if (inet_pton(AF_INET, client->ip_address, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        close(client->fd);
        return -1;
    }

    if (connect(client->fd, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
        perror("Connection Failed");
        close(client->fd);
        return -1;
    }

    if (client->verbose && client->interactive_mode) {
        printf("Connected to server %s on port %d\n", client->ip_address,
               client->port);
    }

    return client_fd;
}

int start_uds_client(client_t *client)
{
    struct sockaddr_un server_addr;

    int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd == -1) {
        perror("socket failed");
        return -1;
    }

    client->fd = client_fd;

    memset(&server_addr, 0, sizeof(struct sockaddr_un));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, client->uds_socket_path,
            sizeof(server_addr.sun_path) - 1);

    if (connect(client_fd, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
        perror("Connection via unix domain socket failed");
        close(client_fd);
        return -1;
    }

    if (client->verbose && client->interactive_mode) {
        printf("Connected to server via unix domain socket \n");
    }
    return client_fd;
}
#endif
