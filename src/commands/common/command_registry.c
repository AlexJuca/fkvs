#include "../common/command_registry.h"
#include "../../response_defs.h"
#include "../../utils.h"
#include "../common/command_defs.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#define MAX_COMMANDS 256

static CommandHandler command_handlers[MAX_COMMANDS] = {0};

void register_command(const uint8_t command_id, const CommandHandler handler)
{
    if (command_id < MAX_COMMANDS) {
        command_handlers[command_id] = handler;
    } else {
        fprintf(stderr, "Command ID %d out of range\n", command_id);
    }
}

static void wbuf_append(client_t *client, const unsigned char *data,
                         size_t len)
{
    if (client->wbuf_used + len > sizeof(client->wbuf)) {
        // Write buffer full, flush first
        wbuf_flush(client);
    }
    if (len > sizeof(client->wbuf)) {
        // Data larger than entire wbuf — send directly
        send(client->fd, data, len, 0);
        return;
    }
    memcpy(client->wbuf + client->wbuf_used, data, len);
    client->wbuf_used += len;
}

void wbuf_flush(client_t *client)
{
    size_t sent = 0;
    while (sent < client->wbuf_used) {
        ssize_t n = send(client->fd, client->wbuf + sent,
                         client->wbuf_used - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // Non-blocking socket can't accept more right now; keep unsent
            // data in the buffer for a future flush.
            break;
        } else {
            // Real error (e.g. EPIPE, ECONNRESET); discard buffer.
            client->wbuf_used = 0;
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
    if (bytes_read < 1) {
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
    assert(client->fd > 0);
    wbuf_append(client, ok, sizeof ok);
}

void send_error(client_t *client)
{
    // Framed error: [2B core_len=1] [1B STATUS_FAILURE]
    const unsigned char error[] = {0x00, 0x01, STATUS_FAILURE};
    assert(client->fd > 0);
    wbuf_append(client, error, sizeof error);
}

void send_reply(client_t *client, const unsigned char *buffer,
                size_t bytes_read)
{
    const size_t core_cmd_len = bytes_read + 3;
    const size_t full_frame_length = core_cmd_len + 2;

    unsigned char frame[65536];
    assert(full_frame_length <= sizeof(frame));

    frame[0] = (core_cmd_len >> 8) & 0xFF;
    frame[1] = core_cmd_len & 0xFF;
    frame[2] = STATUS_SUCCESS;
    frame[3] = (bytes_read >> 8) & 0xFF;
    frame[4] = bytes_read & 0xFF;
    memcpy(&frame[5], buffer, bytes_read);

    assert(client->fd > 0);
    wbuf_append(client, frame, full_frame_length);
}

void send_pong(client_t *client, const unsigned char *buffer)
{
    const size_t value_len = buffer[3] << 8 | buffer[4];
    const size_t core_cmd_len = 1 + 2 + value_len;
    const size_t full_frame_length = 2 + core_cmd_len;

    unsigned char frame[65536];
    assert(full_frame_length <= sizeof(frame));

    frame[0] = (core_cmd_len >> 8) & 0xFF;
    frame[1] = core_cmd_len & 0xFF;
    frame[2] = CMD_PING;
    frame[3] = (value_len >> 8) & 0xFF;
    frame[4] = value_len & 0xFF;
    memcpy(&frame[5], &buffer[5], value_len);

    assert(client->fd > 0);
    wbuf_append(client, frame, full_frame_length);
}
