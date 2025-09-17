#include "client.h"
#include "client_command_handlers.h"
#include "networking.h"
#include <errno.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

typedef struct {
    uint64_t requests; // total requests across all threads
    uint64_t clients;  // number of worker threads
    bool keep_alive;   // persistent connection per thread
    const char *ip;
    int port;
    bool verbose;
    char *command_type;
} benchmark_config_t;

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int ready; // workers that arrived at the gate
    int need;  // how many workers to wait for
    int go;    // broadcast start when set to 1
} start_gate_t;

static void gate_init(start_gate_t *g, const int need)
{
    pthread_mutex_init(&g->mu, NULL);
    pthread_cond_init(&g->cv, NULL);
    g->ready = 0;
    g->need = need;
    g->go = 0;
}

static void print_usage_and_exit(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [-n total_requests] [-c clients] [-h host] [-p port] "
            "[-k] \n"
            "  -n N     total requests across all clients (default 100000)\n"
            "  -c C     number of concurrent clients (default 32)\n"
            "  -h HOST  server host/IP (default 127.0.0.1)\n"
            "  -p PORT  server port (default 5995)\n"
            "  -k       keep-alive (default on)\n"
            "  -t       type of command to use during benchmark (ping, get, set, default ping) \n",
            prog);
    exit(1);
}

static double monotonic_seconds(void)
{
#ifdef __APPLE__
    static mach_timebase_info_data_t tb;
    if (!tb.denom)
        mach_timebase_info(&tb);
    const uint64_t t = mach_absolute_time();
    return ((double)t * (double)tb.numer / (double)tb.denom) * 1e-9;
#else
#ifdef CLOCK_MONOTONIC_RAW
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

static void tune_socket(const int fd)
{
    const int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0; // 2s I/O timeouts
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

typedef struct {
    const benchmark_config_t *cfg;
    start_gate_t *gate;
    uint64_t num_reqs_for_this_thread;
    // these are our outputs:
    uint64_t completed;
    uint64_t failed;
} worker_args_t;

static void *worker(void *arg)
{
    worker_args_t *w = arg;

    client_t client = {0};
    client.ip_address = (char *)w->cfg->ip;
    client.port = w->cfg->port;
    client.verbose = w->cfg->verbose;

    int fd = start_client(&client);
    if (fd != -1) {
        tune_socket(fd);
    }

    // arrive at the start gate
    pthread_mutex_lock(&w->gate->mu);
    w->gate->ready++;
    pthread_cond_signal(&w->gate->cv);
    while (!w->gate->go)
        pthread_cond_wait(&w->gate->cv, &w->gate->mu);
    pthread_mutex_unlock(&w->gate->mu);

    uint64_t ok = 0, ko = 0;
    for (uint64_t i = 0; i < w->num_reqs_for_this_thread; ++i) {
        if (fd == -1) {
            ko++;
            continue;
        }
        execute_command(w->cfg->command_type, &client, command_response_handler);
        ok++;
        // TODO: Handle failures correctly. We currently don't track failures (ko's).
    }

    w->completed = ok;
    w->failed = ko;
    return NULL;
}

int main(const int argc, char **argv)
{
    benchmark_config_t cfg = {.requests = 100000,
                              .clients = 32,
                              .keep_alive = true,
                              .ip = "127.0.0.1",
                              .port = 5995,
                              .command_type = "PING",
                              .verbose = false};

    for (int i = 1; i < argc; ++i) {
        if (strcasecmp(argv[i], "--h") == 0 || strcasecmp(argv[i], "--help") == 0) {
            print_usage_and_exit(argv[0]);
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            cfg.requests = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            cfg.clients = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            cfg.ip = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            cfg.port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-k") == 0) {
            cfg.keep_alive = true;
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
          if (strcmp(argv[++i], "ping") == 0) {
            cfg.command_type = "PING";
          }
        }
    }

    if (cfg.clients == 0)
        cfg.clients = 1;
    if (cfg.requests < cfg.clients)
        cfg.requests = cfg.clients;

    // divide work across threads
    const uint64_t base = cfg.requests / cfg.clients;
    const uint64_t rem = cfg.requests % cfg.clients;

    pthread_t *ths = calloc(cfg.clients, sizeof(*ths));
    worker_args_t *args = calloc(cfg.clients, sizeof(*args));

    start_gate_t gate;
    gate_init(&gate, (int)cfg.clients);

    int started = 0;
    for (uint64_t t = 0; t < cfg.clients; ++t) {
        args[t].cfg = &cfg;
        args[t].gate = &gate;
        args[t].num_reqs_for_this_thread = base + (t < rem ? 1 : 0);

        const int rc = pthread_create(&ths[t], NULL, worker, &args[t]);
        if (rc != 0) {
            fprintf(stderr, "pthread_create failed: %s\n", strerror(rc));
            break;
        }
        started++;
    }

    if (started == 0) {
        fprintf(stderr, "No workers started. exiting.\n");
        free(ths);
        free(args);
        return 1;
    }

    // wait for readiness with a timeout, then we proceed with those that are
    // ready
    pthread_mutex_lock(&gate.mu);
    while (gate.ready < gate.need) {
        struct timespec abs;
        clock_gettime(CLOCK_REALTIME, &abs);
        abs.tv_sec += 2; // 2s to gather
        const int rc = pthread_cond_timedwait(&gate.cv, &gate.mu, &abs);
        if (rc == ETIMEDOUT) {
            fprintf(stderr, "%d/%d workers ready.\n", gate.ready, gate.need);
            gate.need = gate.ready; // Let's proceed with the ones that arrived
            break;
        }
    }
    const double start = monotonic_seconds();
    gate.go = 1;
    pthread_cond_broadcast(&gate.cv);
    pthread_mutex_unlock(&gate.mu);

    // join and aggregate results
    uint64_t done = 0, fail = 0;
    for (int t = 0; t < started; ++t) {
        pthread_join(ths[t], NULL);
        done += args[t].completed;
        fail += args[t].failed;
    }
    const double end = monotonic_seconds();

    const double elapsed = end - start;
    const double rps = elapsed > 0 ? (double)done / elapsed : 0.0;

    printf("Clients: %llu  Total requests: %llu\n",
           (unsigned long long)cfg.clients, (unsigned long long)cfg.requests);
    printf("Completed: %llu  Failed: %llu\n", (unsigned long long)done,
           (unsigned long long)fail);
    printf("Elapsed: %.6f s  Throughput: %.2f req/s\n", elapsed, rps);

    free(ths);
    free(args);
    return 0;
}
