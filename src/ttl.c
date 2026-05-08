#include "ttl.h"
#include "utils.h"
#include <string.h>

bool set_expiry(hashtable_t *expires, const unsigned char *key,
                size_t key_len, int64_t deadline_ms)
{
    unsigned char buf[8];
    buf[0] = (deadline_ms >> 56) & 0xFF;
    buf[1] = (deadline_ms >> 48) & 0xFF;
    buf[2] = (deadline_ms >> 40) & 0xFF;
    buf[3] = (deadline_ms >> 32) & 0xFF;
    buf[4] = (deadline_ms >> 24) & 0xFF;
    buf[5] = (deadline_ms >> 16) & 0xFF;
    buf[6] = (deadline_ms >> 8) & 0xFF;
    buf[7] = deadline_ms & 0xFF;

    return set_value(expires, key, key_len, buf, sizeof(buf),
                     VALUE_ENTRY_TYPE_INT);
}

static bool get_deadline(hashtable_t *expires, const unsigned char *key,
                         size_t key_len, int64_t *deadline_out)
{
    value_entry_t *val = NULL;
    size_t val_len = 0;

    if (!get_value(expires, key, key_len, &val, &val_len))
        return false;

    if (val_len != 8) {
        free_value_entry(val);
        return false;
    }

    const unsigned char *b = val->ptr;
    *deadline_out = ((int64_t)b[0] << 56) | ((int64_t)b[1] << 48) |
                    ((int64_t)b[2] << 40) | ((int64_t)b[3] << 32) |
                    ((int64_t)b[4] << 24) | ((int64_t)b[5] << 16) |
                    ((int64_t)b[6] << 8) | (int64_t)b[7];

    free_value_entry(val);
    return true;
}

bool remove_expiry(hashtable_t *expires, const unsigned char *key,
                   size_t key_len)
{
    return delete_value(expires, key, key_len);
}

bool is_expired(hashtable_t *expires, const unsigned char *key,
                size_t key_len)
{
    int64_t deadline;
    if (!get_deadline(expires, key, key_len, &deadline))
        return false;

    return deadline <= fkvs_now_ms();
}

int64_t get_ttl(hashtable_t *expires, const unsigned char *key,
                size_t key_len)
{
    int64_t deadline;
    if (!get_deadline(expires, key, key_len, &deadline))
        return -2;

    int64_t remaining_ms = deadline - fkvs_now_ms();
    if (remaining_ms <= 0)
        return 0;

    return remaining_ms / 1000;
}

size_t expire_sweep(hashtable_t *store, hashtable_t *expires,
                    size_t sample_count)
{
    static size_t cursor = 0;
    size_t deleted = 0;
    const int64_t now = fkvs_now_ms();

    for (size_t i = 0; i < sample_count; i++) {
        size_t idx = (cursor + i) % expires->size;
        hash_table_entry_t *entry = expires->buckets[idx];

        while (entry) {
            hash_table_entry_t *next = entry->next;

            if (entry->value && entry->value->value_len == 8) {
                const unsigned char *b = entry->value->ptr;
                int64_t deadline =
                    ((int64_t)b[0] << 56) | ((int64_t)b[1] << 48) |
                    ((int64_t)b[2] << 40) | ((int64_t)b[3] << 32) |
                    ((int64_t)b[4] << 24) | ((int64_t)b[5] << 16) |
                    ((int64_t)b[6] << 8) | (int64_t)b[7];

                if (deadline <= now) {
                    delete_value(store, entry->key, entry->key_len);
                    delete_value(expires, entry->key, entry->key_len);
                    deleted++;
                }
            }

            entry = next;
        }
    }

    cursor = (cursor + sample_count) % expires->size;
    return deleted;
}
