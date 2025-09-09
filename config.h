#ifndef CONFIG_H
#define CONFIG_H

#include "client.h"
#include "server.h"

#define DEFAULT_SERVER_CONFIG_FILE_PATH "server.conf"
#define DEFAULT_CLIENT_CONFIG_FILE_PATH "client.conf"

server_t loadServerConfig(const char *path);
client_t loadClientConfig(const char *path);

#endif // CONFIG_H
