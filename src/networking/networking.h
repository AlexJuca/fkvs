#ifndef NETWORKING_H
#define NETWORKING_H

#define FKVS_SOCK_PATH "/tmp/fkvs.sock"

#ifdef SERVER
#include "../client.h"
#include "../counter.h"
#include "../server.h"

int start_server();
int start_uds_server();
void try_process_frames(client_t *c);
void set_tcp_no_delay(const int fd);
void set_nonblocking(const int fd);
#endif

#ifdef CLI
#include "../client.h"

int start_client(client_t *client);
int start_uds_client(client_t *client);
#endif

#endif
