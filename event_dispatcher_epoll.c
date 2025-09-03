#if defined(__linux__)

#include "event_dispatcher.h"
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

// TODO: Ensure we track the number connected clients
// TODO: Update this so it works on linux, I haven't been giving the
// linux event loop the love it needs :(.

#define MAX_EVENTS 4096

int run_event_loop(int server_fd) {
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }

    struct epoll_event events[MAX_EVENTS];
    while (1) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n < 0) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == server_fd) {
                int client_fd = accept(server_fd, NULL, NULL);
                if (client_fd == -1) {
                    perror("accept");
                    continue;
                }

                client_t* new_client = malloc(sizeof(client_t));
                if (new_client == NULL) {
                    perror("malloc");
                    close(client_fd);
                    continue;
                }

                new_client->name = NULL;
                new_client->port = ntohs(((struct sockaddr_in*)&event.data.fd)->sin_port);
                new_client->ip_address = NULL;
                new_client->buffer[0] = '\0';
                new_client->fd = client_fd;

                printf("Accepted new client: %d\n", new_client->fd);
                event.events = EPOLLIN;
                event.data.ptr = new_client;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                    perror("epoll_ctl: client");
                    close(client_fd);
                    free(new_client);
                } else {
                    server.clients = listAddNodeToTail(server.clients, new_client);
                }
            } else {
                char buffer[1024];
                client_t* client = (client_t*)events[i].data.ptr;
                    int bytes_read = read(client->fd, client->buffer, sizeof(client->buffer));

                if (bytes_read <= 0) {
                    client_t* client = (client_t*)events[i].data.ptr;
                    printf("Closing connection %d\n", client->fd);
                    list_node_t *node = listFindNode(server.clients, NULL, (void *)(intptr_t)client->fd);
                    if (node) {
                        listDeleteNode(server.clients, node);
                        server.numClients -= 1;
                    }
                    close(client->fd);
                    free(client);
                } else {
                    // Dispatch the command to the appropriate handler based on command registry
                    dispatch_command(client->fd, client->buffer, bytes_read);
                }
            }
        }
    }
    close(epoll_fd);
    return 0;
}

#endif
