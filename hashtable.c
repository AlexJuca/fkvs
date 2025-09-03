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

// Free the hash table
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

// Set a key-value pair in the hash table
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

// Retrieve a value from the hash table
bool get_value(HashTable* table, unsigned char* key, size_t key_len, unsigned char** value, size_t* value_len) {
    size_t index = hash_function(key, key_len, table->size);
    HashTableEntry* current = table->buckets[index];
    while (current != NULL) {
        if (current->key_len == key_len && memcmp(current->key, key, key_len) == 0) {
            *value = malloc(current->value_len);
            memcpy(*value, current->value, current->value_len);
            *value_len = current->value_len;
            return true;
        }
        current = current->next;
    }
    return false;
}
