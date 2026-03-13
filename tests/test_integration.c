/**
 * Integration tests for fkvs server command handlers.
 *
 * Uses socketpair() to create connected fds, then calls dispatch_command()
 * directly and reads framed responses from the other end — no server process
 * needed.  Frame construction uses the real construct_*_command() functions
 * from command_parser.c so the tests break whenever the protocol changes.
 */

#include "../src/client.h"
#include "../src/commands/common/command_parser.h"
#include "../src/commands/common/command_registry.h"
#include "../src/commands/server/server_command_handlers.h"
#include "../src/core/hashtable.h"
#include "../src/response_defs.h"
#include "../src/server.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ── globals needed by the server code ─────────────────────────────── */

server_t server;

/* stub – memory.c is not linked */
unsigned long get_private_memory_usage_bytes(void) { return 0; }

/* ── test fixture ──────────────────────────────────────────────────── */

typedef struct {
    client_t *client;
    int read_fd;
    db_t *db;
} fixture_t;

static fixture_t setup(void)
{
    int fds[2];
    int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    assert(rc == 0);

    client_t *c = calloc(1, sizeof(client_t));
    assert(c != NULL);
    c->fd = fds[0];
    c->frame_need = -1;

    db_t *db = malloc(sizeof(db_t));
    assert(db != NULL);
    db->store = create_hash_table(TABLE_SIZE);
    db->expires = create_hash_table(TABLE_SIZE);

    init_command_handlers(db);

    return (fixture_t){.client = c, .read_fd = fds[1], .db = db};
}

static void teardown(fixture_t *f)
{
    close(f->client->fd);
    close(f->read_fd);
    free(f->client);
    free_hash_table(f->db->store);
    free_hash_table(f->db->expires);
    free(f->db);
}

/* ── low-level helpers ─────────────────────────────────────────────── */

/** Dispatch a frame and read the raw response. Caller frees `frame`. */
static ssize_t dispatch_and_recv(fixture_t *f, unsigned char *frame,
                                 size_t frame_len, unsigned char *resp,
                                 size_t resp_size)
{
    dispatch_command(f->client, frame, frame_len);
    wbuf_flush(f->client);
    return recv(f->read_fd, resp, resp_size, 0);
}

static bool resp_is_success(const unsigned char *resp, ssize_t len,
                            const char *expected)
{
    const size_t elen = strlen(expected);
    if (len < (ssize_t)(5 + elen))
        return false;
    if (resp[2] != STATUS_SUCCESS)
        return false;
    const size_t vlen = ((size_t)resp[3] << 8) | resp[4];
    if (vlen != elen)
        return false;
    return memcmp(&resp[5], expected, elen) == 0;
}

static bool resp_is_error(const unsigned char *resp, ssize_t len)
{
    if (len < 3)
        return false;
    return resp[0] == 0x00 && resp[1] == 0x01 && resp[2] == STATUS_FAILURE;
}

static bool resp_is_ok(const unsigned char *resp, ssize_t len)
{
    // Framed OK: [0x00][0x01][STATUS_SUCCESS]
    if (len < 3)
        return false;
    return resp[0] == 0x00 && resp[1] == 0x01 && resp[2] == STATUS_SUCCESS;
}

/* ── command assertion helpers ─────────────────────────────────────── *
 *                                                                      *
 * Each one constructs a real protocol frame, dispatches it, asserts on  *
 * the response, and cleans up.  Tests read like a script of user        *
 * commands.                                                             *
 * ──────────────────────────────────────────────────────────────────── */

static void assert_set_ex(fixture_t *f, const char *key, const char *value,
                          const char *seconds, const char *expected)
{
    unsigned char resp[512];
    size_t len;
    unsigned char *cmd = construct_set_ex_command(key, value, seconds, &len);
    assert(cmd);
    ssize_t r = dispatch_and_recv(f, cmd, len, resp, sizeof resp);
    free(cmd);
    assert(r > 0 && resp_is_success(resp, r, expected));
}

static void assert_set(fixture_t *f, const char *key, const char *value,
                       const char *expected)
{
    unsigned char resp[512];
    size_t len;
    unsigned char *cmd = construct_set_command(key, value, &len);
    assert(cmd);
    ssize_t r = dispatch_and_recv(f, cmd, len, resp, sizeof resp);
    free(cmd);
    assert(r > 0 && resp_is_success(resp, r, expected));
}

