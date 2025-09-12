#ifndef NETWORKING_H
#define NETWORKING_H

#ifdef SERVER
#include "client.h"
#include "server.h"

int start_server(server_t *server);
void try_process_frames(client_t *c);
#endif

#ifdef CLI
#include "client.h"
int start_client(client_t *client);
#endif

#endif
