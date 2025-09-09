#ifndef UTILS_H
#define UTILS_H

#include "server.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

extern server_t server;

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static void error_and_exit(const char *ctx, const char *file, const int line)
{
    fprintf(stderr, "[%s - %d]\n", file, line);
    perror(ctx);
    exit(EXIT_FAILURE);
}

static void warn(const char *ctx, const char *file, int line)
{
    fprintf(stderr, "[%s - %d]\n", file, line);
    perror(ctx);
}

// TODO: When process is running as a daemon, ensure we don't print to standard
// file descriptors rather we should write to a log file.
static inline void append_to_log_file(char *ctx) {}

static void log_std_out(char *ctx)
{
    const time_t ct = time(NULL);
    char ts[32];

    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&ct));

    fprintf(stdout, "%s - %s \n", ts, ctx);
}

static void log(char *ctx)
{
    server.daemonize ? append_to_log_file(ctx) : log_std_out(ctx);
}

void inline print_binary_data(const unsigned char *data, const size_t len)
{
    for (size_t j = 0; j < len; j++) {
        const unsigned char c = data[j];
        if (c >= 32 && c <= 126) {
            putchar(c); // printable ASCII
        } else {
            printf("\\x%02x", c); // escape non-printable
        }
    }
    putchar('\n');
}

static inline bool is_integer(const unsigned char *str, const size_t len)
{
    if (len == 0) {
        return false;
    }

    size_t i = 0;

    if (str[0] == '+' || str[0] == '-') {
        i = 1;
        // If only the sign is present, it's invalid
        if (i >= len) {
            return false;
        }
    }

    for (; i < len; i++) {
        if (str[i] < '0' || str[i] > '9') {
            return false;
        }
    }

    return true;
}

static inline char *int_to_string(const uint64_t number)
{
    char *buffer = malloc(22 * sizeof(char));
    if (buffer == NULL) {
        return NULL;
    }

    snprintf(buffer, 22, "%" PRIu64, number);
    return buffer;
}

#define LOG(ctx) log(ctx)
#define ERROR_AND_EXIT(ctx) error_and_exit((ctx), __FILE__, __LINE__)
#define WARN(ctx) warn((ctx), __FILE__, __LINE__)

#endif