static void assert_get(fixture_t *f, const char *key, const char *expected)
{
    unsigned char resp[512];
    size_t len;
    unsigned char *cmd = construct_get_command(key, &len);
    assert(cmd);
    ssize_t r = dispatch_and_recv(f, cmd, len, resp, sizeof resp);
    free(cmd);
    assert(r > 0 && resp_is_success(resp, r, expected));
}

static void assert_get_error(fixture_t *f, const char *key)
{
    unsigned char resp[512];
    size_t len;
    unsigned char *cmd = construct_get_command(key, &len);
    assert(cmd);
    ssize_t r = dispatch_and_recv(f, cmd, len, resp, sizeof resp);
    free(cmd);
    assert(r > 0 && resp_is_error(resp, r));
}

static void assert_incr(fixture_t *f, const char *key, const char *expected)
{
    unsigned char resp[512];
    size_t len;
    unsigned char *cmd = construct_incr_command(key, &len);
    assert(cmd);
    ssize_t r = dispatch_and_recv(f, cmd, len, resp, sizeof resp);
    free(cmd);
    assert(r > 0 && resp_is_success(resp, r, expected));
}

static void assert_incr_error(fixture_t *f, const char *key)
{
    unsigned char resp[512];
    size_t len;
    unsigned char *cmd = construct_incr_command(key, &len);
    assert(cmd);
    ssize_t r = dispatch_and_recv(f, cmd, len, resp, sizeof resp);
    free(cmd);
    assert(r > 0 && resp_is_error(resp, r));
}

static void assert_incrby(fixture_t *f, const char *key, const char *amount,
                          const char *expected)
{
    unsigned char resp[512];
    size_t len;
    unsigned char *cmd = construct_incr_by_command(key, amount, &len);
    assert(cmd);
    ssize_t r = dispatch_and_recv(f, cmd, len, resp, sizeof resp);
    free(cmd);
    assert(r > 0 && resp_is_success(resp, r, expected));
}

static void assert_decr(fixture_t *f, const char *key, const char *expected)
{
    unsigned char resp[512];
    size_t len;
    unsigned char *cmd = construct_decr_command(key, &len);
    assert(cmd);
    ssize_t r = dispatch_and_recv(f, cmd, len, resp, sizeof resp);
    free(cmd);
    assert(r > 0 && resp_is_success(resp, r, expected));
}

static void assert_decrby(fixture_t *f, const char *key, const char *amount,
                          const char *expected)
{
    unsigned char resp[512];
    size_t len;
    unsigned char *cmd = construct_decr_by_command(key, amount, &len);
    assert(cmd);
    const ssize_t r = dispatch_and_recv(f, cmd, len, resp, sizeof resp);
    free(cmd);
    assert(r > 0 && resp_is_success(resp, r, expected));
}

static void assert_del_ok(fixture_t *f, const char *key)
{
    unsigned char resp[512];
    size_t len;
    unsigned char *cmd = construct_del_command(key, &len);
    assert(cmd);
    ssize_t r = dispatch_and_recv(f, cmd, len, resp, sizeof resp);
    free(cmd);
    assert(r > 0 && resp_is_ok(resp, r));
}

static void assert_expire_ok(fixture_t *f, const char *key, const char *seconds)
{
    unsigned char resp[512];
    size_t len;
    unsigned char *cmd = construct_expire_command(key, seconds, &len);
    assert(cmd);
    ssize_t r = dispatch_and_recv(f, cmd, len, resp, sizeof resp);
    free(cmd);
    assert(r > 0 && resp_is_ok(resp, r));
}

static void assert_expire_error(fixture_t *f, const char *key, const char *seconds)
{
    unsigned char resp[512];
    size_t len;
    unsigned char *cmd = construct_expire_command(key, seconds, &len);
    assert(cmd);
    ssize_t r = dispatch_and_recv(f, cmd, len, resp, sizeof resp);
    free(cmd);
    assert(r > 0 && resp_is_error(resp, r));
}

static void assert_ttl(fixture_t *f, const char *key, const char *expected)
{
    unsigned char resp[512];
    size_t len;
    unsigned char *cmd = construct_ttl_command(key, &len);
    assert(cmd);
    ssize_t r = dispatch_and_recv(f, cmd, len, resp, sizeof resp);
    free(cmd);
    assert(r > 0 && resp_is_success(resp, r, expected));
}

static void assert_persist_ok(fixture_t *f, const char *key)
{
    unsigned char resp[512];
    size_t len;
    unsigned char *cmd = construct_persist_command(key, &len);
    assert(cmd);
    ssize_t r = dispatch_and_recv(f, cmd, len, resp, sizeof resp);
    free(cmd);
    assert(r > 0 && resp_is_ok(resp, r));
}

