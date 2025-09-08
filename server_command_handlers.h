#ifndef COMMAND_HANDLERS_H
#define COMMAND_HANDLERS_H

#include "hashtable.h"

// Initialize command handlers with the given hash table
void init_command_handlers(HashTable* ht);

// Command handler for SET command
void handle_set_command(int client_fd, unsigned char* buffer, size_t bytes_read);

// Command handler for GET command
void handle_get_command(int client_fd, unsigned char* buffer, size_t bytes_read);

void handle_incr_command(int client_fd, unsigned char* buffer, size_t bytes_read);

#endif // COMMAND_HANDLERS_H
