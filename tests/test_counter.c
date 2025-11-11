#include "../src/counter.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

// Mock function for get_private_memory_usage_bytes
unsigned long get_private_memory_usage_bytes() {
    return 1234567;
}

void test_init_counter() {
    counter_t *counter = init_counter();
    assert(counter != NULL);
    assert(counter->memory_usage == 0);
    assert(counter->num_executed_commands == 0);
    printf("test_init_counter passed.\n");
    free(counter);
}

void test_update_memory_usage() {
    counter_t *counter = init_counter();
    
    update_memory_usage(counter);
    assert(counter->memory_usage == 1234567);
    printf("test_update_memory_usage passed.\n");
    free(counter);
}

void test_increment_command_count() {
    counter_t *counter = init_counter();

    increment_command_count(counter);
    assert(counter->num_executed_commands == 1);
    increment_command_count(counter);
    assert(counter->num_executed_commands == 2);
    printf("test_increment_command_count passed.\n");
    free(counter);
}

void test_update_disconnected_clients() {
    counter_t *counter = init_counter();

    update_disconnected_clients(counter, 5);
    assert(counter->disconnected_clients == 5);
    update_disconnected_clients(counter, 10);
    assert(counter->disconnected_clients == 10);
    printf("test_update_disconnected_clients passed.\n");
    free(counter);
}

int main() {
    test_init_counter();
    test_update_memory_usage();
    test_increment_command_count();
    test_update_disconnected_clients();
    return 0;
}
