#include "persistence.h"
#include "../core/hashtable.h"
#include "../fkvs_time.h"
#include "../ttl.h"
#include "crc32.h"
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define RDB_MAGIC "FKVS"
#define RDB_VERSION 0x01
#define RDB_MARKER_STORE 0x01
#define RDB_MARKER_EXPIRES 0x02
#define RDB_EOF_MARKER 0xFF

static bool write_u8(FILE *f, uint8_t val, uint32_t *crc)
{
    if (fwrite(&val, 1, 1, f) != 1)
        return false;
    *crc = crc32_update(*crc, &val, 1);
    return true;
}

static bool write_u16_be(FILE *f, uint16_t val, uint32_t *crc)
{
    uint8_t buf[2] = {(val >> 8) & 0xFF, val & 0xFF};
    if (fwrite(buf, 1, 2, f) != 2)
        return false;
    *crc = crc32_update(*crc, buf, 2);
    return true;
}

static bool write_u32_be(FILE *f, uint32_t val, uint32_t *crc)
{
    uint8_t buf[4] = {(val >> 24) & 0xFF, (val >> 16) & 0xFF,
                      (val >> 8) & 0xFF, val & 0xFF};
    if (fwrite(buf, 1, 4, f) != 4)
        return false;
    *crc = crc32_update(*crc, buf, 4);
    return true;
}

static bool write_u64_be(FILE *f, uint64_t val, uint32_t *crc)
{
    uint8_t buf[8] = {(val >> 56) & 0xFF, (val >> 48) & 0xFF,
                      (val >> 40) & 0xFF, (val >> 32) & 0xFF,
                      (val >> 24) & 0xFF, (val >> 16) & 0xFF,
                      (val >> 8) & 0xFF, val & 0xFF};
    if (fwrite(buf, 1, 8, f) != 8)
        return false;
    *crc = crc32_update(*crc, buf, 8);
    return true;
}

static bool write_bytes(FILE *f, const void *data, size_t len, uint32_t *crc)
{
    if (len == 0)
        return true;
    if (fwrite(data, 1, len, f) != len)
        return false;
    *crc = crc32_update(*crc, data, len);
    return true;
}

static bool read_u8(FILE *f, uint8_t *val, uint32_t *crc)
{
    if (fread(val, 1, 1, f) != 1)
        return false;
    *crc = crc32_update(*crc, val, 1);
    return true;
}

static bool read_u16_be(FILE *f, uint16_t *val, uint32_t *crc)
{
    uint8_t buf[2];
    if (fread(buf, 1, 2, f) != 2)
        return false;
    *crc = crc32_update(*crc, buf, 2);
    *val = ((uint16_t)buf[0] << 8) | buf[1];
    return true;
}

static bool read_u32_be(FILE *f, uint32_t *val, uint32_t *crc)
{
    uint8_t buf[4];
    if (fread(buf, 1, 4, f) != 4)
        return false;
    *crc = crc32_update(*crc, buf, 4);
    *val = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) | buf[3];
    return true;
}

static bool read_u64_be(FILE *f, uint64_t *val, uint32_t *crc)
{
    uint8_t buf[8];
    if (fread(buf, 1, 8, f) != 8)
        return false;
    *crc = crc32_update(*crc, buf, 8);
    *val = ((uint64_t)buf[0] << 56) | ((uint64_t)buf[1] << 48) |
           ((uint64_t)buf[2] << 40) | ((uint64_t)buf[3] << 32) |
           ((uint64_t)buf[4] << 24) | ((uint64_t)buf[5] << 16) |
           ((uint64_t)buf[6] << 8) | buf[7];
    return true;
}

static bool read_bytes(FILE *f, void *data, size_t len, uint32_t *crc)
{
    if (len == 0)
        return true;
    if (fread(data, 1, len, f) != len)
        return false;
    *crc = crc32_update(*crc, data, len);
    return true;
}

static size_t count_entries(hashtable_t *table)
{
    size_t count = 0;
    for (size_t i = 0; i < table->size; i++) {
        hash_table_entry_t *entry = table->buckets[i];
        while (entry) {
            count++;
            entry = entry->next;
        }
    }
    return count;
}

