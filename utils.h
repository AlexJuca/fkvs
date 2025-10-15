#ifndef UTILS_H
#define UTILS_H

#include "client.h"
#include "main.h"
#include "server.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static void print_binary_data(const unsigned char *data, const size_t len)
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

#ifdef SERVER

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

static void _log(char *ctx)
{
    server.daemonize ? append_to_log_file(ctx) : log_std_out(ctx);
}

static bool is_integer(const unsigned char *str, const size_t len)
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

static char *int_to_string(const uint64_t number)
{
    char *buffer = malloc(22 * sizeof(char));
    if (buffer == NULL) {
        return NULL;
    }

    snprintf(buffer, 22, "%" PRIu64, number);
    return buffer;
}

// Adds two decimal integer strings (can include leading '+' or '-')
// Returns a newly allocated string with the exact result.
static char *add_strings(const char *a, const char *b)
{
    // TODO: Handle signs: currently we'll only support non-negative for
    // simplicity.
    if (a[0] == '-' || b[0] == '-') {
        fprintf(stderr, "Negative values not supported yet.\n");
        return NULL;
    }

    const size_t len_a = strlen(a);
    const size_t len_b = strlen(b);
    const size_t max_len =
        (len_a > len_b ? len_a : len_b) + 1; // +1 for possible carry

    char *result = malloc(max_len + 1);
    if (!result)
        return NULL;
    result[max_len] = '\0';

    int carry = 0;
    ssize_t i = len_a - 1;
    ssize_t j = len_b - 1;
    ssize_t k = max_len - 1;

    while (i >= 0 || j >= 0 || carry > 0) {
        const int digit_a = (i >= 0) ? a[i--] - '0' : 0;
        const int digit_b = (j >= 0) ? b[j--] - '0' : 0;
        const int sum = digit_a + digit_b + carry;

        carry = sum / 10;
        result[k--] = (sum % 10) + '0';
    }

    // Shift result left if leading zeros
    const char *final = result + k + 1;
    const size_t final_len = strlen(final);
    char *cleaned = malloc(final_len + 1);
    if (!cleaned) {
        free(result);
        return NULL;
    }
    memcpy(cleaned, final, final_len + 1);

    free(result);
    return cleaned;
}

#endif

#define LOG_INFO(ctx) _log(ctx)
#define ERROR_AND_EXIT(ctx) error_and_exit((ctx), __FILE__, __LINE__)
#define WARN(ctx) warn((ctx), __FILE__, __LINE__)

#endif
