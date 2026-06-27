#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

#define VALUE_ENTRY_TYPE_INT 1
#define VALUE_ENTRY_TYPE_RAW 2

typedef struct value_entry_t {
    void *ptr;
    unsigned type : 4;
    unsigned encoding : 4;
    unsigned expirable : 1;
    size_t value_len;
} value_entry_t;

typedef struct hashtable_entry_t {
    unsigned char *key;
    size_t key_len;
    value_entry_t *value;
    struct hashtable_entry_t *next;
} hash_table_entry_t;

/*
 * Separate-chaining hash table with incremental (Redis dict-style) resizing.
 *
 * Two sub-tables are kept so a resize can be spread across many operations
 * instead of stalling one of them:
 *   - index 0 is the primary table;
 *   - index 1 is the larger table that only exists while a resize is in flight.
 *
 * When `rehash_index` is >= 0 a resize is active and it marks the next bucket
 * of table 0 still to be migrated into table 1. New keys are inserted into
 * table 1 during a resize, so table 0 only ever drains. Lookups and deletes
 * consult both tables; a key lives in at most one of them at any instant.
 * Migration only relinks existing nodes between bucket arrays — it never copies
 * keys/values or frees nodes — so entry addresses stay stable across a resize.
 */
typedef struct hashtable {
    hash_table_entry_t **buckets[2];
    size_t size[2]; // bucket counts (always powers of two)
    size_t used[2]; // live entry counts, for load-factor tracking
    ssize_t
        rehash_index; // -1 when not resizing; else next table-0 bucket to move
} hashtable_t;

hashtable_t *create_hash_table(size_t size);
void free_hash_table(hashtable_t *table);
void free_value_entry(value_entry_t *value);
bool set_value(hashtable_t *table, const unsigned char *key, size_t key_len,
               const void *value, size_t value_len, int value_type);
bool get_value(hashtable_t *table, const unsigned char *key, size_t key_len,
               value_entry_t **value, size_t *value_len);
bool delete_value(hashtable_t *table, const unsigned char *key, size_t key_len);
size_t hash_function(const unsigned char *key, size_t key_len,
                     size_t table_size);

#endif // HASHTABLE_H
