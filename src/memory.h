#ifndef MEMORY_H
#define MEMORY_H

#include <stdio.h>

size_t get_memory_usage_bytes();

size_t get_private_memory_usage_bytes();

/* Returns the name (and version, when available) of the active allocator.
 * Reports the jemalloc version when the server is linked against jemalloc,
 * otherwise the system libc allocator. */
const char *get_allocator_name(void);

#endif
