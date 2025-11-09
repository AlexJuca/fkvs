#ifndef SERVER_H
#define SERVER_H

#include "core/hashtable.h"
#include "core/list.h"
#include "counter.h"
#include "io/event_dispatcher.h"
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
    int event_loop_fd;
    pid_t pid;
    bool is_logging_enabled;
    bool verbose;
    bool show_logo;
    list_t *clients;
    u_int32_t num_clients;
    u_int32_t num_disconnected_clients;
    int event_loop_max_events;
    char *uds_socket_path; // Unix domain socket path
    enum socket_domain socket_domain;
    char *config_file_path;
    db_t *database;
    counter_t metrics;
    enum event_loop_dispatcher_kind event_dispatcher_kind;
} server_t;

#endif // SERVER_H
