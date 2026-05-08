#include "../common/command_parser.h"
#include "../../utils.h"
#include "../common/command_defs.h"

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

unsigned char *construct_set_command(const char *key, const char *value,
                                     size_t *command_len)
{
    size_t key_len = strlen(key);
    size_t value_len = strlen(value);
    const size_t core_cmd_len = 1 + 2 + key_len + 2 + value_len;

    *command_len = 2 + core_cmd_len;

    unsigned char *binary_cmd = malloc(*command_len);
    if (!binary_cmd) {
        return NULL;
    }

    binary_cmd[0] = core_cmd_len >> 8 & 0xFF;
    binary_cmd[1] = core_cmd_len & 0xFF;

    binary_cmd[2] = CMD_SET;
    binary_cmd[3] = key_len >> 8 & 0xFF;
    binary_cmd[4] = key_len & 0xFF;
    memcpy(&binary_cmd[5], key, key_len);

    const size_t pos = 5 + key_len;
    binary_cmd[pos + 0] = value_len >> 8 & 0xFF;
    binary_cmd[pos + 1] = value_len & 0xFF;
    memcpy(&binary_cmd[pos + 2], value, value_len);

    return binary_cmd;
}

unsigned char *construct_set_ex_command(const char *key, const char *value,
                                        const char *seconds,
                                        size_t *command_len)
{
    size_t key_len = strlen(key);
    size_t value_len = strlen(value);
    size_t sec_len = strlen(seconds);
    const size_t core_cmd_len = 1 + 2 + key_len + 2 + value_len + 2 + sec_len;

    *command_len = 2 + core_cmd_len;

    unsigned char *binary_cmd = malloc(*command_len);
    if (!binary_cmd) {
        return NULL;
    }

    binary_cmd[0] = core_cmd_len >> 8 & 0xFF;
    binary_cmd[1] = core_cmd_len & 0xFF;

    binary_cmd[2] = CMD_SET;
    binary_cmd[3] = key_len >> 8 & 0xFF;
    binary_cmd[4] = key_len & 0xFF;
    memcpy(&binary_cmd[5], key, key_len);

    size_t pos = 5 + key_len;
    binary_cmd[pos + 0] = value_len >> 8 & 0xFF;
    binary_cmd[pos + 1] = value_len & 0xFF;
    memcpy(&binary_cmd[pos + 2], value, value_len);

    pos = pos + 2 + value_len;
    binary_cmd[pos + 0] = sec_len >> 8 & 0xFF;
    binary_cmd[pos + 1] = sec_len & 0xFF;
    memcpy(&binary_cmd[pos + 2], seconds, sec_len);

    return binary_cmd;
}

unsigned char *construct_get_command(const char *key, size_t *command_len)
{
    size_t key_len = strlen(key);
    const size_t core_cmd_len = 3 + key_len;
    *command_len = 2 + core_cmd_len;

    unsigned char *binary_cmd = malloc(*command_len);
    if (!binary_cmd) {
        return NULL;
    }

    binary_cmd[0] = core_cmd_len >> 8 & 0xFF;
    binary_cmd[1] = core_cmd_len & 0xFF;
    binary_cmd[2] = CMD_GET;
    binary_cmd[3] = key_len >> 8 & 0xFF;
    binary_cmd[4] = key_len & 0xFF;
    memcpy(&binary_cmd[5], key, key_len);

    return binary_cmd;
}

unsigned char *construct_incr_command(const char *key, size_t *command_len)
{
    size_t key_len = strlen(key);
    const size_t core_cmd_len = 3 + key_len;
    *command_len = 2 + core_cmd_len;

    unsigned char *binary_cmd = malloc(*command_len);
    if (!binary_cmd) {
        return NULL;
    }

    binary_cmd[0] = core_cmd_len >> 8 & 0xFF;
    binary_cmd[1] = core_cmd_len & 0xFF;
    binary_cmd[2] = CMD_INCR;
    binary_cmd[3] = (key_len >> 8) & 0xFF;
    binary_cmd[4] = key_len & 0xFF;
    memcpy(&binary_cmd[5], key, key_len);

    return binary_cmd;
}

