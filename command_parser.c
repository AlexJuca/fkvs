#include "command_parser.h"
#include "command_defs.h"
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include "utils.h"

unsigned char* construct_set_command(const char* key, const char* value, size_t* command_len) {
    size_t key_len = strlen(key);
    size_t value_len = strlen(value);
    size_t core_cmd_len = 1 + 2 + key_len + 2 + value_len;

    *command_len = 2 + core_cmd_len;

    unsigned char* binary_cmd = malloc(*command_len);
    if (!binary_cmd) {
        return NULL;
    }

    binary_cmd[0] = (core_cmd_len >> 8) & 0xFF;
    binary_cmd[1] = core_cmd_len & 0xFF;
    
    binary_cmd[2] = CMD_SET;
    binary_cmd[3] = (key_len >> 8) & 0xFF;
    binary_cmd[4] = key_len & 0xFF;
    memcpy(&binary_cmd[5], key, key_len);
    
    size_t pos = 5 + key_len;
    binary_cmd[pos + 0] = (value_len >> 8) & 0xFF;
    binary_cmd[pos + 1] = value_len & 0xFF;
    memcpy(&binary_cmd[pos + 2], value, value_len);

    return binary_cmd;
}

unsigned char* construct_get_command(const char* key, size_t* command_len) {
    size_t key_len = strlen(key);
    size_t core_cmd_len = 3 + key_len;
    *command_len = 2 + core_cmd_len;

    unsigned char* binary_cmd = malloc(*command_len);
    if (!binary_cmd) {
        return NULL;
    }

    binary_cmd[0] = (core_cmd_len >> 8) & 0xFF;
    binary_cmd[1] = core_cmd_len & 0xFF;
    binary_cmd[2] = CMD_GET;
    binary_cmd[3] = (key_len >> 8) & 0xFF;
    binary_cmd[4] = key_len & 0xFF;
    memcpy(&binary_cmd[5], key, key_len);

    return binary_cmd;
}
