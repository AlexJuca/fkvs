#include "../deps/linenoise/linenoise.h"
#include "client.h"
#include "commands/client/client_command_handlers.h"
#include "config.h"
#include "networking/networking.c"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    client_t client = load_client_config(NULL);
    int client_fd;

    if (client.socket_domain == UNIX) {
        client_fd = start_uds_client(&client);
    } else {
        client_fd = start_client(&client);
    }

    if (client_fd == -1) {
        ERROR_AND_EXIT("Failed to connect to server");
        return 1;
    }

    run_repl(client);
    close(client.fd);
}
