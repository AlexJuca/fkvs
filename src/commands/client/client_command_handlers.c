#include "../../commands/client/client_command_handlers.h"
#include "../../client.h"
#include "../../commands/common/command_defs.h"
#include "../../commands/common/command_parser.h"
#include "../../response_defs.h"
#include "../../keygen.h"
#include "../../utils.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

#define MAX_KEY_LEN 512
#define MAX_VALUE_LEN 512

static bool command_equals(const char *input, const char *expected)
{
    while (isspace((unsigned char)*input))
        input++;

    size_t input_len = strlen(input);
    while (input_len > 0 &&
           isspace((unsigned char)input[input_len - 1])) {
        input_len--;
    }

    const size_t expected_len = strlen(expected);
    return input_len == expected_len &&
           strncasecmp(input, expected, expected_len) == 0;
}

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
    if (!command_equals(args.cmd, "KEYS")) {
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
    (void)response_cb;

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
        !command_equals(args.cmd, "KEYS")) {
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
    for (size_t i = 0; i < ARRAY_SIZE(command_table); i++) {
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

typedef enum {
    CLIENT_RESPONSE_ERROR,
    CLIENT_RESPONSE_OK,
    CLIENT_RESPONSE_VALUE,
    CLIENT_RESPONSE_PONG,
    CLIENT_RESPONSE_INFO,
    CLIENT_RESPONSE_KEYS,
} client_response_kind_t;

typedef struct {
    client_response_kind_t kind;
    const unsigned char *payload;
    size_t payload_len;
} client_response_t;

static bool decode_value_response(const unsigned char *frame,
                                  const size_t frame_len,
                                  const uint16_t core_len,
                                  const client_response_kind_t kind,
                                  client_response_t *response)
{
    if (core_len < 3 || frame_len < 5)
        return false;

    const size_t payload_len = ((size_t)frame[3] << 8) | frame[4];
    if (payload_len != (size_t)core_len - 3)
        return false;
    if (frame_len < 5 + payload_len)
        return false;

    response->kind = kind;
    response->payload = &frame[5];
    response->payload_len = payload_len;
    return true;
}

static bool decode_response_frame(const unsigned char *frame,
                                  const size_t frame_len,
                                  client_response_t *response)
{
    if (frame_len < 3)
        return false;

    const uint16_t core_len = ((uint16_t)frame[0] << 8) | frame[1];
    const size_t total_len = (size_t)core_len + 2;
    if (frame_len < total_len)
        return false;

    const unsigned char response_type = frame[2];
    if (core_len == 1) {
        if (response_type == STATUS_FAILURE) {
            response->kind = CLIENT_RESPONSE_ERROR;
            response->payload = NULL;
            response->payload_len = 0;
            return true;
        }
        if (response_type == STATUS_SUCCESS) {
            response->kind = CLIENT_RESPONSE_OK;
            response->payload = NULL;
            response->payload_len = 0;
            return true;
        }
        return false;
    }

    switch (response_type) {
    case STATUS_SUCCESS:
        return decode_value_response(frame, total_len, core_len,
                                     CLIENT_RESPONSE_VALUE, response);
    case CMD_PING:
        return decode_value_response(frame, total_len, core_len,
                                     CLIENT_RESPONSE_PONG, response);
    case CMD_INFO:
        return decode_value_response(frame, total_len, core_len,
                                     CLIENT_RESPONSE_INFO, response);
    case CMD_KEYS:
        return decode_value_response(frame, total_len, core_len,
                                     CLIENT_RESPONSE_KEYS, response);
    default:
        return false;
    }
}

static bool recv_exact(const int fd, unsigned char *buffer, const size_t len)
{
    size_t used = 0;
    while (used < len) {
        const ssize_t n = recv(fd, buffer + used, len - used, 0);
        if (n > 0) {
            used += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        return false;
    }
    return true;
}

static bool read_response(client_t *client, client_response_t *response)
{
    if (!recv_exact(client->fd, client->buffer, 2))
        return false;

    const uint16_t core_len =
        ((uint16_t)client->buffer[0] << 8) | client->buffer[1];
    const size_t total_len = (size_t)core_len + 2;
    if (total_len > BUFFER_SIZE)
        return false;

    if (!recv_exact(client->fd, client->buffer + 2, core_len))
        return false;

    return decode_response_frame(client->buffer, total_len, response);
}

static void print_quoted_payload(const unsigned char *payload,
                                 const size_t payload_len)
{
    printf("\"%.*s\" \n", (int)payload_len, (const char *)payload);
}

static void print_plain_payload_line(const unsigned char *payload,
                                     const size_t payload_len)
{
    printf("%.*s\n", (int)payload_len, (const char *)payload);
}

static void print_response(const client_response_t *response,
                           const bool benchmark_mode)
{
    switch (response->kind) {
    case CLIENT_RESPONSE_ERROR:
        if (!benchmark_mode)
            printf("(nil) \n");
        break;
    case CLIENT_RESPONSE_OK:
        if (!benchmark_mode)
            printf("OK\n");
        break;
    case CLIENT_RESPONSE_VALUE:
        if (benchmark_mode)
            break;
        if (response->payload_len == 0) {
            printf("OK\n");
        } else {
            print_quoted_payload(response->payload, response->payload_len);
        }
        break;
    case CLIENT_RESPONSE_PONG:
        if (benchmark_mode)
            break;
        if (response->payload_len == 0) {
            printf("PONG\n");
        } else {
            print_quoted_payload(response->payload, response->payload_len);
        }
        break;
    case CLIENT_RESPONSE_INFO:
        print_plain_payload_line(response->payload, response->payload_len);
        break;
    case CLIENT_RESPONSE_KEYS:
        if (response->payload_len == 0) {
            printf("(empty list)\n");
        } else {
            print_plain_payload_line(response->payload, response->payload_len);
        }
        break;
    }
}

void command_response_handler(client_t *client)
{
    client_response_t response;
    if (!read_response(client, &response))
        return;

    print_response(&response, client->benchmark_mode);
}
