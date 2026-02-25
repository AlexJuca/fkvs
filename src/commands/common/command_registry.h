#ifndef COMMAND_REGISTRY_H
#define COMMAND_REGISTRY_H

#include "../../client.h"

#include <stdint.h>
#include <stdlib.h>

typedef void (*CommandHandler)(client_t *client, unsigned char *buffer,
                               size_t bytes_read);

void register_command(uint8_t command_id, CommandHandler handler);
void dispatch_command(client_t *client, unsigned char *buffer, size_t bytes_read);

void wbuf_flush(client_t *client);

void send_ok(client_t *client);
void send_error(client_t *client);
void send_reply(client_t *client, const unsigned char *buffer, size_t bytes_read);
void send_pong(client_t *client, const unsigned char *buffer);

#endif // COMMAND_REGISTRY_H
