#include "client.h"
#include "server.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BACKLOG 10  // Number of allowed connections

int start_server(server_t *server) {
  int server_fd;
  struct sockaddr_in server_addr;

  // Creating socket file descriptor
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
      perror("socket failed");
      return -1;
  }

  const int one = 1;
  setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));


  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(server->port);

  // Binding the socket to the network address and port
  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    perror("bind failed");
    close(server_fd);
    return -1;
  }

  // Start listening for incoming connections
  if (listen(server_fd, BACKLOG) < 0) {
    perror("listen");
    close(server_fd);
    return -1;
  }

  printf("listening on port %d \n", server->port);
  return server_fd;
}

int start_client(client_t *client) {
  int client_fd;
  struct sockaddr_in server_addr;

  // Creating socket file descriptor
  if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket failed");
    return -1;
  }
  const int one = 1;
  setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

  client->fd = client_fd;

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(client->port);

  // Convert IPv4 and IPv6 addresses from text to binary form
  if (inet_pton(AF_INET, client->ip_address, &server_addr.sin_addr) <= 0) {
    perror("Invalid address/ Address not supported");
    return -1;
  }

  // Connect to the server
  if (connect(client->fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    perror("Connection Failed");
    return -1;
  }

  printf("Connected to server on port %d\n", client->port);
  return client_fd;
}
