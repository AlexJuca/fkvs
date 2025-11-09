#ifndef COUNTER_H
#define COUNTER_H

#include <time.h>

typedef struct counter_t {
    unsigned long memory_usage;
    unsigned long num_executed_commands;
    long disconnected_clients;
    time_t start_time;
} counter_t;

counter_t *init_counter();
void update_memory_usage(counter_t *counter);
unsigned long get_memory_usage();
void increment_command_count(counter_t *counter);
void update_disconnected_clients(counter_t *counter, int disconnected_clients);

#endif // COUNTER_H
