#ifndef SERVER_H
#define SERVER_H

#include "core/hashtable.h"
#include "core/list.h"
#include "networking/modes.h"
#include <stdbool.h>
#include <sys/types.h>

typedef struct {
#define TABLE_SIZE 8092
    hashtable_t *store;
    hashtable_t *expires;
} db_t;

typedef struct server_t {
    bool daemonize;
    int port;
    int fd;
    pid_t pid;
    bool is_logging_enabled;
    bool verbose;
    bool show_logo;
    list_t *clients;
    u_int32_t num_clients;
    char *uds_socket_path; // Unix domain socket path
    enum SocketDomain socket_domain;
    db_t *database;
} server_t;

#endif // SERVER_H
