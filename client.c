#include "client.h"
#include "networking.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>

client_t *init_client(const int client_fd, const struct sockaddr_storage ss,
                      const enum SocketType socket_type)
{
    client_t *client = calloc(1, sizeof(*client));
    if (!client) {
        perror("calloc client");
        close(client_fd);
        return NULL;
    }

    client->fd = client_fd;
    client->buf_used = 0;
    client->frame_need = -1;
    client->ip_str[0] = '\0';
    client->ip_address = client->ip_str; // keep alias consistent
    client->port = 0;

    if (socket_type == UNIX) {
        client->uds_socket_path = FKVS_SOCK_PATH;
        snprintf(client->ip_str, sizeof(client->ip_str), "unix");
        return client;
    }

    // TCP mode (IPv4/IPv6)
    if (ss.ss_family == AF_INET) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)&ss;
        inet_ntop(AF_INET, &sin->sin_addr, client->ip_str,
                  sizeof(client->ip_str));
        client->port = (int)ntohs(sin->sin_port);
    } else if (ss.ss_family == AF_INET6) {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)&ss;
        inet_ntop(AF_INET6, &sin6->sin6_addr, client->ip_str,
                  sizeof(client->ip_str));
        client->port = (int)ntohs(sin6->sin6_port);
    } else {
        snprintf(client->ip_str, sizeof(client->ip_str), "unknown");
        client->port = 0;
    }

    return client;
}
