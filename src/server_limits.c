#include "server_limits.h"

#include <limits.h>

bool fkvs_server_can_accept_client(const server_t *srv)
{
    return srv != NULL && srv->max_clients > 0 &&
           srv->num_clients < srv->max_clients;
}

void fkvs_server_record_rejected_client(server_t *srv)
{
    if (!srv)
        return;

    if (srv->num_disconnected_clients < INT_MAX) {
        srv->num_disconnected_clients += 1;
    }

    srv->metrics.disconnected_clients = srv->num_disconnected_clients;
}
