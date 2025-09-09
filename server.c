#include "config.c"
#include "event_dispatcher.h"
#include "hashtable.h"
#include "list.c"
#include "list.h"
#include "network.c"
#include "server_command_handlers.h"
#include "utils.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

/* Global Server instance */
server_t server;

int SERVER_FD = -1;
int EPOLL_KQQUEUE_FD = -1;

void show_logo()
{
    FILE *f = fopen("logo.txt", "r");
    char line[256];

    if (f == NULL) {
        WARN("Failed to read logo.txt, Skipping...");
        return;
    }

    while (fgets(line, sizeof(line), f)) {
        fprintf(stdout, "%s", line);
    }

    fclose(f);
}

void daemonize()
{
    int fd;

    if (fork() != 0)
        exit(0);

    if (setsid() == -1) {
        fprintf(stderr, "Error starting a new session: %s \n", strerror(errno));
        exit(1);
    }

    if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO)
            close(fd);
    }
}

void print_usage_and_exit()
{
    fprintf(stderr, "usage: fkvs [-c config_path] \n");
    fprintf(stderr, "fkvs --version \n");
    fprintf(stderr, "fkvs -h or --help \n");

    exit(1);
}

void print_version_and_exit()
{
    struct utsname buffer;

    if (uname(&buffer) != 0) {
        exit(1);
    }

    printf("fkvs v0.0.1 \n");
    printf("running on %s, %s \n", buffer.sysname, buffer.machine);

    exit(0);
}

void handle_sigint(int sig)
{
    printf("\nCaught signal interrupt %d, shutting down server.\n", sig);
    // Ensure we close all client connections
    const list_node_t *current = server.clients->head;
    while (current != NULL) {
        close((intptr_t)current->val);
        current = current->next;
    }

    listEmpty(server.clients);
    server.numClients = 0;

    // Cleanup server
    close(SERVER_FD);
    close(EPOLL_KQQUEUE_FD);
    exit(EXIT_SUCCESS);
}

void setup_client_list()
{
    server.clients = listCreate();
}

int main(int argc, char *argv[])
{
    char *config_path = DEFAULT_SERVER_CONFIG_FILE_PATH;

    server.pid = getpid();

    time_t ct;

    if (argc < 2) {
        print_usage_and_exit();
    }

    for (int i = 0; i < argc; i++) {
        if (strcmp("-c", argv[i]) == 0) {
            config_path = argv[i + 1];
        }
    }

    server = loadServerConfig(config_path);

    for (int i = 0; i < argc; i++) {
        if (strcmp("-h", argv[i]) == 0 || strcmp("--help", argv[i]) == 0) {
            print_usage_and_exit();
        }

        if (strcmp("version", argv[i]) == 0 ||
            strcmp("--version", argv[i]) == 0) {
            print_version_and_exit();
        }

        if (strcmp("--port", argv[i]) == 0) {
            if (i + 1 >= argc) {
                ERROR_AND_EXIT("Expected a port number");
            }

            server.port = (int)strtol(argv[i + 1], NULL, 10);
        }
    }

    if (server.daemonize) {
        daemonize();
    } else {

        if (server.showLogo && !server.daemonize) {
            show_logo();
        }

        LOG("Server starting");
        if (server.verbose) {
            LOG("Current process id: ");
        }

        time(&ct);
        printf("Server started at: %s", ctime(&ct));
    }

    setup_client_list();
    signal(SIGINT, handle_sigint);

    SERVER_FD = start_server(&server);
    if (SERVER_FD == -1) {
        fprintf(stderr, "Failed to start the server. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    HashTable *ht = create_hash_table(8092);

    init_command_handlers(ht);

    EPOLL_KQQUEUE_FD = run_event_loop(SERVER_FD);

    return 0;
}
