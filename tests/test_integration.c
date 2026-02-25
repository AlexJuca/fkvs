/**
 * Integration tests for fkvs server command handlers.
 *
 * Uses socketpair() to create connected fds, then calls dispatch_command()
 * directly and reads framed responses from the other end — no server process
 * needed.  Frame construction uses the real construct_*_command() functions
 * from command_parser.c so the tests break whenever the protocol changes.
 */

#include "../src/client.h"
#include "../src/commands/common/command_defs.h"
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
    hashtable_t *ht;
} fixture_t;

static fixture_t setup(hashtable_t *ht)
{
    int fds[2];
    int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    assert(rc == 0);

    client_t *c = calloc(1, sizeof(client_t));
    assert(c != NULL);
    c->fd = fds[0];
    c->frame_need = -1;

    init_command_handlers(ht);

    return (fixture_t){.client = c, .read_fd = fds[1], .ht = ht};
}

static void teardown(fixture_t *f)
{
    close(f->client->fd);
    close(f->read_fd);
    free(f->client);
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

/* ── command assertion helpers ─────────────────────────────────────── *
 *                                                                      *
 * Each one constructs a real protocol frame, dispatches it, asserts on  *
 * the response, and cleans up.  Tests read like a script of user        *
 * commands.                                                             *
 * ──────────────────────────────────────────────────────────────────── */

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
    ssize_t r = dispatch_and_recv(f, cmd, len, resp, sizeof resp);
    free(cmd);
    assert(r > 0 && resp_is_success(resp, r, expected));
}

/* ── tests ─────────────────────────────────────────────────────────── */

static void test_set_get_roundtrip(hashtable_t *ht)
{
    fixture_t f = setup(ht);

    assert_set(&f, "greeting", "hello", "hello");
    assert_get(&f, "greeting", "hello");

    teardown(&f);
    printf("  test_set_get_roundtrip passed.\n");
}

static void test_set_integer_stored_as_int(hashtable_t *ht)
{
    fixture_t f = setup(ht);

    assert_set(&f, "counter", "10", "10");
    assert_incr(&f, "counter", "11");

    teardown(&f);
    printf("  test_set_integer_stored_as_int passed.\n");
}

static void test_set_string_rejects_incr(hashtable_t *ht)
{
    fixture_t f = setup(ht);

    assert_set(&f, "name", "alice", "alice");
    assert_incr_error(&f, "name");

    teardown(&f);
    printf("  test_set_string_rejects_incr passed.\n");
}

static void test_get_nonexistent_returns_error(hashtable_t *ht)
{
    fixture_t f = setup(ht);

    assert_get_error(&f, "no_such_key");

    teardown(&f);
    printf("  test_get_nonexistent_returns_error passed.\n");
}

static void test_error_response_is_framed(hashtable_t *ht)
{
    fixture_t f = setup(ht);
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

static void test_incr_auto_creates_key(hashtable_t *ht)
{
    fixture_t f = setup(ht);

    assert_incr(&f, "new_counter", "1");
    assert_incr(&f, "new_counter", "2");

    teardown(&f);
    printf("  test_incr_auto_creates_key passed.\n");
}

static void test_incrby_auto_creates_key(hashtable_t *ht)
{
    fixture_t f = setup(ht);

    assert_incrby(&f, "score", "10", "10");
    assert_incrby(&f, "score", "5", "15");

    teardown(&f);
    printf("  test_incrby_auto_creates_key passed.\n");
}

static void test_decr_auto_creates_key(hashtable_t *ht)
{
    fixture_t f = setup(ht);

    assert_decr(&f, "neg", "-1");

    teardown(&f);
    printf("  test_decr_auto_creates_key passed.\n");
}

static void test_decrby_auto_creates_key(hashtable_t *ht)
{
    fixture_t f = setup(ht);

    assert_decrby(&f, "dkey", "7", "7");

    teardown(&f);
    printf("  test_decrby_auto_creates_key passed.\n");
}

static void test_incr_after_set(hashtable_t *ht)
{
    fixture_t f = setup(ht);

    assert_set(&f, "val", "100", "100");
    assert_incr(&f, "val", "101");

    teardown(&f);
    printf("  test_incr_after_set passed.\n");
}

static void test_incrby_after_set(hashtable_t *ht)
{
    fixture_t f = setup(ht);

    assert_set(&f, "pts", "50", "50");
    assert_incrby(&f, "pts", "25", "75");

    teardown(&f);
    printf("  test_incrby_after_set passed.\n");
}

/* ── main ──────────────────────────────────────────────────────────── */

int main(void)
{
    memset(&server, 0, sizeof(server));
    server.verbose = false;
    server.config_file_path = "test";

    hashtable_t *ht = create_hash_table(TABLE_SIZE);
    assert(ht != NULL);

    printf("Running integration tests...\n");

    test_set_get_roundtrip(ht);
    test_set_integer_stored_as_int(ht);
    test_set_string_rejects_incr(ht);
    test_get_nonexistent_returns_error(ht);
    test_error_response_is_framed(ht);
    test_incr_auto_creates_key(ht);
    test_incrby_auto_creates_key(ht);
    test_decr_auto_creates_key(ht);
    test_decrby_auto_creates_key(ht);
    test_incr_after_set(ht);
    test_incrby_after_set(ht);

    free_hash_table(ht);

    printf("All integration tests passed.\n");
    return 0;
}
