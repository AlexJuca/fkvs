#include "client.h"
#include "client_command_handlers.h"
#include "networking.h"
#include "utils.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

typedef struct benchmark_config {
    u_int64_t requests;
    u_int64_t clients;
    bool keep_alive;
} benchmark_config_t;

void print_usage_and_exit()
{
    printf("Usage: fkvs-benchmark [options]\n");
    exit(1);
}

typedef struct benchmark_stats {

} benchmark_stats;

// clock_gettime was a good choice because it works on both macOS and Linux.
static double monotonic_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(int argc, char *argv[])
{
    client_t *client = malloc(sizeof(client_t));
    client->port = 5995;
    client->ip_address = "127.0.0.1";
    client->verbose = false;

    benchmark_config_t *benchmark_config = malloc(sizeof(benchmark_config_t));
    benchmark_config->keep_alive = true;
    benchmark_config->clients = 50;
    benchmark_config->requests = 1000;

    if (argc < 2) {
        print_usage_and_exit();
    }

    for (int i = 0; i < argc; i++) {
        if (strcmp("-p ", argv[i]) == 0) {
            client->port = atoi(argv[i++]);
        }

        if (strcmp("-h ", argv[i]) == 0) {
            client->ip_address = argv[i++];
        }

        if (strcmp("-k ", argv[i]) == 0) {
            benchmark_config->keep_alive = true;
        }

        if (strcmp("-n", argv[i]) == 0) {
            benchmark_config->requests = strtoull(argv[++i], NULL, 10);
        }

        if (strcmp("-c ", argv[i]) == 0) {
            benchmark_config->clients = strtoull(argv[++i], NULL, 10);
        }
    }

    const int client_fd = start_client(client);
    static uint64_t counter = 0;

    char command[BUFFER_SIZE] = {"PING"};

    printf("Benchmark requestz: %llu\n", benchmark_config->requests);

    const char *cmd_part = strtok(command, " ");
    if (cmd_part == NULL) {
        print_usage_and_exit();
    }

    const double t0 = monotonic_seconds();
    for (int i = 0; i < benchmark_config->requests; i++) {
        if (client_fd == -1) {
            ERROR_AND_EXIT("Failed to connect to server");
            return 1;
        }
        execute_command(cmd_part, client, command_response_handler);
        counter += 1;
    }
    const double t1 = monotonic_seconds();

    const double elapsed = t1 - t0;

    const double reqs_sec = (double)counter / elapsed;
    printf("Total calls: %llu\n", counter);
    printf("Elapsed: %.6f s\n", elapsed);
    printf("Estimated reqs/sec: %.2f\n", reqs_sec);
    free(benchmark_config);
    free(client);
}