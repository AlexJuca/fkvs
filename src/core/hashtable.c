#include "hashtable.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

// DJB2 hash. Returns the raw (unfolded) hash; callers fold it into a specific
// table with fold().
static size_t djb2(const unsigned char *key, const size_t key_len)
{
    size_t hash = 5381;
    for (size_t i = 0; i < key_len; i++) {
        hash = ((hash << 5) + hash) + key[i];
    }
    return hash;
}

// Fold a raw hash into a bucket index. `size` must be a power of two.
static inline size_t fold(const size_t hash, const size_t size)
{
    return hash & (size - 1);
}

// Smallest power of two >= n (and >= 1).
static size_t next_pow2(size_t n)
{
    if (n < 2)
        return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
#if SIZE_MAX > 0xffffffffu
    n |= n >> 32;
#endif
    return n + 1;
}

static inline bool is_rehashing(const hashtable_t *table)
{
    return table->rehash_index != -1;
}

// Retained for API/back-compat: behaves like the original (hash modulo size).
size_t hash_function(const unsigned char *key, const size_t key_len,
                     const size_t table_size)
{
    if (table_size == 0 || (!key && key_len > 0))
        return 0;
    return djb2(key, key_len) % table_size;
}

// Create a new hash table. The requested size is rounded up to a power of two
// so bucket indices can be computed with a mask instead of a division.
hashtable_t *create_hash_table(const size_t size)
{
    if (size == 0)
        return NULL;

    hashtable_t *table = calloc(1, sizeof(hashtable_t));
    if (!table)
        return NULL;

    const size_t cap = next_pow2(size);
    table->buckets[0] = calloc(cap, sizeof(hash_table_entry_t *));
    if (!table->buckets[0]) {
        free(table);
        return NULL;
    }
    table->size[0] = cap;
    table->used[0] = 0;
    table->buckets[1] = NULL;
    table->size[1] = 0;
    table->used[1] = 0;
    table->rehash_index = -1;
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

    for (int t = 0; t < 2; t++) {
        if (!table->buckets[t])
            continue;
        for (size_t i = 0; i < table->size[t]; i++) {
            hash_table_entry_t *entry = table->buckets[t][i];
            while (entry) {
                hash_table_entry_t *next = entry->next;
                free(entry->key);
                free_value_entry(entry->value);
                free(entry);
                entry = next;
            }
        }
        free(table->buckets[t]);
    }
    free(table);
}

// Move table 1 into the primary slot once table 0 has fully drained.
static void rehash_finalize(hashtable_t *table)
{
    free(table->buckets[0]);
    table->buckets[0] = table->buckets[1];
    table->size[0] = table->size[1];
    table->used[0] = table->used[1];
    table->buckets[1] = NULL;
    table->size[1] = 0;
    table->used[1] = 0;
    table->rehash_index = -1;
}

// Migrate at most one non-empty bucket from table 0 to table 1, bounding the
// number of empty buckets skipped so a single call stays O(1)-ish.
static void rehash_step(hashtable_t *table)
{
    if (!is_rehashing(table))
        return;

    int empty_visited = 0;
    while ((size_t)table->rehash_index < table->size[0] &&
           table->buckets[0][table->rehash_index] == NULL) {
        table->rehash_index++;
        if (++empty_visited >= 10)
            return; // resume from here on the next operation
    }

    if ((size_t)table->rehash_index >= table->size[0]) {
        rehash_finalize(table);
        return;
    }

    hash_table_entry_t *entry = table->buckets[0][table->rehash_index];
    while (entry) {
        hash_table_entry_t *next = entry->next;
        const size_t j = fold(djb2(entry->key, entry->key_len), table->size[1]);
        entry->next = table->buckets[1][j];
        table->buckets[1][j] = entry;
        table->used[0]--;
        table->used[1]++;
        entry = next;
    }
    table->buckets[0][table->rehash_index] = NULL;
    table->rehash_index++;

    if ((size_t)table->rehash_index >= table->size[0])
        rehash_finalize(table);
}

// Begin a resize when the primary table reaches load factor 1.0. Best-effort:
// if the new array cannot be allocated we simply stay single-table.
static void maybe_start_rehash(hashtable_t *table)
{
    if (is_rehashing(table))
        return;

    size_t new_size = table->size[0] ? table->size[0] : 1;
    while (new_size < table->used[0] * 2)
        new_size <<= 1;

    hash_table_entry_t **buckets =
        calloc(new_size, sizeof(hash_table_entry_t *));
    if (!buckets)
        return;

    table->buckets[1] = buckets;
    table->size[1] = new_size;
    table->used[1] = 0;
    table->rehash_index = 0;
}

// Find an entry by key, consulting both tables while a resize is in flight.
static hash_table_entry_t *find_entry(const hashtable_t *table,
                                      const unsigned char *key,
                                      const size_t key_len, const size_t hash)
{
    const int last = is_rehashing(table) ? 1 : 0;
    for (int t = 0; t <= last; t++) {
        if (table->size[t] == 0)
            continue;
        const size_t idx = fold(hash, table->size[t]);
        for (hash_table_entry_t *entry = table->buckets[t][idx]; entry;
             entry = entry->next) {
            if (entry->key_len == key_len &&
                memcmp(entry->key, key, key_len) == 0) {
                return entry;
            }
        }
    }
    return NULL;
}

