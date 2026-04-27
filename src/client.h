#ifndef CLIENT_H
#define CLIENT_H

#include "networking/modes.h"

#include <arpa/inet.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <unistd.h>

#define FKVS_CLIENT_READ_BUFFER_SIZE 65536
#define FKVS_CLIENT_WRITE_BUFFER_INITIAL_CAPACITY 65536
#define FKVS_CLIENT_WRITE_BUFFER_MAX_CAPACITY (1024U * 1024U)
#define BUFFER_SIZE FKVS_CLIENT_READ_BUFFER_SIZE

typedef struct client_t {
    char *command_type;
    const char *config_file_path;
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

    unsigned char buffer[FKVS_CLIENT_READ_BUFFER_SIZE];
    unsigned char *wbuf;           // queued response bytes
    size_t wbuf_capacity;          // allocated response queue capacity
    size_t wbuf_used;              // bytes currently in response queue
    char ip_str[INET6_ADDRSTRLEN]; // TODO: Remove this in the future
    bool write_failed;             // close client after unrecoverable send error
    bool write_registered;         // event loop is watching write readiness
    bool benchmark_mode;
    bool interactive_mode;
    bool verbose; // print additional information during runtime
} client_t __attribute__((aligned(64)));

client_t *init_client(int client_fd, struct sockaddr_storage ss,
                      enum socket_domain socket_domain);
// Releases client-owned memory. The caller owns closing client->fd.
void free_client(client_t *client);

#endif // CLIENT_H
