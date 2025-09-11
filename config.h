#ifndef CONFIG_H
#define CONFIG_H

#define DEFAULT_SERVER_CONFIG_FILE_PATH "server.conf"
#define DEFAULT_CLIENT_CONFIG_FILE_PATH "client.conf"

#ifdef SERVER
#include "server.h"
server_t loadServerConfig(const char *path);
#endif

#ifdef CLI
#include "client.h"
client_t loadClientConfig(const char *path);
#endif

#endif // CONFIG_H
