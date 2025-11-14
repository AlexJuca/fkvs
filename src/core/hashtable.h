#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stdbool.h>
#include <stdlib.h>

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

typedef struct hashtable {
    hash_table_entry_t **buckets;
    size_t size;
} hashtable_t;

hashtable_t *create_hash_table(size_t size);
void free_hash_table(hashtable_t *table);
bool set_value(const hashtable_t *table, const unsigned char *key, size_t key_len,
               const void *value, size_t value_len, int value_type);
bool get_value(hashtable_t *table, unsigned char *key, size_t key_len,
               value_entry_t **value, size_t *value_len);
size_t hash_function(const unsigned char *key, size_t key_len, size_t table_size);

#endif // HASHTABLE_H
