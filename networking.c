#include "networking.h"
#include "client.h"
#include "utils.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define BACKLOG 2000000 // Number of allowed connections

#ifdef SERVER

#include "command_registry.h"

int start_server()
{
    time_t ct;
    time(&ct);
    int server_fd;
    struct sockaddr_in server_addr;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return -1;
    }

    server.fd = server_fd;

    const int one = 1;
    setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server.port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
        0) {
        perror("bind failed");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    LOG_INFO("Ready to accept connections via tcp");
    return server_fd;
}

int start_uds_server()
{
    time_t ct;
    time(&ct);
    struct sockaddr_un server_addr;

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket failed");
        return -1;
    }

    server.fd = server_fd;

    memset(&server_addr, 0, sizeof(struct sockaddr_un));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, FKVS_SOCK_PATH,
            sizeof(server_addr.sun_path) - 1);
    unlink(server.uds_socket_path);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
        0) {
        perror("bind failed");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, BACKLOG) < 0) {
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

void try_process_frames(client_t *c)
{
    // Parse as many complete frames as possible.
    if (server.verbose) {
        printf("Attempting to process frame \n");
    }
    for (;;) {
        if (c->buf_used < 2)
            return; // need length prefix

        if (c->frame_need < 0) {
            uint16_t core_len = ((uint16_t)c->buffer[0] << 8) | c->buffer[1];
            c->frame_need =
                2 + (ssize_t)core_len; // total frame bytes (prefix + core)
            if ((size_t)c->frame_need > sizeof(c->buffer)) {
                fprintf(stderr, "Frame too large: %zd > %zu\n", c->frame_need,
                        sizeof(c->buffer));
                // Drop the buffer contents to resync; caller should disconnect
                // the client.
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
        }

        // Dispatch exactly one frame.
        dispatch_command(c->fd, c->buffer, frame_len);

        // Shift any remaining bytes (back-to-back frames).
        size_t remain = c->buf_used - frame_len;
        if (remain)
            memmove(c->buffer, c->buffer + frame_len, remain);
        c->buf_used = remain;
        c->frame_need = -1; // recompute for next frame
    }
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

    if (inet_pton(AF_INET, client->ip_address, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        return -1;
    }

    if (connect(client->fd, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
        perror("Connection Failed");
        return -1;
    }

    if (client->verbose) {
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
        return -1;
    }

    if (client->verbose) {
        printf("Connected to server via unix domain socket \n");
    }
    return client_fd;
}
#endif
