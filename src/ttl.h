#ifndef TTL_H
#define TTL_H

#include "core/hashtable.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool set_expiry(hashtable_t *expires, const unsigned char *key,
                size_t key_len, int64_t deadline_ms);

bool remove_expiry(hashtable_t *expires, const unsigned char *key,
                   size_t key_len);

bool is_expired(hashtable_t *expires, const unsigned char *key,
                size_t key_len);

int64_t get_ttl(hashtable_t *expires, const unsigned char *key,
                size_t key_len);

size_t expire_sweep(hashtable_t *store, hashtable_t *expires,
                    size_t sample_count);

#endif // TTL_H
