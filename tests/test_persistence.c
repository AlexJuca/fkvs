#include "../src/core/hashtable.h"
#include "../src/fkvs_time.h"
#include "../src/persistence/persistence.h"
#include "../src/server.h"
#include "../src/ttl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

server_t server;

unsigned long get_private_memory_usage_bytes(void)
{
    return 0;
}

static const char *TEST_RDB = "/tmp/test_fkvs_persistence.rdb";

static db_t *create_test_db(void)
{
    db_t *db = malloc(sizeof(db_t));
    db->store = create_hash_table(64);
    db->expires = create_hash_table(64);
    return db;
}

static void free_test_db(db_t *db)
{
    free_hash_table(db->store);
    free_hash_table(db->expires);
    free(db);
}

void test_round_trip(void)
{
    db_t *db = create_test_db();

    set_value(db->store, (const unsigned char *)"key1", 4, "value1", 6,
              VALUE_ENTRY_TYPE_RAW);
    set_value(db->store, (const unsigned char *)"key2", 4, "value2", 6,
              VALUE_ENTRY_TYPE_RAW);
    set_value(db->store, (const unsigned char *)"key3", 4, "value3", 6,
              VALUE_ENTRY_TYPE_RAW);

    assert(save_snapshot(db, TEST_RDB));

    db_t *db2 = create_test_db();
    assert(load_snapshot(TEST_RDB, db2));

    value_entry_t *val = NULL;
    size_t val_len = 0;

    assert(get_value(db2->store, (unsigned char *)"key1", 4, &val, &val_len));
    assert(val_len == 6);
    assert(memcmp(val->ptr, "value1", 6) == 0);
    free(val->ptr);
    free(val);

    assert(get_value(db2->store, (unsigned char *)"key2", 4, &val, &val_len));
    assert(val_len == 6);
    assert(memcmp(val->ptr, "value2", 6) == 0);
    free(val->ptr);
    free(val);

    assert(get_value(db2->store, (unsigned char *)"key3", 4, &val, &val_len));
    assert(val_len == 6);
    assert(memcmp(val->ptr, "value3", 6) == 0);
    free(val->ptr);
    free(val);

    free_test_db(db);
    free_test_db(db2);
    unlink(TEST_RDB);
    printf("test_round_trip passed.\n");
}

void test_encoding_preserved(void)
{
    db_t *db = create_test_db();

    set_value(db->store, (const unsigned char *)"str", 3, "hello", 5,
              VALUE_ENTRY_TYPE_RAW);
    set_value(db->store, (const unsigned char *)"num", 3, "42", 2,
              VALUE_ENTRY_TYPE_INT);

    assert(save_snapshot(db, TEST_RDB));

    db_t *db2 = create_test_db();
    assert(load_snapshot(TEST_RDB, db2));

    value_entry_t *val = NULL;
    size_t val_len = 0;

    assert(get_value(db2->store, (unsigned char *)"str", 3, &val, &val_len));
    assert(val->encoding == VALUE_ENTRY_TYPE_RAW);
    free(val->ptr);
    free(val);

    assert(get_value(db2->store, (unsigned char *)"num", 3, &val, &val_len));
    assert(val->encoding == VALUE_ENTRY_TYPE_INT);
    free(val->ptr);
    free(val);

    free_test_db(db);
    free_test_db(db2);
    unlink(TEST_RDB);
    printf("test_encoding_preserved passed.\n");
}

void test_ttl_preserved(void)
{
    db_t *db = create_test_db();

    set_value(db->store, (const unsigned char *)"expkey", 6, "val", 3,
              VALUE_ENTRY_TYPE_RAW);
    int64_t future_deadline = fkvs_now_ms() + 300000;
    set_expiry(db->expires, (const unsigned char *)"expkey", 6,
               future_deadline);

    assert(save_snapshot(db, TEST_RDB));

    db_t *db2 = create_test_db();
    assert(load_snapshot(TEST_RDB, db2));

    value_entry_t *val = NULL;
    size_t val_len = 0;
    assert(get_value(db2->store, (unsigned char *)"expkey", 6, &val,
                     &val_len));
    assert(val_len == 3);
    assert(memcmp(val->ptr, "val", 3) == 0);
    free(val->ptr);
    free(val);

    int64_t ttl = get_ttl(db2->expires, (const unsigned char *)"expkey", 6);
    assert(ttl > 0);

    free_test_db(db);
    free_test_db(db2);
    unlink(TEST_RDB);
    printf("test_ttl_preserved passed.\n");
}

