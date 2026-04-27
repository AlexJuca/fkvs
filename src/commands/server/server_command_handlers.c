#include "../../commands/server/server_command_handlers.h"
#include "../../core/hashtable.h"
#include "../../numeric_parse.h"
#include "../../response_defs.h"
#include "../../ttl.h"
#include "../../utils.h"
#include "../common/command_defs.h"
#include "../common/command_registry.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <sys/socket.h>

static hashtable_t *table = NULL;
static hashtable_t *expires = NULL;

static void free_value_copy(value_entry_t *value)
{
    if (!value)
        return;

    free(value->ptr);
    free(value);
}

static bool check_and_expire(const unsigned char *key, size_t key_len)
{
    if (is_expired(expires, key, key_len)) {
        delete_value(table, key, key_len);
        delete_value(expires, key, key_len);
        return true;
    }
    return false;
}

void init_command_handlers(db_t *db)
{
    table = db->store;
    expires = db->expires;
    register_command(CMD_SET, handle_set_command);
    register_command(CMD_GET, handle_get_command);
    register_command(CMD_INCR, handle_incr_command);
    register_command(CMD_INCR_BY, handle_incr_by_command);
    register_command(CMD_PING, handle_ping_command);
    register_command(CMD_DECR, handle_decr_command);
    register_command(CMD_INFO, handle_info_command);
    register_command(CMD_DECR_BY, handle_decr_by_command);
    register_command(CMD_DEL, handle_del_command);
    register_command(CMD_EXPIRE, handle_expire_command);
    register_command(CMD_TTL, handle_ttl_command);
    register_command(CMD_PERSIST, handle_persist_command);
}

