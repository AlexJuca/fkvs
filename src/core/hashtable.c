#include "hashtable.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

// DJB2 hash function
size_t hash_function(unsigned char *key, size_t key_len, size_t table_size)
{
    size_t hash = 5381;
    for (size_t i = 0; i < key_len; i++) {
        hash = ((hash << 5) + hash) + key[i];
    }
    return hash % table_size;
}

// Create a new hash table
hashtable_t *create_hash_table(size_t size)
{
    hashtable_t *table = malloc(sizeof(hashtable_t));
    table->buckets = calloc(size, sizeof(hash_table_entry_t *));
    table->size = size;
    return table;
}

void free_hash_table(hashtable_t *table)
{
    for (size_t i = 0; i < table->size; i++) {
        hash_table_entry_t *entry = table->buckets[i];
        while (entry) {
            hash_table_entry_t *next = entry->next;
            free(entry->key);
            free(entry->value);
            free(entry);
            entry = next;
        }
    }
    free(table->buckets);
    free(table);
}

bool set_value(hashtable_t *table, unsigned char *key, size_t key_len,
               void *value, size_t value_len, int value_type_encoding)
{
    const size_t index = hash_function(key, key_len, table->size);
    hash_table_entry_t *current = table->buckets[index];
    while (current != NULL && (current->key_len != key_len ||
                               memcmp(current->key, key, key_len) != 0)) {
        current = current->next;
    }
    if (current == NULL) {
        // New entry
        current = malloc(sizeof(hash_table_entry_t));
        current->key = malloc(key_len);
        memcpy(current->key, key, key_len);
        current->key_len = key_len;
        current->next = table->buckets[index];
        table->buckets[index] = current;
    } else {
        // Update existing entry
        free(current->value->ptr);
    }
    current->value = malloc(sizeof(value_entry_t));
    current->value->ptr = malloc(value_len);
    current->value->encoding = value_type_encoding;
    memcpy(current->value->ptr, value, value_len);
    current->value->value_len = value_len;
    return true;
}

bool get_value(hashtable_t *table, unsigned char *key, size_t key_len,
               value_entry_t **value, size_t *value_len)
{
    if (!table || !key || !value || !value_len)
        return false;

    const size_t index = hash_function(key, key_len, table->size);
    for (const hash_table_entry_t *current = table->buckets[index]; current;
         current = current->next) {
        if (current->key_len == key_len &&
            memcmp(current->key, key, key_len) == 0) {
            value_entry_t *out = malloc(sizeof(current->value));
            if (!out)
                return false; // allocation failed

            out->ptr = current->value->ptr;
            out->encoding = current->value->encoding;
            out->expirable = current->value->expirable;
            out->type = current->value->type;
            *value = out;
            *value_len = current->value->value_len;
            return true;
        }
    }

    // not found
    *value = NULL;
    *value_len = 0;
    return false;
}
