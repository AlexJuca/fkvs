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
    assert(get_value(table, key, strlen((const char *)key), &value,
                     &value_len));
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
    assert(!set_value(table, key, sizeof(key) - 1, NULL, 1,
                      VALUE_ENTRY_TYPE_RAW));
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

int main(void)
{
    test_zero_length_value_roundtrip_is_freeable();
    test_invalid_inputs_are_rejected();
    test_replace_delete_and_free_are_sanitizer_clean();
    return 0;
}
