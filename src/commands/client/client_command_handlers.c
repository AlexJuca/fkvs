#include "../../commands/client/client_command_handlers.h"
#include "../../client.h"
#include "../../commands/common/command_defs.h"
#include "../../commands/common/command_parser.h"
#include "../../response_defs.h"
#include "../../keygen.h"
#include "../../utils.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define MAX_KEY_LEN 512
#define MAX_VALUE_LEN 512

void cmd_get(const command_args_t args, void (*response_cb)(client_t *client))
{
    if (strncasecmp(args.cmd, "GET ", 4) != 0) {
        return;
    }

    char key[MAX_KEY_LEN];
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

    assert(cmd_len > 0);
    assert(args.client->fd > 0);

    send(args.client->fd, binary_cmd, cmd_len, 0);
    free(binary_cmd);
    response_cb(args.client);
}

void cmd_set(const command_args_t args, void (*response_cb)(client_t *client))
{
    if (strncasecmp(args.cmd, "SET ", 4) != 0) {
        return;
    }

    char value[MAX_VALUE_LEN];
    char key[MAX_KEY_LEN];
    char ex_keyword[4];
    char seconds[MAX_VALUE_LEN];

    size_t cmd_len;
    unsigned char *binary_cmd = NULL;

    int nfields = sscanf(args.cmd, "SET %511s %511s %3s %511s", key, value,
                         ex_keyword, seconds);

    if (nfields == 4 && strcasecmp(ex_keyword, "EX") == 0) {
        binary_cmd = construct_set_ex_command(key, value, seconds, &cmd_len);
    } else if (nfields == 2) {
        binary_cmd = construct_set_command(key, value, &cmd_len);
    } else {
        printf("(error) ERR syntax error in 'set' command\n");
        printf("(info) Usage: SET <key> <value> [EX seconds]\n");
        return;
    }

    if (binary_cmd == NULL) {
        fprintf(stderr, "Failed to construct SET command\n");
        return;
    }

    assert(cmd_len > 0);
    assert(args.client->fd > 0);

    send(args.client->fd, binary_cmd, cmd_len, 0);
    free(binary_cmd);
    response_cb(args.client);
}

void cmd_incr(const command_args_t args, void (*response_cb)(client_t *client))
{
    if (strncasecmp(args.cmd, "INCR ", 5) != 0) {
        return;
    }

    char key[MAX_KEY_LEN];
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

    assert(cmd_len > 0);
    assert(args.client->fd > 0);

    send(args.client->fd, binary_cmd, cmd_len, 0);
    free(binary_cmd);
    response_cb(args.client);
}

void cmd_incr_by(const command_args_t args,
                 void (*response_cb)(client_t *client))
{
    if (strncasecmp(args.cmd, "INCRBY ", 7) != 0) {
        return;
    }

    char key[MAX_KEY_LEN];
    char value[MAX_VALUE_LEN];
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

    assert(cmd_len > 0);
    assert(args.client->fd > 0);

    send(args.client->fd, binary_cmd, cmd_len, 0);
    free(binary_cmd);
    response_cb(args.client);
}

void cmd_ping(const command_args_t args, void (*response_cb)(client_t *client))
{
    if (strncmp(args.cmd, "PING ", 5) != 0 && strncmp(args.cmd, "PING", 4) != 0) {
      return;
    }

    char value[MAX_VALUE_LEN];
    if (sscanf(args.cmd, "PING \"%127[^\"]\"s", value) == 1) {

    } else if (strcmp(args.cmd, "PING") == 0) {
      // TODO: Find a cleaner alternative for this.
      value[0] = 'P';
      value[1] = 'O';
      value[2] = 'N';
      value[3] = 'G';
      value[4] = '\0';
    } else if (sscanf(args.cmd, "PING") == 1) {

    } else if (sscanf(args.cmd, "PING %127s", value) == 1) {

    } else {
        printf("(error) ERR wrong number of arguments for 'ping' command\n");
        printf("(info) Usage: PING or PING <key> \n");
        return;
    }

    size_t cmd_len;
    unsigned char *binary_cmd = construct_ping_command(value, &cmd_len);
    if (binary_cmd == NULL) {
        fprintf(stderr, "Failed to construct PING command\n");
        return;
    }

    assert(cmd_len > 0);
    assert(args.client->fd > 0);

    send(args.client->fd, binary_cmd, cmd_len, 0);
    free(binary_cmd);
    response_cb(args.client);
}

