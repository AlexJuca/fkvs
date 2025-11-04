#include "../../commands/server/server_command_handlers.h"
#include "../../core/hashtable.h"
#include "../../response_defs.h"
#include "../../utils.h"
#include "../common/command_defs.h"
#include "../common/command_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <sys/socket.h>

static hashtable_t *table = NULL;

void init_command_handlers(hashtable_t *ht)
{
    table = ht;
    register_command(CMD_SET, handle_set_command);
    register_command(CMD_GET, handle_get_command);
    register_command(CMD_INCR, handle_incr_command);
    register_command(CMD_INCR_BY, handle_incr_by_command);
    register_command(CMD_PING, handle_ping_command);
    register_command(CMD_DECR, handle_decr_command);
}

void handle_set_command(int client_fd, unsigned char *buffer, size_t bytes_read)
{
    if (server.verbose) {
        printf("Server received %d bytes from client %d \n", (int)bytes_read,
               client_fd);
        print_binary_data(buffer, bytes_read);
    }
    // Need at least: core_len(2) + cmd(1) + key_len(2)
    if (bytes_read < 5) {
        const unsigned char fail[] = {STATUS_FAILURE};
        (void)send(client_fd, fail, sizeof fail, 0);
        fprintf(stderr, "Incomplete SET: header too short\n");
        return;
    }

    // Total bytes expected = 2 + core_len
    const uint16_t core_len = ((uint16_t)buffer[0] << 8) | buffer[1];
    const size_t total_needed = (size_t)core_len + 2;
    if (bytes_read < total_needed) {
        unsigned char fail[] = {STATUS_FAILURE};
        (void)send(client_fd, fail, sizeof fail, 0);
        fprintf(stderr,
                "Incomplete SET: message shorter than advertised core_len\n");
        return;
    }

    if (buffer[2] != CMD_SET) {
        const unsigned char fail[] = {STATUS_FAILURE};
        (void)send(client_fd, fail, sizeof fail, 0);
        fprintf(stderr, "SET parse error: wrong command byte (%u)\n",
                (unsigned)buffer[2]);
        return;
    }

    // Key length
    const uint16_t key_len = ((uint16_t)buffer[3] << 8) | buffer[4];

    // Offsets inside the full buffer
    const size_t pos_key = 5;                   // start of key bytes
    const size_t after_key = pos_key + key_len; // first byte after key

    // Ensure key bytes are present inside the advertised core
    // payload layout size up to the end of key_len field is: 1(cmd) +
    // 2(key_len) + key_len
    const size_t min_core_up_to_key = (size_t)1 + 2 + key_len;
    if (min_core_up_to_key > core_len) {
        send_error(client_fd);
        fprintf(stderr, "Incomplete SET: key bytes exceed core_len\n");
        return;
    }

    // Need the 2-byte value_len after the key
    if ((after_key + 2) > bytes_read) {
        send_error(client_fd);
        fprintf(stderr, "Incomplete SET: missing value_len\n");
        return;
    }

    // Value length lives immediately after the key
    uint16_t value_len =
        ((uint16_t)buffer[after_key] << 8) | buffer[after_key + 1];
    const size_t pos_value = after_key + 2; // start of value bytes
    const size_t end_value = pos_value + value_len;

    // Check that the whole value fits inside the core and the
    // received buffer
    const size_t core_payload_size = (size_t)1 + 2 + key_len + 2 + value_len;
    if (core_payload_size > core_len || (end_value + 0) > bytes_read) {
        send_error(client_fd);
        fprintf(stderr, "Incomplete SET: value bytes exceed bounds\n");
        return;
    }

    char *data = malloc(value_len + 1);
    memcpy(data, &buffer[pos_value], value_len);

    if (server.verbose) {
        printf("Wrote value '%s' to database \n", data);
        printf("Wrote %d bytes to database \n", value_len);
    }

    if (!is_integer(&buffer[pos_value], value_len)) {
        set_value(table, &buffer[pos_key], key_len, &buffer[pos_value],
                  value_len, VALUE_ENTRY_TYPE_RAW);
    }

    set_value(table, &buffer[pos_key], key_len, &buffer[pos_value], value_len,
              VALUE_ENTRY_TYPE_INT);

    send_reply(client_fd, &buffer[pos_value], value_len);
    free(data);
}

void handle_get_command(int client_fd, unsigned char *buffer, size_t bytes_read)
{
    const size_t command_len = buffer[0] << 8 | buffer[1];

    const size_t key_len = buffer[3] << 8 | buffer[4];

    if (bytes_read - 2 == command_len) {
        value_entry_t *value;
        size_t value_len;
        if (get_value(table, &buffer[5], key_len, &value, &value_len)) {
            unsigned char *resp_buffer = malloc(value->value_len + 1);
            if (!resp_buffer) {
                send_error(client_fd);
                perror("malloc failed");
                free(buffer);
            }

            memcpy(resp_buffer, value->ptr, value_len);
            resp_buffer[value_len] = '\0';
            printf("Returning: %s \n", (const char *)resp_buffer);
            send_reply(client_fd, resp_buffer, value_len);
            free(resp_buffer);
        } else {
            send_error(client_fd);
        }
    } else {
        fprintf(stderr, "Incomplete command data for GET.\n");
        send_error(client_fd);
    }
}

