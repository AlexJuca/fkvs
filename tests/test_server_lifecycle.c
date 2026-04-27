#include "../src/client.h"
#include "../src/core/hashtable.h"
#include "../src/core/list.h"
#include "../src/server_lifecycle.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static bool fd_is_closed(const int fd)
{
    errno = 0;
    return fcntl(fd, F_GETFD) == -1 && errno == EBADF;
}

static client_t *test_client(const int fd)
{
    struct sockaddr_storage ss;
    memset(&ss, 0, sizeof(ss));
    client_t *client = init_client(fd, ss, UNIX);
    assert(client != NULL);
    return client;
}

static void test_signal_handler_only_sets_shutdown_flag(void)
{
    reset_server_shutdown_request();
    assert(!server_shutdown_requested());
    request_server_shutdown(SIGTERM);
    assert(server_shutdown_requested());
    reset_server_shutdown_request();
    assert(!server_shutdown_requested());

    printf("test_signal_handler_only_sets_shutdown_flag passed.\n");
}

static void test_shutdown_server_releases_clients_and_database(void)
{
    int client_pair[2];
    int server_pair[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, client_pair) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, server_pair) == 0);

    const char uds_path[] = "/tmp/fkvs-test-server-lifecycle.sock";
    int marker = open(uds_path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    assert(marker >= 0);
    close(marker);

    server_t srv = {0};
    srv.fd = server_pair[0];
    srv.socket_domain = UNIX;
    srv.uds_socket_path = strdup(uds_path);
    assert(srv.uds_socket_path != NULL);
    srv.owns_uds_socket_path = true;
    srv.clients = listCreate();
    assert(srv.clients != NULL);

    client_t *client = test_client(client_pair[0]);
    srv.clients = listAddNodeToTail(srv.clients, client);
    srv.num_clients = 1;

    srv.database = malloc(sizeof(*srv.database));
    assert(srv.database != NULL);
    srv.database->store = create_hash_table(TABLE_SIZE);
    srv.database->expires = create_hash_table(TABLE_SIZE);
    assert(srv.database->store != NULL);
    assert(srv.database->expires != NULL);
    assert(set_value(srv.database->store, (const unsigned char *)"key", 3,
                     "value", 5, VALUE_ENTRY_TYPE_RAW));
    assert(set_value(srv.database->expires, (const unsigned char *)"key", 3,
                     "123", 3, VALUE_ENTRY_TYPE_RAW));

    shutdown_server(&srv);

    assert(srv.clients == NULL);
    assert(srv.database == NULL);
    assert(srv.num_clients == 0);
    assert(srv.fd == -1);
    assert(fd_is_closed(client_pair[0]));
    assert(fd_is_closed(server_pair[0]));
    assert(access(uds_path, F_OK) == -1);

    close(client_pair[1]);
    close(server_pair[1]);

    printf("test_shutdown_server_releases_clients_and_database passed.\n");
}

int main(void)
{
    test_signal_handler_only_sets_shutdown_flag();
    test_shutdown_server_releases_clients_and_database();
    return 0;
}
