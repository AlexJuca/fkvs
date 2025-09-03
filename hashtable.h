#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Structure for a HashTable entry
typedef struct HashTableEntry {
    unsigned char* key;
    size_t key_len;
    unsigned char* value;
    size_t value_len;
    struct HashTableEntry* next;
} HashTableEntry;

// Structure for the HashTable
typedef struct HashTable {
    HashTableEntry** buckets;
    size_t size;
} HashTable;

// Function prototypes
HashTable* create_hash_table(size_t size);
void free_hash_table(HashTable* table);
bool set_value(HashTable* table, unsigned char* key, size_t key_len, unsigned char* value, size_t value_len);
bool get_value(HashTable* table, unsigned char* key, size_t key_len, unsigned char** value, size_t* value_len);
size_t hash_function(unsigned char* key, size_t key_len, size_t table_size);

#endif // HASHTABLE_H
