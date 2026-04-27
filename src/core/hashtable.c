#include "hashtable.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

// DJB2 hash function
size_t hash_function(const unsigned char *key, const size_t key_len,
                     const size_t table_size)
{
    if (table_size == 0 || (!key && key_len > 0))
        return 0;

    size_t hash = 5381;
    for (size_t i = 0; i < key_len; i++) {
        hash = ((hash << 5) + hash) + key[i];
    }
    return hash % table_size;
}

// Create a new hash table
hashtable_t *create_hash_table(const size_t size)
{
    if (size == 0)
        return NULL;

    hashtable_t *table = malloc(sizeof(hashtable_t));
    if (!table)
        return NULL;
    table->buckets = calloc(size, sizeof(hash_table_entry_t *));
    if (!table->buckets) {
        free(table);
        return NULL;
    }
    table->size = size;
    return table;
}

void free_value_entry(value_entry_t *value)
{
    if (!value)
        return;

    free(value->ptr);
    free(value);
}

void free_hash_table(hashtable_t *table)
{
    if (!table)
        return;

    if (!table->buckets) {
        free(table);
        return;
    }

    for (size_t i = 0; i < table->size; i++) {
        hash_table_entry_t *entry = table->buckets[i];
        while (entry) {
            hash_table_entry_t *next = entry->next;
            free(entry->key);
            free_value_entry(entry->value);
            free(entry);
            entry = next;
        }
    }
    free(table->buckets);
    free(table);
}

bool set_value(const hashtable_t *table, const unsigned char *key,
               size_t key_len, const void *value, size_t value_len,
               int value_type_encoding)
{
    if (!table || !table->buckets || table->size == 0 || !key ||
        (!value && value_len > 0))
        return false;

    const size_t index = hash_function(key, key_len, table->size);
    hash_table_entry_t *current = table->buckets[index];

    // Search for existing key
    while (current != NULL && (current->key_len != key_len ||
                               memcmp(current->key, key, key_len) != 0)) {
        current = current->next;
    }

    const bool is_new_entry = (current == NULL);
    if (is_new_entry) {
        // Create entry
        current = malloc(sizeof(*current));
        if (!current)
            return false;

        current->key = malloc(key_len == 0 ? 1 : key_len);
        if (!current->key) {
            free(current);
            return false;
        }

        memcpy(current->key, key, key_len);
        current->key_len = key_len;
        current->value = NULL;

        // Insert into bucket list
        current->next = table->buckets[index];
        table->buckets[index] = current;
    }

    // Prepare new value entry before touching old one
    value_entry_t *new_val = calloc(1, sizeof(value_entry_t));
    if (!new_val) {
        if (is_new_entry) {
            // Undo insertion
            table->buckets[index] = current->next;
            free(current->key);
            free(current);
        }
        return false;
    }

    void *new_ptr = NULL;

    if (value_len > 0) {
        new_ptr = malloc(value_len);
        if (!new_ptr) {
            free(new_val);
            if (is_new_entry) {
                table->buckets[index] = current->next;
                free(current->key);
                free(current);
            }
            return false;
        }
        memcpy(new_ptr, value, value_len);
    }

    // Fill new value
    new_val->ptr = new_ptr;
    new_val->value_len = value_len;
    new_val->encoding = value_type_encoding;

    // We are now safe to free old value
    if (!is_new_entry && current->value) {
        free_value_entry(current->value);
    }

    current->value = new_val;
    return true;
}

bool delete_value(hashtable_t *table, const unsigned char *key, size_t key_len)
{
    if (!table || !table->buckets || table->size == 0 || !key)
        return false;

    const size_t index = hash_function(key, key_len, table->size);
    hash_table_entry_t *current = table->buckets[index];
    hash_table_entry_t *prev = NULL;

    while (current != NULL) {
        if (current->key_len == key_len &&
            memcmp(current->key, key, key_len) == 0) {
            // Unlink from chain
            if (prev) {
                prev->next = current->next;
            } else {
                table->buckets[index] = current->next;
            }
            free(current->key);
            free_value_entry(current->value);
            free(current);
            return true;
        }
        prev = current;
        current = current->next;
    }

    return false;
}

bool get_value(hashtable_t *table, const unsigned char *key, size_t key_len,
               value_entry_t **value, size_t *value_len)
{
    if (value)
        *value = NULL;
    if (value_len)
        *value_len = 0;

    if (!table || !table->buckets || table->size == 0 || !key || !value ||
        !value_len)
        return false;

    const size_t index = hash_function(key, key_len, table->size);

    for (hash_table_entry_t *current = table->buckets[index]; current;
         current = current->next) {
        if (current->key_len == key_len &&
            memcmp(current->key, key, key_len) == 0) {
            if (!current->value)
                return false;

            // allocate full value_entry_t
            value_entry_t *out = malloc(sizeof(value_entry_t));
            if (!out)
                return false;

            // Deep copy value bytes. The trailing NUL is only a defensive pad;
            // value_len remains authoritative because values may be binary.
            out->ptr = malloc(current->value->value_len + 1);
            if (!out->ptr) {
                free(out);
                return false;
            }

            if (current->value->value_len > 0) {
                if (!current->value->ptr) {
                    free_value_entry(out);
                    return false;
                }
                memcpy(out->ptr, current->value->ptr,
                       current->value->value_len);
            }
            ((unsigned char *)out->ptr)[current->value->value_len] = '\0';

            // copy metadata
            out->value_len = current->value->value_len;
            out->encoding = current->value->encoding;
            out->expirable = current->value->expirable;
            out->type = current->value->type;

            *value = out;
            *value_len = out->value_len;

            return true;
        }
    }

    *value = NULL;
    *value_len = 0;
    return false;
}
