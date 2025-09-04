#include "hashtable.h"
#include <stdio.h>

// DJB2 hash function
size_t hash_function(unsigned char* key, size_t key_len, size_t table_size) {
    size_t hash = 5381;
    for (size_t i = 0; i < key_len; i++) {
        hash = ((hash << 5) + hash) + key[i];
    }
    return hash % table_size;
}

// Create a new hash table
HashTable* create_hash_table(size_t size) {
    HashTable* table = malloc(sizeof(HashTable));
    table->buckets = calloc(size, sizeof(HashTableEntry*));
    table->size = size;
    return table;
}

void free_hash_table(HashTable* table) {
    for (size_t i = 0; i < table->size; i++) {
        HashTableEntry* entry = table->buckets[i];
        while (entry) {
            HashTableEntry* next = entry->next;
            free(entry->key);
            free(entry->value);
            free(entry);
            entry = next;
        }
    }
    free(table->buckets);
    free(table);
}

bool set_value(HashTable* table, unsigned char* key, size_t key_len, unsigned char* value, size_t value_len) {
    size_t index = hash_function(key, key_len, table->size);
    HashTableEntry* current = table->buckets[index];
    while (current != NULL && (current->key_len != key_len || memcmp(current->key, key, key_len) != 0)) {
        current = current->next;
    }
    if (current == NULL) {
        // New entry
        current = malloc(sizeof(HashTableEntry));
        current->key = malloc(key_len);
        memcpy(current->key, key, key_len);
        current->key_len = key_len;
        current->next = table->buckets[index];
        table->buckets[index] = current;
    } else {
        // Update existing entry
        free(current->value);
    }
    current->value = malloc(value_len);
    memcpy(current->value, value, value_len);
    current->value_len = value_len;
    return true;
}

bool get_value(HashTable* table,unsigned char* key, size_t key_len, unsigned char** value, size_t* value_len) {
    if (!table || !key || !value || !value_len) return false;

    size_t index = hash_function(key, key_len, table->size);
    for (HashTableEntry* current = table->buckets[index]; current; current = current->next) {
        if (current->key_len == key_len && memcmp(current->key, key, key_len) == 0) {
            unsigned char* out = malloc(current->value_len);
            if (!out) return false; // allocation failed

            memcpy(out, current->value, current->value_len);
            *value = out;
            *value_len = current->value_len;
            return true;
        }
    }

    // not found
    *value = NULL;
    *value_len = 0;
    return false;
}

