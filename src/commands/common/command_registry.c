#include "../common/command_registry.h"
#include "../../response_defs.h"
#include "../../utils.h"
#include "../common/command_defs.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#define MAX_COMMANDS 256

#ifdef MSG_NOSIGNAL
#define FKVS_SEND_FLAGS MSG_NOSIGNAL
#else
#define FKVS_SEND_FLAGS 0
#endif

static CommandHandler command_handlers[MAX_COMMANDS] = {0};

void register_command(const uint8_t command_id, const CommandHandler handler)
{
    command_handlers[command_id] = handler;
}

static bool wbuf_reserve(client_t *client, const size_t len)
{
    if (!client || client->write_failed)
        return false;

    if (len == 0)
        return true;

    if (len > FKVS_CLIENT_WRITE_BUFFER_MAX_CAPACITY - client->wbuf_used) {
        client->write_failed = true;
        return false;
    }

    if (!client->wbuf || client->wbuf_capacity == 0) {
        client->write_failed = true;
        return false;
    }

    size_t needed = client->wbuf_used + len;
    if (needed <= client->wbuf_capacity)
        return true;

    if (client->wbuf_used > 0)
        wbuf_flush(client);

    if (client->write_failed)
        return false;

    needed = client->wbuf_used + len;
    if (needed <= client->wbuf_capacity)
        return true;

    size_t new_capacity = client->wbuf_capacity;
    while (new_capacity < needed &&
           new_capacity < FKVS_CLIENT_WRITE_BUFFER_MAX_CAPACITY) {
        if (new_capacity > FKVS_CLIENT_WRITE_BUFFER_MAX_CAPACITY / 2) {
            new_capacity = FKVS_CLIENT_WRITE_BUFFER_MAX_CAPACITY;
        } else {
            new_capacity *= 2;
        }
    }

    if (new_capacity < needed) {
        client->write_failed = true;
        return false;
    }

    unsigned char *new_wbuf = realloc(client->wbuf, new_capacity);
    if (!new_wbuf) {
        client->write_failed = true;
        return false;
    }

    client->wbuf = new_wbuf;
    client->wbuf_capacity = new_capacity;
    return true;
}

static void wbuf_append(client_t *client, const unsigned char *data,
                        size_t len)
{
    if (!wbuf_reserve(client, len))
        return;

    memcpy(client->wbuf + client->wbuf_used, data, len);
    client->wbuf_used += len;
}

void wbuf_flush(client_t *client)
{
    if (!client || !client->wbuf || client->wbuf_used == 0 ||
        client->write_failed)
        return;

    // Event loops may use edge-triggered write readiness, so drain until the
    // socket would block or the response queue is empty.
    size_t sent = 0;
    while (sent < client->wbuf_used) {
        ssize_t n = send(client->fd, client->wbuf + sent,
                         client->wbuf_used - sent, FKVS_SEND_FLAGS);
        if (n > 0) {
            sent += (size_t)n;
        } else if (n == 0) {
            break;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // Non-blocking socket can't accept more right now; keep unsent
            // data in the buffer for a future flush.
            break;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            // Real error (e.g. EPIPE, ECONNRESET); discard buffer.
            client->wbuf_used = 0;
            client->write_failed = true;
            return;
        }
    }

    // Compact: shift unsent bytes to the front.
    size_t remaining = client->wbuf_used - sent;
    if (remaining > 0)
        memmove(client->wbuf, client->wbuf + sent, remaining);
    client->wbuf_used = remaining;
}

void dispatch_command(client_t *client, unsigned char *buffer,
                      const size_t bytes_read)
{
    if (bytes_read < 3) {
        fprintf(stderr, "Buffer too short for command dispatching\n");
        return;
    }

    const uint8_t command_id = buffer[2];
    if (command_handlers[command_id] != NULL) {
        command_handlers[command_id](client, buffer, bytes_read);
    } else {
        fprintf(stderr, "No handler registered for command ID %d\n",
                command_id);
    }
}

void send_ok(client_t *client)
{
    // Framed OK: [2B core_len=1] [1B STATUS_SUCCESS]
    const unsigned char ok[] = {0x00, 0x01, STATUS_SUCCESS};
    if (client->fd < 0)
        return;
    wbuf_append(client, ok, sizeof ok);
}

void send_error(client_t *client)
{
    // Framed error: [2B core_len=1] [1B STATUS_FAILURE]
    const unsigned char error[] = {0x00, 0x01, STATUS_FAILURE};
    if (client->fd < 0)
        return;
    wbuf_append(client, error, sizeof error);
}

void send_reply(client_t *client, const unsigned char *buffer,
                size_t bytes_read)
{
    if (client->fd < 0)
        return;

    const size_t core_cmd_len = bytes_read + 3;
    const size_t full_frame_length = core_cmd_len + 2;

    if (core_cmd_len > UINT16_MAX) {
        send_error(client);
        return;
    }

    if (!wbuf_reserve(client, full_frame_length))
        return;

    const unsigned char header[] = {
        (core_cmd_len >> 8) & 0xFF,
        core_cmd_len & 0xFF,
        STATUS_SUCCESS,
        (bytes_read >> 8) & 0xFF,
        bytes_read & 0xFF,
    };
    wbuf_append(client, header, sizeof(header));
    wbuf_append(client, buffer, bytes_read);
}

void send_pong(client_t *client, const unsigned char *buffer,
               size_t bytes_read)
{
    if (client->fd < 0)
        return;

    if (bytes_read < 5) {
        send_error(client);
        return;
    }

    const size_t value_len = buffer[3] << 8 | buffer[4];

    if (5 + value_len > bytes_read) {
        send_error(client);
        return;
    }

    const size_t core_cmd_len = 1 + 2 + value_len;
    const size_t full_frame_length = 2 + core_cmd_len;

    if (core_cmd_len > UINT16_MAX) {
        send_error(client);
        return;
    }

    if (!wbuf_reserve(client, full_frame_length))
        return;

    const unsigned char header[] = {
        (core_cmd_len >> 8) & 0xFF,
        core_cmd_len & 0xFF,
        CMD_PING,
        (value_len >> 8) & 0xFF,
        value_len & 0xFF,
    };
    wbuf_append(client, header, sizeof(header));
    wbuf_append(client, &buffer[5], value_len);
}
