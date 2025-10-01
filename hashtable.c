#include "hashtable.h"
#include <stdio.h>

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
HashTable *create_hash_table(size_t capacity)
{
    HashTable *table = malloc(sizeof(HashTable));
    table->entries = calloc(capacity, sizeof(HashTableEntry));
    table->metadata = calloc(capacity, sizeof(uint8_t));
    table->capacity = capacity;
    table->count = 0;
    return table;
}

void free_hash_table(HashTable *table)
{
    for (size_t i = 0; i < table->capacity; i++) {
        if (table->metadata[i] && table->entries[i].occupied) {
            free(table->entries[i].key);
            free(table->entries[i].value);
        }
    }
    free(table->entries);
    free(table->metadata);
    free(table);
}

bool set_value(HashTable *table, unsigned char *key, size_t key_len,
               unsigned char *value, size_t value_len)
{
    if (table->count >= table->capacity * 0.75) {
        // Resize and rehash
        // The function to resize and rehash would be implemented here
    }

    size_t index = hash_function(key, key_len, table->capacity);
    for (size_t i = 0; i < table->capacity; i++) {
        size_t pos = (index + i) % table->capacity;
        if (!table->metadata[pos] ||
            (table->entries[pos].key_len == key_len &&
             memcmp(table->entries[pos].key, key, key_len) == 0)) {
            if (!table->metadata[pos]) {
                table->entries[pos].key = malloc(key_len);
                memcpy(table->entries[pos].key, key, key_len);
                table->entries[pos].key_len = key_len;
                table->metadata[pos] = 1;
                table->count++;
            } else {
                free(table->entries[pos].value);
            }
            table->entries[pos].value = malloc(value_len);
            memcpy(table->entries[pos].value, value, value_len);
            table->entries[pos].value_len = value_len;
            table->entries[pos].occupied = true;
            return true;
        }
    }
    return false; // Table is at capacity and needs resizing
}

bool get_value(HashTable *table, unsigned char *key, size_t key_len,
               unsigned char **value, size_t *value_len)
{
    if (!table || !key || !value || !value_len)
        return false;

    size_t index = hash_function(key, key_len, table->capacity);
    for (size_t i = 0; i < table->capacity; i++) {
        size_t pos = (index + i) % table->capacity;
        if (table->metadata[pos] && table->entries[pos].key_len == key_len &&
            memcmp(table->entries[pos].key, key, key_len) == 0) {
            *value = malloc(table->entries[pos].value_len);
            if (!*value)
                return false; // allocation failed

            memcpy(*value, table->entries[pos].value, table->entries[pos].value_len);
            *value_len = table->entries[pos].value_len;
            return true;
        }
        if (!table->metadata[pos])
            break; // Stop if a slot is empty (no key here)
    }

    // not found
    *value = NULL;
    *value_len = 0;
    return false;
}
