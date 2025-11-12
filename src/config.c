#include "config.h"
#include "client.h"
#include "io/event_dispatcher.h"
#include "networking/networking.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SERVER
server_t load_server_config(const char *path)
{
    FILE *config =
        fopen(path == NULL ? DEFAULT_SERVER_CONFIG_FILE_PATH : path, "r");

    if (!config) {
        ERROR_AND_EXIT("Failed to open server config file: ");
    }

    server.num_clients = 0;
    server.uds_socket_path = NULL;
    server.socket_domain = TCP_IP;
    if (path) {
        server.config_file_path = path;
    } else {
        server.config_file_path = DEFAULT_CLIENT_CONFIG_FILE_PATH;
    }

    char line[1024];

    while (fgets(line, sizeof(line), config)) {
        if (line[0] == '#' || line[0] == '\0')
            continue; // Let's ignore comment lines and blank lines in
                      // configuration files

        char key[512];
        char value[512];
        sscanf(line, "%s %s", key, value);

        if (strcmp(key, "port") == 0) {
            server.port = atoi(value);
        }

        if (strcmp(key, "event-loop-max-events") == 0) {
            server.event_loop_max_events = atoi(value);
        } else {
            server.event_loop_max_events = MAX_EVENTS;
        }

        if (strcmp(key, "unixsocket") == 0) {
            server.uds_socket_path = value;
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

            if (strcmp(key, "log-enabled") == 0) {
                if (strcmp(value, "true") == 0) {
                    server.is_logging_enabled = true;
                } else if (strcmp(value, "false") == 0) {
                    server.is_logging_enabled = false;
                } else {
                    ERROR_AND_EXIT("'log-enabled' expects a truthy value.");
                }
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
    if (client.socket_domain == TCP_IP) {
        client.ip_address = "127.0.0.1";
    }
    if (path) {
        client.config_file_path = path;
    } else {
        client.config_file_path = DEFAULT_CLIENT_CONFIG_FILE_PATH;
    }

    char line[1024];

    while (fgets(line, sizeof(line), config)) {
        if (line[0] == '#' || line[0] == '\0')
            continue;

        char key[256];
        char value[512];

        sscanf(line, "%s %s", key, value);

        if (strcmp(key, "bind") == 0) {
            client.ip_address = value;
        }

        if (strcmp(key, "port") == 0) {
            client.port = atoi(value);
        }

        if (strcmp(key, "verbose") == 0) {
            client.verbose = true;
        }

        if (strcmp(key, "unixsocket") == 0) {
            client.uds_socket_path = value;
            client.socket_domain = UNIX;
        }
    }

    return client;
}
#endif
