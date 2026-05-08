#include "../src/config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

server_t server;

static char *write_temp_config(const char *contents)
{
    char *path = strdup("/tmp/fkvs-server-config-test-XXXXXX");
    assert(path != NULL);

    const int fd = mkstemp(path);
    assert(fd >= 0);

    const size_t len = strlen(contents);
    assert(write(fd, contents, len) == (ssize_t)len);
    assert(close(fd) == 0);

    return path;
}

static void reset_test_server(void)
{
    if (server.owns_bind_address) {
        free(server.bind_address);
    }
    if (server.owns_uds_socket_path) {
        free(server.uds_socket_path);
    }
    memset(&server, 0, sizeof(server));
}

static void remove_temp_config(char *path)
{
    assert(unlink(path) == 0);
    free(path);
}

static void test_server_config_uses_safe_network_defaults(void)
{
    char *path = write_temp_config("port 5995\n");
    reset_test_server();

    server_t loaded = load_server_config(path);

    assert(loaded.port == 5995);
    assert(loaded.bind_address != NULL);
    assert(strcmp(loaded.bind_address, FKVS_DEFAULT_BIND_ADDRESS) == 0);
    assert(!loaded.owns_bind_address);
    assert(loaded.max_clients == FKVS_DEFAULT_MAX_CLIENTS);
    assert(loaded.event_loop_max_events == MAX_EVENTS);
    assert(loaded.socket_domain == TCP_IP);

    reset_test_server();
    remove_temp_config(path);
    printf("test_server_config_uses_safe_network_defaults passed.\n");
}

static void test_server_config_allows_explicit_network_overrides(void)
{
    char *path = write_temp_config("port 6000\n"
                                   "bind 0.0.0.0\n"
                                   "max-clients 64\n"
                                   "event-loop-max-events 256\n");
    reset_test_server();

    server_t loaded = load_server_config(path);

    assert(loaded.port == 6000);
    assert(loaded.bind_address != NULL);
    assert(strcmp(loaded.bind_address, "0.0.0.0") == 0);
    assert(loaded.owns_bind_address);
    assert(loaded.max_clients == 64);
    assert(loaded.event_loop_max_events == 256);

    reset_test_server();
    remove_temp_config(path);
    printf("test_server_config_allows_explicit_network_overrides passed.\n");
}

int main(void)
{
    test_server_config_uses_safe_network_defaults();
    test_server_config_allows_explicit_network_overrides();
    return 0;
}
