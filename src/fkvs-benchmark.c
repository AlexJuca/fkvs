#include "client.h"
#include "commands/client/client_command_handlers.h"
#include "commands/common/command_parser.h"
#include "keygen.h"
#include "networking/networking.h"

#include <assert.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

typedef struct {
    uint64_t requests; // total requests across all threads
    uint64_t clients;  // number of worker threads
    uint64_t pipeline_depth; // number of commands to pipeline (default 1)
    bool keep_alive;   // persistent connection per thread
    const char *ip;
    int port;
    bool verbose;
    char *command_type;
    enum socket_domain socket_domain;
    bool use_random_keys;
    double rate; // open-model target req/s across all clients (0 = off)
} benchmark_config_t;

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int ready; // workers that arrived at the gate
    int need;  // how many workers to wait for
    int go;    // broadcast start when set to 1
    uint64_t start_ns; // shared schedule origin for open-model pacing
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
            "[-k] [-P pipeline] \n"
            "  -n N     total requests across all clients (default 100000)\n"
            "  -c C     number of concurrent clients (default 32)\n"
            "  -h HOST  server host/IP (default 127.0.0.1)\n"
            "  -p PORT  server port (default 5995)\n"
            "  -k       keep-alive (default on)\n"
            "  -u       connect via unix domain socket \n"
            "  -t       type of command to use during benchmark (ping, "
            "set, default ping) \n"
            "  -r       use a unique key per insertion command (set, setx, "
            "etc) instead of reusing a fixed key\n"
            "  -P N     pipeline N commands per batch (default 1, no "
            "pipelining)\n"
            "  -R RATE  open-model: drive a constant RATE req/s (across all "
            "clients) and report coordinated-omission-corrected latency "
            "percentiles via HdrHistogram. Overrides -P (uses depth 1).",
            prog);
    exit(1);
}

/**
 * We use CLOCK_MONOTONIC_RAW for micro-benchmarking because it is
 * unaffected by NTP adjustments and other time corrections.
 * It provides a raw hardware-based monotonic clock source.
 * This typically comes from a CPU’s TSC (time stamp counter)
 * on x86 and the System Counter(CNTVCT_EL0) on M1+ processors
 * or another stable hardware counter chosen by the kernel.
 * Unlike CLOCK_MONOTONIC, it is not smoothed or adjusted by the OS.
 * This makes it ideal for precise interval measurements,
 * such as micro-benchmarking, where absolute wall-clock time is irrelevant
 * and only raw monotonically increasing time matters.
 */
