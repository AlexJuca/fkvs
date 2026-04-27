#include "commands/server/server_command_handlers.h"
#include "config.h"
#include "core/hashtable.h"
#include "core/list.h"
#include "counter.h"
#include "io/event_dispatcher.h"
#include "networking/networking.h"
#include "server_lifecycle.h"
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
#include <unistd.h>

/* Global Server instance */
server_t server;

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
    fprintf(stderr, "usage: fkvs-server [-c config_path] \n");
    fprintf(stderr, "fkvs-server --version \n");
    fprintf(stderr, "fkvs-server -h or --help \n");

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

    exit(1);
}

static void install_signal_handlers(void)
{
    struct sigaction shutdown_action;
    memset(&shutdown_action, 0, sizeof(shutdown_action));
    shutdown_action.sa_handler = request_server_shutdown;
    sigemptyset(&shutdown_action.sa_mask);
    sigaction(SIGINT, &shutdown_action, NULL);
    sigaction(SIGTERM, &shutdown_action, NULL);

    struct sigaction pipe_action;
    memset(&pipe_action, 0, sizeof(pipe_action));
    pipe_action.sa_handler = SIG_IGN;
    sigemptyset(&pipe_action.sa_mask);
    sigaction(SIGPIPE, &pipe_action, NULL);
}

void setup_client_list()
{
    server.clients = listCreate();
}

int main(int argc, char *argv[])
{
    char *config_path = DEFAULT_SERVER_CONFIG_FILE_PATH;

    if (argc < 2) {
        print_usage_and_exit();
    }

    for (int i = 0; i < argc; i++) {
        if (strcmp("-c", argv[i]) == 0) {
            config_path = argv[i + 1];
        }
    }

    server = load_server_config(config_path);
    server.pid = getpid();
    counter_t *metrics = init_counter();
    if (!metrics) {
        fprintf(stderr, "Failed to initialize server metrics\n");
        exit(EXIT_FAILURE);
    }
    server.metrics = *metrics;
    free(metrics);

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

        if (server.show_logo && !server.daemonize) {
            show_logo();
        }

        LOG_INFO("Server starting");
    }

    setup_client_list();
    install_signal_handlers();

    if (server.socket_domain == UNIX) {
        server.fd = start_uds_server();
    } else {
        server.fd = start_server();
    }

    if (server.fd == -1) {
        fprintf(stderr, "Failed to start the server. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    server.database = malloc(sizeof(db_t));
    server.database->store = create_hash_table(TABLE_SIZE);
    server.database->expires = create_hash_table(TABLE_SIZE);

    init_command_handlers(server.database);

#ifdef __linux__
    if (server.use_io_uring) {
        server.event_dispatcher_kind = io_uring_kind;
        LOG_INFO("event-loop-kind: io_uring");
    } else {
        server.event_dispatcher_kind = epoll_kind;
        LOG_INFO("event-loop-kind: epoll");
    }
#elif defined(__APPLE__)
    server.event_dispatcher_kind = kqueue_kind;
    LOG_INFO("event-loop-kind: kqueue");
#else
#error                                                                         \
    "Platform not supported: io_uring currently supports only Linux and macOS uses kqueue."
#endif

    const int event_loop_result = run_event_loop();
    shutdown_server(&server);

    return event_loop_result == 0 ? 0 : 1;
}
