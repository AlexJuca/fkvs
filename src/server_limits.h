#ifndef SERVER_LIMITS_H
#define SERVER_LIMITS_H

#include "server.h"

#include <stdbool.h>

bool fkvs_server_can_accept_client(const server_t *srv);
void fkvs_server_record_rejected_client(server_t *srv);

#endif // SERVER_LIMITS_H
