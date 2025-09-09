#include "client.h"
#include "client_command_handlers.h"
#include "config.h"
#include "network.c"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void run_repl(client_t client)
{
    char command[BUFFER_SIZE];

    printf("Connected to server. Type 'exit' to quit.\n");
    while (1) {
        printf("%s:%d> ", client.ip_address, client.port);
        if (fgets(command, BUFFER_SIZE, stdin) == NULL) {
            break;
        }

        command[strcspn(command, "\n")] = 0;
        if (strcmp(command, "exit") == 0) {
            break;
        }

        const char *cmd_part = strtok(command, " ");
        if (cmd_part == NULL) {
            continue;
        }

        execute_command(cmd_part, &client, command_response_handler);
    }
}

int main(int argc, char *argv[])
{
    client_t client = loadClientConfig(NULL);

    const int client_fd = start_client(&client);
    if (client_fd == -1) {
        ERROR_AND_EXIT("Failed to connect to server");
        return 1;
    }

    run_repl(client);
    close(client.fd);
}
