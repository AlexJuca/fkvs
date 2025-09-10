#include "client_command_handlers.h"
#include "client.h"
#include "command_parser.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cmd_get(const command_args_t args, void (*response_cb)(client_t *client))
{
    if (!strcasecmp(args.cmd, "GET")) {
        const char *key = strtok(NULL, " ");
        if (key == NULL) {
            printf("Usage: GET <key>\n");
            return;
        }

        size_t cmd_len;
        unsigned char *binary_cmd = construct_get_command(key, &cmd_len);
        if (binary_cmd == NULL) {
            fprintf(stderr, "Failed to construct GET command\n");
            return;
        }

        send(args.client->fd, binary_cmd, cmd_len, 0);
        free(binary_cmd);
        response_cb(args.client);
    }
}

void cmd_set(const command_args_t args, void (*response_cb)(client_t *client))
{
    if (!strcasecmp(args.cmd, "SET")) {
        const char *key = strtok(NULL, " ");
        const char *value = strtok(NULL, " ");

        if (key == NULL || value == NULL) {
            printf("Usage: SET <key> <value>\n");
            return;
        }

        size_t cmd_len;
        unsigned char *binary_cmd = construct_set_command(key, value, &cmd_len);
        if (binary_cmd == NULL) {
            fprintf(stderr, "Failed to construct SET command\n");
            return;
        }

        send(args.client->fd, binary_cmd, cmd_len, 0);
        free(binary_cmd);
        response_cb(args.client);
    }
}

void cmd_inc(const command_args_t args, void (*response_cb)(client_t *client))
{
    if (!strcasecmp(args.cmd, "INCR")) {
        const char *key = strtok(NULL, " ");
        if (key == NULL) {
            printf("Usage: INCR <key>\n");
            return;
        }

        size_t cmd_len;
        unsigned char *binary_cmd = construct_incr_command(key, &cmd_len);
        if (binary_cmd == NULL) {
            fprintf(stderr, "Failed to construct INCR command\n");
            return;
        }

        send(args.client->fd, binary_cmd, cmd_len, 0);
        free(binary_cmd);
        response_cb(args.client);
    }
}

/*
 * TODO: This approach works but is cumbersome to maintain. For future reference,
 * lets implement a solution that doesn't require us to have a conditional with
 * possible command values.
 */
void cmd_unknown(const command_args_t args,
                 void (*response_cb)(client_t *client))
{
  if (strcasecmp(args.cmd, "INCR") & strcasecmp(args.cmd, "GET") & strcasecmp(args.cmd, "SET")) {
    printf("Unknown command \n");
  }
}

cmd_t command_table[] = {{"cmd_set", cmd_set},
                         {"cmd_get", cmd_get},
                         {"cmd_inc", cmd_inc},
                         {"cmd_unknown", cmd_unknown}};

void execute_command(const char *cmd, client_t *client,
                     void (*response_cb)(client_t *client))
{
    const command_args_t args = {.cmd = cmd, .client = client};
    for (int i = 0; i < ARRAY_SIZE(command_table); i++) {
      command_table[i].cmd_fn(args, response_cb);
    }
}

void command_response_handler(client_t *client)
{
    const int bytes_received =
        recv(client->fd, client->buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received > 0) {
        if (bytes_received >= 2) {
            const uint16_t core_len =
                ((uint16_t)client->buffer[0] << 8) | client->buffer[1];
            if (bytes_received > core_len) {
                printf("Bytes received: %u\n", bytes_received);
                const size_t value_len =
                    client->buffer[3] << 8 | client->buffer[4];
                char *data = malloc(value_len + 1);
                memcpy(data, &client->buffer[5], value_len);
                printf("Ok> %s \n", data);
                free(data);
            }
        } else {
            printf("OK> \n");
        }
    }
}