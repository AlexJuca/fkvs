#ifndef UTILS_H
#define UTILS_H

#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

extern server_t server;

static inline void error_and_exit(char *ctx, const char *file, int line) {
  fprintf(stderr, "[%s - %d]\n", file, line); 
  perror(ctx);
  exit(EXIT_FAILURE);
}

static inline void warn(char *ctx, const char *file, int line) {
  fprintf(stderr, "[%s - %d]\n", file, line);
  perror(ctx);
}

//TODO: When process is running as a daemon, ensure we don't print to standard file descriptors
//rather we should write to a log file.
static inline void append_to_log_file(char *ctx) {
    
}

static inline void log_std_out(char *ctx) {
  time_t ct = time(NULL);
  char ts[32];

  strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&ct));

  fprintf(stdout, "%s - %s \n", ts, ctx);
}

static inline void log(char *ctx) {
  server.daemonize ? append_to_log_file(ctx) : log_std_out(ctx);
}

void inline print_binary_data(unsigned char* data, size_t len) {
    for (size_t j = 0; j < len; j++) {
        unsigned char c = data[j];
        if (c >= 32 && c <= 126) {
            putchar(c); // printable ASCII
        } else {
            printf("\\x%02x", c); // escape non-printable
        }
    }
    putchar('\n');
}

#define LOG(ctx) log(ctx)
#define ERROR_AND_EXIT(ctx) error_and_exit((ctx), __FILE__, __LINE__)
#define WARN(ctx) warn((ctx), __FILE__, __LINE__)

#endif
