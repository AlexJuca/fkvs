#ifndef CLIENT_H
#define CLIENT_H

#include <netinet/in.h>
#include <stdbool.h>

typedef struct client_t {
#define BUFFER_SIZE 655365
    int fd;
    unsigned char buffer[65536]; // adjust if we expect larger frames
    size_t buf_used;             // bytes currently in buffer
    ssize_t
        frame_need; // -1 until we know; else total frame size (2 + core_len)
    char ip_str[INET6_ADDRSTRLEN]; // TODO: Remove this in the future
    char *ip_address;
    int port;
    bool verbose; // print additional information
} client_t;

#endif // CLIENT_H
