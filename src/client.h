#ifndef CLIENT_H
#define CLIENT_H

#include "networking/modes.h"

#include <arpa/inet.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct client_t {
#define BUFFER_SIZE 655365
    char *command_type;
    char *config_file_path;
    char *command;
    char *uds_socket_path; // Unix domain socket path
    char *ip_address;
    size_t buf_used; // bytes currently in buffer
    ssize_t
        frame_need; // -1 until we know; else total frame size (2 + core_len)
    int port;
    int fd;
    enum socket_domain
        socket_domain; // The socket domain we are using (Unix Domain or TCP/IP)

    unsigned char buffer[65536];   // adjust if we expect larger frames
    char ip_str[INET6_ADDRSTRLEN]; // TODO: Remove this in the future
    bool benchmark_mode;
    bool interactive_mode;
    bool verbose; // print additional information during runtime
} client_t __attribute__((aligned(64)));

client_t *init_client(int client_fd, struct sockaddr_storage ss,
                      enum socket_domain socket_domain);

#endif // CLIENT_H