static void fsync_directory(const char *filepath)
{
    char tmp[4096];
    strncpy(tmp, filepath, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *dir = dirname(tmp);
    int fd = open(dir, O_RDONLY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
}

bool save_snapshot(db_t *db, const char *filepath)
{
    char tmp_path[4096];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", filepath);

    FILE *f = fopen(tmp_path, "wb");
    if (!f)
        return false;

    uint32_t crc = 0;

    // HEADER: magic + version + timestamp
    if (!write_bytes(f, RDB_MAGIC, 4, &crc))
        goto fail;
    if (!write_u8(f, RDB_VERSION, &crc))
        goto fail;
    uint64_t timestamp = (uint64_t)fkvs_now_ms();
    if (!write_u64_be(f, timestamp, &crc))
        goto fail;

    // STORE SECTION
    if (!write_u8(f, RDB_MARKER_STORE, &crc))
        goto fail;
    uint64_t num_entries = count_entries(db->store);
    if (!write_u64_be(f, num_entries, &crc))
        goto fail;

    for (size_t i = 0; i < db->store->size; i++) {
        hash_table_entry_t *entry = db->store->buckets[i];
        while (entry) {
            if (!write_u16_be(f, (uint16_t)entry->key_len, &crc))
                goto fail;
            if (!write_bytes(f, entry->key, entry->key_len, &crc))
                goto fail;
            if (!write_u32_be(f, (uint32_t)entry->value->value_len, &crc))
                goto fail;
            if (!write_bytes(f, entry->value->ptr, entry->value->value_len,
                             &crc))
                goto fail;
            if (!write_u8(f, (uint8_t)entry->value->encoding, &crc))
                goto fail;
            entry = entry->next;
        }
    }

    // EXPIRES SECTION
    if (!write_u8(f, RDB_MARKER_EXPIRES, &crc))
        goto fail;
    uint64_t num_expires = count_entries(db->expires);
    if (!write_u64_be(f, num_expires, &crc))
        goto fail;

    for (size_t i = 0; i < db->expires->size; i++) {
        hash_table_entry_t *entry = db->expires->buckets[i];
        while (entry) {
            if (!write_u16_be(f, (uint16_t)entry->key_len, &crc))
                goto fail;
            if (!write_bytes(f, entry->key, entry->key_len, &crc))
                goto fail;

            const unsigned char *b = entry->value->ptr;
            uint64_t deadline = ((uint64_t)b[0] << 56) |
                                ((uint64_t)b[1] << 48) |
                                ((uint64_t)b[2] << 40) |
                                ((uint64_t)b[3] << 32) |
                                ((uint64_t)b[4] << 24) |
                                ((uint64_t)b[5] << 16) |
                                ((uint64_t)b[6] << 8) | (uint64_t)b[7];
            if (!write_u64_be(f, deadline, &crc))
                goto fail;

            entry = entry->next;
        }
    }

    // FOOTER: CRC32 (not included in CRC) + EOF marker
    uint8_t crc_buf[4] = {(crc >> 24) & 0xFF, (crc >> 16) & 0xFF,
                          (crc >> 8) & 0xFF, crc & 0xFF};
    if (fwrite(crc_buf, 1, 4, f) != 4)
        goto fail;
    uint8_t eof = RDB_EOF_MARKER;
    if (fwrite(&eof, 1, 1, f) != 1)
        goto fail;

    if (fflush(f) != 0)
        goto fail;
    if (fsync(fileno(f)) != 0)
        goto fail;
    fclose(f);

    if (rename(tmp_path, filepath) != 0) {
        unlink(tmp_path);
        return false;
    }

    fsync_directory(filepath);
    return true;

fail:
    fclose(f);
    unlink(tmp_path);
    return false;
}

bool load_snapshot(const char *filepath, db_t *db)
{
    FILE *f = fopen(filepath, "rb");
    if (!f)
        return false;

    hashtable_t *tmp_store = create_hash_table(db->store->size);
    hashtable_t *tmp_expires = create_hash_table(db->expires->size);
    if (!tmp_store || !tmp_expires) {
        if (tmp_store)
            free_hash_table(tmp_store);
        if (tmp_expires)
            free_hash_table(tmp_expires);
        fclose(f);
        return false;
    }

    uint32_t crc = 0;

    // HEADER
    char magic[4];
    if (!read_bytes(f, magic, 4, &crc) || memcmp(magic, RDB_MAGIC, 4) != 0)
        goto load_fail;

    uint8_t version;
    if (!read_u8(f, &version, &crc) || version != RDB_VERSION)
        goto load_fail;

    uint64_t timestamp;
    if (!read_u64_be(f, &timestamp, &crc))
        goto load_fail;

    // STORE SECTION
    uint8_t marker;
    if (!read_u8(f, &marker, &crc) || marker != RDB_MARKER_STORE)
        goto load_fail;

    uint64_t num_entries;
    if (!read_u64_be(f, &num_entries, &crc))
        goto load_fail;

    for (uint64_t i = 0; i < num_entries; i++) {
        uint16_t key_len;
        if (!read_u16_be(f, &key_len, &crc))
            goto load_fail;

        unsigned char *key = malloc(key_len);
        if (!key)
            goto load_fail;
        if (!read_bytes(f, key, key_len, &crc)) {
            free(key);
            goto load_fail;
        }

        uint32_t value_len;
        if (!read_u32_be(f, &value_len, &crc)) {
            free(key);
            goto load_fail;
        }

        unsigned char *value = malloc(value_len);
        if (!value) {
            free(key);
            goto load_fail;
        }
        if (!read_bytes(f, value, value_len, &crc)) {
            free(key);
            free(value);
            goto load_fail;
        }

        uint8_t encoding;
        if (!read_u8(f, &encoding, &crc)) {
            free(key);
            free(value);
            goto load_fail;
        }

        set_value(tmp_store, key, key_len, value, value_len, encoding);
        free(key);
        free(value);
    }

    // EXPIRES SECTION
    if (!read_u8(f, &marker, &crc) || marker != RDB_MARKER_EXPIRES)
        goto load_fail;

    uint64_t num_expires;
    if (!read_u64_be(f, &num_expires, &crc))
        goto load_fail;

    int64_t now = fkvs_now_ms();

    for (uint64_t i = 0; i < num_expires; i++) {
        uint16_t key_len;
        if (!read_u16_be(f, &key_len, &crc))
            goto load_fail;

        unsigned char *key = malloc(key_len);
        if (!key)
            goto load_fail;
        if (!read_bytes(f, key, key_len, &crc)) {
            free(key);
            goto load_fail;
        }

        uint64_t deadline;
        if (!read_u64_be(f, &deadline, &crc)) {
            free(key);
            goto load_fail;
        }

        if ((int64_t)deadline <= now) {
            delete_value(tmp_store, key, key_len);
        } else {
            set_expiry(tmp_expires, key, key_len, (int64_t)deadline);
        }

        free(key);
    }

    // FOOTER: verify CRC32
    uint32_t computed_crc = crc;
    uint32_t file_crc;
    uint8_t crc_buf[4];
    if (fread(crc_buf, 1, 4, f) != 4)
        goto load_fail;
    file_crc = ((uint32_t)crc_buf[0] << 24) | ((uint32_t)crc_buf[1] << 16) |
               ((uint32_t)crc_buf[2] << 8) | crc_buf[3];

    if (file_crc != computed_crc) {
        fprintf(stderr, "Snapshot CRC mismatch: expected 0x%08X, got 0x%08X\n",
                computed_crc, file_crc);
        goto load_fail;
    }

    uint8_t eof;
    if (fread(&eof, 1, 1, f) != 1 || eof != RDB_EOF_MARKER)
        goto load_fail;

    fclose(f);

    // Success: swap temporary tables into db, free the old empty ones
    free_hash_table(db->store);
    free_hash_table(db->expires);
    db->store = tmp_store;
    db->expires = tmp_expires;
    return true;

load_fail:
    fclose(f);
    free_hash_table(tmp_store);
    free_hash_table(tmp_expires);
    return false;
}