void handle_incr_command(int client_fd, unsigned char *buffer,
                         size_t bytes_read)
{
    const size_t command_len = (buffer[0] << 8) | buffer[1];
    const size_t key_len = (buffer[3] << 8) | buffer[4];

    if (bytes_read - 2 != command_len) {
        fprintf(stderr, "Incomplete command data for INCR.\n");
        send_error(client_fd);
        return;
    }

    value_entry_t *value;
    size_t value_len;

    if (!get_value(table, &buffer[5], key_len, &value, &value_len)) {
        send_error(client_fd);
        return;
    }

    if (value->encoding != VALUE_ENTRY_TYPE_INT) {
        fprintf(stderr, "Stored value is not an integer.\n");
        send_error(client_fd);
        free(value);
        return;
    }

    const uint64_t current = strtoull(value->ptr, NULL, 10);
    const uint64_t sum = current + 1;

    if (server.verbose) {
        printf("Value incremented to %llu\n", sum);
    }

    char *reply = uint64_to_string(sum);
    const size_t reply_len = strlen(reply);

    if (!set_value(table, &buffer[5], key_len, reply, reply_len,
                   VALUE_ENTRY_TYPE_INT)) {
        fprintf(stderr, "Unable to set incremented value.\n");
        send_error(client_fd);
        free(reply);
        return;
    }

    send_reply(client_fd, (const unsigned char *)reply, reply_len);
    free(reply);
}

void handle_incr_by_command(const int client_fd, unsigned char *buffer,
                            const size_t bytes_read)
{
    const size_t command_len = (buffer[0] << 8) | buffer[1];
    const size_t key_len = (buffer[3] << 8) | buffer[4];
    const size_t pos = 5 + key_len;

    if (pos + 2 > bytes_read) {
        fprintf(stderr, "Invalid buffer: too short for value length.\n");
        send_error(client_fd);
        return;
    }

    const size_t incr_len = (buffer[pos] << 8) | buffer[pos + 1];
    if (pos + 2 + incr_len > bytes_read) {
        fprintf(stderr, "Invalid buffer: too short for increment.\n");
        send_error(client_fd);
        return;
    }

    unsigned char *incr_str = malloc(pos + 2 + incr_len);
    if (!incr_str) {
        send_error(client_fd);
        return;
    }

    memcpy(incr_str, buffer + pos + 2, incr_len);

    if (!is_integer(incr_str, incr_len)) {
        fprintf(stderr, "Increment value is not an integer.\n");
        send_error(client_fd);
        return;
    }

    if (bytes_read - 2 != command_len) {
        fprintf(stderr, "Incomplete command data for INCR_BY.\n");
        send_error(client_fd);
        return;
    }

    value_entry_t *old_value;
    size_t old_value_len;
    if (!get_value(table, &buffer[5], key_len, &old_value, &old_value_len)) {
        send_error(client_fd);
        return;
    }

    if (old_value->encoding != VALUE_ENTRY_TYPE_INT) {
        fprintf(stderr, "Stored value is not an integer.\n");
        send_error(client_fd);
        free(old_value);
        return;
    }

    const uint64_t current = strtoull(old_value->ptr, NULL, 10);
    const uint64_t increment = strtoull((const char *)incr_str, NULL, 10);

    const uint64_t sum = current + increment;
    if (server.verbose) {
        printf("Value incremented to %llu\n", sum);
    }

    const char *result = uint64_to_string(sum);
    const size_t result_len = strlen(result);

    if (!set_value(table, &buffer[5], key_len, (unsigned char *)result,
                   result_len, VALUE_ENTRY_TYPE_INT)) {
        fprintf(stderr, "Unable to set incremented value.\n");
        send_error(client_fd);
        free(old_value);
        return;
    }

    send_reply(client_fd, (unsigned char *)result, result_len);
    free(old_value);
}

void handle_ping_command(int client_fd, unsigned char *buffer,
                         size_t bytes_read)
{
    const size_t command_len = buffer[0] << 8 | buffer[1];

    if (server.verbose) {
        printf("Server received %d bytes from client %d \n", (int)bytes_read,
               client_fd);
        print_binary_data(buffer, bytes_read);
    }

    if (bytes_read - 2 == command_len) {
        send_pong(client_fd, buffer);
    } else {
        fprintf(stderr, "Incomplete command data for PING.\n");
        send_error(client_fd);
    }
}

void handle_decr_command(int client_fd, unsigned char *buffer,
                         size_t bytes_read)
{
    const size_t command_len = (buffer[0] << 8) | buffer[1];
    const size_t key_len = (buffer[3] << 8) | buffer[4];

    if (bytes_read - 2 != command_len) {
        fprintf(stderr, "Incomplete command data for DECR.\n");
        send_error(client_fd);
        return;
    }

    value_entry_t *value;
    size_t value_len;

    if (!get_value(table, &buffer[5], key_len, &value, &value_len)) {
        send_error(client_fd);
        return;
    }

    if (!is_integer(value->ptr, value_len)) {
        fprintf(stderr, "Stored value is not an integer.\n");
        send_error(client_fd);
        free(value);
        return;
    }

    const int64_t current = strtoll(value->ptr, NULL, 10);
    const int64_t decrement = current - 1;

    const char *result_str = int64_to_string(decrement);
    const size_t result_len = strlen(result_str);

    if (!set_value(table, &buffer[5], key_len, (unsigned char *)result_str,
                   result_len, VALUE_ENTRY_TYPE_INT)) {
        fprintf(stderr, "Unable to set incremented value.\n");
        send_error(client_fd);
        return;
    }

    send_reply(client_fd, (unsigned char *)result_str, result_len);
    free(value);
}