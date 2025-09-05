#include "client.h"
#include "command_parser.h"
#include "config.h"
#include "network.c"
#include "response_defs.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool print_reply_from_frame(const unsigned char *frame, size_t frame_len) {
    if (frame_len < 5) return false; // need at least len(2)+status(1)+plen(2)

    uint16_t body_len = (uint16_t)((frame[0] << 8) | frame[1]);
    if (frame_len != (size_t)body_len + 2) return false;

    const unsigned char *body = frame + 2;
    unsigned char status = body[0];
    uint16_t payload_len = (uint16_t)((body[1] << 8) | body[2]);

    if (body_len != (size_t)(1 + 2 + payload_len)) return false;

    const unsigned char *payload = body + 3;

    // Output as requested
    printf("Read %u byte%s\n", (unsigned)payload_len, payload_len == 1 ? "" : "s");
    if (status == STATUS_SUCCESS) {
        printf("Ok>");
    } else {
        printf("Err%u>", (unsigned)status);
    }
    fwrite(payload, 1, payload_len, stdout);
    putchar('\n');

    return true;
}

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
            if (bytes_received >= 2) {
                uint16_t core_len = ((uint16_t)client.buffer[0] << 8) | client.buffer[1];
                if (bytes_received > core_len) {
                    printf("Bytes read: %u\n", bytes_received);
                    const size_t value_len = client.buffer[3] << 8 | client.buffer[4];
                    char *data = malloc(value_len + 1);
                    memcpy(data, &client.buffer[5], value_len);
                    printf("Ok> %s \n", data);
                }
            } else {
                printf("OK> \n");
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
