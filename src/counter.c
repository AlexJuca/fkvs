#include "counter.h"

#include "main.h"
#include "memory.h"

#include <limits.h>
#include <stdlib.h>
#include <time.h>

counter_t *init_counter()
{
    counter_t *counter = malloc(sizeof(counter_t));
    if (!counter)
        return NULL;
    counter->memory_usage = 0;
    counter->num_executed_commands = 0;
    counter->start_time = time(NULL);
    return counter;
}

unsigned long get_memory_usage()
{
    return get_private_memory_usage_bytes();
}

void update_memory_usage(counter_t *counter)
{
    counter->memory_usage = get_memory_usage();
}

void increment_command_count(counter_t *counter)
{
    counter->num_executed_commands++;
}

void update_disconnected_clients(counter_t *counter,
                                 const int disconnected_clients)
{
    counter->disconnected_clients = disconnected_clients;
}