void handle_set_command(client_t *client, unsigned char *buffer, size_t bytes_read)
{
    if (server.verbose) {
        printf("Server received %d bytes from client %d \n", (int)bytes_read,
               client->fd);
        print_binary_data(buffer, bytes_read);
    }

    // Need at least: core_len(2) + cmd(1) + key_len(2)
    if (bytes_read < 5) {
        send_error(client);
        fprintf(stderr, "Incomplete SET: header too short\n");
        return;
    }

    // Total bytes expected = 2 + core_len
    const uint16_t core_len = ((uint16_t)buffer[0] << 8) | buffer[1];
    const size_t total_needed = (size_t)core_len + 2;
    if (bytes_read < total_needed) {
        send_error(client);
        fprintf(stderr,
                "Incomplete SET: message shorter than advertised core_len\n");
        return;
    }

    if (buffer[2] != CMD_SET) {
        send_error(client);
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
    const size_t min_core_up_to_key = (size_t)1 + 2 + key_len;
    if (min_core_up_to_key > core_len) {
        send_error(client);
        fprintf(stderr, "Incomplete SET: key bytes exceed core_len\n");
        return;
    }

    // Need the 2-byte value_len after the key
    if ((after_key + 2) > bytes_read) {
        send_error(client);
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
        send_error(client);
        fprintf(stderr, "Incomplete SET: value bytes exceed bounds\n");
        return;
    }

    char *data = malloc(value_len + 1);
    if (!data) {
        send_error(client);
        return;
    }
    memcpy(data, &buffer[pos_value], value_len);
    data[value_len] = '\0';

    if (server.verbose) {
        printf("Wrote value '%s' to database \n", data);
        printf("Wrote %d bytes to database \n", value_len);
    }

    // Check for optional inline EX (extra bytes after value in core)
    bool has_expiry = false;
    int64_t deadline_ms = 0;
    if (core_payload_size < core_len) {
        // Parse [ex_len:2][ex_str]
        const size_t ex_offset = end_value;
        if (ex_offset + 2 > bytes_read) {
            send_error(client);
            fprintf(stderr, "Incomplete SET EX: missing ex_len\n");
            free(data);
            return;
        }

        const uint16_t ex_len =
            ((uint16_t)buffer[ex_offset] << 8) | buffer[ex_offset + 1];
        const size_t pos_ex = ex_offset + 2;

        if (pos_ex + ex_len > bytes_read) {
            send_error(client);
            fprintf(stderr, "Incomplete SET EX: ex bytes exceed bounds\n");
            free(data);
            return;
        }

        char sec_buf[32];
        size_t copy_len =
            ex_len < sizeof(sec_buf) - 1 ? ex_len : sizeof(sec_buf) - 1;
        memcpy(sec_buf, &buffer[pos_ex], copy_len);
        sec_buf[copy_len] = '\0';

        if (copy_len != ex_len ||
            !fkvs_parse_deadline_ms((const unsigned char *)sec_buf, copy_len,
                                    fkvs_now_ms(), &deadline_ms)) {
            send_error(client);
            fprintf(stderr, "Invalid SET EX ttl value\n");
            free(data);
            return;
        }
        has_expiry = true;
    }

    int64_t parsed_integer;
    const int value_encoding = fkvs_parse_i64_decimal(
                                   &buffer[pos_value], value_len, INT64_MIN,
                                   INT64_MAX, &parsed_integer)
                                   ? VALUE_ENTRY_TYPE_INT
                                   : VALUE_ENTRY_TYPE_RAW;

    value_entry_t *old_value = NULL;
    value_entry_t *old_expiry = NULL;
    size_t old_value_len = 0;
    size_t old_expiry_len = 0;
    const bool had_value =
        has_expiry && get_value(table, &buffer[pos_key], key_len, &old_value,
                                &old_value_len);
    const bool had_expiry =
        has_expiry && get_value(expires, &buffer[pos_key], key_len,
                                &old_expiry, &old_expiry_len);

    if (!set_value(table, &buffer[pos_key], key_len, &buffer[pos_value],
                   value_len, value_encoding)) {
        send_error(client);
        fprintf(stderr, "Unable to store SET value\n");
        free_value_copy(old_value);
        free_value_copy(old_expiry);
        free(data);
        return;
    }

    if (has_expiry) {
        if (!set_expiry(expires, &buffer[pos_key], key_len, deadline_ms)) {
            send_error(client);
            fprintf(stderr, "Unable to store SET EX ttl\n");
            if (had_value) {
                (void)set_value(table, &buffer[pos_key], key_len,
                                old_value->ptr, old_value->value_len,
                                old_value->encoding);
            } else {
                delete_value(table, &buffer[pos_key], key_len);
            }
            if (had_expiry) {
                (void)set_value(expires, &buffer[pos_key], key_len,
                                old_expiry->ptr, old_expiry->value_len,
                                old_expiry->encoding);
            } else {
                delete_value(expires, &buffer[pos_key], key_len);
            }
            free_value_copy(old_value);
            free_value_copy(old_expiry);
            free(data);
            return;
        }
    } else {
        // SET clears any existing TTL (matching Redis behavior)
        remove_expiry(expires, &buffer[pos_key], key_len);
    }

    send_reply(client, &buffer[pos_value], value_len);
    free_value_copy(old_value);
    free_value_copy(old_expiry);
    free(data);
}

void handle_get_command(client_t *client, unsigned char *buffer, size_t bytes_read)
{
    if (bytes_read < 5) {
        send_error(client);
        return;
    }

    const size_t command_len = buffer[0] << 8 | buffer[1];

    const size_t key_len = buffer[3] << 8 | buffer[4];

    if (buffer[2] != CMD_GET) {
        send_error(client);
        return;
    }

    if (bytes_read - 2 == command_len) {
        // Lazy expiry check
        if (check_and_expire(&buffer[5], key_len)) {
            send_error(client);
            return;
        }

        value_entry_t *value;
        size_t value_len;
        if (get_value(table, &buffer[5], key_len, &value, &value_len)) {
            unsigned char *resp_buffer = malloc(value->value_len + 1);
            if (!resp_buffer) {
                send_error(client);
                perror("malloc failed");
                free_value_copy(value);
                return;
            }

            memcpy(resp_buffer, value->ptr, value_len);

            resp_buffer[value_len] = '\0';
            send_reply(client, resp_buffer, value_len);
            free(resp_buffer);
            free_value_copy(value);
        } else {
            send_error(client);
        }
    } else {
        fprintf(stderr, "Incomplete command data for GET.\n");
        send_error(client);
    }
}

void handle_incr_command(client_t *client, unsigned char *buffer,
                         size_t bytes_read)
{
    if (bytes_read < 5) {
        send_error(client);
        return;
    }

    const size_t command_length = (buffer[0] << 8) | buffer[1];
    const size_t key_len = buffer[3] << 8 | buffer[4];
    const size_t offset = 2;

    if (key_len < 1 || command_length < 1) {
        send_error(client);
        return;
    }
    if (buffer[2] != CMD_INCR) {
        send_error(client);
        return;
    }

    if (bytes_read - offset != command_length) {
        fprintf(stderr, "Incomplete command data for INCR.\n");
        send_error(client);
        return;
    }

    // Lazy expiry: if expired, treat as nonexistent
    check_and_expire(&buffer[5], key_len);

    value_entry_t *value;
    size_t value_len;

    if (!get_value(table, &buffer[5], key_len, &value, &value_len)) {
        const char *default_value = "0";
        const size_t default_value_len = strlen(default_value);
        if (!set_value(table, &buffer[5], key_len,
                       (unsigned char *)default_value, default_value_len,
                       VALUE_ENTRY_TYPE_INT)) {
            fprintf(stderr, "Unable to set default value.\n");
            send_error(client);
            return;
        }
        if (!get_value(table, &buffer[5], key_len, &value, &value_len)) {
            send_error(client);
            return;
        }
    }

    if (value->encoding != VALUE_ENTRY_TYPE_INT) {
        fprintf(stderr, "Stored value is not an integer.\n");
        send_error(client);
        free_value_copy(value);
        return;
    }

    int64_t current;
    if (!fkvs_parse_i64_decimal(value->ptr, value_len, INT64_MIN, INT64_MAX,
                                &current) ||
        current == INT64_MAX) {
        fprintf(stderr, "Stored integer is out of range.\n");
        send_error(client);
        free_value_copy(value);
        return;
    }
    const int64_t sum = current + 1;

    if (server.verbose) {
        printf("Value incremented to %lld\n", (long long)sum);
    }

    char *reply = int64_to_string(sum);
    if (!reply) {
        send_error(client);
        free_value_copy(value);
        return;
    }
    const size_t reply_len = strlen(reply);

    if (!set_value(table, &buffer[5], key_len, reply, reply_len,
                   VALUE_ENTRY_TYPE_INT)) {
        fprintf(stderr, "Unable to set incremented value.\n");
        send_error(client);
        free(reply);
        free_value_copy(value);
        return;
    }

    send_reply(client, (const unsigned char *)reply, reply_len);
    free(reply);
    free_value_copy(value);
}

void handle_incr_by_command(client_t *client, unsigned char *buffer,
                            const size_t bytes_read)
{
    if (bytes_read < 5) {
        send_error(client);
        return;
    }

    const size_t command_length = buffer[0] << 8 | buffer[1];
    const size_t key_len = buffer[3] << 8 | buffer[4];
    const size_t key_position_offset = 5;
    const size_t pos = key_position_offset + key_len;
    const size_t offset = 2;

    if (buffer[2] != CMD_INCR_BY) {
        send_error(client);
        return;
    }

    if (pos + offset > bytes_read) {
        fprintf(stderr, "Invalid buffer: too short for value length.\n");
        send_error(client);
        return;
    }

    const size_t value_length = buffer[pos] << 8 | buffer[pos + 1];
    if (pos + offset + value_length > bytes_read) {
        fprintf(stderr, "Invalid buffer: too short for increment.\n");
        send_error(client);
        return;
    }

    unsigned char *incr_str = malloc(value_length + 1);
    if (!incr_str) {
        send_error(client);
        return;
    }

    memcpy(incr_str, buffer + pos + offset, value_length);
    incr_str[value_length] = '\0';

    if (bytes_read - offset != command_length) {
        fprintf(stderr, "Incomplete command data for INCR_BY.\n");
        send_error(client);
        free(incr_str);
        return;
    }

    // Lazy expiry: if expired, treat as nonexistent
    check_and_expire(&buffer[5], key_len);

    value_entry_t *old_value;
    size_t old_value_len;
    if (!get_value(table, &buffer[5], key_len, &old_value, &old_value_len)) {
        const char *default_value = "0";
        const size_t default_value_len = strlen(default_value);
        if (!set_value(table, &buffer[5], key_len,
                       (unsigned char *)default_value, default_value_len,
                       VALUE_ENTRY_TYPE_INT)) {
            fprintf(stderr, "Unable to set default value.\n");
            send_error(client);
            free(incr_str);
            return;
        }
        if (!get_value(table, &buffer[5], key_len, &old_value,
                       &old_value_len)) {
            send_error(client);
            free(incr_str);
            return;
        }
    }

    if (old_value->encoding != VALUE_ENTRY_TYPE_INT) {
        fprintf(stderr, "Stored value is not an integer.\n");
        send_error(client);
        free_value_copy(old_value);
        free(incr_str);
        return;
    }

    int64_t current;
    int64_t increment;
    if (!fkvs_parse_i64_decimal(old_value->ptr, old_value_len, INT64_MIN,
                                INT64_MAX, &current) ||
        !fkvs_parse_i64_decimal(incr_str, value_length, INT64_MIN, INT64_MAX,
                                &increment) ||
        (increment > 0 && current > INT64_MAX - increment) ||
        (increment < 0 && current < INT64_MIN - increment)) {
        fprintf(stderr, "Integer increment is out of range.\n");
        send_error(client);
        free_value_copy(old_value);
        free(incr_str);
        return;
    }

    const int64_t sum = current + increment;
    if (server.verbose) {
        printf("Value incremented to %lld\n", (long long)sum);
    }

    char *result = int64_to_string(sum);
    if (!result) {
        send_error(client);
        free_value_copy(old_value);
        free(incr_str);
        return;
    }
    const size_t result_len = strlen(result);

    if (!set_value(table, &buffer[5], key_len, (unsigned char *)result,
                   result_len, VALUE_ENTRY_TYPE_INT)) {
        fprintf(stderr, "Unable to set incremented value.\n");
        send_error(client);
        free_value_copy(old_value);
        free(incr_str);
        free(result);
        return;
    }

    send_reply(client, (unsigned char *)result, result_len);
    free_value_copy(old_value);
    free(incr_str);
    free(result);
}

void handle_decr_by_command(client_t *client, unsigned char *buffer,
                            const size_t bytes_read)
{
    if (bytes_read < 5) {
        send_error(client);
        return;
    }

    const size_t command_length = buffer[0] << 8 | buffer[1];
    const size_t key_len = buffer[3] << 8 | buffer[4];
    const size_t key_position_offset = 5;
    const size_t pos = key_position_offset + key_len;
    const size_t offset = 2;

    if (buffer[2] != CMD_DECR_BY) {
        send_error(client);
        return;
    }

    if (pos + offset > bytes_read) {
        fprintf(stderr, "Invalid buffer: too short for value length.\n");
        send_error(client);
        return;
    }

    const size_t value_length = buffer[pos] << 8 | buffer[pos + 1];
    if (pos + offset + value_length > bytes_read) {
        fprintf(stderr, "Invalid buffer: too short for increment.\n");
        send_error(client);
        return;
    }

    unsigned char *decr_str = malloc(value_length + 1);
    if (!decr_str) {
        send_error(client);
        return;
    }

    memcpy(decr_str, buffer + pos + offset, value_length);
    decr_str[value_length] = '\0';

    if (bytes_read - offset != command_length) {
        fprintf(stderr, "Incomplete command data for DECR_BY.\n");
        send_error(client);
        free(decr_str);
        return;
    }

    // Lazy expiry: if expired, treat as nonexistent
    check_and_expire(&buffer[5], key_len);

    value_entry_t *old_value;
    size_t old_value_len;
    if (!get_value(table, &buffer[5], key_len, &old_value, &old_value_len)) {
        const char *default_value = "0";
        const size_t default_value_len = strlen(default_value);
        if (!set_value(table, &buffer[5], key_len,
                       (unsigned char *)default_value, default_value_len,
                       VALUE_ENTRY_TYPE_INT)) {
            fprintf(stderr, "Unable to set default value.\n");
            send_error(client);
            free(decr_str);
            return;
        }

        if (!get_value(table, &buffer[5], key_len, &old_value,
                       &old_value_len)) {
            send_error(client);
            free(decr_str);
            return;
        }
    }

    if (old_value->encoding != VALUE_ENTRY_TYPE_INT) {
        fprintf(stderr, "Stored value is not an integer.\n");
        send_error(client);
        free_value_copy(old_value);
        free(decr_str);
        return;
    }

    int64_t current;
    int64_t decrement;
    if (!fkvs_parse_i64_decimal(old_value->ptr, old_value_len, INT64_MIN,
                                INT64_MAX, &current) ||
        !fkvs_parse_i64_decimal(decr_str, value_length, INT64_MIN, INT64_MAX,
                                &decrement) ||
        (decrement > 0 && current < INT64_MIN + decrement) ||
        (decrement < 0 && current > INT64_MAX + decrement)) {
        fprintf(stderr, "Integer decrement is out of range.\n");
        send_error(client);
        free_value_copy(old_value);
        free(decr_str);
        return;
    }

    const int64_t result_val = current - decrement;
    if (server.verbose) {
        printf("Value decremented to %lld\n", (long long)result_val);
    }

    char *result = int64_to_string(result_val);
    if (!result) {
        send_error(client);
        free_value_copy(old_value);
        free(decr_str);
        return;
    }
    const size_t result_len = strlen(result);

    if (!set_value(table, &buffer[5], key_len, (unsigned char *)result,
                   result_len, VALUE_ENTRY_TYPE_INT)) {
        fprintf(stderr, "Unable to set decremented value.\n");
        send_error(client);
        free_value_copy(old_value);
        free(decr_str);
        free(result);
        return;
    }

    send_reply(client, (unsigned char *)result, result_len);
    free_value_copy(old_value);
    free(decr_str);
    free(result);
}

void handle_ping_command(client_t *client, unsigned char *buffer,
                         size_t bytes_read)
{
    if (bytes_read < 5) {
        send_error(client);
        return;
    }

    const size_t command_length = buffer[0] << 8 | buffer[1];
    const size_t offset = 2;

    if (buffer[2] != CMD_PING) {
        send_error(client);
        return;
    }

    if (server.verbose) {
        printf("Server received %d bytes from client %d \n", (int)bytes_read,
               client->fd);
        print_binary_data(buffer, bytes_read);
    }

    if (bytes_read - offset == command_length) {
        send_pong(client, buffer, bytes_read);
    } else {
        fprintf(stderr, "Incomplete command data for PING.\n");
        send_error(client);
    }
}

void handle_info_command(client_t *client, unsigned char *buffer,
                         size_t bytes_read)
{
    if (bytes_read < 3 || buffer[2] != CMD_INFO) {
        send_error(client);
        return;
    }

    if (server.verbose) {
        printf("INFO command received. Gathering and returning metrics...\n");
    }

    update_memory_usage(&server.metrics);

    char formatted_uptime[50];
    format_uptime(&server.metrics, formatted_uptime, sizeof(formatted_uptime));

    char metrics[512];
    int n = snprintf(
        metrics, sizeof(metrics),
        "# Server \n"
        "pid: %d \n"
        "port: %d \n"
        "config file: %s \n"
        "Uptime: %s \n"
        "event_loop_max_events: %d \n"
        "event_dispatcher_kind: %s \n"
        "\n"
        "# Clients \n"
        "connected clients: %d \n"
        "disconnected clients: %lu \n"
        "\n"
        "#Stats \n"
        "commands executed: %lu \n"
        "\n"
        "# Memory \n"
        "Memory Usage: %lu bytes (%lu KiB)\n"
        "\n",
        server.pid, server.port, server.config_file_path, formatted_uptime,
        server.event_loop_max_events,
        event_loop_dispatcher_kind_to_string(server.event_dispatcher_kind),
        server.num_clients, server.metrics.disconnected_clients,
        server.metrics.num_executed_commands, server.metrics.memory_usage,
        server.metrics.memory_usage / 1024);
    if (n < 0 || (size_t)n >= sizeof(metrics)) {
        fprintf(stderr, "Formatting error or buffer overflow while preparing "
                        "metrics reply.\n");
        send_error(client);
        return;
    }

    send_reply(client, (const unsigned char *)metrics, n);
}

void handle_decr_command(client_t *client, unsigned char *buffer,
                         size_t bytes_read)
{
    if (bytes_read < 5) {
        send_error(client);
        return;
    }

    const size_t command_length = buffer[0] << 8 | buffer[1];
    const size_t key_len = buffer[3] << 8 | buffer[4];
    const size_t offset = 2;

    if (buffer[2] != CMD_DECR) {
        send_error(client);
        return;
    }

    if (bytes_read - offset != command_length) {
        fprintf(stderr, "Incomplete command data for DECR.\n");
        send_error(client);
        return;
    }

    // Lazy expiry: if expired, treat as nonexistent
    check_and_expire(&buffer[5], key_len);

    value_entry_t *value;
    size_t value_len;
    if (!get_value(table, &buffer[5], key_len, &value, &value_len)) {
        const char *default_value = "0";
        const size_t default_value_len = strlen(default_value);
        if (!set_value(table, &buffer[5], key_len,
                       (unsigned char *)default_value, default_value_len,
                       VALUE_ENTRY_TYPE_INT)) {
            fprintf(stderr, "Unable to set default value.\n");
            send_error(client);
            return;
        }
        if (!get_value(table, &buffer[5], key_len, &value, &value_len)) {
            send_error(client);
            return;
        }
    }

    if (value->encoding != VALUE_ENTRY_TYPE_INT) {
        fprintf(stderr, "Stored value is not an integer.\n");
        send_error(client);
        free_value_copy(value);
        return;
    }

    int64_t current;
    if (!fkvs_parse_i64_decimal(value->ptr, value_len, INT64_MIN, INT64_MAX,
                                &current) ||
        current == INT64_MIN) {
        fprintf(stderr, "Stored integer is out of range.\n");
        send_error(client);
        free_value_copy(value);
        return;
    }
    const int64_t decrement = current - 1;

    char *result_str = int64_to_string(decrement);
    if (!result_str) {
        send_error(client);
        free_value_copy(value);
        return;
    }
    const size_t result_length = strlen(result_str);

    if (!set_value(table, &buffer[5], key_len, (unsigned char *)result_str,
                   result_length, VALUE_ENTRY_TYPE_INT)) {
        fprintf(stderr, "Unable to set decremented value.\n");
        send_error(client);
        free_value_copy(value);
        free(result_str);
        return;
    }

    send_reply(client, (unsigned char *)result_str, result_length);
    free_value_copy(value);
    free(result_str);
}

void handle_del_command(client_t *client, unsigned char *buffer,
                        size_t bytes_read)
{
    if (bytes_read < 5) {
        send_error(client);
        return;
    }

    const size_t command_len = buffer[0] << 8 | buffer[1];
    const size_t key_len = buffer[3] << 8 | buffer[4];

    if (buffer[2] != CMD_DEL) {
        send_error(client);
        return;
    }

    if (bytes_read - 2 != command_len) {
        fprintf(stderr, "Incomplete command data for DEL.\n");
        send_error(client);
        return;
    }

    delete_value(table, &buffer[5], key_len);
    delete_value(expires, &buffer[5], key_len);

    send_ok(client);
}

void handle_expire_command(client_t *client, unsigned char *buffer,
                           size_t bytes_read)
{
    if (bytes_read < 5) {
        send_error(client);
        return;
    }

    const uint16_t core_len = ((uint16_t)buffer[0] << 8) | buffer[1];

    if (buffer[2] != CMD_EXPIRE) {
        send_error(client);
        return;
    }

    if (bytes_read - 2 < core_len) {
        send_error(client);
        return;
    }

    const uint16_t key_len = ((uint16_t)buffer[3] << 8) | buffer[4];
    const size_t pos_key = 5;
    const size_t after_key = pos_key + key_len;

    if (after_key + 2 > bytes_read) {
        send_error(client);
        return;
    }

    const uint16_t ttl_str_len =
        ((uint16_t)buffer[after_key] << 8) | buffer[after_key + 1];
    const size_t pos_ttl = after_key + 2;

    if (pos_ttl + ttl_str_len > bytes_read) {
        send_error(client);
        return;
    }

    // Verify key exists in store
    value_entry_t *val;
    size_t val_len;
    if (!get_value(table, &buffer[pos_key], key_len, &val, &val_len)) {
        send_error(client);
        return;
    }
    free(val->ptr);
    free(val);

    // Parse seconds string
    char sec_buf[32];
    size_t copy_len = ttl_str_len < sizeof(sec_buf) - 1 ? ttl_str_len : sizeof(sec_buf) - 1;
    memcpy(sec_buf, &buffer[pos_ttl], copy_len);
    sec_buf[copy_len] = '\0';

    int64_t deadline_ms;
    if (copy_len != ttl_str_len ||
        !fkvs_parse_deadline_ms((const unsigned char *)sec_buf, copy_len,
                                fkvs_now_ms(), &deadline_ms)) {
        send_error(client);
        return;
    }

    if (!set_expiry(expires, &buffer[pos_key], key_len, deadline_ms)) {
        send_error(client);
        return;
    }

    send_ok(client);
}

void handle_ttl_command(client_t *client, unsigned char *buffer,
                        size_t bytes_read)
{
    if (bytes_read < 5) {
        send_error(client);
        return;
    }

    const size_t command_len = buffer[0] << 8 | buffer[1];
    const size_t key_len = buffer[3] << 8 | buffer[4];

    if (buffer[2] != CMD_TTL) {
        send_error(client);
        return;
    }

    if (bytes_read - 2 != command_len) {
        fprintf(stderr, "Incomplete command data for TTL.\n");
        send_error(client);
        return;
    }

    // Lazy expiry: if expired, clean up before reporting TTL
    check_and_expire(&buffer[5], key_len);

    // Check if key exists in store at all
    value_entry_t *val;
    size_t val_len;
    bool key_exists = get_value(table, &buffer[5], key_len, &val, &val_len);
    if (key_exists) {
        free(val->ptr);
        free(val);
    }

    int64_t ttl;
    if (!key_exists) {
        ttl = -2;
    } else {
        ttl = get_ttl(expires, &buffer[5], key_len);
        // get_ttl returns -2 if not in expires table; for existing key with no TTL, return -1
        if (ttl == -2)
            ttl = -1;
    }

    char ttl_str[32];
    int n = snprintf(ttl_str, sizeof(ttl_str), "%lld", (long long)ttl);

    send_reply(client, (const unsigned char *)ttl_str, n);
}

void handle_persist_command(client_t *client, unsigned char *buffer,
                            size_t bytes_read)
{
    if (bytes_read < 5) {
        send_error(client);
        return;
    }

    const size_t command_len = buffer[0] << 8 | buffer[1];
    const size_t key_len = buffer[3] << 8 | buffer[4];

    if (buffer[2] != CMD_PERSIST) {
        send_error(client);
        return;
    }

    if (bytes_read - 2 != command_len) {
        fprintf(stderr, "Incomplete command data for PERSIST.\n");
        send_error(client);
        return;
    }

    remove_expiry(expires, &buffer[5], key_len);

    send_ok(client);
}