void test_ttl_expired_discarded(void)
{
    db_t *db = create_test_db();

    set_value(db->store, (const unsigned char *)"old", 3, "stale", 5,
              VALUE_ENTRY_TYPE_RAW);
    int64_t past_deadline = fkvs_now_ms() - 1000;
    set_expiry(db->expires, (const unsigned char *)"old", 3, past_deadline);

    assert(save_snapshot(db, TEST_RDB));

    db_t *db2 = create_test_db();
    assert(load_snapshot(TEST_RDB, db2));

    value_entry_t *val = NULL;
    size_t val_len = 0;
    assert(!get_value(db2->store, (unsigned char *)"old", 3, &val, &val_len));

    free_test_db(db);
    free_test_db(db2);
    unlink(TEST_RDB);
    printf("test_ttl_expired_discarded passed.\n");
}

void test_empty_database(void)
{
    db_t *db = create_test_db();

    assert(save_snapshot(db, TEST_RDB));

    db_t *db2 = create_test_db();
    assert(load_snapshot(TEST_RDB, db2));

    free_test_db(db);
    free_test_db(db2);
    unlink(TEST_RDB);
    printf("test_empty_database passed.\n");
}

void test_missing_file(void)
{
    db_t *db = create_test_db();

    assert(!load_snapshot("/tmp/nonexistent_fkvs.rdb", db));

    free_test_db(db);
    printf("test_missing_file passed.\n");
}

void test_corrupted_header(void)
{
    db_t *db = create_test_db();
    set_value(db->store, (const unsigned char *)"k", 1, "v", 1,
              VALUE_ENTRY_TYPE_RAW);
    assert(save_snapshot(db, TEST_RDB));

    FILE *f = fopen(TEST_RDB, "r+b");
    assert(f != NULL);
    fseek(f, 0, SEEK_SET);
    fwrite("XXXX", 1, 4, f);
    fclose(f);

    db_t *db2 = create_test_db();
    assert(!load_snapshot(TEST_RDB, db2));

    free_test_db(db);
    free_test_db(db2);
    unlink(TEST_RDB);
    printf("test_corrupted_header passed.\n");
}

void test_corrupted_crc(void)
{
    db_t *db = create_test_db();
    set_value(db->store, (const unsigned char *)"key", 3, "value", 5,
              VALUE_ENTRY_TYPE_RAW);
    assert(save_snapshot(db, TEST_RDB));

    // Corrupt a data byte: seek to 6 bytes before EOF (1 byte before CRC+EOF footer)
    FILE *f = fopen(TEST_RDB, "r+b");
    assert(f != NULL);
    fseek(f, -6, SEEK_END);
    uint8_t original;
    fread(&original, 1, 1, f);
    fseek(f, -1, SEEK_CUR);
    uint8_t garbage = original ^ 0xFF;
    fwrite(&garbage, 1, 1, f);
    fclose(f);

    db_t *db2 = create_test_db();
    assert(!load_snapshot(TEST_RDB, db2));

    free_test_db(db);
    free_test_db(db2);
    unlink(TEST_RDB);
    printf("test_corrupted_crc passed.\n");
}

int main(void)
{
    test_round_trip();
    test_encoding_preserved();
    test_ttl_preserved();
    test_ttl_expired_discarded();
    test_empty_database();
    test_missing_file();
    test_corrupted_header();
    test_corrupted_crc();

    printf("All persistence tests passed.\n");
    return 0;
}
