#include "client.h"
#include "command_parser.h"
#include "config.h"
#include "network.c"
#include "response_defs.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void run_repl(client_t client) {
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

        char *cmd_part = strtok(command, " ");
        if (cmd_part == NULL) {
            continue;
        }

        if (strcasecmp(cmd_part, "SET") == 0) {
            char *key = strtok(NULL, " ");
            char *value = strtok(NULL, " ");
            if (key == NULL || value == NULL) {
                printf("Usage: SET <key> <value>\n");
                continue;
            }

            size_t cmd_len;
            printf("key: %s value: %s %d \n", key, value, (int) cmd_len);
            unsigned char *binary_cmd = construct_set_command(key, value, &cmd_len);
            if (binary_cmd == NULL) {
                fprintf(stderr, "Failed to construct SET command\n");
                continue;
            }

            send(client.fd, binary_cmd, cmd_len, 0);
            free(binary_cmd);
        } else if (strcasecmp(cmd_part, "GET") == 0) {
            char *key = strtok(NULL, " ");
            if (key == NULL) {
                printf("Usage: GET <key>\n");
                continue;
            }

            size_t cmd_len;
            unsigned char *binary_cmd = construct_get_command(key, &cmd_len);
            if (binary_cmd == NULL) {
                fprintf(stderr, "Failed to construct GET command\n");
                continue;
            }

            send(client.fd, binary_cmd, cmd_len, 0);
            free(binary_cmd);
        } else {
            printf("Unknown command: %s\n", cmd_part);
            continue;
        }

        int bytes_received = recv(client.fd, client.buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received > 0) {
            const unsigned char status = client.buffer[0];
            if (status == STATUS_SUCCESS) {
                if (bytes_received > 2) {
                    // TODO: Ensure server response is structured, so we can
                    // send both status, data and the number of bytes written
                    // size_t len = (client.buffer[1] << 8) | client.buffer[2];
                    // printf("Success; Length: %zu; Data: ", len);
                    // fwrite(&client.buffer[3], 2, len, stdout);
                    // printf("\n");
                    printf("OK>\n");
                } else {
                    printf("OK> No additional data in reply.\n");
                }
            } else {
                fprintf(stderr, "Failed> \n");
            }
        }
    }
}

int main(int argc, char *argv[]) {
    client_t client = loadClientConfig(NULL);

    const int client_fd = start_client(&client);
    if (client_fd == -1) {
        ERROR_AND_EXIT("Failed to connect to server");
        return 1;
    }

    run_repl(client);

    close(client.fd);
    return 0;
}