void cmd_info(const command_args_t args, void (*response_cb)(client_t *client))
{
    if (strncasecmp(args.cmd, "INFO", 4) != 0) {
        return;
    }

    size_t cmd_len;
    unsigned char *info_cmd = construct_info_command(&cmd_len);
    if (!info_cmd) {
        fprintf(stderr, "Failed to construct INFO command\n");
        return;
    }

    assert(cmd_len > 0);
    assert(args.client->fd > 0);

    send(args.client->fd, info_cmd, cmd_len, 0);
    free(info_cmd);
    response_cb(args.client);
}

void cmd_decr(const command_args_t args, void (*response_cb)(client_t *client))
{
    if (strncasecmp(args.cmd, "DECR ", 5) != 0) {
        return;
    }

    char key[MAX_KEY_LEN];
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

    assert(cmd_len > 0);
    assert(args.client->fd > 0);

    send(args.client->fd, binary_cmd, cmd_len, 0);
    free(binary_cmd);
    response_cb(args.client);
}

void cmd_decr_by(const command_args_t args,
                 void (*response_cb)(client_t *client))
{
    if (strncasecmp(args.cmd, "DECRBY ", 7) != 0) {
        return;
    }

    char key[MAX_KEY_LEN];
    char value[MAX_VALUE_LEN];
    if (sscanf(args.cmd, "DECRBY %511s %511s", key, value) == 2) {

    } else {
        printf("(error) ERR wrong number of arguments for 'decrby' command\n");
        printf("(info) Usage: DECRBY <key> <value>\n");
        return;
    }

    size_t cmd_len;
    unsigned char *binary_cmd = construct_decr_by_command(key, value, &cmd_len);
    if (binary_cmd == NULL) {
        fprintf(stderr, "(error) Failed to construct DECRBY command\n");
        return;
    }

    assert(cmd_len > 0);
    assert(args.client->fd > 0);

    send(args.client->fd, binary_cmd, cmd_len, 0);
    free(binary_cmd);
    response_cb(args.client);
}

void cmd_del(const command_args_t args, void (*response_cb)(client_t *client))
{
    if (strncasecmp(args.cmd, "DEL ", 4) != 0) {
        return;
    }

    char key[MAX_KEY_LEN];
    if (sscanf(args.cmd, "DEL %511s", key) != 1) {
        printf("(error) ERR wrong number of arguments for 'del' command\n");
        printf("(info) Usage: DEL <key>\n");
        return;
    }

    size_t cmd_len;
    unsigned char *binary_cmd = construct_del_command(key, &cmd_len);
    if (binary_cmd == NULL) {
        fprintf(stderr, "Failed to construct DEL command\n");
        return;
    }

    assert(cmd_len > 0);
    assert(args.client->fd > 0);

    send(args.client->fd, binary_cmd, cmd_len, 0);
    free(binary_cmd);
    response_cb(args.client);
}

void cmd_expire(const command_args_t args, void (*response_cb)(client_t *client))
{
    if (strncasecmp(args.cmd, "EXPIRE ", 7) != 0) {
        return;
    }

    char key[MAX_KEY_LEN];
    char seconds[MAX_VALUE_LEN];
    if (sscanf(args.cmd, "EXPIRE %511s %511s", key, seconds) != 2) {
        printf("(error) ERR wrong number of arguments for 'expire' command\n");
        printf("(info) Usage: EXPIRE <key> <seconds>\n");
        return;
    }

    size_t cmd_len;
    unsigned char *binary_cmd = construct_expire_command(key, seconds, &cmd_len);
    if (binary_cmd == NULL) {
        fprintf(stderr, "Failed to construct EXPIRE command\n");
        return;
    }

    assert(cmd_len > 0);
    assert(args.client->fd > 0);

    send(args.client->fd, binary_cmd, cmd_len, 0);
    free(binary_cmd);
    response_cb(args.client);
}

