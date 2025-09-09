#ifndef SERVER_H
#define SERVER_H

#include "list.h"
#include <stdbool.h>
#include <sys/types.h>

typedef struct server_t {
    bool daemonize;
    int port;
    pid_t pid;
    bool isLogEnabled;
    bool verbose;
    bool showLogo;
    list_t *clients;
    u_int32_t numClients;
} server_t;

#endif // SERVER_H
