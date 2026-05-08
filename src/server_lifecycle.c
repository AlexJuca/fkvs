#include "server_lifecycle.h"
#include "client.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile sig_atomic_t shutdown_requested = 0;

void request_server_shutdown(int sig)
{
    (void)sig;
    // Async-signal-safe: event loops observe this flag and clean up normally.
    shutdown_requested = 1;
}

bool server_shutdown_requested(void)
{
    return shutdown_requested != 0;
}

void reset_server_shutdown_request(void)
{
    shutdown_requested = 0;
}

static void free_client_ptr(void *ptr)
{
    free_client(ptr);
}

static void close_clients(list_t *clients)
{
    for (list_node_t *node = clients->head; node; node = node->next) {
        client_t *client = node->val;
        if (client && client->fd >= 0) {
            close(client->fd);
            client->fd = -1;
        }
    }

    clients->free = free_client_ptr;
    listEmpty(clients);
}

void server_drop_client(server_t *srv, client_t *client)
{
    if (!srv || !client)
        return;

    if (srv->verbose) {
        printf("Dropping client fd=%d (%s:%d)\n", client->fd, client->ip_str,
               client->port);
    }

    bool removed_from_list = false;
    if (srv->clients) {
        list_node_t *node = listFindNode(srv->clients, NULL, client);
        if (node) {
            void (*free_callback)(void *) = srv->clients->free;
            srv->clients->free = NULL;
            listDeleteNode(srv->clients, node);
            srv->clients->free = free_callback;
            removed_from_list = true;
        }
    }

    if (removed_from_list && srv->num_clients > 0) {
        srv->num_clients -= 1;
    }

    if (client->fd >= 0) {
        close(client->fd);
        client->fd = -1;
    }

    if (srv->num_disconnected_clients < INT32_MAX) {
        srv->num_disconnected_clients += 1;
    }
    srv->metrics.disconnected_clients = srv->num_disconnected_clients;

    free_client(client);
}

void shutdown_server(server_t *srv)
{
    if (!srv)
        return;

    if (srv->clients) {
        close_clients(srv->clients);
        free(srv->clients);
        srv->clients = NULL;
    }
    srv->num_clients = 0;

    if (srv->database) {
        if (srv->database->store)
            free_hash_table(srv->database->store);
        if (srv->database->expires)
            free_hash_table(srv->database->expires);
        free(srv->database);
        srv->database = NULL;
    }

    if (srv->socket_domain == UNIX && srv->uds_socket_path) {
        unlink(srv->uds_socket_path);
    }

    if (srv->owns_uds_socket_path) {
        free(srv->uds_socket_path);
        srv->uds_socket_path = NULL;
        srv->owns_uds_socket_path = false;
    }

    if (srv->owns_bind_address) {
        free(srv->bind_address);
        srv->bind_address = NULL;
        srv->owns_bind_address = false;
    }

    if (srv->fd >= 0) {
        close(srv->fd);
        srv->fd = -1;
    }
}
