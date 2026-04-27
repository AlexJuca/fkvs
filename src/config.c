#include "config.h"
#include "client.h"
#include "io/event_dispatcher.h"
#include "networking/networking.h"
#include "numeric_parse.h"
#include "utils.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SERVER
static int64_t parse_config_i64(const char *key, const char *value,
                                const int64_t min_value,
                                const int64_t max_value)
{
    int64_t parsed = 0;
    if (!value ||
        !fkvs_parse_i64_decimal((const unsigned char *)value, strlen(value),
                                min_value, max_value, &parsed)) {
        fprintf(stderr,
                "Invalid config value for '%s': '%s' (expected integer in "
                "range %" PRId64 "..%" PRId64 ")\n",
                key, value ? value : "(null)", min_value, max_value);
        exit(EXIT_FAILURE);
    }

    return parsed;
}

static void set_server_bind_address(const char *value)
{
    char *copy = strdup(value);
    if (!copy) {
        ERROR_AND_EXIT("Failed to allocate bind address");
    }

    if (server.owns_bind_address) {
        free(server.bind_address);
    }

    server.bind_address = copy;
    server.owns_bind_address = true;
}

server_t load_server_config(const char *path)
{
    FILE *config =
        fopen(path == NULL ? DEFAULT_SERVER_CONFIG_FILE_PATH : path, "r");

    if (!config) {
        ERROR_AND_EXIT("Failed to open server config file: ");
    }

    if (server.owns_bind_address) {
        free(server.bind_address);
    }
    if (server.owns_uds_socket_path) {
        free(server.uds_socket_path);
    }

    server.num_clients = 0;
    server.fd = -1;
    server.event_loop_fd = -1;
    server.bind_address = (char *)FKVS_DEFAULT_BIND_ADDRESS;
    server.owns_bind_address = false;
    server.max_clients = FKVS_DEFAULT_MAX_CLIENTS;
    server.uds_socket_path = NULL;
    server.owns_uds_socket_path = false;
    server.socket_domain = TCP_IP;
    server.event_loop_max_events = MAX_EVENTS;
    if (path) {
        server.config_file_path = path;
    } else {
        server.config_file_path = DEFAULT_SERVER_CONFIG_FILE_PATH;
    }

    char line[1024];

    while (fgets(line, sizeof(line), config)) {
        if (line[0] == '#' || line[0] == '\0')
            continue; // Let's ignore comment lines and blank lines in
                      // configuration files

        char key[512] = {0};
        char value[512] = {0};
        if (sscanf(line, "%511s %511s", key, value) != 2)
            continue;

        if (strcmp(key, "port") == 0) {
            server.port =
                (int)parse_config_i64(key, value, 1, UINT16_MAX);
        }

        if (strcmp(key, "bind") == 0) {
            set_server_bind_address(value);
        }

        if (strcmp(key, "event-loop-max-events") == 0) {
            server.event_loop_max_events =
                (int)parse_config_i64(key, value, 1, INT_MAX);
        }

        if (strcmp(key, "max-clients") == 0) {
            server.max_clients =
                (uint32_t)parse_config_i64(key, value, 1, UINT32_MAX);
        }

        if (strcmp(key, "unixsocket") == 0) {
            server.uds_socket_path = strdup(value);
            if (!server.uds_socket_path) {
                ERROR_AND_EXIT("Failed to allocate unixsocket path");
            }
            server.owns_uds_socket_path = true;
            server.socket_domain = UNIX;
        }

        if (strcmp(key, "show-logo") == 0) {
            if (strcmp(value, "true") == 0) {
                server.show_logo = true;
            } else if (strcmp(value, "false") == 0) {
                server.show_logo = false;
            } else {
                ERROR_AND_EXIT("'show-logo' expects a truthy value.");
            }
        }

        if (strcmp(key, "verbose") == 0) {
            if (strcmp(value, "true") == 0) {
                server.verbose = true;
            } else if (strcmp(value, "false") == 0) {
                server.verbose = false;
            } else {
                ERROR_AND_EXIT("'verbose' expects a truthy value.");
            }
        }

        if (strcmp(key, "daemonize") == 0) {
            if (strcmp(value, "true") == 0) {
                server.daemonize = true;
            } else if (strcmp(value, "false") == 0) {
                server.daemonize = false;
            } else {
                ERROR_AND_EXIT("'daemonize' expects a truth value");
            }
        }

        if (strcmp(key, "log-enabled") == 0) {
            if (strcmp(value, "true") == 0) {
                server.is_logging_enabled = true;
            } else if (strcmp(value, "false") == 0) {
                server.is_logging_enabled = false;
            } else {
                ERROR_AND_EXIT("'log-enabled' expects a truthy value.");
            }
        }

        if (strcmp(key, "use-io-uring") == 0) {
            if (strcmp(value, "true") == 0) {
                server.use_io_uring = true;
            } else if (strcmp(value, "false") == 0) {
                server.use_io_uring = false;
            } else {
                ERROR_AND_EXIT("'use-io-uring' expects a truthy value.");
            }
        }
    }

    fclose(config);
    return server;
}
#endif

#ifdef CLI
client_t load_client_config(const char *path)
{
    FILE *config =
        fopen(path == NULL ? DEFAULT_CLIENT_CONFIG_FILE_PATH : path, "r");

    if (!config) {
        ERROR_AND_EXIT("failed to open client config file: ");
    }

    client_t client;
    client.verbose = false;
    client.benchmark_mode = false;
    client.uds_socket_path = NULL;
    client.socket_domain = TCP_IP;
    client.interactive_mode = true;
    client.ip_address = strdup("127.0.0.1");
    if (path) {
        client.config_file_path = path;
    } else {
        client.config_file_path = DEFAULT_CLIENT_CONFIG_FILE_PATH;
    }

    char line[1024];

    while (fgets(line, sizeof(line), config)) {
        if (line[0] == '#' || line[0] == '\0')
            continue;

        char key[256] = {0};
        char value[512] = {0};

        if (sscanf(line, "%255s %511s", key, value) != 2)
            continue;

        if (strcmp(key, "bind") == 0) {
            free(client.ip_address);
            client.ip_address = strdup(value);
        }

        if (strcmp(key, "port") == 0) {
            client.port = atoi(value);
        }

        if (strcmp(key, "verbose") == 0) {
            client.verbose = true;
        }

        if (strcmp(key, "unixsocket") == 0) {
            client.uds_socket_path = strdup(value);
            client.socket_domain = UNIX;
        }
    }

    return client;
}
#endif
