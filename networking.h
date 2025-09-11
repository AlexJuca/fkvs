#ifndef NETWORKING_H
#define NETWORKING_H

#ifdef SERVER
#include "server.h"
int start_server(server_t *server);
#endif

#ifdef CLI
#include "client.h"
int start_client(client_t *client);
#endif

#endif