unsigned char *construct_incr_by_command(const char *key, const char *value,
                                         size_t *command_len)
{
    size_t key_len = strlen(key);
    size_t value_len = strlen(value);
    const size_t core_cmd_len = 1 + 2 + key_len + 2 + value_len;

    *command_len = 2 + core_cmd_len;

    unsigned char *binary_cmd = malloc(*command_len);
    if (!binary_cmd) {
        return NULL;
    }

    binary_cmd[0] = core_cmd_len >> 8 & 0xFF;
    binary_cmd[1] = core_cmd_len & 0xFF;

    binary_cmd[2] = CMD_INCR_BY;
    binary_cmd[3] = key_len >> 8 & 0xFF;
    binary_cmd[4] = key_len & 0xFF;
    memcpy(&binary_cmd[5], key, key_len);

    const size_t pos = 5 + key_len;
    binary_cmd[pos + 0] = (value_len >> 8) & 0xFF;
    binary_cmd[pos + 1] = value_len & 0xFF;
    memcpy(&binary_cmd[pos + 2], value, value_len);

    return binary_cmd;
}

unsigned char *construct_ping_command(const char *value, size_t *command_len)
{
    const size_t value_len = strlen(value);
    const size_t core_cmd_len = 3 + value_len;
    *command_len = 2 + core_cmd_len;

    unsigned char *binary_cmd = malloc(*command_len);
    if (!binary_cmd) {
        return NULL;
    }

    binary_cmd[0] = core_cmd_len >> 8 & 0xFF;
    binary_cmd[1] = core_cmd_len & 0xFF;
    binary_cmd[2] = CMD_PING;
    binary_cmd[3] = value_len >> 8 & 0xFF;
    binary_cmd[4] = value_len & 0xFF;
    memcpy(&binary_cmd[5], value, value_len);

    return binary_cmd;
}

unsigned char *construct_info_command(size_t *command_len)
{
    const size_t core_cmd_len = 1;
    *command_len = 2 + core_cmd_len;

    unsigned char *binary_cmd = malloc(*command_len);
    if (!binary_cmd) {
        return NULL;
    }

    binary_cmd[0] = core_cmd_len >> 8 & 0xFF;
    binary_cmd[1] = core_cmd_len & 0xFF;

    binary_cmd[2] = CMD_INFO;

    return binary_cmd;
}

unsigned char *construct_decr_command(const char *key, size_t *command_len)
{
    size_t key_len = strlen(key);
    const size_t core_cmd_len = 3 + key_len;
    *command_len = 2 + core_cmd_len;

    unsigned char *binary_cmd = malloc(*command_len);
    if (!binary_cmd) {
        return NULL;
    }

    binary_cmd[0] = core_cmd_len >> 8 & 0xFF;
    binary_cmd[1] = core_cmd_len & 0xFF;
    binary_cmd[2] = CMD_DECR;
    binary_cmd[3] = key_len >> 8 & 0xFF;
    binary_cmd[4] = key_len & 0xFF;
    memcpy(&binary_cmd[5], key, key_len);

    return binary_cmd;
}

unsigned char *construct_decr_by_command(const char *key, const char *value,
                                         size_t *command_len)
{
  size_t key_len = strlen(key);
  size_t value_len = strlen(value);
  const size_t core_cmd_len = 1 + 2 + key_len + 2 + value_len;

  *command_len = 2 + core_cmd_len;

  unsigned char *binary_cmd = malloc(*command_len);
  if (!binary_cmd) {
    return NULL;
  }

  binary_cmd[0] = core_cmd_len >> 8 & 0xFF;
  binary_cmd[1] = core_cmd_len & 0xFF;

  binary_cmd[2] = CMD_DECR_BY;
  binary_cmd[3] = key_len >> 8 & 0xFF;
  binary_cmd[4] = key_len & 0xFF;
  memcpy(&binary_cmd[5], key, key_len);

  const size_t pos = 5 + key_len;
  binary_cmd[pos + 0] = (value_len >> 8) & 0xFF;
  binary_cmd[pos + 1] = value_len & 0xFF;
  memcpy(&binary_cmd[pos + 2], value, value_len);

  return binary_cmd;
}

