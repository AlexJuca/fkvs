#include "command_registry.h"
#include "response_defs.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#define MAX_COMMANDS 256

static CommandHandler command_handlers[MAX_COMMANDS] = {0};

void register_command(uint8_t command_id, CommandHandler handler) {
    if (command_id < MAX_COMMANDS) {
        command_handlers[command_id] = handler;
    } else {
        fprintf(stderr, "Command ID %d out of range\n", command_id);
    }
}

void dispatch_command(int client_fd, unsigned char *buffer,
                      const size_t bytes_read) {
    if (bytes_read < 1) {
        fprintf(stderr, "Buffer too short for command dispatching\n");
        return;
    }

    uint8_t command_id = buffer[2];
    if (command_handlers[command_id] != NULL) {
        command_handlers[command_id](client_fd, buffer, bytes_read);
    } else {
        fprintf(stderr, "No handler registered for command ID %d\n", command_id);
    }
}

void send_ok(int client_fd) {
    const unsigned char ok[] = { STATUS_SUCCESS };
    send(client_fd, ok, sizeof ok, 0);
}

void send_error(int client_fd) {
    const unsigned char error[] = { STATUS_FAILURE };
    send(client_fd, error, sizeof error, 0);
}

void send_reply(int client_fd, unsigned char *buffer, size_t bytes_read) {
    const size_t core_cmd_len = bytes_read + 3;
    const size_t full_frame_length = core_cmd_len + 2;

    unsigned char* reply = malloc(core_cmd_len);

    reply[0] = (core_cmd_len >> 8) & 0xFF;
    reply[1] = core_cmd_len & 0xFF;
    reply[2] = STATUS_SUCCESS;
    reply[3] = (bytes_read >> 8) & 0xFF;
    reply[4] = bytes_read & 0xFF;
    memcpy(&reply[5], buffer, bytes_read);

    send(client_fd, reply, full_frame_length, 0);
    free(reply);
}
