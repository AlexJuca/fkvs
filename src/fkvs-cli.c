#include "../deps/linenoise/linenoise.h"
#include "client.h"
#include "commands/client/client_command_handlers.h"
#include "config.h"
#include "networking/networking.c"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage_and_exit(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [-c command] [-h host] [-p port] "
            "  -c C     Command to execute (default ping)\n"
            "  -h HOST  server host/IP (default 127.0.0.1)\n"
            "  -p PORT  server port (default 5995)\n"
            "  -d path to config file \n"
            " --non-interactive start client in non-repl mode\n",
            prog);
    exit(1);
}

void run_repl(client_t client)
{
    char cli_prompt[64];
    snprintf(cli_prompt, sizeof(cli_prompt), "%s:%d> ",
             strdup(client.ip_address), client.port);

    char cli_history_path[512];
    const char *home = getenv("HOME");
    if (home) {
        snprintf(cli_history_path, sizeof(cli_history_path),
                 "%s/.fkvs_cli_history", home);
        linenoiseHistoryLoad(cli_history_path);
    } else {
        linenoiseHistoryLoad(".fkvs_cli_history");
    }

    linenoiseHistorySetMaxLen(50);

    printf("Type 'exit' to quit.\n");
    fflush(stdout);

    char *line;
    while (1) {
        if ((line = linenoise(client.socket_domain == TCP_IP
                                  ? cli_prompt
                                  : "localhost:5995")) != NULL) {
            if (strcmp(line, "exit") == 0) {
                break;
            }

            if (*line != '\0') {
                execute_command(line, &client, command_response_handler);
                linenoiseHistoryAdd(line);
                if (home) {
                    linenoiseHistorySave(cli_history_path);
                } else {
                    linenoiseHistorySave(".fkvs_cli_history");
                }
            }
        }
    }
}

int main(int argc, char *argv[])
{
    char *config_path = DEFAULT_CLIENT_CONFIG_FILE_PATH;

    for (int i = 1; i < argc; i++) {
        if (strcmp("-d", argv[i]) == 0) {
            config_path = argv[i + 1];
        }
    }

    client_t client = load_client_config(config_path);
    int client_fd;

    for (int i = 1; i < argc; ++i) {
        if (strcasecmp(argv[i], "--h") == 0 ||
            strcasecmp(argv[i], "--help") == 0) {
            print_usage_and_exit(argv[0]);
        } else if (strcmp(argv[i], "--non-interactive") == 0) {
            client.interactive_mode = false;
            client.command_type = "PING";
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            client.ip_address = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            client.port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-u") == 0) {
            client.socket_domain = UNIX;
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            if (strcmp(argv[i + 1], "ping") == 0) {
                client.command_type = "PING";
            }
        }
    }

    if (client.socket_domain == UNIX) {
        client_fd = start_uds_client(&client);
    } else {
        client_fd = start_client(&client);
    }

    if (client_fd == -1) {
        ERROR_AND_EXIT("Failed to connect to server");
        return 1;
    }

    if (client.interactive_mode) {
        run_repl(client);
    } else {
        execute_command(client.command_type, &client, command_response_handler);
    }
    close(client.fd);
}
