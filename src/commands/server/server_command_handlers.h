#ifndef SERVER_COMMAND_HANDLERS_H
#define SERVER_COMMAND_HANDLERS_H

#include "../../client.h"
#include "../../core/hashtable.h"

void init_command_handlers(hashtable_t *ht);

void handle_set_command(client_t *client, unsigned char *buffer,
                        size_t bytes_read);

void handle_get_command(client_t *client, unsigned char *buffer,
                        size_t bytes_read);

void handle_incr_command(client_t *client, unsigned char *buffer,
                         size_t bytes_read);

void handle_incr_by_command(client_t *client, unsigned char *buffer,
                            size_t bytes_read);

void handle_ping_command(client_t *client, unsigned char *buffer,
                         size_t bytes_read);

void handle_decr_command(client_t *client, unsigned char *buffer,
                         size_t bytes_read);

void handle_decr_by_command(client_t *client, unsigned char *buffer,
                         size_t bytes_read);

void handle_info_command(client_t *client, unsigned char *buffer,
                         size_t bytes_read);

#endif // SERVER_COMMAND_HANDLERS_H
