#include "client.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>

client_t *init_client(const int client_fd, struct sockaddr_storage ss)
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
    client->ip_address =
        client
            ->ip_str; // conform to struct; alias to ip_str for now, then remove
    client->port = 0;

    // Peer address → ip_str & port
    if (ss.ss_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&ss;
        inet_ntop(AF_INET, &sin->sin_addr, client->ip_str,
                  sizeof(client->ip_str));
        client->port = (int)ntohs(sin->sin_port);
    } else if (ss.ss_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&ss;
        inet_ntop(AF_INET6, &sin6->sin6_addr, client->ip_str,
                  sizeof(client->ip_str));
        client->port = (int)ntohs(sin6->sin6_port);
    } else {
        snprintf(client->ip_str, sizeof(client->ip_str), "unknown");
        client->port = 0;
    }

    return client;
}