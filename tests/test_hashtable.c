#include "../src/core/hashtable.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_zero_length_value_roundtrip_is_freeable(void)
{
    hashtable_t *table = create_hash_table(8);
    assert(table != NULL);

    const unsigned char key[] = "empty";
    assert(set_value(table, key, strlen((const char *)key), NULL, 0,
                     VALUE_ENTRY_TYPE_RAW));

    value_entry_t *value = NULL;
    size_t value_len = 99;
    assert(
        get_value(table, key, strlen((const char *)key), &value, &value_len));
    assert(value != NULL);
    assert(value_len == 0);
    assert(value->value_len == 0);
    assert(value->ptr != NULL);
    assert(((unsigned char *)value->ptr)[0] == '\0');

    free_value_entry(value);
    free_hash_table(table);

    printf("test_zero_length_value_roundtrip_is_freeable passed.\n");
}

static void test_invalid_inputs_are_rejected(void)
{
    hashtable_t *table = create_hash_table(4);
    assert(table != NULL);

    const unsigned char key[] = "key";
    const unsigned char value[] = "value";
    value_entry_t *out = NULL;
    size_t out_len = 0;

    assert(create_hash_table(0) == NULL);
    assert(!set_value(NULL, key, sizeof(key) - 1, value, sizeof(value) - 1,
                      VALUE_ENTRY_TYPE_RAW));
    assert(!set_value(table, NULL, sizeof(key) - 1, value, sizeof(value) - 1,
                      VALUE_ENTRY_TYPE_RAW));
    assert(
        !set_value(table, key, sizeof(key) - 1, NULL, 1, VALUE_ENTRY_TYPE_RAW));
    assert(!get_value(NULL, key, sizeof(key) - 1, &out, &out_len));
    assert(!get_value(table, NULL, sizeof(key) - 1, &out, &out_len));
    assert(!get_value(table, key, sizeof(key) - 1, NULL, &out_len));
    assert(!get_value(table, key, sizeof(key) - 1, &out, NULL));
    assert(!delete_value(NULL, key, sizeof(key) - 1));
    assert(!delete_value(table, NULL, sizeof(key) - 1));

    free_hash_table(table);

    printf("test_invalid_inputs_are_rejected passed.\n");
}

static void test_replace_delete_and_free_are_sanitizer_clean(void)
{
    hashtable_t *table = create_hash_table(2);
    assert(table != NULL);

    const unsigned char key[] = "name";
    const unsigned char first[] = "alice";
    const unsigned char second[] = "alexandre";

    assert(set_value(table, key, sizeof(key) - 1, first, sizeof(first) - 1,
                     VALUE_ENTRY_TYPE_RAW));
    assert(set_value(table, key, sizeof(key) - 1, second, sizeof(second) - 1,
                     VALUE_ENTRY_TYPE_RAW));

    value_entry_t *value = NULL;
    size_t value_len = 0;
    assert(get_value(table, key, sizeof(key) - 1, &value, &value_len));
    assert(value_len == sizeof(second) - 1);
    assert(memcmp(value->ptr, second, value_len) == 0);
    free_value_entry(value);

    assert(delete_value(table, key, sizeof(key) - 1));
    assert(!get_value(table, key, sizeof(key) - 1, &value, &value_len));

    assert(set_value(table, key, sizeof(key) - 1, first, sizeof(first) - 1,
                     VALUE_ENTRY_TYPE_RAW));
    free_hash_table(table);

    printf("test_replace_delete_and_free_are_sanitizer_clean passed.\n");
}

// Insert far more keys than the initial bucket count to force many incremental
// resizes, then exercise reads (which occur while a resize is mid-flight),
// in-place updates, and deletes — verifying no entry is lost or duplicated
// across the rehash.
static void test_incremental_resize_preserves_all_entries(void)
{
    hashtable_t *table = create_hash_table(4); // tiny start -> many resizes
    assert(table != NULL);

    const int n = 50000;
    char key[32];
    char val[32];

    for (int i = 0; i < n; i++) {
        const int kl = snprintf(key, sizeof(key), "key:%d", i);
        const int vl = snprintf(val, sizeof(val), "val:%d", i);
        assert(set_value(table, (const unsigned char *)key, (size_t)kl, val,
                         (size_t)vl, VALUE_ENTRY_TYPE_RAW));
    }

    // Read every key back. Reads do not advance the resize, so if one is in
    // flight these lookups exercise the dual-table find path throughout.
    for (int i = 0; i < n; i++) {
        const int kl = snprintf(key, sizeof(key), "key:%d", i);
        const int vl = snprintf(val, sizeof(val), "val:%d", i);
        value_entry_t *out = NULL;
        size_t out_len = 0;
        assert(get_value(table, (const unsigned char *)key, (size_t)kl, &out,
                         &out_len));
        assert(out_len == (size_t)vl);
        assert(memcmp(out->ptr, val, (size_t)vl) == 0);
        free_value_entry(out);
    }

    // Update even keys in place, delete odd keys.
    for (int i = 0; i < n; i += 2) {
        const int kl = snprintf(key, sizeof(key), "key:%d", i);
        const int vl = snprintf(val, sizeof(val), "UPD:%d", i);
        assert(set_value(table, (const unsigned char *)key, (size_t)kl, val,
                         (size_t)vl, VALUE_ENTRY_TYPE_RAW));
    }
    for (int i = 1; i < n; i += 2) {
        const int kl = snprintf(key, sizeof(key), "key:%d", i);
        assert(delete_value(table, (const unsigned char *)key, (size_t)kl));
    }

    // Final state: even keys present with updated values, odd keys gone.
    for (int i = 0; i < n; i++) {
        const int kl = snprintf(key, sizeof(key), "key:%d", i);
        value_entry_t *out = NULL;
        size_t out_len = 0;
        const bool found = get_value(table, (const unsigned char *)key,
                                     (size_t)kl, &out, &out_len);
        if (i % 2 == 0) {
            const int vl = snprintf(val, sizeof(val), "UPD:%d", i);
            assert(found);
            assert(out_len == (size_t)vl);
            assert(memcmp(out->ptr, val, (size_t)vl) == 0);
            free_value_entry(out);
        } else {
            assert(!found);
        }
    }

    free_hash_table(table);

    printf("test_incremental_resize_preserves_all_entries passed.\n");
}

int main(void)
{
    test_zero_length_value_roundtrip_is_freeable();
    test_invalid_inputs_are_rejected();
    test_replace_delete_and_free_are_sanitizer_clean();
    test_incremental_resize_preserves_all_entries();
    return 0;
}
