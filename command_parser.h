#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include "hashtable.h"

// Function prototypes for command parsing
void handle_command(HashTable* table, int client_fd, const unsigned char* buffer, size_t bytes_read);

// Construct SET command in binary format
unsigned char* construct_set_command(const char* key, const char* value, size_t* command_len);

// Construct GET command in binary format
unsigned char* construct_get_command(const char* key, size_t* command_len);

#endif // COMMAND_PARSER_H