unsigned char *construct_del_command(const char *key, size_t *command_len)
{
    size_t key_len = strlen(key);
    const size_t core_cmd_len = 3 + key_len;
    *command_len = 2 + core_cmd_len;

    unsigned char *binary_cmd = malloc(*command_len);
    if (!binary_cmd) {
        return NULL;
    }

    binary_cmd[0] = core_cmd_len >> 8 & 0xFF;
    binary_cmd[1] = core_cmd_len & 0xFF;
    binary_cmd[2] = CMD_DEL;
    binary_cmd[3] = key_len >> 8 & 0xFF;
    binary_cmd[4] = key_len & 0xFF;
    memcpy(&binary_cmd[5], key, key_len);

    return binary_cmd;
}

unsigned char *construct_expire_command(const char *key, const char *seconds,
                                        size_t *command_len)
{
    size_t key_len = strlen(key);
    size_t sec_len = strlen(seconds);
    const size_t core_cmd_len = 1 + 2 + key_len + 2 + sec_len;

    *command_len = 2 + core_cmd_len;

    unsigned char *binary_cmd = malloc(*command_len);
    if (!binary_cmd) {
        return NULL;
    }

    binary_cmd[0] = core_cmd_len >> 8 & 0xFF;
    binary_cmd[1] = core_cmd_len & 0xFF;

    binary_cmd[2] = CMD_EXPIRE;
    binary_cmd[3] = key_len >> 8 & 0xFF;
    binary_cmd[4] = key_len & 0xFF;
    memcpy(&binary_cmd[5], key, key_len);

    const size_t pos = 5 + key_len;
    binary_cmd[pos + 0] = sec_len >> 8 & 0xFF;
    binary_cmd[pos + 1] = sec_len & 0xFF;
    memcpy(&binary_cmd[pos + 2], seconds, sec_len);

    return binary_cmd;
}

unsigned char *construct_ttl_command(const char *key, size_t *command_len)
{
    size_t key_len = strlen(key);
    const size_t core_cmd_len = 3 + key_len;
    *command_len = 2 + core_cmd_len;

    unsigned char *binary_cmd = malloc(*command_len);
    if (!binary_cmd) {
        return NULL;
    }

    binary_cmd[0] = core_cmd_len >> 8 & 0xFF;
    binary_cmd[1] = core_cmd_len & 0xFF;
    binary_cmd[2] = CMD_TTL;
    binary_cmd[3] = key_len >> 8 & 0xFF;
    binary_cmd[4] = key_len & 0xFF;
    memcpy(&binary_cmd[5], key, key_len);

    return binary_cmd;
}

unsigned char *construct_persist_command(const char *key, size_t *command_len)
{
    size_t key_len = strlen(key);
    const size_t core_cmd_len = 3 + key_len;
    *command_len = 2 + core_cmd_len;

    unsigned char *binary_cmd = malloc(*command_len);
    if (!binary_cmd) {
        return NULL;
    }

    binary_cmd[0] = core_cmd_len >> 8 & 0xFF;
    binary_cmd[1] = core_cmd_len & 0xFF;
    binary_cmd[2] = CMD_PERSIST;
    binary_cmd[3] = key_len >> 8 & 0xFF;
    binary_cmd[4] = key_len & 0xFF;
    memcpy(&binary_cmd[5], key, key_len);

    return binary_cmd;
}

unsigned char *construct_keys_command(size_t *command_len)
{
    const size_t core_cmd_len = 1;
    *command_len = 2 + core_cmd_len;

    unsigned char *binary_cmd = malloc(*command_len);
    if (!binary_cmd) {
        return NULL;
    }

    binary_cmd[0] = core_cmd_len >> 8 & 0xFF;
    binary_cmd[1] = core_cmd_len & 0xFF;

    binary_cmd[2] = CMD_KEYS;

    return binary_cmd;
}