/* ── tests ─────────────────────────────────────────────────────────── */

static void test_set_get_roundtrip(void)
{
    fixture_t f = setup();

    assert_set(&f, "greeting", "hello", "hello");
    assert_get(&f, "greeting", "hello");

    teardown(&f);
    printf("  test_set_get_roundtrip passed.\n");
}

static void test_set_integer_stored_as_int(void)
{
    fixture_t f = setup();

    assert_set(&f, "counter", "10", "10");
    assert_incr(&f, "counter", "11");

    teardown(&f);
    printf("  test_set_integer_stored_as_int passed.\n");
}

static void test_set_string_rejects_incr(void)
{
    fixture_t f = setup();

    assert_set(&f, "name", "alice", "alice");
    assert_incr_error(&f, "name");

    teardown(&f);
    printf("  test_set_string_rejects_incr passed.\n");
}

static void test_get_nonexistent_returns_error(void)
{
    fixture_t f = setup();

    assert_get_error(&f, "no_such_key");

    teardown(&f);
    printf("  test_get_nonexistent_returns_error passed.\n");
}

static void test_error_response_is_framed(void)
{
    fixture_t f = setup();
    unsigned char resp[256];
    size_t len;

    unsigned char *cmd = construct_get_command("missing", &len);
    assert(cmd);
    ssize_t r = dispatch_and_recv(&f, cmd, len, resp, sizeof resp);
    free(cmd);

    /* Must be exactly 3 bytes: [0x00][0x01][0x00] */
    assert(r == 3);
    assert(resp[0] == 0x00);
    assert(resp[1] == 0x01);
    assert(resp[2] == STATUS_FAILURE);

    teardown(&f);
    printf("  test_error_response_is_framed passed.\n");
}

static void test_incr_auto_creates_key(void)
{
    fixture_t f = setup();

    assert_incr(&f, "new_counter", "1");
    assert_incr(&f, "new_counter", "2");

    teardown(&f);
    printf("  test_incr_auto_creates_key passed.\n");
}

static void test_incrby_auto_creates_key(void)
{
    fixture_t f = setup();

    assert_incrby(&f, "score", "10", "10");
    assert_incrby(&f, "score", "5", "15");

    teardown(&f);
    printf("  test_incrby_auto_creates_key passed.\n");
}

static void test_decr_auto_creates_key(void)
{
    fixture_t f = setup();

    assert_decr(&f, "neg", "-1");

    teardown(&f);
    printf("  test_decr_auto_creates_key passed.\n");
}



static void test_incr_after_set(void)
{
    fixture_t f = setup();

    assert_set(&f, "val", "100", "100");
    assert_incr(&f, "val", "101");

    teardown(&f);
    printf("  test_incr_after_set passed.\n");
}

static void test_incrby_after_set(void)
{
    fixture_t f = setup();

    assert_set(&f, "pts", "50", "50");
    assert_incrby(&f, "pts", "25", "75");

    teardown(&f);
    printf("  test_incrby_after_set passed.\n");
}

/* ── TTL tests ─────────────────────────────────────────────────────── */

static void test_del_removes_key(void)
{
    fixture_t f = setup();

    assert_set(&f, "delme", "value", "value");
    assert_del_ok(&f, "delme");
    assert_get_error(&f, "delme");

    teardown(&f);
    printf("  test_del_removes_key passed.\n");
}

static void test_del_nonexistent_key(void)
{
    fixture_t f = setup();

    // DEL on missing key returns OK (no-op)
    assert_del_ok(&f, "nosuchkey");

    teardown(&f);
    printf("  test_del_nonexistent_key passed.\n");
}

static void test_expire_and_get(void)
{
    fixture_t f = setup();

    assert_set(&f, "ephemeral", "temp", "temp");
    assert_expire_ok(&f, "ephemeral", "1");

    // Sleep 2 seconds so the key expires
    sleep(2);

    // GET should return error (key expired via lazy check)
    assert_get_error(&f, "ephemeral");

    teardown(&f);
    printf("  test_expire_and_get passed.\n");
}

