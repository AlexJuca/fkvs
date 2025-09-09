#include "server_command_handlers.h"
#include "command_defs.h"
#include "command_registry.h"
#include "hashtable.h"
#include "response_defs.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

static HashTable *table = NULL;

void init_command_handlers(HashTable *ht)
{
    table = ht;
    register_command(CMD_SET, handle_set_command);
    register_command(CMD_GET, handle_get_command);
    register_command(CMD_INCR, handle_incr_command);
}

void handle_set_command(int client_fd, unsigned char *buffer, size_t bytes_read)
{
    // Need at least: core_len(2) + cmd(1) + key_len(2)
    if (bytes_read < 5) {
        unsigned char fail[] = {STATUS_FAILURE};
        (void)send(client_fd, fail, sizeof fail, 0);
        fprintf(stderr, "Incomplete SET: header too short\n");
        return;
    }

    // Total bytes expected = 2 + core_len
    uint16_t core_len = ((uint16_t)buffer[0] << 8) | buffer[1];
    size_t total_needed = (size_t)core_len + 2;
    if (bytes_read < total_needed) {
        unsigned char fail[] = {STATUS_FAILURE};
        (void)send(client_fd, fail, sizeof fail, 0);
        fprintf(stderr,
                "Incomplete SET: message shorter than advertised core_len\n");
        return;
    }

    // Command byte
    if (buffer[2] != CMD_SET) {
        unsigned char fail[] = {STATUS_FAILURE};
        (void)send(client_fd, fail, sizeof fail, 0);
        fprintf(stderr, "SET parse error: wrong command byte (%u)\n",
                (unsigned)buffer[2]);
        return;
    }

    // Key length
    uint16_t key_len = ((uint16_t)buffer[3] << 8) | buffer[4];

    // Offsets inside the full buffer
    size_t pos_key = 5;                   // start of key bytes
    size_t after_key = pos_key + key_len; // first byte after key

    // Ensure key bytes are present inside the advertised core
    // Core payload layout size up to the end of key_len field is: 1(cmd) +
    // 2(key_len) + key_len
    size_t min_core_up_to_key = (size_t)1 + 2 + key_len;
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
    size_t pos_value = after_key + 2; // start of value bytes
    size_t end_value = pos_value + value_len;

    // Check that the whole value fits inside the *advertised* core and the
    // received buffer
    size_t core_payload_size = (size_t)1 + 2 + key_len + 2 + value_len;
    if (core_payload_size > core_len || (end_value + 0) > bytes_read) {
        send_error(client_fd);
        fprintf(stderr, "Incomplete SET: value bytes exceed bounds\n");
        return;
    }

    char *data = malloc(value_len + 1);
    memcpy(data, &buffer[pos_value], value_len);

    printf("Wrote value '%s' to database \n", data);
    printf("Wrote %d bytes to database \n", value_len);

    set_value(table, &buffer[pos_key], key_len, &buffer[pos_value], value_len);

    send_reply(client_fd, buffer, value_len);
}

void handle_get_command(int client_fd, unsigned char *buffer, size_t bytes_read)
{
    size_t command_len = buffer[0] << 8 | buffer[1];

    size_t key_len = buffer[3] << 8 | buffer[4];

    if (bytes_read - 2 == command_len) {
        unsigned char *value;
        size_t value_len;
        if (get_value(table, &buffer[5], key_len, &value, &value_len)) {
            send_reply(client_fd, value, value_len);
            free(value);
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
    size_t command_len = buffer[0] << 8 | buffer[1];

    size_t key_len = buffer[3] << 8 | buffer[4];

    if (bytes_read - 2 == command_len) {
        unsigned char *value;
        size_t value_len;
        if (get_value(table, &buffer[5], key_len, &value, &value_len)) {
            if (!is_integer(value, value_len)) {
                fprintf(stderr, "value is not an integer.\n");
                send_error(client_fd);
                free(value);
                return;
            }

            if (!set_value(table, &buffer[5], key_len, value, value_len)) {
                fprintf(stderr, "unable to increment value.\n");
            }

            char *ptr;
            const long long parsed_value =
                strtoll((const char *)value, &ptr, 10);

            const uint64_t number = (uint64_t)parsed_value + 1;
            unsigned char *incremented_number =
                (unsigned char *)int_to_string(number);

            set_value(table, &buffer[5], key_len, incremented_number,
                      value_len);
            send_reply(client_fd, incremented_number, value_len);

            free(value);
        } else {
            send_error(client_fd);
        }
    } else {
        fprintf(stderr, "Incomplete command data for GET.\n");
        send_error(client_fd);
    }
}
