#include "client_command_handlers.h"
#include "client.h"
#include "command_defs.h"
#include "command_parser.h"
#include "keygen.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEY_LEN 512
#define VALUE_LEN 512

void cmd_get(const command_args_t args, void (*response_cb)(client_t *client))
{
    if (strncmp(args.cmd, "GET ", 4) != 0) {
        return;
    }

    char key[KEY_LEN];
    if (sscanf(args.cmd, "GET %511s", key) == 1) {

    } else {
        printf("(error) ERR wrong number of arguments for 'get' command\n");
        printf("(info) Usage: GET <key>\n");
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

void cmd_set(const command_args_t args, void (*response_cb)(client_t *client))
{
    if (strncmp(args.cmd, "SET ", 4) != 0) {
        return;
    }

    char value[VALUE_LEN];
    char key[KEY_LEN];
    if (sscanf(args.cmd, "SET %511s %511s", key, value) == 2) {

    } else {
        printf("(error) ERR wrong number of arguments for 'set' command\n");
        printf("(info) Usage: SET <key> <value>\n");
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

void cmd_incr(const command_args_t args, void (*response_cb)(client_t *client))
{
    if (strncmp(args.cmd, "INCR ", 4) != 0) {
        return;
    }

    char key[KEY_LEN];
    if (sscanf(args.cmd, "INCR %511s", key) == 1) {

    } else {
        printf("(error) ERR wrong number of arguments for 'incr' command\n");
        printf("(info) Usage: INCR <key>\n");
        return;
    }

    size_t cmd_len;
    unsigned char *binary_cmd = construct_incr_command(key, &cmd_len);
    if (binary_cmd == NULL) {
        fprintf(stderr, "(error) Failed to construct INCR command\n");
        return;
    }

    send(args.client->fd, binary_cmd, cmd_len, 0);
    free(binary_cmd);
    response_cb(args.client);
}

void cmd_incr_by(const command_args_t args,
                 void (*response_cb)(client_t *client))
{
    if (strncmp(args.cmd, "INCRBY ", 7) != 0) {
        return;
    }

    char key[KEY_LEN];
    char value[VALUE_LEN];
    if (sscanf(args.cmd, "INCRBY %511s %511s", key, value) == 2) {

    } else {
        printf("(error) ERR wrong number of arguments for 'incrby' command\n");
        printf("(info) Usage: INCRBY <key> <value>\n");
        return;
    }

    size_t cmd_len;
    unsigned char *binary_cmd = construct_incr_by_command(key, value, &cmd_len);
    if (binary_cmd == NULL) {
        fprintf(stderr, "(error) Failed to construct INCR command\n");
        return;
    }

    send(args.client->fd, binary_cmd, cmd_len, 0);
    free(binary_cmd);
    response_cb(args.client);
}

void cmd_ping(const command_args_t args, void (*response_cb)(client_t *client))
{
    if (strncmp(args.cmd, "PING ", 5) != 0) {
        return;
    }

    size_t cmd_len;
    char value[VALUE_LEN];
    if (sscanf(args.cmd, "PING \"%127[^\"]\"s", value) == 1) {

    } else if (sscanf(args.cmd, "PING") == 1) {

    } else if (sscanf(args.cmd, "PING %127s", value) == 1) {

    } else {
        printf("(error) ERR wrong number of arguments for 'ping' command\n");
        printf("(info) Usage: PING or PING <key> \n");
        return;
    }

    unsigned char *binary_cmd = construct_ping_command(value, &cmd_len);
    if (binary_cmd == NULL) {
        fprintf(stderr, "Failed to construct PING command\n");
        return;
    }

    send(args.client->fd, binary_cmd, cmd_len, 0);
    free(binary_cmd);
    response_cb(args.client);
}

void cmd_decr(const command_args_t args, void (*response_cb)(client_t *client))
{
    if (strncmp(args.cmd, "DECR ", 4) != 0) {
        return;
    }

    char key[KEY_LEN];
    if (sscanf(args.cmd, "DECR %511s", key) == 1) {

    } else {
        printf("(error) ERR wrong number of arguments for 'decr' command\n");
        printf("(info) Usage: DECR <key>\n");
        return;
    }

    size_t cmd_len;
    unsigned char *binary_cmd = construct_decr_command(key, &cmd_len);
    if (binary_cmd == NULL) {
        fprintf(stderr, "(error) Failed to construct DECR command\n");
        return;
    }

    send(args.client->fd, binary_cmd, cmd_len, 0);
    free(binary_cmd);
    response_cb(args.client);
}

/*
 * TODO: This approach works but is cumbersome to maintain. For future
 * reference, lets implement a solution that doesn't require us to have a
 * conditional with possible command values.
 */
void cmd_unknown(const command_args_t args,
                 void (*response_cb)(client_t *client))
{
    if (strncmp(args.cmd, "INCR ", 5) && strncmp(args.cmd, "INCRBY ", 6) &&
        strncmp(args.cmd, "GET ", 4) && strncmp(args.cmd, "SET ", 4) &&
        strncmp(args.cmd, "PING ", 5) && strncmp(args.cmd, "DECR ", 5)) {
        printf("Unknown command \n");
    }
}

const cmd_t command_table[] = {
    {"cmd_set", cmd_set},         {"cmd_get", cmd_get},
    {"cmd_decr", cmd_decr},       {"cmd_incr", cmd_incr},
    {"cmd_incr_by", cmd_incr_by}, {"cmd_ping", cmd_ping},
    {"cmd_unknown", cmd_unknown}};

void execute_command(const char *cmd, client_t *client,
                     void (*response_cb)(client_t *client))
{
    const command_args_t args = {.cmd = cmd, .client = client};
    for (int i = 0; i < ARRAY_SIZE(command_table); i++) {
        command_table[i].cmd_fn(args, response_cb);
    }
}

void execute_command_benchmark(const char *cmd, client_t *client,
                               bool use_pregenerated_keys,
                               void (*response_cb)(client_t *client))
{
    const command_args_t args = {.cmd = cmd, .client = client};

    size_t cmd_len;
    if (!strcasecmp(cmd, "set")) {

        unsigned char *binary_cmd;

        if (use_pregenerated_keys) {
            /**
             * This benchmarks the write path, using different keys on every
             * command execution. In the future, we might want to pregen keys
             * and use them here so we don't spend too much of our benchmarking
             * time generating and formatting uuid keys as strings
             */
            char key[32];
            generate_unique_key(key);

            binary_cmd = construct_set_command(key, "world", &cmd_len);
        } else {
            binary_cmd = construct_set_command("hello", "world", &cmd_len);
        }

        send(args.client->fd, binary_cmd, cmd_len, 0);
        free(binary_cmd);
        response_cb(args.client);
    } else if (!strcasecmp(cmd, "ping")) {
        unsigned char *binary_cmd = construct_ping_command("", &cmd_len);

        send(args.client->fd, binary_cmd, cmd_len, 0);
        free(binary_cmd);
        response_cb(args.client);
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
                if (client->buffer[2] == CMD_PING) {
                    const size_t value_len =
                        client->buffer[3] << 8 | client->buffer[4];
                    if ((int)value_len ==
                        0) { // if the length of the value relayed
                        // back to client is 0, we assume no PING received no
                        // arguments
                        if (!client->benchmark_mode) {
                            printf("PONG\n");
                        }
                    } else {
                        char *data = malloc(value_len + 1);
                        memcpy(data, &client->buffer[5], value_len);
                        if (!client->benchmark_mode) {
                            printf("\"%s\" \n", data);
                        }
                        free(data);
                    }
                } else {
                    const size_t value_len =
                        client->buffer[3] << 8 | client->buffer[4];
                    char *data = malloc(value_len + 1);
                    memcpy(data, &client->buffer[5], value_len);
                    if (strlen(data) == 0) {
                        if (!client->benchmark_mode) {
                            printf("OK\n");
                        }
                    } else {
                        if (!client->benchmark_mode) {
                            printf("\"%s\" \n", data);
                        }
                    }

                    free(data);
                }
            }
        } else {
            if (!client->benchmark_mode) {
                printf("(nil) \n");
            }
        }
    }
}