static void test_ttl_returns_remaining(void)
{
    fixture_t f = setup();

    assert_set(&f, "ttlkey", "val", "val");
    assert_expire_ok(&f, "ttlkey", "10");

    // TTL should return something around 9 or 10
    unsigned char resp[512];
    size_t len;
    unsigned char *cmd = construct_ttl_command("ttlkey", &len);
    assert(cmd);
    ssize_t r = dispatch_and_recv(&f, cmd, len, resp, sizeof resp);
    free(cmd);
    assert(r > 0);
    assert(resp[2] == STATUS_SUCCESS);
    size_t vlen = ((size_t)resp[3] << 8) | resp[4];
    char buf[32];
    assert(vlen < sizeof(buf));
    memcpy(buf, &resp[5], vlen);
    buf[vlen] = '\0';
    long ttl = strtol(buf, NULL, 10);
    assert(ttl >= 8 && ttl <= 10);

    teardown(&f);
    printf("  test_ttl_returns_remaining passed.\n");
}

static void test_ttl_no_expiry(void)
{
    fixture_t f = setup();

    assert_set(&f, "permkey", "val", "val");
    assert_ttl(&f, "permkey", "-1");

    teardown(&f);
    printf("  test_ttl_no_expiry passed.\n");
}

static void test_ttl_missing_key(void)
{
    fixture_t f = setup();

    assert_ttl(&f, "ghost", "-2");

    teardown(&f);
    printf("  test_ttl_missing_key passed.\n");
}

static void test_persist_removes_ttl(void)
{
    fixture_t f = setup();

    assert_set(&f, "pkey", "val", "val");
    assert_expire_ok(&f, "pkey", "10");
    assert_persist_ok(&f, "pkey");
    assert_ttl(&f, "pkey", "-1");

    teardown(&f);
    printf("  test_persist_removes_ttl passed.\n");
}

static void test_set_clears_ttl(void)
{
    fixture_t f = setup();

    assert_set(&f, "skey", "v1", "v1");
    assert_expire_ok(&f, "skey", "10");
    // SET again should clear the TTL
    assert_set(&f, "skey", "v2", "v2");
    assert_ttl(&f, "skey", "-1");

    teardown(&f);
    printf("  test_set_clears_ttl passed.\n");
}

static void test_lazy_expiry_on_incr(void)
{
    fixture_t f = setup();

    assert_set(&f, "lctr", "100", "100");
    assert_expire_ok(&f, "lctr", "1");

    sleep(2);

    // INCR on expired key should auto-create from 0
    assert_incr(&f, "lctr", "1");

    teardown(&f);
    printf("  test_lazy_expiry_on_incr passed.\n");
}

static void test_ttl_on_expired_key(void)
{
    fixture_t f = setup();

    assert_set(&f, "ttlexp", "val", "val");
    assert_expire_ok(&f, "ttlexp", "1");

    sleep(2);

    // TTL on expired key should trigger lazy expiry and return -2
    assert_ttl(&f, "ttlexp", "-2");

    teardown(&f);
    printf("  test_ttl_on_expired_key passed.\n");
}

static void test_ok_response_is_framed(void)
{
    fixture_t f = setup();
    unsigned char resp[256];
    size_t len;

    assert_set(&f, "okframe", "val", "val");

    unsigned char *cmd = construct_del_command("okframe", &len);
    assert(cmd);
    ssize_t r = dispatch_and_recv(&f, cmd, len, resp, sizeof resp);
    free(cmd);

    // Must be exactly 3 bytes: [0x00][0x01][0x01]
    assert(r == 3);
    assert(resp[0] == 0x00);
    assert(resp[1] == 0x01);
    assert(resp[2] == STATUS_SUCCESS);

    teardown(&f);
    printf("  test_ok_response_is_framed passed.\n");
}

static void test_expire_nonexistent_key(void)
{
    fixture_t f = setup();

    // EXPIRE on a key that doesn't exist should return error
    assert_expire_error(&f, "nokey", "10");

    teardown(&f);
    printf("  test_expire_nonexistent_key passed.\n");
}

static void test_del_clears_ttl(void)
{
    fixture_t f = setup();

    assert_set(&f, "delttl", "val", "val");
    assert_expire_ok(&f, "delttl", "60");
    assert_del_ok(&f, "delttl");

    // Key is gone, TTL should report -2
    assert_ttl(&f, "delttl", "-2");

    teardown(&f);
    printf("  test_del_clears_ttl passed.\n");
}