static double monotonic_seconds(void)
{
    /**
     * We use mach_absolute_time time on macOS only because, older versions of
     * macOS, e.g, macOS Sierra did not have CLOCK_MONOTONIC_RAW.
     * We could remove this in future if we don't intend to support macOS Sierra
     *and older.
     */
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

// Nanosecond monotonic clock for open-model pacing and latency timing.
static uint64_t monotonic_ns(void)
{
#ifdef __APPLE__
    static mach_timebase_info_data_t tb;
    if (!tb.denom)
        mach_timebase_info(&tb);
    return (uint64_t)((double)mach_absolute_time() * (double)tb.numer /
                      (double)tb.denom);
#else
#ifdef CLOCK_MONOTONIC_RAW
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

// Sleep until an absolute monotonic deadline. nanosleep handles the bulk; the
// final ~150us is busy-spun for the precision open-model pacing needs.
static void sleep_until_ns(const uint64_t target)
{
    for (;;) {
        const uint64_t now = monotonic_ns();
        if (now >= target)
            return;
        const uint64_t rem = target - now;
        if (rem > 150000) {
            const uint64_t s = rem - 100000;
            struct timespec ts = {.tv_sec = (time_t)(s / 1000000000ull),
                                  .tv_nsec = (long)(s % 1000000000ull)};
            nanosleep(&ts, NULL);
        }
        // else spin
    }
}

/*
 * Compact HdrHistogram (High Dynamic Range). Records values with constant
 * relative error across a wide range, so high percentiles (p99.9, p99.99) stay
 * accurate. Self-contained and integer-only (no libm). This is what makes the
 * open-model mode coordinated-omission honest: we record latency from each
 * request's *intended* send time, and HdrHistogram preserves the resulting tail.
 */
typedef struct {
    int64_t highest;
    int32_t unit_magnitude;
    int32_t sub_bucket_half_count_magnitude;
    int32_t sub_bucket_count;
    int32_t sub_bucket_half_count;
    int64_t sub_bucket_mask;
    int32_t bucket_count;
    int32_t counts_len;
    int64_t total_count;
    int64_t min_value;
    int64_t max_value;
    int64_t *counts;
} hdr_t;

static int32_t floor_log2_i(const int64_t v)
{
    return 63 - __builtin_clzll((uint64_t)v);
}
static int32_t ceil_log2_i(const int64_t v)
{
    const int32_t f = floor_log2_i(v);
    return ((int64_t)1 << f) == v ? f : f + 1;
}
static int64_t ipow10(int32_t e)
{
    int64_t r = 1;
    while (e-- > 0)
        r *= 10;
    return r;
}

static int32_t hdr_buckets_needed(int64_t value, const int32_t sub_bucket_count,
                                  const int32_t unit_magnitude)
{
    int64_t smallest_untrackable = (int64_t)sub_bucket_count << unit_magnitude;
    int32_t needed = 1;
    while (smallest_untrackable <= value) {
        if (smallest_untrackable > INT64_MAX / 2)
            return needed + 1;
        smallest_untrackable <<= 1;
        needed++;
    }
    return needed;
}

static bool hdr_init(hdr_t *h, const int64_t lowest, const int64_t highest,
                     const int32_t sig)
{
    h->highest = highest;
    h->unit_magnitude = floor_log2_i(lowest);
    const int64_t largest_single_unit = 2 * ipow10(sig);
    const int32_t sub_bucket_count_magnitude = ceil_log2_i(largest_single_unit);
    h->sub_bucket_half_count_magnitude =
        (sub_bucket_count_magnitude > 1 ? sub_bucket_count_magnitude : 1) - 1;
    h->sub_bucket_count = (int32_t)1 << (h->sub_bucket_half_count_magnitude + 1);
    h->sub_bucket_half_count = h->sub_bucket_count / 2;
    h->sub_bucket_mask = ((int64_t)h->sub_bucket_count - 1) << h->unit_magnitude;
    h->bucket_count =
        hdr_buckets_needed(highest, h->sub_bucket_count, h->unit_magnitude);
    h->counts_len = (h->bucket_count + 1) * (h->sub_bucket_count / 2);
    h->counts = calloc((size_t)h->counts_len, sizeof(int64_t));
    h->total_count = 0;
    h->min_value = INT64_MAX;
    h->max_value = 0;
    return h->counts != NULL;
}

static int32_t hdr_counts_index_for(const hdr_t *h, const int64_t value)
{
    const int32_t pow2ceiling =
        64 - __builtin_clzll((uint64_t)(value | h->sub_bucket_mask));
    const int32_t bucket_index =
        pow2ceiling - h->unit_magnitude - (h->sub_bucket_half_count_magnitude + 1);
    const int32_t sub_bucket_index =
        (int32_t)((uint64_t)value >> (bucket_index + h->unit_magnitude));
    const int32_t bucket_base = (bucket_index + 1)
                                << h->sub_bucket_half_count_magnitude;
    return bucket_base + (sub_bucket_index - h->sub_bucket_half_count);
}

static void hdr_record(hdr_t *h, int64_t value)
{
    if (value < 0)
        value = 0;
    if (value > h->highest)
        value = h->highest;
    const int32_t idx = hdr_counts_index_for(h, value);
    if (idx < 0 || idx >= h->counts_len)
        return;
    h->counts[idx]++;
    h->total_count++;
    if (value < h->min_value)
        h->min_value = value;
    if (value > h->max_value)
        h->max_value = value;
}

static int64_t hdr_value_at_index(const hdr_t *h, const int32_t index)
{
    int32_t bucket_index = (index >> h->sub_bucket_half_count_magnitude) - 1;
    int32_t sub_bucket_index =
        (index & (h->sub_bucket_half_count - 1)) + h->sub_bucket_half_count;
    if (bucket_index < 0) {
        sub_bucket_index -= h->sub_bucket_half_count;
        bucket_index = 0;
    }
    return (int64_t)sub_bucket_index << (bucket_index + h->unit_magnitude);
}

static int64_t hdr_value_at_percentile(const hdr_t *h, double percentile)
{
    if (h->total_count == 0)
        return 0;
    if (percentile > 100.0)
        percentile = 100.0;
    int64_t count_at = (int64_t)((percentile / 100.0) * (double)h->total_count + 0.5);
    if (count_at < 1)
        count_at = 1;
    int64_t total = 0;
    for (int32_t i = 0; i < h->counts_len; i++) {
        total += h->counts[i];
        if (total >= count_at)
            return hdr_value_at_index(h, i);
    }
    return h->max_value;
}

static double hdr_mean(const hdr_t *h)
{
    if (h->total_count == 0)
        return 0.0;
    double sum = 0.0;
    for (int32_t i = 0; i < h->counts_len; i++)
        if (h->counts[i])
            sum += (double)h->counts[i] * (double)hdr_value_at_index(h, i);
    return sum / (double)h->total_count;
}

static void hdr_add(hdr_t *dst, const hdr_t *src)
{
    if (!src->counts)
        return;
    const int32_t n = dst->counts_len < src->counts_len ? dst->counts_len
                                                        : src->counts_len;
    for (int32_t i = 0; i < n; i++)
        dst->counts[i] += src->counts[i];
    dst->total_count += src->total_count;
    if (src->total_count > 0) {
        if (src->min_value < dst->min_value)
            dst->min_value = src->min_value;
        if (src->max_value > dst->max_value)
            dst->max_value = src->max_value;
    }
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
    uint64_t interval_ns; // per-worker send interval in open-model mode
    uint64_t phase_ns;    // per-worker schedule offset (staggers arrivals)
    // these are our outputs:
    uint64_t completed;
    uint64_t failed;
    hdr_t hist; // per-worker latency histogram (open-model mode)
} worker_args_t;

static void *worker(void *arg)
{
    worker_args_t *w = arg;
    client_t client = {0};
    client.frame_need = -1;

    if (w->cfg->socket_domain == UNIX) {
        client.socket_domain = w->cfg->socket_domain;
        client.uds_socket_path = FKVS_SOCK_PATH;
    }
    client.ip_address = (char *)w->cfg->ip;
    client.port = w->cfg->port;
    client.benchmark_mode = true;
    client.verbose = w->cfg->verbose;

    int fd;

    if (client.socket_domain == UNIX) {
        fd = start_uds_client(&client);
    } else {
        fd = start_client(&client);
    }

    assert(fd != -1);
    if (fd != -1) {
        if (client.socket_domain == TCP_IP) {
            tune_socket(fd);
        }
    }

    const uint64_t total = w->num_reqs_for_this_thread;
    const bool is_set = strcasecmp(w->cfg->command_type, "set") == 0;

    // Pre-build every command before the start gate so that key generation and
    // command allocation are excluded from the timed loop. With unique keys we
    // need one distinct command per request; otherwise a single command is
    // reused for all sends. Memory here scales with the request count, which is
    // the same order as the keyspace the server stores.
    unsigned char **cmds = NULL; // one per request (unique-key mode)
    size_t *lens = NULL;
    unsigned char *single_cmd = NULL; // reused for all sends (fixed key / ping)
    size_t single_len = 0;

    if (is_set && w->cfg->use_random_keys) {
        cmds = calloc(total, sizeof(*cmds));
        lens = calloc(total, sizeof(*lens));
        if (!cmds || !lens) {
            fprintf(stderr, "Failed to allocate %llu command slots\n",
                    (unsigned long long)total);
            free(cmds);
            free(lens);
            w->completed = 0;
            w->failed = total;
            return NULL;
        }
        for (uint64_t i = 0; i < total; i++) {
            char key[33];
            generate_unique_key(key);
            cmds[i] = construct_set_command(key, "world", &lens[i]);
        }
    } else if (is_set) {
        single_cmd = construct_set_command("hello", "world", &single_len);
    } else {
        single_cmd = construct_ping_command("", &single_len);
    }

    // arrive at the start gate
    pthread_mutex_lock(&w->gate->mu);
    w->gate->ready++;
    pthread_cond_signal(&w->gate->cv);
    while (!w->gate->go)
        pthread_cond_wait(&w->gate->cv, &w->gate->mu);
    const uint64_t start_ns = w->gate->start_ns;
    pthread_mutex_unlock(&w->gate->mu);

    uint64_t ok = 0, ko = 0;

    // Open-model mode: send on a fixed schedule regardless of when responses
    // arrive, and measure each request's latency from its *intended* send time.
    // This is the coordinated-omission correction — a server stall surfaces as a
    // growing backlog of late requests rather than being silently skipped.
    if (w->cfg->rate > 0.0) {
        const uint64_t interval = w->interval_ns;
        for (uint64_t i = 0; i < total; i++) {
            const uint64_t intended = start_ns + w->phase_ns + i * interval;
            sleep_until_ns(intended);
            const unsigned char *buf = cmds ? cmds[i] : single_cmd;
            const size_t len = cmds ? lens[i] : single_len;
            if (buf)
                send(client.fd, buf, len, 0);
            const uint64_t got = recv_pipeline_responses(&client, 1);
            const uint64_t end = monotonic_ns();
            if (got == 1) {
                ok++;
                hdr_record(&w->hist, (int64_t)((end - intended) / 1000));
            } else {
                ko++;
            }
        }

        if (cmds) {
            for (uint64_t i = 0; i < total; i++)
                free(cmds[i]);
            free(cmds);
            free(lens);
        }
        free(single_cmd);
        w->completed = ok;
        w->failed = ko;
        return NULL;
    }

    uint64_t remaining = total;
    uint64_t sent = 0;
    const uint64_t pdepth = w->cfg->pipeline_depth;

    while (remaining > 0) {
        const uint64_t batch = remaining < pdepth ? remaining : pdepth;

        // Send phase: fire off `batch` pre-built commands without waiting.
        for (uint64_t j = 0; j < batch; j++) {
            const unsigned char *buf = cmds ? cmds[sent] : single_cmd;
            const size_t len = cmds ? lens[sent] : single_len;
            sent++;
            if (buf)
                send(client.fd, buf, len, 0);
        }

        // Recv phase: consume exactly `batch` framed responses
        const uint64_t got = recv_pipeline_responses(&client, batch);
        ok += got;
        ko += (batch - got);
        remaining -= batch;
    }

    if (cmds) {
        for (uint64_t i = 0; i < total; i++)
            free(cmds[i]);
        free(cmds);
        free(lens);
    }
    free(single_cmd);

    w->completed = ok;
    w->failed = ko;
    return NULL;
}

int main(const int argc, char **argv)
{
    benchmark_config_t cfg = {.requests = 100000,
                              .clients = 32,
                              .pipeline_depth = 1,
                              .keep_alive = true,
                              .ip = "127.0.0.1",
                              .port = 5995,
                              .command_type = "PING",
                              .use_random_keys = false,
                              .verbose = false,
                              .rate = 0.0};

    for (int i = 1; i < argc; ++i) {
        if (strcasecmp(argv[i], "--h") == 0 ||
            strcasecmp(argv[i], "--help") == 0) {
            print_usage_and_exit(argv[0]);
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            cfg.requests = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            cfg.clients = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            cfg.ip = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            cfg.port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-u") == 0) {
            cfg.socket_domain = UNIX;
        } else if (strcmp(argv[i], "-k") == 0) {
            // TODO: Currently, client always keeps the connection alive, in the
            // future we want to ensure this value passes down to a client to
            // ensure
            // connection is handled based on this value.
            cfg.keep_alive = true;
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            if (strcmp(argv[i + 1], "ping") == 0) {
                cfg.command_type = "PING";
            }

            if (strcmp(argv[i + 1], "set") == 0) {
                cfg.command_type = "SET";
            }
        } else if (strcmp(argv[i], "-r") == 0) {
            cfg.use_random_keys = true;
        } else if (strcmp(argv[i], "-P") == 0 && i + 1 < argc) {
            cfg.pipeline_depth = strtoull(argv[++i], NULL, 10);
            if (cfg.pipeline_depth == 0)
                cfg.pipeline_depth = 1;
        } else if (strcmp(argv[i], "-R") == 0 && i + 1 < argc) {
            cfg.rate = strtod(argv[++i], NULL);
            if (cfg.rate < 0)
                cfg.rate = 0;
        }
    }

    // Open-model mode paces one request at a time per client and records
    // coordinated-omission-corrected latency, so pipelining does not apply.
    if (cfg.rate > 0.0)
        cfg.pipeline_depth = 1;

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
        if (cfg.rate > 0.0) {
            // Per-worker send interval so the C clients sum to the target rate.
            args[t].interval_ns =
                (uint64_t)(1e9 * (double)cfg.clients / cfg.rate);
            // Stagger workers across one aggregate inter-arrival (1e9/rate ns)
            // so the C clients produce smooth arrivals, not synchronized bursts.
            args[t].phase_ns = (uint64_t)(1e9 / cfg.rate) * t;
            // 1us..60s range, 3 significant figures.
            if (!hdr_init(&args[t].hist, 1, 60LL * 1000 * 1000, 3)) {
                fprintf(stderr, "Failed to allocate latency histogram\n");
                free(ths);
                free(args);
                return 1;
            }
        }

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
        abs.tv_sec += 1; // We wait N seconds to gather
        const int rc = pthread_cond_timedwait(&gate.cv, &gate.mu, &abs);
        if (rc == ETIMEDOUT) {
            fprintf(stderr, "%d/%d workers ready.\n", gate.ready, gate.need);
            gate.need = gate.ready; // Let's proceed with the ones that arrived
            break;
        }
    }
    const double start = monotonic_seconds();
    // Schedule origin a hair in the future so all workers wake before request 0
    // is due (avoids a startup spike polluting the latency tail).
    gate.start_ns = monotonic_ns() + 2000000; // +2ms
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

    printf("Clients: %llu  Total requests: %llu  Pipeline: %llu\n",
           (unsigned long long)cfg.clients, (unsigned long long)cfg.requests,
           (unsigned long long)cfg.pipeline_depth);
    printf("Completed: %llu  Failed: %llu\n", (unsigned long long)done,
           (unsigned long long)fail);
    printf("Elapsed: %.6f s  Throughput: %.2f req/s\n", elapsed, rps);

    if (cfg.rate > 0.0) {
        hdr_t all;
        if (hdr_init(&all, 1, 60LL * 1000 * 1000, 3)) {
            for (int t = 0; t < started; ++t)
                hdr_add(&all, &args[t].hist);

            printf("\nOpen-model latency (coordinated-omission corrected):\n");
            printf("  Target rate : %.0f req/s   Achieved: %.0f req/s\n",
                   cfg.rate, rps);
            printf("  Samples     : %lld\n", (long long)all.total_count);
            printf("  min         : %9.1f us\n", (double)all.min_value);
            printf("  p50         : %9.1f us\n",
                   (double)hdr_value_at_percentile(&all, 50.0));
            printf("  p90         : %9.1f us\n",
                   (double)hdr_value_at_percentile(&all, 90.0));
            printf("  p99         : %9.1f us\n",
                   (double)hdr_value_at_percentile(&all, 99.0));
            printf("  p99.9       : %9.1f us\n",
                   (double)hdr_value_at_percentile(&all, 99.9));
            printf("  p99.99      : %9.1f us\n",
                   (double)hdr_value_at_percentile(&all, 99.99));
            printf("  max         : %9.1f us\n", (double)all.max_value);
            printf("  mean        : %9.1f us\n", hdr_mean(&all));
            free(all.counts);
        }
        for (int t = 0; t < started; ++t)
            free(args[t].hist.counts);
    }

    free(ths);
    free(args);
    return 0;
}
