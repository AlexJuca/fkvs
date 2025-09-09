#ifndef COMMAND_REGISTRY_H
#define COMMAND_REGISTRY_H

#include <stdint.h>
#include <stdlib.h>

typedef void (*CommandHandler)(int client_fd, unsigned char *buffer,
                               size_t bytes_read);

void register_command(uint8_t command_id, CommandHandler handler);
void dispatch_command(int client_fd, unsigned char *buffer, size_t bytes_read);

void send_ok(int client_fd);
void send_error(int client_fd);
void send_reply(int client_fd, const unsigned char *buffer, size_t bytes_read);

#endif // COMMAND_REGISTRY_H