void cmd_ttl(const command_args_t args, void (*response_cb)(client_t *client))
{
    if (strncasecmp(args.cmd, "TTL ", 4) != 0) {
        return;
    }

    char key[MAX_KEY_LEN];
    if (sscanf(args.cmd, "TTL %511s", key) != 1) {
        printf("(error) ERR wrong number of arguments for 'ttl' command\n");
        printf("(info) Usage: TTL <key>\n");
        return;
    }

    size_t cmd_len;
    unsigned char *binary_cmd = construct_ttl_command(key, &cmd_len);
    if (binary_cmd == NULL) {
        fprintf(stderr, "Failed to construct TTL command\n");
        return;
    }

    assert(cmd_len > 0);
    assert(args.client->fd > 0);

    send(args.client->fd, binary_cmd, cmd_len, 0);
    free(binary_cmd);
    response_cb(args.client);
}

void cmd_persist(const command_args_t args, void (*response_cb)(client_t *client))
{
    if (strncasecmp(args.cmd, "PERSIST ", 8) != 0) {
        return;
    }

    char key[MAX_KEY_LEN];
    if (sscanf(args.cmd, "PERSIST %511s", key) != 1) {
        printf("(error) ERR wrong number of arguments for 'persist' command\n");
        printf("(info) Usage: PERSIST <key>\n");
        return;
    }

    size_t cmd_len;
    unsigned char *binary_cmd = construct_persist_command(key, &cmd_len);
    if (binary_cmd == NULL) {
        fprintf(stderr, "Failed to construct PERSIST command\n");
        return;
    }

    assert(cmd_len > 0);
    assert(args.client->fd > 0);

    send(args.client->fd, binary_cmd, cmd_len, 0);
    free(binary_cmd);
    response_cb(args.client);
}

void cmd_keys(const command_args_t args, void (*response_cb)(client_t *client))
{
    if (strncasecmp(args.cmd, "KEYS", 4) != 0) {
        return;
    }

    size_t cmd_len;
    unsigned char *keys_cmd = construct_keys_command(&cmd_len);
    if (!keys_cmd) {
        fprintf(stderr, "Failed to construct KEYS command\n");
        return;
    }

    assert(cmd_len > 0);
    assert(args.client->fd > 0);

    send(args.client->fd, keys_cmd, cmd_len, 0);
    free(keys_cmd);
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
    if (strncmp(args.cmd, "INCR ", 5) &&
        strncmp(args.cmd, "INCRBY ", 6) &&
        strncmp(args.cmd, "GET ", 4) &&
        strncmp(args.cmd, "SET ", 4) &&
        strncmp(args.cmd, "PING", 4) &&
        strncmp(args.cmd, "PING ", 5) &&
        strncmp(args.cmd, "DECR ", 5) &&
        strncmp(args.cmd, "DECRBY ", 7) &&
        strncmp(args.cmd, "DEL ", 4) &&
        strncmp(args.cmd, "EXPIRE ", 7) &&
        strncmp(args.cmd, "TTL ", 4) &&
        strncmp(args.cmd, "PERSIST ", 8) &&
        strncmp(args.cmd, "INFO", 5) &&
        strncmp(args.cmd, "KEYS", 4)) {
        printf("Unknown command \n");
    }
}

const cmd_t command_table[] = {
    {"cmd_set", cmd_set},         {"cmd_get", cmd_get},
    {"cmd_decr", cmd_decr},       {"cmd_decr_by", cmd_decr_by},
    {"cmd_incr", cmd_incr},
    {"cmd_incr_by", cmd_incr_by}, {"cmd_ping", cmd_ping},
    {"cmd_del", cmd_del},         {"cmd_expire", cmd_expire},
    {"cmd_ttl", cmd_ttl},         {"cmd_persist", cmd_persist},
    {"cmd_unknown", cmd_unknown},
    {"cmd_info", cmd_info},
    {"cmd_keys", cmd_keys}};

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
            char key[MAX_KEY_LEN];
            generate_unique_key(key);

            binary_cmd = construct_set_command(key, "world", &cmd_len);
        } else {
            binary_cmd = construct_set_command("hello", "world", &cmd_len);
        }

        assert(client->fd > 0);
        assert(cmd_len > 0);

        send(args.client->fd, binary_cmd, cmd_len, 0);
        free(binary_cmd);
        response_cb(args.client);
    } else if (!strcasecmp(cmd, "ping")) {
        unsigned char *binary_cmd = construct_ping_command("", &cmd_len);

        assert(client->fd > 0);
        assert(cmd_len > 0);

        send(args.client->fd, binary_cmd, cmd_len, 0);
        free(binary_cmd);
        response_cb(args.client);
    }
}

