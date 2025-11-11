
#ifndef CLIENT_COMMAND_HANDLERS
#define CLIENT_COMMAND_HANDLERS

#include "../../client.h"

typedef struct {
    const char *cmd;
    client_t *client;
} command_args_t;

typedef void (*cmd_fn)(command_args_t args,
                       void (*response_cb)(client_t *client));

typedef struct {
    const char *cmd_name;
    void (*cmd_fn)(command_args_t args, void (*response_cb)(client_t *client));
} cmd_t;

void execute_command(const char *cmd, client_t *client,
                     void (*response_cb)(client_t *client));

void execute_command_benchmark(const char *cmd, client_t *client,
                               bool use_pregenerated_keys,
                               void (*response_cb)(client_t *client));

void cmd_get(command_args_t args, void (*response_cb)(client_t *client));

void cmd_set(command_args_t args, void (*response_cb)(client_t *client));

void cmd_incr(command_args_t args, void (*response_cb)(client_t *client));

void cmd_incr_by(command_args_t args, void (*response_cb)(client_t *client));

void cmd_ping(command_args_t args, void (*response_cb)(client_t *client));

void cmd_info(command_args_t args, void (*response_cb)(client_t *client));

void cmd_unknown(command_args_t args, void (*response_cb)(client_t *client));

void command_response_handler(client_t *client);

#endif // CLIENT_COMMAND_HANDLERS