#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../hashtable.h"

void test_set_and_get() {
    HashTable *table = create_hash_table(10);
    unsigned char key[] = "testkey";
    unsigned char value[] = "testvalue";

    bool set_result = set_value(table, key, strlen((char *)key), value, strlen((char *)value));
    assert(set_result);

    unsigned char *retrieved_value;
    size_t retrieved_value_len;
    bool get_result = get_value(table, key, strlen((char *)key), &retrieved_value, &retrieved_value_len);
    assert(get_result);
    assert(memcmp(retrieved_value, value, retrieved_value_len) == 0);

    free(retrieved_value);
    free_hash_table(table);
}

int main() {
    test_set_and_get();
    printf("All tests passed.\n");
    return 0;
}