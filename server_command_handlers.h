#ifndef SERVER_COMMAND_HANDLERS_H
#define SERVER_COMMAND_HANDLERS_H

#include "hashtable.h"

void init_command_handlers(hashtable_t *ht);

void handle_set_command(int client_fd, unsigned char *buffer,
                        size_t bytes_read);

void handle_get_command(int client_fd, unsigned char *buffer,
                        size_t bytes_read);

void handle_incr_command(int client_fd, unsigned char *buffer,
                         size_t bytes_read);

void handle_incr_by_command(int client_fd, unsigned char *buffer,
                            size_t bytes_read);

void handle_ping_command(int client_fd, unsigned char *buffer,
                         size_t bytes_read);

void handle_decr_command(int client_fd, unsigned char *buffer,
                         size_t bytes_read);

#endif // SERVER_COMMAND_HANDLERS_H