static void test_expire_overwrites_previous(void)
{
    fixture_t f = setup();

    assert_set(&f, "reexpire", "val", "val");
    assert_expire_ok(&f, "reexpire", "5");
    // Overwrite with a longer TTL
    assert_expire_ok(&f, "reexpire", "60");

    // TTL should reflect the latest EXPIRE, not the first
    unsigned char resp[512];
    size_t len;
    unsigned char *cmd = construct_ttl_command("reexpire", &len);
    assert(cmd);
    ssize_t r = dispatch_and_recv(&f, cmd, len, resp, sizeof resp);
    free(cmd);
    assert(r > 0 && resp[2] == STATUS_SUCCESS);
    size_t vlen = ((size_t)resp[3] << 8) | resp[4];
    char buf[32];
    assert(vlen < sizeof(buf));
    memcpy(buf, &resp[5], vlen);
    buf[vlen] = '\0';
    long ttl = strtol(buf, NULL, 10);
    assert(ttl >= 55 && ttl <= 60);

    teardown(&f);
    printf("  test_expire_overwrites_previous passed.\n");
}

static void test_persist_on_key_without_ttl(void)
{
    fixture_t f = setup();

    assert_set(&f, "nottl", "val", "val");
    // PERSIST on a key with no TTL is a no-op, should still return OK
    assert_persist_ok(&f, "nottl");
    assert_ttl(&f, "nottl", "-1");
    // Key should still be accessible
    assert_get(&f, "nottl", "val");

    teardown(&f);
    printf("  test_persist_on_key_without_ttl passed.\n");
}

static void test_persist_on_nonexistent_key(void)
{
    fixture_t f = setup();

    // PERSIST on nonexistent key is a no-op, returns OK
    assert_persist_ok(&f, "ghost_persist");

    teardown(&f);
    printf("  test_persist_on_nonexistent_key passed.\n");
}

static void test_lazy_expiry_on_decr(void)
{
    fixture_t f = setup();

    assert_set(&f, "dctr", "100", "100");
    assert_expire_ok(&f, "dctr", "1");

    sleep(2);

    // DECR on expired key should auto-create from 0
    assert_decr(&f, "dctr", "-1");

    teardown(&f);
    printf("  test_lazy_expiry_on_decr passed.\n");
}

static void test_lazy_expiry_on_incrby(void)
{
    fixture_t f = setup();

    assert_set(&f, "ibctr", "100", "100");
    assert_expire_ok(&f, "ibctr", "1");

    sleep(2);

    // INCRBY on expired key should auto-create from 0
    assert_incrby(&f, "ibctr", "5", "5");

    teardown(&f);
    printf("  test_lazy_expiry_on_incrby passed.\n");
}

static void test_lazy_expiry_on_decrby(void)
{
    fixture_t f = setup();

    assert_set(&f, "dbctr", "100", "100");
    assert_expire_ok(&f, "dbctr", "1");

    sleep(2);

    // DECRBY on expired key should auto-create from 0
    assert_decrby(&f, "dbctr", "3", "3");

    teardown(&f);
    printf("  test_lazy_expiry_on_decrby passed.\n");
}

static void test_expire_zero_expires_immediately(void)
{
    fixture_t f = setup();

    assert_set(&f, "zerottl", "val", "val");
    assert_expire_ok(&f, "zerottl", "0");

    // Key should be expired on next access
    assert_get_error(&f, "zerottl");
    assert_ttl(&f, "zerottl", "-2");

    teardown(&f);
    printf("  test_expire_zero_expires_immediately passed.\n");
}

static void test_del_double(void)
{
    fixture_t f = setup();

    assert_set(&f, "dbl", "val", "val");
    assert_del_ok(&f, "dbl");
    // Second DEL on same key is a no-op, should still return OK
    assert_del_ok(&f, "dbl");
    assert_get_error(&f, "dbl");

    teardown(&f);
    printf("  test_del_double passed.\n");
}

static void test_set_after_del_works(void)
{
    fixture_t f = setup();

    assert_set(&f, "revive", "old", "old");
    assert_del_ok(&f, "revive");
    assert_get_error(&f, "revive");

    // SET should recreate the key
    assert_set(&f, "revive", "new", "new");
    assert_get(&f, "revive", "new");

    teardown(&f);
    printf("  test_set_after_del_works passed.\n");
}

static void test_set_after_expire_restores_key(void)
{
    fixture_t f = setup();

    assert_set(&f, "reborn", "v1", "v1");
    assert_expire_ok(&f, "reborn", "1");

    sleep(2);

    assert_get_error(&f, "reborn");

    // SET after expiry should create a fresh key with no TTL
    assert_set(&f, "reborn", "v2", "v2");
    assert_get(&f, "reborn", "v2");
    assert_ttl(&f, "reborn", "-1");

    teardown(&f);
    printf("  test_set_after_expire_restores_key passed.\n");
}

