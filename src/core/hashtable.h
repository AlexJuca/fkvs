#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

#define VALUE_ENTRY_TYPE_INT 1
#define VALUE_ENTRY_TYPE_RAW 2

/*
 * A value entry owns its bytes inline: `ptr` points just past this header into
 * the same allocation, so a value costs one malloc and free_value_entry() is a
 * single free. Do not free `ptr` separately.
 */
typedef struct value_entry_t {
    void *ptr;
    unsigned type : 4;
    unsigned encoding : 4;
    unsigned expirable : 1;
    size_t value_len;
} value_entry_t;

/*
 * An entry owns its key inline: `key` points just past this header into the same
 * allocation, so a new key costs one malloc (node+key) and the key never needs a
 * separate free. The key is immutable for the entry's lifetime, keeping the node
 * address stable across resizes.
 */
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
/*
 * Borrowing lookup: returns a pointer to the live stored value (no allocation,
 * no copy), or NULL if absent. The pointer is owned by the table; do NOT free
 * it, and treat it as invalidated by any later set_value/delete_value/expiry on
 * the same key. Prefer this over get_value() for read-only access.
 */
const value_entry_t *lookup_value(hashtable_t *table, const unsigned char *key,
                                  size_t key_len);
bool delete_value(hashtable_t *table, const unsigned char *key, size_t key_len);
size_t hash_function(const unsigned char *key, size_t key_len,
                     size_t table_size);

#endif // HASHTABLE_H
