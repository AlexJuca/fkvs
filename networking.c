#include "networking.h"
#include "client.h"
#include "utils.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define BACKLOG 4096 // Number of allowed connections

#ifdef SERVER
int start_server(server_t *server)
{
    time_t ct;
    time(&ct);
    int server_fd;
    struct sockaddr_in server_addr;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return -1;
    }

    const int one = 1;
    setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server->port);

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

    LOG("Ready to accept connections via tcp");
    return server_fd;
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

    printf("Connected to server on port %d\n", client->port);
    return client_fd;
}
#endif
