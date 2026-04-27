#ifndef SERVER_LIFECYCLE_H
#define SERVER_LIFECYCLE_H

#include "server.h"

#include <stdbool.h>

typedef struct client_t client_t;

void request_server_shutdown(int sig);
bool server_shutdown_requested(void);
void reset_server_shutdown_request(void);
void server_drop_client(server_t *srv, client_t *client);
void shutdown_server(server_t *srv);

#endif // SERVER_LIFECYCLE_H