/* ── SET EX tests ──────────────────────────────────────────────────── */

static void test_set_ex_sets_ttl(void)
{
    fixture_t f = setup();

    assert_set_ex(&f, "exkey", "val", "10", "val");

    // TTL should return something around 9 or 10
    unsigned char resp[512];
    size_t len;
    unsigned char *cmd = construct_ttl_command("exkey", &len);
    assert(cmd);
    ssize_t r = dispatch_and_recv(&f, cmd, len, resp, sizeof resp);
    free(cmd);
    assert(r > 0 && resp[2] == STATUS_SUCCESS);
    size_t vlen = ((size_t)resp[3] << 8) | resp[4];
    char buf[32];
    assert(vlen < sizeof(buf));
    memcpy(buf, &resp[5], vlen);
    buf[vlen] = '\0';
    long ttl = strtol(buf, NULL, 10);
    assert(ttl >= 8 && ttl <= 10);

    teardown(&f);
    printf("  test_set_ex_sets_ttl passed.\n");
}

static void test_set_ex_value_accessible(void)
{
    fixture_t f = setup();

    assert_set_ex(&f, "exval", "hello", "10", "hello");
    assert_get(&f, "exval", "hello");

    teardown(&f);
    printf("  test_set_ex_value_accessible passed.\n");
}

static void test_set_ex_expires(void)
{
    fixture_t f = setup();

    assert_set_ex(&f, "exexp", "temp", "1", "temp");

    sleep(2);

    assert_get_error(&f, "exexp");

    teardown(&f);
    printf("  test_set_ex_expires passed.\n");
}

static void test_set_ex_overwritten_by_set(void)
{
    fixture_t f = setup();

    assert_set_ex(&f, "exover", "v1", "10", "v1");
    // Plain SET should clear the TTL
    assert_set(&f, "exover", "v2", "v2");
    assert_ttl(&f, "exover", "-1");

    teardown(&f);
    printf("  test_set_ex_overwritten_by_set passed.\n");
}

static void test_set_ex_zero(void)
{
    fixture_t f = setup();

    assert_set_ex(&f, "exzero", "gone", "0", "gone");

    // Key should be expired on next access
    assert_get_error(&f, "exzero");
    assert_ttl(&f, "exzero", "-2");

    teardown(&f);
    printf("  test_set_ex_zero passed.\n");
}

/* ── main ──────────────────────────────────────────────────────────── */

int main(void)
{
    memset(&server, 0, sizeof(server));
    server.verbose = false;
    server.config_file_path = "test";

    printf("Running integration tests...\n");

    test_set_get_roundtrip();
    test_set_integer_stored_as_int();
    test_set_string_rejects_incr();
    test_get_nonexistent_returns_error();
    test_error_response_is_framed();
    test_incr_auto_creates_key();
    test_incrby_auto_creates_key();
    test_decr_auto_creates_key();
    test_incr_after_set();
    test_incrby_after_set();

    /* TTL tests */
    test_del_removes_key();
    test_del_nonexistent_key();
    test_expire_and_get();
    test_ttl_returns_remaining();
    test_ttl_no_expiry();
    test_ttl_missing_key();
    test_persist_removes_ttl();
    test_set_clears_ttl();
    test_lazy_expiry_on_incr();

    /* Edge cases — protocol framing */
    test_ok_response_is_framed();

    /* Edge cases — TTL lazy expiry */
    test_ttl_on_expired_key();
    test_lazy_expiry_on_decr();
    test_lazy_expiry_on_incrby();
    test_lazy_expiry_on_decrby();

    /* Edge cases — EXPIRE */
    test_expire_nonexistent_key();
    test_expire_overwrites_previous();
    test_expire_zero_expires_immediately();

    /* Edge cases — PERSIST */
    test_persist_on_key_without_ttl();
    test_persist_on_nonexistent_key();

    /* Edge cases — DEL */
    test_del_clears_ttl();
    test_del_double();

    /* Edge cases — key lifecycle */
    test_set_after_del_works();
    test_set_after_expire_restores_key();

    /* SET EX (atomic SET with TTL) */
    test_set_ex_sets_ttl();
    test_set_ex_value_accessible();
    test_set_ex_expires();
    test_set_ex_overwritten_by_set();
    test_set_ex_zero();

    printf("All integration tests passed.\n");
    return 0;
}
