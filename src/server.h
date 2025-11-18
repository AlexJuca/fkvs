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
    list_t *clients;
    char *config_file_path;
    db_t *database;
    char *uds_socket_path; // Unix domain socket path
    counter_t metrics;
    int port;
    int fd;
    int event_loop_fd;
    int event_loop_max_events;
    int32_t num_disconnected_clients;
    pid_t pid;
    u_int32_t num_clients;
    enum socket_domain socket_domain;
    event_loop_dispatcher_kind event_dispatcher_kind;
    bool use_io_uring;
    bool is_logging_enabled;
    bool verbose;
    bool show_logo;
    bool daemonize;
} server_t __attribute__((aligned(128)));
;

#endif // SERVER_H