bool set_value(hashtable_t *table, const unsigned char *key, size_t key_len,
               const void *value, size_t value_len, int value_type_encoding)
{
    if (!table || !table->buckets[0] || table->size[0] == 0 || !key ||
        (!value && value_len > 0))
        return false;

    // Advance any in-flight resize by one step on every insert.
    if (is_rehashing(table))
        rehash_step(table);

    const size_t hash = djb2(key, key_len);
    hash_table_entry_t *current = find_entry(table, key, key_len, hash);

    // Fast path: overwrite an existing value of the same length in place, with
    // no allocation or free. This is the common case for repeated SETs of the
    // same key (and matches calloc semantics by resetting type/expirable).
    if (current && current->value && current->value->value_len == value_len) {
        value_entry_t *v = current->value;
        if (value_len > 0)
            memcpy(v->ptr, value, value_len);
        v->encoding = value_type_encoding;
        v->type = 0;
        v->expirable = 0;
        return true;
    }
    return NULL;
}

bool set_value(hashtable_t *table, const unsigned char *key, size_t key_len,
               const void *value, size_t value_len, int value_type_encoding)
{
    if (!table || !table->buckets[0] || table->size[0] == 0 || !key ||
        (!value && value_len > 0))
        return false;

    // Advance any in-flight resize by one step on every insert.
    if (is_rehashing(table))
        rehash_step(table);

    // Build the new value entry up front so an OOM never corrupts the old one.
    value_entry_t *new_val = calloc(1, sizeof(value_entry_t));
    if (!new_val)
        return false;

    void *new_ptr = NULL;
    if (value_len > 0) {
        new_ptr = malloc(value_len);
        if (!new_ptr) {
            free(new_val);
            return false;
        }
        memcpy(new_ptr, value, value_len);
    }
    new_val->ptr = new_ptr;
    new_val->value_len = value_len;
    new_val->encoding = value_type_encoding;

    // Existing key: replace the value in place.
    if (current) {
        if (current->value)
            free_value_entry(current->value);
        current->value = new_val;
        return true;
    }
    memcpy(node->key, key, key_len);
    node->key_len = key_len;
    node->value = new_val;

    // Insert into the active insertion table: table 1 mid-resize, else table 0.
    const int t = is_rehashing(table) ? 1 : 0;
    const size_t idx = fold(hash, table->size[t]);
    node->next = table->buckets[t][idx];
    table->buckets[t][idx] = node;
    table->used[t]++;

    // Grow once the primary table hits load factor 1.0.
    if (!is_rehashing(table) && table->used[0] >= table->size[0])
        maybe_start_rehash(table);

    // New key: allocate the node and its key.
    hash_table_entry_t *node = malloc(sizeof(*node));
    if (!node) {
        free_value_entry(new_val);
        return false;
    }
    node->key = malloc(key_len == 0 ? 1 : key_len);
    if (!node->key) {
        free(node);
        free_value_entry(new_val);
        return false;
    }
    memcpy(node->key, key, key_len);
    node->key_len = key_len;
    node->value = new_val;

    // Insert into the active insertion table: table 1 mid-resize, else table 0.
    const int t = is_rehashing(table) ? 1 : 0;
    const size_t idx = fold(hash, table->size[t]);
    node->next = table->buckets[t][idx];
    table->buckets[t][idx] = node;
    table->used[t]++;

    // Grow once the primary table hits load factor 1.0.
    if (!is_rehashing(table) && table->used[0] >= table->size[0])
        maybe_start_rehash(table);

    return true;
}

bool delete_value(hashtable_t *table, const unsigned char *key, size_t key_len)
{
    if (!table || !table->buckets[0] || table->size[0] == 0 || !key)
        return false;

    const size_t hash = djb2(key, key_len);
    const int last = is_rehashing(table) ? 1 : 0;
    for (int t = 0; t <= last; t++) {
        if (table->size[t] == 0)
            continue;
        const size_t idx = fold(hash, table->size[t]);
        hash_table_entry_t *current = table->buckets[t][idx];
        hash_table_entry_t *prev = NULL;

        while (current != NULL) {
            if (current->key_len == key_len &&
                memcmp(current->key, key, key_len) == 0) {
                if (prev) {
                    prev->next = current->next;
                } else {
                    table->buckets[t][idx] = current->next;
                }
                free(current->key);
                free_value_entry(current->value);
                free(current);
                table->used[t]--;
                return true;
            }
            prev = current;
            current = current->next;
        }
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

    if (!table || !table->buckets[0] || table->size[0] == 0 || !key || !value ||
        !value_len)
        return false;

    const size_t hash = djb2(key, key_len);
    hash_table_entry_t *current = find_entry(table, key, key_len, hash);
    if (!current || !current->value)
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
        memcpy(out->ptr, current->value->ptr, current->value->value_len);
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
