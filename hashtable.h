#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct HashTableEntry {
    unsigned char *key;
    size_t key_len;
    unsigned char *value;
    size_t value_len;
    bool occupied;  // Indicate if this slot is occupied
} HashTableEntry;

typedef struct HashTable {
    HashTableEntry *entries;  // Array of entries
    uint8_t *metadata;       // Metadata array for occupancy
    size_t capacity;         // Current capacity of the table
    size_t count;            // Number of elements in the table
} HashTable;

HashTable *create_hash_table(size_t size);
void free_hash_table(HashTable *table);
bool set_value(HashTable *table, unsigned char *key, size_t key_len,
               unsigned char *value, size_t value_len);
bool get_value(HashTable *table, unsigned char *key, size_t key_len,
               unsigned char **value, size_t *value_len);
size_t hash_function(const unsigned char *key, size_t key_len, size_t table_size);

#endif // HASHTABLE_H