void send_command_benchmark(const char *cmd, client_t *client,
                            bool use_random_keys)
{
    size_t cmd_len;
    unsigned char *binary_cmd = NULL;

    if (!strcasecmp(cmd, "set")) {
        if (use_random_keys) {
            char key[MAX_KEY_LEN];
            generate_unique_key(key);
            binary_cmd = construct_set_command(key, "world", &cmd_len);
        } else {
            binary_cmd = construct_set_command("hello", "world", &cmd_len);
        }
    } else if (!strcasecmp(cmd, "ping")) {
        binary_cmd = construct_ping_command("", &cmd_len);
    }

    if (binary_cmd == NULL)
        return;

    assert(client->fd > 0);
    assert(cmd_len > 0);

    send(client->fd, binary_cmd, cmd_len, 0);
    free(binary_cmd);
}

uint64_t recv_pipeline_responses(client_t *client, uint64_t count)
{
    uint64_t received = 0;

    while (received < count) {
        // Try to parse complete frames from what we already have buffered
        while (received < count && client->buf_used >= 2) {
            if (client->frame_need < 0) {
                uint16_t core_len =
                    ((uint16_t)client->buffer[0] << 8) | client->buffer[1];
                client->frame_need = 2 + (ssize_t)core_len;
                if ((size_t)client->frame_need > sizeof(client->buffer)) {
                    // Frame too large, reset and bail
                    client->buf_used = 0;
                    client->frame_need = -1;
                    return received;
                }
            }

            if ((ssize_t)client->buf_used < client->frame_need)
                break; // need more data

            // Consume one complete frame
            const size_t frame_len = (size_t)client->frame_need;
            size_t remain = client->buf_used - frame_len;
            if (remain)
                memmove(client->buffer, client->buffer + frame_len, remain);
            client->buf_used = remain;
            client->frame_need = -1;
            received++;
        }

        if (received >= count)
            break;

        // Need more data from the socket
        size_t space = sizeof(client->buffer) - client->buf_used;
        if (space == 0) {
            // Buffer full but no complete frame — protocol error
            client->buf_used = 0;
            client->frame_need = -1;
            return received;
        }

        ssize_t n = recv(client->fd, client->buffer + client->buf_used,
                         space, 0);
        if (n <= 0) {
            // Connection error or timeout
            return received;
        }
        client->buf_used += (size_t)n;
    }

    return received;
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
                if (client->buffer[2] == STATUS_FAILURE) {
                    if (!client->benchmark_mode) {
                        printf("(nil) \n");
                    }
                } else if (client->buffer[2] == CMD_INFO) {
                        const size_t value_len = client->buffer[0] << 8 | client->buffer[1];
                        char *data = malloc(value_len + 1);
                        if (data) {
                            memcpy(data, &client->buffer[5], value_len);
                            data[value_len] = '\0';
                            printf("%s\n", data);
                            free(data);
                        } else {
                            printf("Memory allocation failed\n");
                        }

                    } else if (client->buffer[2] == CMD_KEYS) {
                        const size_t value_len =
                            client->buffer[3] << 8 | client->buffer[4];
                        if (value_len == 0) {
                            printf("(empty list)\n");
                        } else {
                            char *data = malloc(value_len + 1);
                            if (data) {
                                memcpy(data, &client->buffer[5], value_len);
                                data[value_len] = '\0';
                                printf("%s\n", data);
                                free(data);
                            }
                        }

                    } else if (client->buffer[2] == CMD_PING) {
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
                        data[value_len] = '\0';
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
                    data[value_len] = '\0';
                    if (value_len == 0) {
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
        } else if (bytes_received == 1 ||
                   (bytes_received >= 3 && client->buffer[2] == STATUS_FAILURE)) {
            if (!client->benchmark_mode) {
                printf("(nil) \n");
            }
        }
    }
}
